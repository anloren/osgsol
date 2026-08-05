// applications/earth_explorer/ai_media.cpp
// 快照(抓帧)+ 生图管线实现。设计与 ai_chat.cpp 的 worker 模式一致:耗时的网络调用
// 放独立线程,主线程只在 update() 里轮询状态、绝不阻塞。
#include "ai_media.h"
#include "ai_photo_request.h"
#include "ai_cards.h"
#include "ai_chat.h"   // AIChatCore 完整定义(ai_media.h 只前置声明):addErrorNote() 调用需要
#include "ai_orbit_trajectory.h"
#include "cinematic_video_encoder.h"
#include "earth_config.h"
#include <readerwriter/EarthManipulator.h>
#include "3rdparty/libhv/all/client/requests.h"
#include "3rdparty/libhv/all/base64.h"
#include <OpenThreads/Thread>
#include <osgViewer/ViewerEventHandlers>
#include <osgDB/FileUtils>
#include <osgDB/ReadFile>
#include <osgDB/WriteFile>
#include <osg/Notify>
#include <algorithm>
#include <atomic>
#include <fstream>
#include <memory>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include <ctime>
#include <cstdio>
#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace earthai
{
    const std::string& resolvedCinematicImageModel()
    {
        static const std::string model = cinematicImageModelName(
            getenv("EARTH_AI_IMAGE_MODEL"));
        return model;
    }

    // 错误摘要截断,与 ai_chat.cpp::truncate200 同规则(避免长 base64/大段文本刷屏日志)。
    static std::string truncate200(const std::string& s)
    {
        if (s.size() <= 200) return s;
        std::string r = s.substr(0, 200);
        while (!r.empty() && ((unsigned char)r.back() & 0xC0) == 0x80) r.pop_back();
        if (!r.empty() && (unsigned char)r.back() >= 0xC0) r.pop_back();
        return r;
    }

    // 连接层失败(resp 为空:DNS/TLS 握手/连接拒绝/超时无响应)自动重试,带线性退避。
    // 单次 HTTP 调用对真实错误码(4xx/5xx)不做内部重试;Veo 轮询会在下一轮
    // 处理瞬时状态。retries 从配置读
    // (earthcfg "http.retries",默认 3)。与 ai_chat.cpp 的同名 static helper 逻辑一致。
    static requests::Response httpRequestRetry(requests::Request req)
    {
        int retries = earthcfg::getInt("http.retries");   // 默认 3
        requests::Response resp;
        for (int attempt = 0; attempt <= retries; ++attempt)
        {
            resp = requests::request(req);
            if (resp) return resp;                          // 拿到任何响应(含 4xx/5xx)即返回
            if (attempt < retries)
                OpenThreads::Thread::microSleep((attempt + 1) * 300 * 1000);  // 300/600/900ms 退避
        }
        return resp;   // 仍为空 = 连接层彻底失败
    }

    static bool readFileBytes(const std::string& path, std::string& out)
    {
        std::ifstream ifs(path.c_str(), std::ios::binary);
        if (!ifs.is_open()) return false;
        std::stringstream ss; ss << ifs.rdbuf();
        out = ss.str();
        return true;
    }

    // grab(pngPath) 的实际落盘文件:去掉 ".png" 再拼 ScreenCaptureHandler 的 "_0.png" 后缀。
    // 此逻辑曾在 grab/ready/照片提交三处各写一份,视频提交第四份拷贝时把后缀直接追加到
    // ".png" 之后酿成真机 bug(离线 FAKE 路径跳过读快照,测不到)—— 统一收敛到这里。
    static std::string capturedPath(const std::string& pngPath)
    {
        std::string prefix = pngPath;
        const std::string ext = ".png";
        if (prefix.size() >= ext.size() && prefix.compare(prefix.size() - ext.size(), ext.size(), ext) == 0)
            prefix.resize(prefix.size() - ext.size());
        return prefix + "_0.png";
    }

    // ScreenCaptureHandler invokes its operation after a future render traversal.  The wrapper
    // keeps cancellation state with that individual invocation, so changing the handler's next
    // operation can never turn an old cancelled callback into an untracked late file write.
    class CancellableCaptureOperation : public osgViewer::ScreenCaptureHandler::CaptureOperation
    {
    public:
        CancellableCaptureOperation(
            osgViewer::ScreenCaptureHandler::WriteToFile* delegate,
            const std::shared_ptr<SnapshotCaptureController>& controller)
            : _delegate(delegate), _controller(controller) {}

        virtual void operator()(const osg::Image& image, const unsigned int contextId)
        {
            if (!_controller ||
                _controller->beginCallback() == SNAPSHOT_CAPTURE_SKIP_CANCELLED)
                return;
            try
            {
                (*_delegate)(image, contextId);
                _controller->completeCallback(true);
            }
            catch (...)
            {
                _controller->completeCallback(false);
                throw;
            }
        }

    private:
        osg::ref_ptr<osgViewer::ScreenCaptureHandler::WriteToFile> _delegate;
        std::shared_ptr<SnapshotCaptureController> _controller;
    };

    // The OSG implementation updates WindowCaptureCallback's operation for every GraphicsContext
    // without a synchronization boundary. Never call setCaptureOperation after arming: a render
    // callback can be partway through readPixels() before it invokes the operation.
    class GenerationScreenCaptureHandler : public osgViewer::ScreenCaptureHandler
    {
    public:
        GenerationScreenCaptureHandler(CaptureOperation* operation, int frames)
            : osgViewer::ScreenCaptureHandler(operation, frames) {}

        osg::Camera::DrawCallback* callbackIdentity() const { return _callback.get(); }
    };

    static bool writeFileBytes(const std::string& path, const std::string& bytes)
    {
        std::ofstream ofs(path.c_str(), std::ios::binary | std::ios::trunc);
        if (!ofs.is_open()) return false;
        ofs.write(bytes.data(), (std::streamsize)bytes.size());
        return ofs.good();
    }

    // ---------------- SnapshotGrabber ----------------

    struct SnapshotGrabber::CaptureGeneration
    {
        std::shared_ptr<SnapshotCaptureController> token;
        osg::ref_ptr<GenerationScreenCaptureHandler> handler;
        osg::ref_ptr<osg::Camera::DrawCallback> callback;
        mutable std::atomic<unsigned int> _inFlight { 0 };

        void invoke(osg::RenderInfo& renderInfo) const
        {
            _inFlight.fetch_add(1, std::memory_order_acq_rel);
            struct ExitGuard
            {
                const CaptureGeneration* generation;
                ~ExitGuard()
                {
                    generation->_inFlight.fetch_sub(1, std::memory_order_acq_rel);
                }
            } guard { this };
            if (callback.valid()) (*callback)(renderInfo);
        }

        bool quiescent() const
        { return _inFlight.load(std::memory_order_acquire) == 0; }
    };

    // OSG's RenderStage can read Camera::_finalDrawCallback more than once without a lock.
    // Install this dispatcher once before viewer.run(); runtime ownership changes only publish
    // or revoke immutable shared generations through the C++14 shared_ptr atomic operations.
    struct SnapshotGrabber::GenerationDispatcher : public osg::Camera::DrawCallback
    {
        virtual void operator()(osg::RenderInfo& renderInfo) const
        {
            const std::shared_ptr<CaptureGeneration> generation =
                std::atomic_load_explicit(&_generation, std::memory_order_acquire);
            if (generation) generation->invoke(renderInfo);
        }

        void publish(const std::shared_ptr<CaptureGeneration>& generation)
        {
            std::atomic_store_explicit(&_generation, generation, std::memory_order_release);
        }

        void revoke(const std::shared_ptr<CaptureGeneration>& generation)
        {
            std::shared_ptr<CaptureGeneration> expected = generation;
            std::atomic_compare_exchange_strong_explicit(
                &_generation, &expected, std::shared_ptr<CaptureGeneration>(),
                std::memory_order_acq_rel, std::memory_order_acquire);
        }

    private:
        mutable std::shared_ptr<CaptureGeneration> _generation;
    };

    SnapshotGrabber::SnapshotGrabber(osg::Camera* captureCamera)
        : _captureCamera(captureCamera)
    {
        // configureAIChat receives Earth's final composition camera (cameras[3]), constructs
        // MediaManager before registering FRAME handlers, and is called before viewer.run().
        // This is the sole Camera callback mutation boundary; runtime paths only publish or
        // revoke a generation through the dispatcher atomic shared_ptr.
        if (!_captureCamera.valid())
        {
            OSG_WARN << "[AIChat] cannot install stable snapshot dispatcher" << std::endl;
            return;
        }
        // A pre-existing callback may clear or replace Camera::_finalDrawCallback itself.
        // Do not wrap an unknown lifecycle: fail closed so the camera slot remains untouched.
        if (_captureCamera->getFinalDrawCallback())
        {
            OSG_WARN << "[AIChat] final camera callback already occupied; "
                     << "snapshot dispatcher disabled" << std::endl;
            return;
        }
        _dispatcher = new GenerationDispatcher;
        _captureCamera->setFinalDrawCallback(_dispatcher.get());
        _dispatcherInstalled = true;
    }

    SnapshotGrabber::~SnapshotGrabber() {}

    std::shared_ptr<SnapshotCaptureController> SnapshotGrabber::grab(
        const std::string& pngPath)
    {
        if (!_dispatcherInstalled || !_dispatcher.valid())
        {
            OSG_WARN << "[AIChat] snapshot dispatcher unavailable: " << pngPath << std::endl;
            return std::shared_ptr<SnapshotCaptureController>();
        }
        const std::shared_ptr<SnapshotCaptureController> token = _slot.begin(pngPath);
        if (!token)
        {
            OSG_WARN << "[AIChat] snapshot request still awaits cancelled callback: "
                     << pngPath << std::endl;
            return std::shared_ptr<SnapshotCaptureController>();
        }

        // A normal callback may have reached token terminal before its generation invocation
        // returns. Reap only after the invocation wrapper proves quiescent.
        reapTerminalGeneration();

        // WriteToFile does not expose writeImageFile() success. Remove any expected output
        // before arming so a completed token plus a stable old file can never look successful.
        const std::string actualPath = capturedPath(pngPath);
        if (osgDB::fileExists(actualPath) &&
            std::remove(actualPath.c_str()) != 0 && osgDB::fileExists(actualPath))
        {
            token->retireIfNotStarted();
            OSG_WARN << "[AIChat] cannot remove stale snapshot before capture: "
                     << actualPath << std::endl;
            return std::shared_ptr<SnapshotCaptureController>();
        }

        // pngPath 末尾去掉 ".png" 作为 WriteToFile 的前缀(它自己会拼回 "_0.png")。
        std::string prefix = pngPath;
        const std::string ext = ".png";
        if (prefix.size() >= ext.size() && prefix.compare(prefix.size() - ext.size(), ext.size(), ext) == 0)
            prefix.resize(prefix.size() - ext.size());

        osg::ref_ptr<osgViewer::ScreenCaptureHandler::WriteToFile> writer =
            new osgViewer::ScreenCaptureHandler::WriteToFile(
                prefix, "png", osgViewer::ScreenCaptureHandler::WriteToFile::OVERWRITE);
        osg::ref_ptr<CancellableCaptureOperation> operation =
            new CancellableCaptureOperation(writer.get(), token);
        std::shared_ptr<CaptureGeneration> generation =
            std::make_shared<CaptureGeneration>();
        generation->token = token;
        generation->handler = new GenerationScreenCaptureHandler(operation.get(), 0);
        generation->callback = generation->handler->callbackIdentity();
        // The dispatcher load in render obtains its own shared_ptr. FRAME only changes that
        // atomic publication, so a late RenderStage read can retain this immutable generation
        // without touching Camera::_finalDrawCallback.
        _activeGeneration = generation;
        _dispatcher->publish(generation);
        OSG_NOTICE << "[AIChat] snapshot grab -> " << pngPath << std::endl;
        return token;
    }

    void SnapshotGrabber::retire(
        const std::shared_ptr<SnapshotCaptureController>& capture)
    {
        if (!capture) return;
        if (!capture->retireIfNotStarted()) return;
        if (_activeGeneration && _activeGeneration->token == capture)
        {
            // The dispatcher shared_ptr protects a render traversal that had already loaded
            // this generation. Keep only an active invocation; otherwise release it now.
            _dispatcher->revoke(_activeGeneration);
            if (!_activeGeneration->quiescent())
                _reapableGenerations.push_back(_activeGeneration);
            _activeGeneration.reset();
        }
        reapTerminalGeneration();
    }

    void SnapshotGrabber::reapTerminalGeneration()
    {
        if (_activeGeneration && _activeGeneration->token &&
            _activeGeneration->token->terminal())
        {
            _dispatcher->revoke(_activeGeneration);
            if (_activeGeneration->quiescent())
                _activeGeneration.reset();
            else
            {
                _reapableGenerations.push_back(_activeGeneration);
                _activeGeneration.reset();
            }
        }
        for (std::size_t index = 0; index < _reapableGenerations.size(); )
        {
            const std::shared_ptr<CaptureGeneration>& generation =
                _reapableGenerations[index];
            if (generation && generation->quiescent())
                _reapableGenerations.erase(_reapableGenerations.begin() + index);
            else
                ++index;
        }
    }

    bool SnapshotGrabber::ready(
        const std::shared_ptr<SnapshotCaptureController>& capture,
        const std::string& pngPath)
    {
        if (!capture || capture->requestedPath() != pngPath) return false;
        // numFrames=0 prevents OSG's post-operation self-removal. The stable camera dispatcher
        // stays installed; FRAME revokes only this generation before polling its artifact.
        if (capture->terminal()) reapTerminalGeneration();
        if (!capture->completedSuccessfully()) return false;
        // WriteToFile 用 "_0" 后缀(单 GraphicsContext、OVERWRITE 策略)拼实际文件名,
        // 统一经 capturedPath() 计算,与提交侧读取路径保持一致。
        std::string actual = capturedPath(pngPath);

        // 写盘是渲染线程在捕获回调里做的,文件可能"存在但还没写完"——真正的跨帧稳定性:
        // 本次测到的大小要与"上一次 ready() 调用"记录的大小相同才算稳定,而不是同一次调用
        // 内部读两次(那样两次 stat 间隔仅几微秒,写到一半也几乎总能读到同一个字节数,
        // 起不到防御作用)。调用方(MediaManager::update())每帧调一次 ready(),两次真实的
        // update() tick 之间隔了一整帧的时间,足够覆盖磁盘写入延迟。
        if (!osgDB::fileExists(actual)) return capture->observeFileSize(pngPath, 0);
        std::ifstream ifs(actual.c_str(), std::ios::binary | std::ios::ate);
        if (!ifs.is_open()) return capture->observeFileSize(pngPath, 0);
        std::streamsize sz = ifs.tellg();
        ifs.close();
        return capture->observeFileSize(pngPath, sz);
    }

    void SnapshotGrabber::cropToViewport(const std::string& pngPath)
    {
        std::string actual = capturedPath(pngPath);
        osg::ref_ptr<osg::Image> img = osgDB::readImageFile(actual);
        if (!img.valid() || !_captureCamera.valid()) return;
        int vx = 0, vy = 0, vw = 0, vh = 0;
        const osg::Viewport* vp = _captureCamera->getViewport();
        if (vp)
        {
            vx = (int)vp->x();
            vy = (int)vp->y();
            vw = (int)vp->width();
            vh = (int)vp->height();
        }
        if (vw <= 0 || vh <= 0)
        {
            vw = _contentW;
            vh = _contentH;
        }
        if (vw <= 0 || vh <= 0) return;
        if (vx == 0 && vy == 0 && vw == img->s() && vh == img->t()) return;   // 已是视口大小
        if (vx + vw > img->s() || vy + vh > img->t()) return;                 // 视口超出图像,放弃
        // glReadPixels/osg::Image 都是左下原点,直接按行 memcpy 子区
        int bpp = img->getPixelSizeInBits() / 8; if (bpp <= 0) return;
        osg::ref_ptr<osg::Image> out = new osg::Image;
        out->allocateImage(vw, vh, 1, img->getPixelFormat(), img->getDataType());
        for (int row = 0; row < vh; ++row)
            memcpy(out->data(0, row), img->data(vx, vy + row), (size_t)vw * bpp);
        if (osgDB::writeImageFile(*out, actual))
            OSG_NOTICE << "[AIChat] snapshot cropped to viewport " << vw << "x" << vh << std::endl;
    }

    // ---------------- GeminiMediaProvider ----------------

    GeminiMediaProvider::GeminiMediaProvider(const std::string& apiKey) : _apiKey(apiKey) {}

    // 防御式解析 candidates[0].content.parts[] 找 inlineData.data(base64),风格与
    // ai_chat.cpp::parseGeminiResponse 一致:任何字段缺失/类型不符都落到 err,不抛异常。
    static bool parseImageResponse(const std::string& body, std::string& outPngBytes, std::string& err)
    {
        picojson::value v;
        std::string perr = picojson::parse(v, body);
        if (!perr.empty() || !v.is<picojson::object>())
        { err = "bad json: " + truncate200(perr.empty() ? body : perr); return false; }

        if (!v.contains("candidates") || !v.get("candidates").is<picojson::array>()
            || v.get("candidates").get<picojson::array>().empty())
        {
            if (v.contains("promptFeedback") && v.get("promptFeedback").is<picojson::object>()
                && v.get("promptFeedback").contains("blockReason"))
                err = "blocked: " + truncate200(v.get("promptFeedback").get("blockReason").to_str());
            else if (v.contains("error") && v.get("error").is<picojson::object>()
                     && v.get("error").contains("message"))
                err = "api error: " + truncate200(v.get("error").get("message").to_str());
            else err = "no candidates: " + truncate200(body);
            return false;
        }

        const picojson::value& cand0 = v.get("candidates").get<picojson::array>()[0];
        if (!cand0.is<picojson::object>() || !cand0.contains("content")
            || !cand0.get("content").is<picojson::object>()
            || !cand0.get("content").contains("parts")
            || !cand0.get("content").get("parts").is<picojson::array>())
        {
            if (cand0.is<picojson::object>() && cand0.contains("finishReason"))
                err = "no content: finishReason=" + truncate200(cand0.get("finishReason").to_str());
            else err = "no content parts: " + truncate200(body);
            return false;
        }

        const picojson::array& parts = cand0.get("content").get("parts").get<picojson::array>();
        for (size_t i = 0; i < parts.size(); ++i)
        {
            const picojson::value& part = parts[i];
            if (!part.is<picojson::object>()) continue;
            if (part.contains("inlineData") && part.get("inlineData").is<picojson::object>())
            {
                const picojson::value& id = part.get("inlineData");
                if (id.contains("data") && id.get("data").is<std::string>())
                {
                    const std::string& b64 = id.get("data").get<std::string>();
                    outPngBytes = hv::Base64Decode(b64.c_str(), (unsigned int)b64.size());
                    if (outPngBytes.empty()) { err = "empty decoded image"; return false; }
                    return true;
                }
            }
        }
        err = "no inlineData in response parts: " + truncate200(body);
        return false;
    }

    std::string GeminiMediaProvider::generateImage(
        const std::string& pngBytes, const std::string& prompt, std::string& err,
        const CinematicImageOutputOptions& output)
    {
        std::string b64 = hv::Base64Encode((const unsigned char*)pngBytes.data(), (unsigned int)pngBytes.size());

        picojson::object textPart; textPart["text"] = picojson::value(prompt);
        picojson::object inlineData;
        inlineData["mime_type"] = picojson::value(std::string("image/png"));
        inlineData["data"] = picojson::value(b64);
        picojson::object imagePart; imagePart["inline_data"] = picojson::value(inlineData);

        picojson::array parts;
        parts.push_back(picojson::value(textPart));
        parts.push_back(picojson::value(imagePart));

        picojson::object content; content["parts"] = picojson::value(parts);
        picojson::array contents; contents.push_back(picojson::value(content));

        picojson::object body;
        body["contents"] = picojson::value(contents);
        body["generationConfig"] = picojson::value(
            cinematicGeminiImageGenerationConfig(output));

        requests::Request req(new HttpRequest);
        req->method = HTTP_POST;
        req->timeout = 60;   // 生图比对话慢,给足 60s(spec 要求)
        // 注意:key 拼在 URL 里,下面任何日志/错误信息都不得把 req->url 整串打印出来
        // 生图模型:EARTH_AI_IMAGE_MODEL 覆盖。默认使用官方 Nano Banana 2 的稳定模型名；
        // 需要更高推理精度时仍可显式切换 gemini-3-pro-image，但不能由应用暗中升级计费。
        const std::string& kImageModel = resolvedCinematicImageModel();
        req->url = "https://generativelanguage.googleapis.com/v1beta/models/" +
                   kImageModel + ":generateContent?key=" + _apiKey;
        req->headers["Content-Type"] = "application/json";
        req->body = picojson::value(body).serialize();

        requests::Response resp = httpRequestRetry(req);
        if (!resp || resp->status_code != 200)
        {
            // resp 为空 = 连接层失败(已重试仍无响应);区分于真实 HTTP 错误码。
            err = resp
                ? ("HTTP " + std::to_string((int)resp->status_code) + ": " + truncate200(resp->body))
                : u8"网络连接失败(多次重试无响应):可能网络中断 / 请求超时 / TLS 握手失败,请检查网络后重试";
            return std::string();
        }

        std::string outBytes;
        if (!parseImageResponse(resp->body, outBytes, err)) return std::string();
        return outBytes;
    }

    // ---------------- VeoVideoProvider(Task 9)----------------

    VeoVideoProvider::VeoVideoProvider(const std::string& apiKey, const std::string& model)
        : _apiKey(apiKey), _model(model) {}

    // 防御式解析 predictLongRunning 响应,取 "name" 字段(operation 名字)。
    static bool parseOperationName(const std::string& body, std::string& outName, std::string& err)
    {
        picojson::value v;
        std::string perr = picojson::parse(v, body);
        if (!perr.empty() || !v.is<picojson::object>())
        { err = "bad json: " + truncate200(perr.empty() ? body : perr); return false; }
        if (v.contains("name") && v.get("name").is<std::string>())
        { outName = v.get("name").get<std::string>(); return true; }
        if (v.contains("error") && v.get("error").is<picojson::object>()
            && v.get("error").contains("message"))
        { err = "api error: " + truncate200(v.get("error").get("message").to_str()); return false; }
        err = "no operation name: " + truncate200(body);
        return false;
    }

    std::string VeoVideoProvider::submit(const std::string& pngBytesA, const std::string& pngBytesB,
                                         const std::string& motionPrompt, std::string& err)
    {
        std::string b64A = hv::Base64Encode((const unsigned char*)pngBytesA.data(), (unsigned int)pngBytesA.size());

        picojson::object image;
        image["bytesBase64Encoded"] = picojson::value(b64A);
        image["mimeType"] = picojson::value(std::string("image/png"));

        picojson::object instance;
        instance["prompt"] = picojson::value(motionPrompt);
        instance["image"] = picojson::value(image);
        if (!pngBytesB.empty())
        {
            const std::string b64B = hv::Base64Encode(
                (const unsigned char*)pngBytesB.data(),
                (unsigned int)pngBytesB.size());
            picojson::object lastFrame;
            lastFrame["bytesBase64Encoded"] = picojson::value(b64B);
            lastFrame["mimeType"] = picojson::value(std::string("image/png"));
            instance["lastFrame"] = picojson::value(lastFrame);
        }

        picojson::array instances; instances.push_back(picojson::value(instance));
        picojson::object body; body["instances"] = picojson::value(instances);

        requests::Request req(new HttpRequest);
        req->method = HTTP_POST;
        req->timeout = 60;
        // 注意:key 拼在 URL 里,下面任何日志/错误信息都不得把 req->url 整串打印出来
        req->url = "https://generativelanguage.googleapis.com/v1beta/models/" + _model +
                   ":predictLongRunning?key=" + _apiKey;
        req->headers["Content-Type"] = "application/json";
        req->body = picojson::value(body).serialize();

        requests::Response resp = httpRequestRetry(req);
        if (!resp || resp->status_code != 200)
        {
            // resp 为空 = 连接层失败(已重试仍无响应);区分于真实 HTTP 错误码。
            err = resp
                ? ("HTTP " + std::to_string((int)resp->status_code) + ": " + truncate200(resp->body))
                : u8"网络连接失败(多次重试无响应):可能网络中断 / 请求超时 / TLS 握手失败,请检查网络后重试";
            return std::string();
        }

        std::string opName;
        if (!parseOperationName(resp->body, opName, err)) return std::string();
        return opName;
    }

    void VeoVideoProvider::poll(const std::string& operationName, bool& done,
                                std::string& mp4Bytes, std::string& err)
    {
        done = false; mp4Bytes.clear(); err.clear();

        requests::Request req(new HttpRequest);
        req->method = HTTP_GET;
        req->timeout = 30;
        // operationName 形如 "models/veo-3.1-generate-001/operations/xxxx",GET v1beta/<name>。
        req->url = "https://generativelanguage.googleapis.com/v1beta/" + operationName + "?key=" + _apiKey;

        requests::Response resp = httpRequestRetry(req);
        VideoPollDisposition disposition = classifyVideoPollHttp((bool)resp,
            resp ? (int)resp->status_code : 0);
        if (disposition == VIDEO_POLL_RETRY)
        {
            done = false;
            err.clear();
            return;
        }
        if (disposition == VIDEO_POLL_TERMINAL_ERROR)
        {
            done = true;
            err = "HTTP " + std::to_string((int)resp->status_code) + ": " +
                  truncate200(resp->body);
            return;
        }

        picojson::value v;
        std::string perr = picojson::parse(v, resp->body);
        if (!perr.empty() || !v.is<picojson::object>())
        { err = "bad json: " + truncate200(perr.empty() ? resp->body : perr); done = true; return; }

        if (!v.contains("done") || !v.get("done").is<bool>() || !v.get("done").get<bool>())
        { done = false; return; }   // 仍在跑

        done = true;
        if (v.contains("error") && v.get("error").is<picojson::object>())
        {
            std::string msg = v.get("error").contains("message")
                ? v.get("error").get("message").to_str() : resp->body;
            err = "operation error: " + truncate200(msg);
            return;
        }
        if (!v.contains("response") || !v.get("response").is<picojson::object>())
        { err = "done but no response field: " + truncate200(resp->body); return; }

        const picojson::value& response = v.get("response");
        if (!response.contains("generateVideoResponse")
            || !response.get("generateVideoResponse").is<picojson::object>())
        { err = "no generateVideoResponse: " + truncate200(resp->body); return; }

        const picojson::value& gvr = response.get("generateVideoResponse");
        if (!gvr.contains("generatedSamples") || !gvr.get("generatedSamples").is<picojson::array>()
            || gvr.get("generatedSamples").get<picojson::array>().empty())
        { err = "no generatedSamples: " + truncate200(resp->body); return; }

        const picojson::value& sample0 = gvr.get("generatedSamples").get<picojson::array>()[0];
        if (!sample0.is<picojson::object>() || !sample0.contains("video")
            || !sample0.get("video").is<picojson::object>())
        { err = "no video in sample: " + truncate200(resp->body); return; }

        const picojson::value& video = sample0.get("video");
        // 防御两种形状:bytesBase64Encoded(内联字节)或 uri(需要再发一次 GET 下载,
        // uri 可能没带 key 查询参数,这里统一补上——已带 "?" 的话改用 "&" 拼接)。
        if (video.contains("bytesBase64Encoded") && video.get("bytesBase64Encoded").is<std::string>())
        {
            const std::string& b64 = video.get("bytesBase64Encoded").get<std::string>();
            mp4Bytes = hv::Base64Decode(b64.c_str(), (unsigned int)b64.size());
            if (mp4Bytes.empty()) err = "empty decoded video bytes";
            return;
        }
        if (video.contains("uri") && video.get("uri").is<std::string>())
        {
            std::string uri = video.get("uri").get<std::string>();
            std::string sep = (uri.find('?') != std::string::npos) ? "&" : "?";
            std::string dlUrl = uri + sep + "key=" + _apiKey;

            requests::Request dlReq(new HttpRequest);
            dlReq->method = HTTP_GET;
            dlReq->timeout = 60;
            dlReq->url = dlUrl;
            requests::Response dlResp = httpRequestRetry(dlReq);
            if (!dlResp || dlResp->status_code != 200)
            {
                // resp 为空 = 连接层失败(已重试仍无响应);区分于真实 HTTP 错误码。
                err = dlResp
                    ? ("video download HTTP " + std::to_string((int)dlResp->status_code))
                    : u8"网络连接失败(多次重试无响应):可能网络中断 / 请求超时 / TLS 握手失败,请检查网络后重试";
                return;
            }
            mp4Bytes = dlResp->body;
            if (mp4Bytes.empty()) err = "empty downloaded video";
            return;
        }
        err = "video object has neither bytesBase64Encoded nor uri: " + truncate200(resp->body);
    }

    // ---------------- OmniVideoProvider(Interactions API)----------------

    OmniVideoProvider::OmniVideoProvider(const std::string& apiKey, const std::string& model)
        : _apiKey(apiKey), _model(model) {}

    bool OmniVideoProvider::generate(const std::string& firstPngBytes, const std::string& motionPrompt,
                                     const CinematicVideoOutputOptions& output,
                                     std::string& mp4Bytes, std::string& err)
    {
        std::string b64 = hv::Base64Encode((const unsigned char*)firstPngBytes.data(),
                                           (unsigned int)firstPngBytes.size());
        picojson::object imgPart, txtPart;
        imgPart["type"] = picojson::value(std::string("image"));
        imgPart["data"] = picojson::value(b64);
        imgPart["mime_type"] = picojson::value(std::string("image/png"));
        txtPart["type"] = picojson::value(std::string("text"));
        txtPart["text"] = picojson::value(motionPrompt);
        picojson::array input;
        input.push_back(picojson::value(imgPart));
        input.push_back(picojson::value(txtPart));

        picojson::object body;
        body["model"] = picojson::value(_model);
        body["input"] = picojson::value(input);
        body["response_format"] = picojson::value(
            cinematicGeminiVideoResponseFormat(output));
        body["generation_config"] = picojson::value(
            cinematicGeminiImageToVideoConfig());

        requests::Request req(new HttpRequest);
        req->method = HTTP_POST;
        req->timeout = 300;   // Interactions 同步返回,官方 3-10s 视频通常 30-180s 内出结果
        // 注意:key 拼在 URL 里,任何日志/错误信息都不得把 req->url 整串打印出来
        req->url = "https://generativelanguage.googleapis.com/v1beta/interactions?key=" + _apiKey;
        req->headers["Content-Type"] = "application/json";
        req->body = picojson::value(body).serialize();

        requests::Response resp = httpRequestRetry(req);
        if (!resp || resp->status_code != 200)
        {
            // resp 为空 = 连接层失败(已重试仍无响应);区分于真实 HTTP 错误码。
            err = resp
                ? ("HTTP " + std::to_string((int)resp->status_code) + ": " + truncate200(resp->body))
                : u8"网络连接失败(多次重试无响应):可能网络中断 / 请求超时 / TLS 握手失败,请检查网络后重试";
            return false;
        }

        picojson::value v; std::string perr = picojson::parse(v, resp->body);
        if (!perr.empty() || !v.is<picojson::object>()) { err = "bad json: " + truncate200(resp->body); return false; }

        // 形状 1:steps[] 里 type=="model_output" 的 content[] 中 type=="video" 的 data(base64)
        if (v.contains("steps") && v.get("steps").is<picojson::array>())
        {
            picojson::array& steps = v.get("steps").get<picojson::array>();
            for (size_t i = 0; i < steps.size(); ++i)
            {
                if (!steps[i].is<picojson::object>() || !steps[i].contains("content")) continue;
                if (steps[i].contains("type") && steps[i].get("type").to_str() != "model_output") continue;
                picojson::value& c = steps[i].get("content");
                if (!c.is<picojson::array>()) continue;
                picojson::array& parts = c.get<picojson::array>();
                for (size_t j = 0; j < parts.size(); ++j)
                {
                    if (!parts[j].is<picojson::object>()) continue;
                    if (parts[j].contains("type") && parts[j].get("type").to_str() == "video"
                        && parts[j].contains("data"))
                    {
                        std::string vb64 = parts[j].get("data").to_str();
                        mp4Bytes = hv::Base64Decode(vb64.c_str(), (unsigned int)vb64.size());
                        if (!mp4Bytes.empty()) return true;
                    }
                }
            }
        }
        // 形状 2:大文件走 output_video.uri,需再 GET 下载(uri 通常要拼 key 参数)
        if (v.contains("output_video") && v.get("output_video").is<picojson::object>()
            && v.get("output_video").contains("uri"))
        {
            std::string uri = v.get("output_video").get("uri").to_str();
            std::string sep = (uri.find('?') == std::string::npos) ? "?" : "&";
            requests::Request dl(new HttpRequest);
            dl->method = HTTP_GET; dl->timeout = 180;
            dl->url = uri + sep + "key=" + _apiKey;   // 同样不得整串打印
            requests::Response dresp = httpRequestRetry(dl);
            if (dresp && dresp->status_code == 200 && !dresp->body.empty())
            { mp4Bytes = dresp->body; return true; }
            // resp 为空 = 连接层失败(已重试仍无响应);区分于真实 HTTP 错误码。
            err = dresp
                ? ("uri download HTTP " + std::to_string((int)dresp->status_code))
                : u8"网络连接失败(多次重试无响应):可能网络中断 / 请求超时 / TLS 握手失败,请检查网络后重试";
            return false;
        }
        // 都没有:带出 status / error 信息
        std::string status = v.contains("status") ? v.get("status").to_str() : "";
        err = "no video in response (status=" + status + "): " + truncate200(resp->body);
        return false;
    }

    // ---------------- MediaManager ----------------

    // 输出目录:EARTH_AI_OUTDIR 覆盖,默认 $HOME/Pictures/EarthExplorer(仅算一次,
    // 与 EARTH_AI_FAKE_IMG 一样用 static 局部量作"进程期只读一次"的缓存)。
    static std::string outDir()
    {
        static std::string dir;
        static bool inited = false;
        if (!inited)
        {
            inited = true;
            const char* env = getenv("EARTH_AI_OUTDIR");
            if (env && *env) dir = env;
            else
            {
                const char* home = getenv("HOME");
                dir = (home && *home) ? (std::string(home) + "/Pictures/EarthExplorer") : "./EarthExplorer_out";
            }
            osgDB::makeDirectory(dir);
        }
        return dir;
    }

    // EARTH_AI_FAKE_IMG=<png路径>:离线 E2E 用,generateImage 步骤替换成拷贝该文件字节
    // (不联网、不需要 key)。只读一次环境变量(static),与 EARTH_AI_FAKE 的约定一致。
    static bool fakeImgPath(std::string& path)
    {
        static std::string cached;
        static bool inited = false, has = false;
        if (!inited)
        {
            inited = true;
            const char* env = getenv("EARTH_AI_FAKE_IMG");
            if (env && *env) { cached = env; has = true; }
        }
        if (has) path = cached;
        return has;
    }

    // EARTH_AI_FAKE_IMG_FAIL=<错误消息>:失败注入测试钩子(v0.15-vision 收尾修复的验证用)——
    // 与 EARTH_AI_FAKE_IMG 一样离线可测、只读一次环境变量(static),但语义相反:设置后照片
    // 生成 worker 直接合成一个 AIJob::FAILED 结果(用给定文本作为错误消息),不尝试真实/伪造
    // 生成,用于 E2E 验证"生成失败 -> addErrorNote -> transcript -> headless 日志"整条链路。
    // 仅覆盖照片路径(见任务 Step 5:视频路径的等价钩子留待需要时再补,不在本次范围内)。
    static bool fakeImgFailMsg(std::string& msg)
    {
        static std::string cached;
        static bool inited = false, has = false;
        if (!inited)
        {
            inited = true;
            const char* env = getenv("EARTH_AI_FAKE_IMG_FAIL");
            if (env && *env) { cached = env; has = true; }
        }
        if (has) msg = cached;
        return has;
    }

    // EARTH_AI_FAKE_MP4=<mp4路径>:离线 E2E 用,confirmVideo 的"提交+轮询+下载"整段替换成
    // 拷贝该文件字节(不联网、不需要 key),模拟一个短暂延迟(见 VideoJob::Phase 注释)后
    // 直接进入完成态。只读一次环境变量(static),与 fakeImgPath 的约定一致。
    static bool fakeMp4Path(std::string& path)
    {
        static std::string cached;
        static bool inited = false, has = false;
        if (!inited)
        {
            inited = true;
            const char* env = getenv("EARTH_AI_FAKE_MP4");
            if (env && *env) { cached = env; has = true; }
        }
        if (has) path = cached;
        return has;
    }

    // 视频模型名:EARTH_AI_VIDEO_MODEL 覆盖。默认 Omni Flash(Interactions API,同步、快、
    // 支持对话式编辑;但官方明确不支持首尾帧插值——B 点只进运动提示词)。要严格的
    // 首尾帧穿越效果请设 EARTH_AI_VIDEO_MODEL=veo-3.1-fast-generate-preview(或去掉 fast
    // 的高画质版;真机 key 实测 2026-07 无 -001 GA 名)。具体路由始终由
    // classifyCinematicVideoProvider() 的严格 allowlist 决定：任意包含 omni 的名称走
    // Interactions，只有列出的 Veo ID 才走 predictLongRunning；其余名称拒绝提交。
    static std::string videoModel()
    {
        const char* env = getenv("EARTH_AI_VIDEO_MODEL");
        return (env && *env) ? std::string(env) : std::string("gemini-omni-flash-preview");
    }

    // WAITING_SNAPSHOT 状态下 update() 的超时阈值(见类头注释与 update() 里的判定)。
    // 600 次≈600 帧,正常帧率下几秒钟就该等到快照文件;真出现"文件永不出现"的极端情况
    // (例如磁盘写失败但没报错、或 CaptureOperation 目标被意外替换),超过这个次数就判超时,
    // 避免 job 永远卡在 WAITING_SNAPSHOT、后续所有 generate_photo 都被"already running"拒绝。
    static const int kWaitSnapshotTimeoutTicks = 600;

    // ---------------- 视频状态机(Task 9)----------------
    // review 建议:与照片流程字段雷同的地方一旦重复三次以上就该提取——视频流程比照片多一个
    // "两点采集 + 确认 Modal"前置阶段,以及提交/轮询两段网络交互,字段数明显更多,硬塞进
    // MediaManager 本体会让类过胖;单独一个 VideoJob 结构体把"一次视频任务从头到尾"的所有
    // 状态收在一起,MediaManager 只持有一个指针(_video,构造时 new,析构时 delete)。
    struct MediaManager::VideoJob
    {
        enum Phase
        {
            IDLE = 0,           // 空闲
            WAIT_A,             // A 点快照抓取中(还没稳定)
            WAIT_B,             // A 点已就绪,等待用户触发 B 点采集(beginVideoCapture 已完成)
            CAPTURING_B,        // 已触发 B 点采集,B 点快照抓取中
            AWAIT_CONFIRM,      // A/B 都就绪,等待用户在确认 Modal 里点「确认」
            SUBMITTING,         // 已确认,worker 线程正在提交 Veo predictLongRunning 请求
            POLLING,            // 已拿到 operation 名字,worker 线程在轮询直到 done
            DOWNLOADING_FAKE,   // EARTH_AI_FAKE_MP4 路径的"模拟延迟"阶段(见 update() 里的 tick 判断)
            CAPTURING_ORBIT,    // 应用控制同一台相机逐帧走完整 360° 轨迹
            ENCODING_ORBIT,     // 原生 AVFoundation 把已渲染 PNG 帧编码为 H.264 MP4
            CANCELLING          // worker 已收到取消请求；FRAME 只等终态后再 reap
            // review:曾有 DONE_HANDLED 收尾态,但 phase 从未被赋成它——DONE/FAILED/超时
            // 三条路径都是"处理完直接 resetVideo() 回 IDLE"(见 POLLING 分支与
            // DOWNLOADING_FAKE 分支),不存在"下一帧再回 IDLE"这一步,枚举值和对应分支都是
            // 死代码,已删除。
        };

        Phase phase = IDLE;
        int jobId = 0;
        osg::Vec3d llaA, llaB;
        std::string snapPathA, snapPathB;   // 传给 SnapshotGrabber::grab() 的路径(不含 "_0" 后缀)
        std::string style;                  // generate_video 工具的可选风格描述(UI 按钮路径为空串)
        std::string motionPrompt;           // Modal 预览用:buildVideoPrompt 输出(见 updateVideoInternal)
        std::string artifactId;             // timestamp + monotonic request serial, never epoch-only
        std::string mp4Path;                // 最终保存路径(worker 完成后写入)
        std::string generatedFramePathA, generatedFramePathB;
        std::string operationName;          // Veo predictLongRunning 返回的 operation 名字
        bool cinematic = false;
        bool singleAnchor = false;
        CinematicGenerationSettings cinematicSettings;
        PhotoCaptureRequest anchorCapture;
        PhotoCaptureRequest endCapture;
        OneTakeOrbitPlan orbitPlan;
        std::size_t orbitFrameIndex = 0;
        std::vector<std::string> orbitFramePaths;
        // Each route retains its own capture token. A cancellation must never infer ownership
        // from the shared grabber's currently active request, which could already be a photo.
        std::shared_ptr<SnapshotCaptureController> snapCaptureTokenA;
        std::shared_ptr<SnapshotCaptureController> snapCaptureTokenB;
        std::shared_ptr<SnapshotCaptureController> orbitCaptureToken;
        std::string orbitCaptureDir;
        bool hudHidden = false;
        bool hudAdjustScene = true;

        std::thread worker;
        bool workerJoinable = false;
        std::shared_ptr<std::atomic<bool>> workerDone =
            std::make_shared<std::atomic<bool>>(true);
        std::shared_ptr<std::atomic<bool>> cancelRequested =
            std::make_shared<std::atomic<bool>>(false);

        int waitSnapshotTicks = 0;   // WAIT_A/CAPTURING_B 等待快照稳定的计数,超时判定复用 kWaitSnapshotTimeoutTicks
        int fakeDelayTicks = 0;      // DOWNLOADING_FAKE 阶段的模拟延迟计数(约 100 ticks,见 spec)
        int pollTicks = 0;           // POLLING 阶段累计经过的 tick 数(用于超时判定与进度爬升)
        int ticksSinceLastPoll = 0;  // 距上一次发起轮询过去的 tick 数,达到 kPollIntervalTicks 才发下一次 GET
        bool pollInFlight = false;   // 当前是否有一次轮询 worker 正在跑(避免同时发出两个 GET)

        // bug fix:一个"已经跑完但还没被主线程 join"的 std::thread 仍然 joinable()——
        // 之前用 "!v.worker.joinable()" 当"worker 已完成"的信号是错的,join() 之前它永远
        // 是 true,导致 pollInFlight 永远不清零、后续轮询被 :994 的守卫永久拦住(真实 Veo
        // 任务必然撞 10 分钟超时)。改用 worker 主动写的完成标志位 pollDone 来判断。
        // std::atomic<bool> 本身不可移动,而 resetVideo() 里 "*_video = VideoJob()" 是
        // move-assign(std::thread 成员要求),两者冲突;这里选"共享指针包一层"的方案——
        // 侵入最小:VideoJob 依旧可移动/可默认重置,resetVideo() 的"先 join 再整体赋值"
        // 语义不用改。每次起一次新的轮询 worker 前重新 make_shared 一个新 flag(而不是复用
        // 旧的),避免上一轮遗留的 flag 对象被下一轮误读。
        std::shared_ptr<std::atomic<bool>> pollDone = std::make_shared<std::atomic<bool>>(false);
    };

    MediaManager::MediaManager(osgViewer::Viewer* viewer, AICardPanel* cards,
                               const std::string& apiKeyOrEmpty,
                               osgVerse::EarthManipulator* photoManipulator,
                               osg::Camera* captureCamera)
        : _viewer(viewer), _photoManipulator(photoManipulator), _cards(cards),
          _apiKey(apiKeyOrEmpty), _imageModel(resolvedCinematicImageModel()),
          _videoModel(videoModel()), _grabber(captureCamera),
          _state(IDLE), _jobId(0), _photoRequestId(0), _workerJoinable(false),
          _viewRenderUpdateTicks(0),
          _waitSnapshotTicks(0),
          _hudHideCount(0), _captureSceneAdjustmentCount(0),
          _video(new VideoJob)
    {
        std::string ignored;
        _routeCapabilities.hasRealMediaKey = !_apiKey.empty();
        _routeCapabilities.hasFakeImage = fakeImgPath(ignored);
        _routeCapabilities.hasFakeMp4 = fakeMp4Path(ignored);
        _routeCapabilities.videoProvider =
            classifyCinematicVideoProvider(_videoModel);
#if defined(__APPLE__)
        _routeCapabilities.deterministicLocalEncoder = true;
#endif
    }

    MediaManager::~MediaManager()
    {
        joinWorkerIfAny();
        if (_video)
        {
            if (_video->workerJoinable && _video->worker.joinable()) _video->worker.join();
            delete _video;
        }
    }

    PhotoCameraContext MediaManager::currentPhotoCameraContext() const
    {
        if (!_viewer || !_viewer->getCamera() || !_photoManipulator)
            return PhotoCameraContext();
        int viewportWidth = 0;
        int viewportHeight = 0;
        const osg::Viewport* viewport = _viewer->getCamera()->getViewport();
        if (viewport)
        {
            viewportWidth = (int)viewport->width();
            viewportHeight = (int)viewport->height();
        }
        if (viewportWidth <= 0 || viewportHeight <= 0)
        {
            viewportWidth = _contentW;
            viewportHeight = _contentH;
        }
        return makePhotoCameraContext(
            _photoManipulator->computeEyeLatLonHeight(),
            _photoManipulator->computeViewPointLatLonHeight(),
            _viewer->getCamera()->getViewMatrix(),
            _viewer->getCamera()->getProjectionMatrix(),
            viewportWidth, viewportHeight);
    }

    PhotoCameraContext MediaManager::cinematicCameraContext() const
    {
        return currentPhotoCameraContext();
    }

    bool MediaManager::rebuildPhotoCaptureContract()
    {
        if (_photoUsesCinematic)
            return rebuildCinematicImageCaptureContract(
                _captureRequest, _pendingCinematicSettings, _prompt,
                _photoOutputOptions);

        _prompt = buildPhotoPrompt(_captureRequest);
        const CinematicGenerationRequest request = cinematicRequestUnchecked(
            _captureRequest, defaultImageCinematicSettings());
        _photoOutputOptions = cinematicImageOutputOptions(request);
        return true;
    }

    void MediaManager::joinWorkerIfAny()
    {
        if (_workerJoinable && _worker.joinable())
        {
            _worker.join();
            _workerJoinable = false;
        }
    }

    // ---- HUD 隐藏(用户反馈 1)----
    // 只在主线程调用:grab() 触发点(startPhotoJob/beginVideoCapture/captureVideoEnd)与
    // update()/updateVideoInternal() 里"该次快照 ready() 判真/超时/取消"的收尾路径,
    // 均在主线程(渲染线程的 FRAME 回调)执行,与 SnapshotGrabber 本身的线程约束一致。
    // 不碰任何相机 NodeMask(见 ai_media.h 构造函数注释的踩坑记录)——只是个计数器,真正的
    // "跳过 ImGui 内容"发生在 EarthControlUI::runInternal() 读 isHudHidden() 的地方。
    void MediaManager::hudHide(bool adjustScene)
    {
        _hudHideCount.fetch_add(1);
        if (!adjustScene) return;

        const int previousCount = _captureSceneAdjustmentCount.fetch_add(1);
        // 快门补光:第一次进入快门态时保存当前太阳方向,并把太阳对准相机——保证夜面/背光
        // 视角的快照也是亮的(构图参考不能是黑图)。恢复在 hudRestore() 计数归零时。
        // 注意:若用户开了"真实时间太阳",EarthControlUI 每帧会重写 WorldSunDir,可能在
        // 抓帧那一帧盖掉补光(时序取决于 ImGui 回调与渲染顺序)——该场景下补光是尽力而为;
        // update() 在快门期间每 tick 重设一次以尽量赢得竞争(见 WAITING_SNAPSHOT 分支)。
        if (previousCount == 0 && _earth && _earth->commonUniforms.count("WorldSunDir"))
        {
            _earth->commonUniforms["WorldSunDir"]->get(_savedSunDir);
            _sunDirSaved = true;
            // 顺手隐藏路网地名标注层(POI 图标/路名会污染构图参考;提示词虽有 no-map-labels
            // 兜底,但源头干净更稳)。恢复同样在 hudRestore() 归零时。
            if (_earth->commonUniforms.count("LabelOpacity"))
            { _earth->commonUniforms["LabelOpacity"]->get(_savedLabelOpacity);
              _earth->commonUniforms["LabelOpacity"]->set(0.0f); }
            applyFillLight();
        }
    }

    void MediaManager::applyFillLight()
    {
        if (!_earth || !_viewer || !_earth->commonUniforms.count("WorldSunDir")) return;
        osg::Vec3d eye, center, up;
        _viewer->getCamera()->getViewMatrixAsLookAt(eye, center, up);
        osg::Vec3 dir(eye); dir.normalize();
        _earth->commonUniforms["WorldSunDir"]->set(dir);
    }

    void MediaManager::hudRestore(bool adjustScene)
    {
        int hudCount = _hudHideCount.load();
        while (hudCount > 0 &&
               !_hudHideCount.compare_exchange_weak(hudCount, hudCount - 1)) {}
        if (hudCount <= 0) return;   // 防御:没有对应 hide 时不触碰任何计数/场景状态
        if (!adjustScene) return;

        int sceneCount = _captureSceneAdjustmentCount.load();
        while (sceneCount > 0 &&
               !_captureSceneAdjustmentCount.compare_exchange_weak(
                   sceneCount, sceneCount - 1)) {}
        if (sceneCount <= 0) return;   // 防御:不应发生,但 CAS 保证计数不会下溢
        if (sceneCount == 1 && _sunDirSaved && _earth &&
            _earth->commonUniforms.count("WorldSunDir"))
        {
            _earth->commonUniforms["WorldSunDir"]->set(_savedSunDir);   // 快门结束:恢复原太阳
            if (_earth->commonUniforms.count("LabelOpacity"))
                _earth->commonUniforms["LabelOpacity"]->set(_savedLabelOpacity);
            _sunDirSaved = false;
        }
    }

    picojson::value MediaManager::startPhotoJob(const std::string& stylePrompt, const osg::Vec3d& lla,
                                                bool showCameraPlatform)
    {
        if (_state != IDLE)
        {
            picojson::object err; err["error"] = picojson::value(std::string("photo job already running"));
            return picojson::value(err);
        }

        // 失败注入钩子(EARTH_AI_FAKE_IMG_FAIL)本身就不需要一张真图——它的全部目的就是
        // "不生成、直接判失败"——因此单独设置它也应该足以让 Job 起步(不强制同时要求
        // EARTH_AI_KEY 或 EARTH_AI_FAKE_IMG),否则这个测试钩子在没有 key/真图的最常见离线
        // 场景下反而用不了,自相矛盾。
        std::string failMsgCheck; bool hasFailHook = fakeImgFailMsg(failMsgCheck);
        if (!_routeCapabilities.canGenerateImage() && !hasFailHook)
        {
            picojson::object err;
            err["error"] = picojson::value(
                std::string("provider image route is unavailable"));
            return picojson::value(err);
        }

        const long long epoch = (long long)time(nullptr);
        std::string dir = outDir();
        _jobId = _jobs.create("photo", u8"生成实景照片");
        // 秒级时间戳不足以区分快速完成的连续任务（离线/缓存命中时可在同一秒启动第二张）。
        // 追加单调 job id，保证每次生成拥有独立的输入快照与输出文件，绝不覆盖上一张。
        std::string requestId = std::to_string(epoch) + "_" + std::to_string(_jobId);
        _snapPath = dir + "/snap_" + requestId + ".png";
        _genPath = dir + "/gen_" + requestId + ".png";
        _pendingPhotoInput.lla = lla;
        _pendingPhotoInput.style = stylePrompt;
        _pendingPhotoInput.showCameraPlatform = showCameraPlatform;
        _photoUsesCinematic = false;
        _pendingCinematicSettings = defaultImageCinematicSettings();
        _photoRequestId = _jobId;

        _jobs.update(_jobId, AIJob::RUNNING, 0.1f, "", "");
        if (_cards) _cards->pushJob(&_jobs, _jobId, u8"生成实景照片");
        // 工具层可能刚把相机切到本次照片的独立目标。先让新视角完整渲染一帧，再在
        // update() 的 WAITING_VIEW_RENDER 分支触发抓帧，避免捕获切换前的旧 framebuffer。
        _state = WAITING_VIEW_RENDER;
        _viewRenderUpdateTicks = 0;
        _waitSnapshotTicks = 0;  // 重新计数,供 update() 判断等待超时

        OSG_NOTICE << "[AIChat] generate_photo job=" << _jobId << " snap=" << _snapPath << std::endl;

        picojson::object r;
        r["status"] = picojson::value(std::string("started"));
        r["job_id"] = picojson::value((double)_jobId);
        return picojson::value(r);
    }

    picojson::value MediaManager::startCinematicImageJob(
        const CinematicGenerationSettings& settings)
    {
        if (settings.mediaKind != CINEMATIC_IMAGE ||
            settings.motion != CINEMATIC_MOTION_STATIC)
        {
            picojson::object err;
            err["error"] = picojson::value(
                std::string("invalid cinematic image settings"));
            return picojson::value(err);
        }
        if (!cinematicSubmissionCanStart(settings, _routeCapabilities))
        {
            picojson::object err;
            err["error"] = picojson::value(
                std::string("cinematic image provider is unavailable"));
            return picojson::value(err);
        }
        const PhotoCameraContext camera = currentPhotoCameraContext();
        if (!camera.viewTargetValid || camera.viewportWidth <= 0 ||
            camera.viewportHeight <= 0)
        {
            picojson::object err;
            err["error"] = picojson::value(
                std::string("current view has no visible Earth target"));
            return picojson::value(err);
        }
        picojson::value result = startPhotoJob(
            std::string(), camera.viewTargetLla, false);
        if (result.is<picojson::object>() && result.contains("status") &&
            result.get("status").is<std::string>() &&
            result.get("status").get<std::string>() == "started")
        {
            _photoUsesCinematic = true;
            _pendingCinematicSettings = settings;
        }
        return result;
    }

    void MediaManager::update()
    {
        std::vector<VideoUiRequest> requests = _videoRequests.drain();
        for (size_t i = 0; i < requests.size(); ++i)
        {
            const VideoUiRequest& request = requests[i];
            VideoUiDispatchResult result;
            result.dispatched = true;
            result.kind = request.kind;
            if (request.kind == VideoUiRequest::Begin)
            {
                result.succeeded = beginVideoCapture(request.lla, request.style);
                if (!result.succeeded) result.error = "video capture is not idle";
            }
            else if (request.kind == VideoUiRequest::CaptureEnd)
            {
                result.succeeded = captureVideoEnd(request.lla);
                if (!result.succeeded) result.error = "video is not waiting for B";
            }
            else if (request.kind == VideoUiRequest::Confirm)
            {
                picojson::value response = confirmVideo();
                result.succeeded = !(response.is<picojson::object>() &&
                                     response.contains("error") &&
                                     response.get("error").is<std::string>());
                if (!result.succeeded)
                    result.error = response.get("error").get<std::string>();
            }
            else if (request.kind == VideoUiRequest::Cancel)
            {
                cancelVideo();
                result.succeeded = true;
            }
            else if (request.kind == VideoUiRequest::DismissStatus)
            {
                _videoStatusBanner = reduceVideoStatusBanner(
                    _videoStatusBanner, VIDEO_STATUS_DISMISS);
                result.succeeded = true;
            }
            _videoCommandError = reduceVideoCommandError(_videoCommandError, result);
        }

        const std::vector<CinematicUiRequest> cinematicRequests =
            _cinematicRequests.drain();
        for (size_t i = 0; i < cinematicRequests.size(); ++i)
        {
            const CinematicGenerationSettings& settings =
                cinematicRequests[i].settings;
            bool succeeded = false;
            std::string error;
            if (settings.mediaKind == CINEMATIC_IMAGE)
            {
                picojson::value response = startCinematicImageJob(settings);
                succeeded = !(response.is<picojson::object>() &&
                    response.contains("error") &&
                    response.get("error").is<std::string>());
                if (!succeeded)
                    error = response.get("error").get<std::string>();
            }
            else
            {
                succeeded = beginCinematicVideoCapture(settings);
                if (!succeeded)
                    error = "cinematic video capture could not start";
            }
            if (!succeeded)
            {
                _videoCommandError = error;
                if (settings.mediaKind == CINEMATIC_VIDEO)
                    publishVideoFailure(error);
                if (_chatCore)
                    _chatCore->addErrorNote(u8"时空影像请求失败：" + error);
            }
            else
            {
                _videoCommandError.clear();
                clearVideoStatusBanner();
            }
        }

        // 快门期间每 tick 重申补光(对抗"真实时间太阳"每帧重写,见 hudHide 注释)
        if (_captureSceneAdjustmentCount.load() > 0) applyFillLight();
        updateVideoInternal();
        updatePhotoInternal();
        _grabber.reapTerminalGeneration();
        reapDeferredCaptureCleanups();

        VideoUiSnapshot snapshot;
        snapshot.phase = videoPhase();
        snapshot.pending = pendingVideoInfo();
        snapshot.commandError = _videoCommandError;
        snapshot.statusBanner = _videoStatusBanner;
        {
            std::lock_guard<std::mutex> lock(_videoSnapshotMutex);
            _videoSnapshot = snapshot;
        }
    }

    void MediaManager::updatePhotoInternal()
    {
        if (_state == WAITING_VIEW_RENDER)
        {
            // AIFrameHandler 同一 FRAME 内先 drainMainThread()(可能启动本任务)、再 update()。
            // 因此第一次到这里必须只等待，让新相机真正完成一帧渲染；下一帧 update 才预约
            // 快照，保证输入图属于本次任务而不是切换前的 framebuffer。
            if (!photoCaptureHasFreshView(_viewRenderUpdateTicks))
            {
                ++_viewRenderUpdateTicks;
                return;
            }
            if (!_viewer || !_viewer->getCamera() || !_photoManipulator)
            {
                _jobs.update(_jobId, AIJob::FAILED, 1.0f, "", "camera unavailable");
                if (_cards) _cards->removeJob(_jobId);
                if (_chatCore) _chatCore->addErrorNote(u8"照片生成失败：camera unavailable");
                OSG_WARN << "[AIChat] photo job " << _jobId
                         << " failed: camera unavailable" << std::endl;
                _state = IDLE;
                return;
            }
            _captureRequest = makePhotoCaptureRequest(
                _pendingPhotoInput,
                currentPhotoCameraContext(),
                _photoRequestId);
            if (!rebuildPhotoCaptureContract())
            {
                _jobs.update(_jobId, AIJob::FAILED, 1.0f, "",
                             "invalid cinematic capture context");
                if (_cards) _cards->removeJob(_jobId);
                if (_chatCore)
                    _chatCore->addErrorNote(u8"图像生成失败：当前视角无法冻结");
                _state = IDLE;
                return;
            }
            hudHide();
            _photoCaptureToken = _grabber.grab(_snapPath);
            if (!_photoCaptureToken)
            {
                hudRestore();
                _jobs.update(_jobId, AIJob::FAILED, 1.0f, "",
                             "photo capture busy: previous capture is still draining");
                if (_cards) _cards->removeJob(_jobId);
                if (_chatCore)
                    _chatCore->addErrorNote(
                        u8"照片生成失败：前一截图仍在回收");
                OSG_WARN << "[AIChat] photo capture refused: shared snapshot busy" << std::endl;
                _state = IDLE;
                return;
            }
            _state = WAITING_SNAPSHOT;
            _waitSnapshotTicks = 0;
            return;
        }

        if (_state == WAITING_SNAPSHOT)
        {
            if (!_grabber.ready(_photoCaptureToken, _snapPath))
            {
                // 快照文件迟迟不出现(例如捕获回调没被触发/磁盘异常):计数超阈值就判超时,
                // 收尾 job 为 FAILED 并把状态机拉回 IDLE,避免永久卡住导致后续 generate_photo
                // 全部被 startPhotoJob 的 "photo job already running" 拒绝。
                if (++_waitSnapshotTicks > kWaitSnapshotTimeoutTicks)
                {
                    hudRestore();   // 超时收尾:即便快照没成功,HUD 也必须复原,不能永久隐藏
                    std::vector<std::string> paths;
                    if (!_snapPath.empty()) paths.push_back(capturedPath(_snapPath));
                    deferCaptureCleanup(_photoCaptureToken, paths, std::string());
                    _jobs.update(_jobId, AIJob::FAILED, 1.0f, "", "snapshot timeout");
                    if (_cards) _cards->removeJob(_jobId);
                    if (_chatCore) _chatCore->addErrorNote(u8"照片生成失败：snapshot timeout");
                    OSG_WARN << "[AIChat] photo job timeout waiting snapshot" << std::endl;
                    _state = IDLE;
                }
                return;
            }

            // ScreenCaptureHandler writes the framebuffer later in the frame than this FRAME
            // handler reads the camera. If a drag, wheel event, projection change or resize
            // changed the view in between, discard that reference and arm one more capture.
            // Only a frame whose view/projection/viewport still matches its immutable prompt
            // context may proceed to generation.
            const PhotoCameraContext completedFrameCamera =
                currentPhotoCameraContext();
            if (!photoCameraContextsMatchFrame(
                    _captureRequest.camera, completedFrameCamera))
            {
                _captureRequest = makePhotoCaptureRequest(
                    _pendingPhotoInput, completedFrameCamera,
                    _photoRequestId);
                if (!rebuildPhotoCaptureContract())
                {
                    hudRestore();
                    _jobs.update(_jobId, AIJob::FAILED, 1.0f, "",
                                 "invalid cinematic recapture context");
                    if (_cards) _cards->removeJob(_jobId);
                    if (_chatCore)
                        _chatCore->addErrorNote(
                            u8"图像生成失败：当前视角无法重建");
                    _state = IDLE;
                    return;
                }
                _photoCaptureToken = _grabber.grab(_snapPath);
                if (!_photoCaptureToken)
                {
                    hudRestore();
                    _jobs.update(_jobId, AIJob::FAILED, 1.0f, "",
                                 "photo recapture busy: previous capture is still draining");
                    if (_cards) _cards->removeJob(_jobId);
                    if (_chatCore)
                        _chatCore->addErrorNote(
                            u8"照片生成失败：前一截图仍在回收");
                    _state = IDLE;
                    return;
                }
                _waitSnapshotTicks = 0;
                return;
            }
            if (!photoTargetVisibleInCameraContext(
                    _captureRequest.targetLla, _captureRequest.camera))
            {
                hudRestore();
                _jobs.update(_jobId, AIJob::FAILED, 1.0f, "",
                             "photo_target_not_visible");
                if (_cards) _cards->removeJob(_jobId);
                if (_chatCore)
                    _chatCore->addErrorNote(
                        u8"照片生成失败：目标不在当前画面内");
                _state = IDLE;
                return;
            }

            // 本次快照已稳定(ready()==true):HUD 已经不再需要保持隐藏——无论接下来读文件/
            // 生图是否成功,截图这一步已经完成,立即恢复,避免 UI 多等一整个生图耗时才复现。
            hudRestore();
            _grabber.cropToViewport(_snapPath);   // 裁掉帧缓冲比视口多出的未渲染边条

            std::string snapBytes;
            std::string actual = capturedPath(_snapPath);

            if (!readFileBytes(actual, snapBytes) || snapBytes.empty())
            {
                _jobs.update(_jobId, AIJob::FAILED, 1.0f, "", "failed to read snapshot file");
                if (_cards) _cards->removeJob(_jobId);
                if (_chatCore) _chatCore->addErrorNote(u8"照片生成失败：failed to read snapshot file");
                OSG_WARN << "[AIChat] photo job " << _jobId << " failed: cannot read " << actual << std::endl;
                _state = DONE_HANDLED;
                return;
            }

            _jobs.update(_jobId, AIJob::RUNNING, 0.4f, "", "");
            _state = GENERATING;

            joinWorkerIfAny();
            std::string apiKey = _apiKey;
            std::string prompt = _prompt;
            std::string genPath = _genPath;
            CinematicImageOutputOptions outputOptions = _photoOutputOptions;
            int jobId = _jobId;
            JobManager* jobsPtr = &_jobs;
            std::string fakeImg; bool hasFake = fakeImgPath(fakeImg);
            std::string failMsg; bool hasFail = fakeImgFailMsg(failMsg);

            _worker = std::thread([snapBytes, prompt, genPath, outputOptions,
                                   jobId, jobsPtr, apiKey,
                                   hasFake, fakeImg, hasFail, failMsg]()
            {
                std::string outBytes, err;
                if (hasFail)
                {
                    // 失败注入测试钩子(EARTH_AI_FAKE_IMG_FAIL,见 fakeImgFailMsg 注释):
                    // 直接合成失败,不尝试真实/伪造生成——outBytes 留空,下面 "outBytes.empty()"
                    // 分支会把 err 当作 FAILED 的错误消息落进 job。真实用户 shell 里残留这个变量
                    // 会让每次 generate_photo 悄悄跳过真实生成,加一行 WARN 防止误以为在调真接口。
                    OSG_WARN << "[AIChat] EARTH_AI_FAKE_IMG_FAIL active — real photo generation bypassed" << std::endl;
                    err = failMsg;
                }
                else if (hasFake)
                {
                    // 离线 E2E:直接拷贝指定 PNG 的字节作为"生成结果",不联网不需要 key。同上,
                    // 残留的环境变量会让真实用户的请求悄悄跳过真实生成,加一行 WARN 提醒。
                    OSG_WARN << "[AIChat] EARTH_AI_FAKE_IMG active — real photo generation bypassed" << std::endl;
                    if (!readFileBytes(fakeImg, outBytes) || outBytes.empty())
                        err = "EARTH_AI_FAKE_IMG unreadable: " + fakeImg;
                }
                else
                {
                    GeminiMediaProvider provider(apiKey);
                    try
                    {
                        outBytes = provider.generateImage(
                            snapBytes, prompt, err, outputOptions);
                    }
                    catch (const std::exception& e)
                    { err = std::string("provider exception: ") + e.what(); }
                }

                if (outBytes.empty())
                {
                    jobsPtr->update(jobId, AIJob::FAILED, 1.0f, "", err.empty() ? "unknown error" : err);
                    return;
                }
                if (!writeFileBytes(genPath, outBytes))
                {
                    jobsPtr->update(jobId, AIJob::FAILED, 1.0f, "", "failed to write " + genPath);
                    return;
                }
                jobsPtr->update(jobId, AIJob::DONE, 1.0f, genPath, "");
            });
            _workerJoinable = true;
            return;
        }

        if (_state == GENERATING)
        {
            AIJob snap;
            if (!_jobs.get(_jobId, snap)) { _state = DONE_HANDLED; return; }
            if (snap.status == AIJob::DONE)
            {
                joinWorkerIfAny();
                if (_cards) { _cards->removeJob(_jobId); _cards->pushPhoto(snap.resultPath, u8"实景照片"); }
                OSG_NOTICE << "[AIChat] photo job done -> " << snap.resultPath << std::endl;
                _state = DONE_HANDLED;
            }
            else if (snap.status == AIJob::FAILED)
            {
                joinWorkerIfAny();
                if (_cards) _cards->removeJob(_jobId);
                if (_chatCore) _chatCore->addErrorNote(u8"照片生成失败：" + snap.error);
                OSG_WARN << "[AIChat] photo job " << _jobId << " failed: " << snap.error << std::endl;
                _state = DONE_HANDLED;
            }
            return;
        }

        if (_state == DONE_HANDLED)
        {
            // 一次性收尾:回到 IDLE,允许下一次 generate_photo。
            _state = IDLE;
        }
    }

    // ---------------- 视频两点巡航流程实现(Task 9)----------------

    void MediaManager::applyVideoOwnerCommandResult(bool succeeded)
    {
        _videoCommandError = reduceVideoOwnerCommandError(_videoCommandError, succeeded);
        if (succeeded) clearVideoStatusBanner();
    }

    void MediaManager::publishVideoFailure(const std::string& message)
    {
        _videoStatusBanner = reduceVideoStatusBanner(
            _videoStatusBanner, VIDEO_STATUS_FAILURE, message);
        if (_chatCore) _chatCore->addErrorNote(u8"视频生成失败：" + message);
        OSG_WARN << "[AIChat] video failure: " << message << std::endl;
    }

    void MediaManager::clearVideoStatusBanner()
    {
        _videoStatusBanner = reduceVideoStatusBanner(
            _videoStatusBanner, VIDEO_STATUS_SUCCESS);
    }

    VideoPhaseKindPublic MediaManager::videoPhase() const
    {
        switch (_video->phase)
        {
        case VideoJob::IDLE:            return VIDEO_IDLE;
        case VideoJob::WAIT_A:          return VIDEO_WAIT_A;
        case VideoJob::WAIT_B:          return VIDEO_WAIT_B;
        case VideoJob::CAPTURING_B:     return VIDEO_CAPTURING_B;
        case VideoJob::AWAIT_CONFIRM:   return VIDEO_AWAIT_CONFIRM;
        default:                        return VIDEO_RUNNING;   // SUBMITTING/POLLING/DOWNLOADING_FAKE
        }
    }

    bool MediaManager::beginVideoCapture(
        const osg::Vec3d& llaA, const std::string& style,
        const PhotoCaptureRequest* frozenCapture)
    {
        if (_video->phase != VideoJob::IDLE)
        {
            publishVideoFailure("video capture is not idle");
            applyVideoOwnerCommandResult(false);
            return false;
        }
        // The ordinary two-point tool has no cinematic local route.  Reject before
        // capturing A when the selected model cannot consume an end frame.
        if (!frozenCapture && !_routeCapabilities.canGeneratePointToPoint())
        {
            publishVideoFailure("two-point video route is unavailable");
            applyVideoOwnerCommandResult(false);
            return false;
        }

        const long long epoch = (long long)time(nullptr);
        // This is deliberately independent from the wall clock: two requests can start in
        // the same second, and their snapshots/provider artifacts must never alias.
        const long long captureSerial = ++_cinematicRequestSerial;
        std::string dir = outDir();
        _video->llaA = llaA;
        _video->style = style;
        if (frozenCapture)
            _video->anchorCapture = *frozenCapture;
        else
        {
            PhotoRequest input;
            input.lla = llaA;
            input.style = style;
            _video->anchorCapture = makePhotoCaptureRequest(
                input, currentPhotoCameraContext(),
                static_cast<long long>(time(NULL)) * 100000LL +
                    (captureSerial % 100000LL));
        }
        _video->artifactId = std::to_string(epoch) + "_" +
            std::to_string(captureSerial);
        _video->snapPathA = dir + "/tourA_" + _video->artifactId + ".png";
        // 用户反馈 1:视频 A 点抓帧同样要隐藏 HUD——hudRestore() 在 updateVideoInternal()
        // 的 WAIT_A 分支里,该点快照 ready()/超时判定出结果的那一刻立即调用。
        hudHide();
        _video->hudHidden = true;
        _video->hudAdjustScene = true;
        _video->snapCaptureTokenA = _grabber.grab(_video->snapPathA);
        if (!_video->snapCaptureTokenA)
        {
            hudRestore();
            _video->hudHidden = false;
            *_video = VideoJob();
            publishVideoFailure("previous cancelled snapshot is still draining");
            applyVideoOwnerCommandResult(false);
            return false;
        }
        _video->phase = VideoJob::WAIT_A;
        _video->waitSnapshotTicks = 0;

        OSG_NOTICE << "[AIChat] generate_video phase=A snap=" << _video->snapPathA << std::endl;
        applyVideoOwnerCommandResult(true);
        return true;
    }

    bool MediaManager::beginCinematicVideoCapture(
        const CinematicGenerationSettings& settings)
    {
        const CinematicGenerationSettings normalizedSettings =
            normalizedCinematicSubmissionSettings(settings);
        if (normalizedSettings.mediaKind != CINEMATIC_VIDEO ||
            normalizedSettings.motion == CINEMATIC_MOTION_STATIC)
        {
            publishVideoFailure("invalid cinematic video settings");
            return false;
        }
        if (!cinematicSubmissionCanStart(
                normalizedSettings, _routeCapabilities))
        { publishVideoFailure("cinematic video route is unavailable"); return false; }

        const PhotoCameraContext camera = currentPhotoCameraContext();
        if (!camera.viewTargetValid || camera.viewportWidth <= 0 ||
            camera.viewportHeight <= 0)
        { publishVideoFailure("cinematic camera context is invalid"); return false; }

        PhotoRequest input;
        input.lla = camera.viewTargetLla;
        const long long requestId =
            static_cast<long long>(time(NULL)) * 100000LL +
            (++_cinematicRequestSerial % 100000LL);
        const PhotoCaptureRequest capture = makePhotoCaptureRequest(
            input, camera, requestId);

        if (cinematicMotionUsesDeterministicLocalRenderer(
                normalizedSettings.motion))
        {
            // This path is deliberately local: no image-to-video provider is allowed to
            // invent the camera motion.  Preserve the exact world-space camera eye/up,
            // freeze the visible ground target, and rotate both around that target's
            // geodetic vertical.  The provider-based production gate remains false until
            // a manual visual pass approves this rendered-frame path.
            if (normalizedSettings.durationSeconds <
                    cinematicMinimumDurationSeconds(normalizedSettings.motion) ||
                normalizedSettings.durationSeconds > 30)
            { publishVideoFailure("local orbit duration is invalid"); return false; }
        }
        else
        {
            CinematicGenerationRequest validation;
            if (!makeCinematicGenerationRequest(
                    capture, normalizedSettings, validation))
            { publishVideoFailure("cinematic provider request is invalid"); return false; }
        }

        if (!beginVideoCapture(
                camera.cameraEyeLla, std::string(), &capture))
        { publishVideoFailure("cinematic capture could not start"); return false; }
        _video->cinematic = true;
        _video->singleAnchor =
            !cinematicMotionNeedsEndFrame(normalizedSettings.motion);
        _video->cinematicSettings = normalizedSettings;
        return true;
    }

    bool MediaManager::applyDeterministicVideoCamera()
    {
        if (!_viewer || !_viewer->getCamera() ||
            _video->phase != VideoJob::CAPTURING_ORBIT ||
            _video->orbitFrameIndex >= _video->orbitPlan.frames.size())
            return false;
        _viewer->getCamera()->setViewMatrix(
            _video->orbitPlan.frames[_video->orbitFrameIndex].viewMatrix);
        return true;
    }

    bool MediaManager::captureVideoEnd(const osg::Vec3d& llaB)
    {
        // 只有"A 点已经稳定就绪、且还没触发过 B 点采集"这一个阶段允许调用——WAIT_B 是
        // beginVideoCapture 完成后 update() 里自动进入的稳态(见 update()/updateVideo() 里
        // WAIT_A→WAIT_B 的转换),不是"WAIT_A 尚在等待快照稳定"那个瞬态。
        if (_video->phase != VideoJob::WAIT_B)
        {
            applyVideoOwnerCommandResult(false);
            return false;
        }

        std::string dir = outDir();
        _video->llaB = llaB;
        if (_video->cinematic)
        {
            const PhotoCameraContext camera = currentPhotoCameraContext();
            if (!camera.viewTargetValid)
            {
                applyVideoOwnerCommandResult(false);
                return false;
            }
            PhotoRequest input;
            input.lla = camera.viewTargetLla;
            input.style = _video->style;
            _video->endCapture = makePhotoCaptureRequest(
                input, camera, static_cast<long long>(time(NULL)) * 100000LL +
                    (++_cinematicRequestSerial % 100000LL));
        }
        _video->snapPathB = dir + "/tourB_" + _video->artifactId + ".png";
        // 用户反馈 1:B 点抓帧同样要隐藏 HUD——hudRestore() 在 updateVideoInternal() 的
        // CAPTURING_B 分支里,该点快照 ready()/超时判定出结果的那一刻立即调用。
        hudHide();
        _video->hudHidden = true;
        _video->hudAdjustScene = true;
        _video->snapCaptureTokenB = _grabber.grab(_video->snapPathB);
        if (!_video->snapCaptureTokenB)
        {
            hudRestore();
            _video->hudHidden = false;
            _video->phase = VideoJob::WAIT_B;
            publishVideoFailure("previous cancelled snapshot is still draining");
            applyVideoOwnerCommandResult(false);
            return false;
        }
        _video->phase = VideoJob::CAPTURING_B;
        _video->waitSnapshotTicks = 0;

        OSG_NOTICE << "[AIChat] generate_video phase=B snap=" << _video->snapPathB << std::endl;
        applyVideoOwnerCommandResult(true);
        return true;
    }

    MediaManager::PendingVideoInfo MediaManager::pendingVideoInfo() const
    {
        PendingVideoInfo info;
        info.llaA = _video->llaA; info.llaB = _video->llaB;
        info.cinematic = _video->cinematic;
        info.singleAnchor = _video->singleAnchor;
        info.settings = _video->cinematicSettings;
        info.anchorCapture = _video->anchorCapture;
        info.endCapture = _video->endCapture;
        if (info.cinematic)
        {
            // 工作台面向用户报告的是画面中心的地理锚点，而不是相机眼点经纬度；
            // 高度仍使用相机真实椭球高，避免把地面目标的 0 m 误写成拍摄高度。
            info.llaA = _video->anchorCapture.targetLla;
            info.llaA[2] = _video->anchorCapture.camera.cameraEyeLla[2];
            if (!info.singleAnchor)
            {
                info.llaB = _video->endCapture.targetLla;
                info.llaB[2] = _video->endCapture.camera.cameraEyeLla[2];
            }
        }
        info.ready = (_video->phase == VideoJob::AWAIT_CONFIRM);
        if (info.ready) info.motionPrompt = _video->motionPrompt;
        return info;
    }

    MediaManager::VideoUiSnapshot MediaManager::videoUiSnapshot() const
    {
        std::lock_guard<std::mutex> lock(_videoSnapshotMutex);
        return _videoSnapshot;
    }

    picojson::value MediaManager::confirmVideo()
    {
        if (_video->phase != VideoJob::AWAIT_CONFIRM)
        {
            picojson::object err;
            err["error"] = picojson::value(std::string("no video pending confirmation"));
            applyVideoOwnerCommandResult(false);
            return picojson::value(err);
        }

        const bool deterministicOrbit = _video->cinematic &&
            _video->cinematicSettings.motion == CINEMATIC_MOTION_ORBIT_360 &&
            !_video->orbitPlan.frames.empty();
        const bool pointToPoint = !_video->singleAnchor;
        const bool providerRouteAvailable = pointToPoint
            ? _routeCapabilities.canGeneratePointToPoint()
            : _routeCapabilities.canGenerateProviderVideo();
        if ((!deterministicOrbit && !providerRouteAvailable) ||
            (deterministicOrbit && !_routeCapabilities.canRenderLocalOrbit()))
        {
            picojson::object err;
            err["error"] = picojson::value(pointToPoint
                ? std::string("two-point video requires a Veo last-frame-capable model")
                : std::string("provider video route is unavailable"));
            applyVideoOwnerCommandResult(false);
            return picojson::value(err);
        }

        std::string fakeMp4;
        const bool hasFake = fakeMp4Path(fakeMp4);

        _video->jobId = _jobs.create("video", u8"生成巡航视频");
        _video->mp4Path = outDir() + "/tour_" + _video->artifactId + "_" +
            std::to_string(_video->jobId) + ".mp4";
        _jobs.update(_video->jobId, AIJob::RUNNING, 0.2f, "", "");
        if (_cards) _cards->pushJob(&_jobs, _video->jobId, u8"生成巡航视频");

        if (deterministicOrbit)
        {
            _video->orbitCaptureDir = outDir() + "/orbit_frames_" +
                _video->artifactId + "_" + std::to_string(_video->jobId);
            if (!osgDB::makeDirectory(_video->orbitCaptureDir))
            {
                _jobs.update(_video->jobId, AIJob::FAILED, 1.0f, "",
                             "cannot create deterministic orbit frame directory");
                if (_cards) _cards->removeJob(_video->jobId);
                publishVideoFailure("cannot create deterministic orbit frame directory");
                resetVideo();
                picojson::object err;
                err["error"] = picojson::value(std::string(
                    "cannot create deterministic orbit frame directory"));
                applyVideoOwnerCommandResult(false);
                return picojson::value(err);
            }
            _video->orbitFrameIndex = 0;
            _video->orbitFramePaths.clear();
            std::ostringstream frameName;
            frameName << _video->orbitCaptureDir << "/frame_"
                      << std::setw(6) << std::setfill('0')
                      << _video->orbitFrameIndex << ".png";
            // 本地确定性环拍必须逐帧保留用户正在看的太阳、时间与标注状态；这里只隐藏
            // 应用 UI，不能套用 AI 参考图的补光/去标注处理。
            hudHide(false);
            _video->hudHidden = true;
            _video->hudAdjustScene = false;
            _video->orbitCaptureToken = _grabber.grab(frameName.str());
            if (!_video->orbitCaptureToken)
            {
                hudRestore(false); _video->hudHidden = false;
                _jobs.update(_video->jobId, AIJob::FAILED, 1.0f, "",
                             "deterministic orbit capture busy");
                if (_cards) _cards->removeJob(_video->jobId);
                publishVideoFailure("deterministic orbit capture busy");
                resetVideo();
                picojson::object err;
                err["error"] = picojson::value(std::string(
                    "deterministic orbit capture busy"));
                applyVideoOwnerCommandResult(false);
                return picojson::value(err);
            }
            _video->orbitFramePaths.push_back(capturedPath(frameName.str()));
            _video->waitSnapshotTicks = 0;
            _video->phase = VideoJob::CAPTURING_ORBIT;
            OSG_NOTICE << "[AIChat] deterministic orbit capture started frames="
                       << _video->orbitPlan.frames.size() << std::endl;
        }
        else if (hasFake)
        {
            // 离线 E2E:跳过网络,模拟一个短延迟(~100 ticks,见类头 spec)后直接"完成"。
            _video->fakeDelayTicks = 0;
            _video->phase = VideoJob::DOWNLOADING_FAKE;
            OSG_NOTICE << "[AIChat] video autotest confirm (fake mp4=" << fakeMp4 << ")" << std::endl;
        }
        else
        {
            // 用户反馈 3(照片先行的视频管线):产品原始设计是"生成的照片(而非原始截图)
            // 作为视频起始帧"。worker 线程内先跑 banana 生图步骤——
            //   Omni 路径:只生成 A 点照片(banana photo A)→ OmniVideoProvider::generate(照片A, ...)
            //   Veo  路径:A/B 两点各生成一张照片(banana photo A、B)→ VeoVideoProvider::submit(照片A, 照片B, ...)
            // 生成的照片落盘为本请求唯一的 frameA_/frameB_ artifact（便于人工核验/复用）。
            // 再喂给视频 provider——不再直接把原始渲染截图(snapA/snapB)传给视频模型。
            // 提示词也换用 buildVideoPrompt(ai_prompts.h):geo 上下文 + A->B 轨迹 + 电影运镜语言,
            // 取代旧的纯 buildMotionPrompt(motionPrompt 仍保留用于 Modal 预览,见 pendingVideoInfo)。
            //
            // 注意:视频用 _video->worker 这个独立线程句柄,与照片流程的 _worker 完全分开
            // (两条流程可能同时在跑),因此这里 join 的是 _video->worker 而非 _worker。
            std::string snapA = _video->snapPathA, snapB = _video->snapPathB;
            osg::Vec3d llaA = _video->llaA, llaB = _video->llaB;
            std::string style = _video->style;
            const bool cinematic = _video->cinematic;
            const bool hasEndFrame = !_video->singleAnchor;
            std::string videoPrompt = _video->motionPrompt;
            if (videoPrompt.empty())
                videoPrompt = buildVideoPrompt(llaA, llaB, style);
            std::string photoPromptA = buildPhotoPrompt(llaA, style);
            std::string photoPromptB = buildPhotoPrompt(llaB, style);
            CinematicImageOutputOptions outputA, outputB;
            CinematicVideoOutputOptions videoOutput;
            if (cinematic)
            {
                const CinematicGenerationRequest requestA =
                    cinematicRequestUnchecked(
                        _video->anchorCapture,
                        _video->cinematicSettings);
                photoPromptA = buildCinematicImagePrompt(requestA);
                outputA = cinematicImageOutputOptions(requestA);
                videoOutput = cinematicVideoOutputOptions(requestA);
                if (hasEndFrame)
                {
                    const CinematicGenerationRequest requestB =
                        cinematicRequestUnchecked(
                            _video->endCapture,
                            _video->cinematicSettings);
                    photoPromptB = buildCinematicImagePrompt(requestB);
                    outputB = cinematicImageOutputOptions(requestB);
                }
            }
            std::string apiKey = _apiKey;
            std::string model = _videoModel;
            const CinematicVideoProviderKind providerKind =
                _routeCapabilities.videoProvider;
            int jobId = _video->jobId;
            JobManager* jobsPtr = &_jobs;
            _video->generatedFramePathA = outDir() + "/frameA_" +
                _video->artifactId + "_" + std::to_string(jobId) + ".png";
            _video->generatedFramePathB = hasEndFrame
                ? outDir() + "/frameB_" + _video->artifactId + "_" +
                    std::to_string(jobId) + ".png"
                : std::string();
            const std::string framePathA = _video->generatedFramePathA;
            const std::string framePathB = _video->generatedFramePathB;

            const bool useOmni = providerKind == CINEMATIC_VIDEO_PROVIDER_OMNI;
            std::string mp4Path = _video->mp4Path;

            // EARTH_AI_FAKE_IMG:离线自测钩子——即使不是 EARTH_AI_FAKE_MP4 整链路跳过,视频
            // worker 内部的 banana 生图这一步也应支持用固定 PNG 代替网络调用(与照片流程的
            // fakeImgPath 用法完全一致),这样 EARTH_AI_VIDEO_AUTOTEST 才能在没有 EARTH_AI_KEY
            // 的情况下把"生图->生视频"这条链路真正跑一遍(即使最终视频仍走 EARTH_AI_FAKE_MP4
            // 短路)。
            std::string fakeImg; bool hasFakeImg = fakeImgPath(fakeImg);

            if (_video->workerJoinable && _video->worker.joinable() &&
                _video->workerDone->load()) _video->worker.join();
            _video->workerDone = std::make_shared<std::atomic<bool>>(false);
            _video->cancelRequested = std::make_shared<std::atomic<bool>>(false);
            std::shared_ptr<std::atomic<bool>> doneFlag = _video->workerDone;
            std::shared_ptr<std::atomic<bool>> cancelFlag = _video->cancelRequested;
            _video->worker = std::thread([snapA, snapB, videoPrompt,
                                          photoPromptA, photoPromptB,
                                          outputA, outputB, videoOutput,
                                          apiKey, model,
                                          jobId, jobsPtr, useOmni,
                                          hasEndFrame, mp4Path, framePathA, framePathB,
                                          hasFakeImg, fakeImg, doneFlag, cancelFlag]()
            {
                struct DoneSetter { std::shared_ptr<std::atomic<bool>> flag;
                    ~DoneSetter() { flag->store(true); } } done{doneFlag};
                if (cancelFlag->load()) return;
                std::string actualA = capturedPath(snapA), actualB = capturedPath(snapB);
                std::string rawA, rawB;
                if (!readFileBytes(actualA, rawA) || rawA.empty()
                    || (!useOmni && hasEndFrame &&
                        (!readFileBytes(actualB, rawB) || rawB.empty())))
                {
                    jobsPtr->update(jobId, AIJob::FAILED, 1.0f, "", "failed to read A/B snapshot files");
                    return;
                }

                // ---- Step 1:banana 生图(A 点必做;Veo 路径 B 点也做)----
                auto genPhoto = [&](const std::string& rawBytes,
                                    const std::string& prompt,
                                    const CinematicImageOutputOptions& output,
                                    const std::string& savePath,
                                    std::string& outBytes, std::string& err) -> bool
                {
                    if (hasFakeImg)
                    {
                        if (!readFileBytes(fakeImg, outBytes) || outBytes.empty())
                        { err = "EARTH_AI_FAKE_IMG unreadable: " + fakeImg; return false; }
                    }
                    else
                    {
                        GeminiMediaProvider provider(apiKey);
                        try
                        {
                            outBytes = provider.generateImage(
                                rawBytes, prompt, err, output);
                        }
                        catch (const std::exception& e) { err = std::string("provider exception: ") + e.what(); }
                    }
                    if (outBytes.empty()) { if (err.empty()) err = "unknown banana error"; return false; }
                    if (!writeFileBytes(savePath, outBytes)) { err = "failed to write " + savePath; return false; }
                    return true;
                };

                std::string photoA, errA;
                if (!genPhoto(rawA, photoPromptA, outputA,
                              framePathA, photoA, errA))
                {
                    jobsPtr->update(jobId, AIJob::FAILED, 1.0f, "", "banana photo A failed: " + errA);
                    return;
                }
                if (cancelFlag->load()) return;
                jobsPtr->creepProgress(jobId, 0.25f);   // 0.25 after banana(A);SUBMITTING 阶段其余进度沿用旧的爬升逻辑

                std::string photoB, errB;
                if (!useOmni && hasEndFrame)
                {
                    if (!genPhoto(rawB, photoPromptB, outputB,
                                  framePathB, photoB, errB))
                    {
                        jobsPtr->update(jobId, AIJob::FAILED, 1.0f, "", "banana photo B failed: " + errB);
                        return;
                    }
                    if (cancelFlag->load()) return;
                }

                // ---- Step 2:把生成的照片(而非原始截图)喂给视频 provider ----
                if (useOmni)
                {
                    // Omni(Interactions API):同步一步到位——banana 照片 A + 运镜提示词进模型,
                    // 直接拿回 mp4 字节(B 帧不上传:官方不支持首尾帧插值,B 点已折进 prompt)。
                    // 无 operation 概念,worker 内直接写盘并把 job 置 DONE;主线程 SUBMITTING
                    // 分支的 DONE 检查负责收尾(推卡片/清状态),不进入 POLLING。
                    OmniVideoProvider provider(apiKey, model);
                    std::string err, mp4Bytes;
                    bool ok = false;
                    try
                    {
                        ok = provider.generate(
                            photoA, videoPrompt, videoOutput, mp4Bytes, err);
                    }
                    catch (const std::exception& e) { err = std::string("provider exception: ") + e.what(); }
                    if (cancelFlag->load()) return;
                    if (!ok || mp4Bytes.empty())
                    {
                        jobsPtr->update(jobId, AIJob::FAILED, 1.0f, "",
                                        err.empty() ? "omni generate failed" : err);
                        return;
                    }
                    if (!writeFileBytes(mp4Path, mp4Bytes))
                    {
                        jobsPtr->update(jobId, AIJob::FAILED, 1.0f, "", "failed to write " + mp4Path);
                        return;
                    }
                    jobsPtr->update(jobId, AIJob::DONE, 1.0f, mp4Path, "");
                    return;
                }

                VeoVideoProvider provider(apiKey, model);
                std::string err, opName;
                if (cancelFlag->load()) return;
                try { opName = provider.submit(photoA, photoB, videoPrompt, err); }
                catch (const std::exception& e) { err = std::string("provider exception: ") + e.what(); }

                if (opName.empty())
                {
                    jobsPtr->update(jobId, AIJob::FAILED, 1.0f, "", err.empty() ? "submit failed" : err);
                    return;
                }
                // resultPath 字段借用来传递 operationName(见上方注释),progress 0.3 标志
                // "已提交、开始轮询"。
                jobsPtr->update(jobId, AIJob::RUNNING, 0.3f, opName, "");
            });
            _video->workerJoinable = true;
            _video->phase = VideoJob::SUBMITTING;
        }

        OSG_NOTICE << "[AIChat] generate_video confirmed job=" << _video->jobId << std::endl;
        picojson::object r;
        r["status"] = picojson::value(std::string("started"));
        r["job_id"] = picojson::value((double)_video->jobId);
        applyVideoOwnerCommandResult(true);
        return picojson::value(r);
    }

    void MediaManager::cancelVideo()
    {
        applyVideoOwnerCommandResult(true);
        if (_video->phase == VideoJob::IDLE) return;
        OSG_NOTICE << "[AIChat] generate_video cancelled (phase="
                   << (int)_video->phase << ")" << std::endl;
        const bool workerLive = _video->workerJoinable &&
            _video->worker.joinable() && !_video->workerDone->load();
        if (classifyVideoCancellation(workerLive) == VIDEO_CANCEL_ASYNC_REAP)
        {
            // Provider billing may already have started after submit; this prevents additional
            // local work/polls but never pretends to revoke a remote provider charge.
            _video->cancelRequested->store(true);
            _video->phase = VideoJob::CANCELLING;
            if (_video->jobId > 0)
                _jobs.update(_video->jobId, AIJob::RUNNING, 1.0f, "", "cancelling");
            return;
        }
        finalizeVideoCancellation();
    }

    void MediaManager::finalizeVideoCancellation()
    {
        VideoJob& v = *_video;
        if (v.hudHidden)
        {
            hudRestore(v.hudAdjustScene);
            v.hudHidden = false;
        }
        if (v.jobId > 0)
        {
            AIJob existing;
            if (_jobs.get(v.jobId, existing) && existing.status == AIJob::RUNNING)
                _jobs.update(v.jobId, AIJob::FAILED, 1.0f, "", "cancelled");
            if (_cards) _cards->removeJob(v.jobId);
        }
        deferCancelledVideoCaptureCleanup();
        resetVideo(false);
    }

    void MediaManager::deferCaptureCleanup(
        const std::shared_ptr<SnapshotCaptureController>& capture,
        const std::vector<std::string>& paths, const std::string& directory)
    {
        DeferredCaptureCleanup cleanup;
        cleanup.capture = capture;
        cleanup.paths = paths;
        cleanup.directory = directory;
        _grabber.retire(cleanup.capture);
        if (!cleanup.paths.empty() || !cleanup.directory.empty())
            _deferredCaptureCleanups.push_back(cleanup);
    }

    void MediaManager::deferVideoCaptureCleanup(
        const std::shared_ptr<SnapshotCaptureController>& capture)
    {
        VideoJob& v = *_video;
        std::vector<std::string> paths;
        if (!v.snapPathA.empty()) paths.push_back(capturedPath(v.snapPathA));
        if (!v.snapPathB.empty()) paths.push_back(capturedPath(v.snapPathB));
        if (!v.generatedFramePathA.empty()) paths.push_back(v.generatedFramePathA);
        if (!v.generatedFramePathB.empty()) paths.push_back(v.generatedFramePathB);
        if (!v.mp4Path.empty()) paths.push_back(v.mp4Path);
        paths.insert(paths.end(), v.orbitFramePaths.begin(), v.orbitFramePaths.end());
        deferCaptureCleanup(capture, paths, v.orbitCaptureDir);
    }

    void MediaManager::deferCancelledVideoCaptureCleanup()
    {
        VideoJob& v = *_video;
        std::shared_ptr<SnapshotCaptureController> capture;
        if (v.phase == VideoJob::WAIT_A)
            capture = v.snapCaptureTokenA;
        else if (v.phase == VideoJob::CAPTURING_B)
            capture = v.snapCaptureTokenB;
        else if (v.phase == VideoJob::CAPTURING_ORBIT)
            capture = v.orbitCaptureToken;
        else if (v.snapCaptureTokenB)
            capture = v.snapCaptureTokenB;
        else
            capture = v.snapCaptureTokenA;
        deferVideoCaptureCleanup(capture);
    }

    void MediaManager::reapDeferredCaptureCleanups()
    {
        for (std::size_t index = 0; index < _deferredCaptureCleanups.size(); )
        {
            DeferredCaptureCleanup& cleanup = _deferredCaptureCleanups[index];
            if (cleanup.capture && !cleanup.capture->terminal())
            {
                ++index;
                continue;
            }

            std::string failedPath;
            for (std::size_t pathIndex = 0; pathIndex < cleanup.paths.size(); ++pathIndex)
            {
                const std::string& path = cleanup.paths[pathIndex];
                if (!osgDB::fileExists(path)) continue;  // ENOENT is already clean.
                if (std::remove(path.c_str()) != 0 && osgDB::fileExists(path))
                {
                    failedPath = path;
                    break;
                }
            }
#if !defined(_WIN32)
            if (failedPath.empty() && !cleanup.directory.empty() &&
                osgDB::fileExists(cleanup.directory) &&
                ::rmdir(cleanup.directory.c_str()) != 0 &&
                osgDB::fileExists(cleanup.directory))
                failedPath = cleanup.directory;
#endif
            if (!failedPath.empty())
            {
                if (!cleanup.failureReported)
                {
                    cleanup.failureReported = true;
                    publishVideoFailure("cancelled capture cleanup failed: " + failedPath);
                }
                ++index;  // Keep retrying later without hiding a persistent failure.
                continue;
            }
            _deferredCaptureCleanups.erase(_deferredCaptureCleanups.begin() + index);
        }
    }

    // std::thread 的 move 赋值要求目标对象此刻不 joinable,否则直接 std::terminate——
    // FRAME 永远不得 join 活 worker。若有未终态 worker，转换为 CANCELLING，由后续
    // FRAME tick 在 workerDone 发布后收割；这让 Esc/按钮取消始终即时。
    void MediaManager::resetVideo(bool removeOrbitArtifacts)
    {
        if (_video->workerJoinable && _video->worker.joinable())
        {
            if (!_video->workerDone->load())
            {
                _video->cancelRequested->store(true);
                _video->phase = VideoJob::CANCELLING;
                return;
            }
            _video->worker.join();
            _video->workerJoinable = false;
        }
        if (removeOrbitArtifacts)
        {
            for (std::size_t index = 0; index < _video->orbitFramePaths.size(); ++index)
                std::remove(_video->orbitFramePaths[index].c_str());
#if !defined(_WIN32)
            if (!_video->orbitCaptureDir.empty())
                ::rmdir(_video->orbitCaptureDir.c_str());
#endif
            // A failed/timeout job may have written one generated provider frame or a
            // partial MP4 before the worker reported failure. Successful artifacts remain
            // available to the user; every non-DONE job is removed here.
            AIJob existing;
            const bool succeeded = _video->jobId > 0 &&
                _jobs.get(_video->jobId, existing) && existing.status == AIJob::DONE;
            if (!succeeded)
            {
                if (!_video->generatedFramePathA.empty())
                    std::remove(_video->generatedFramePathA.c_str());
                if (!_video->generatedFramePathB.empty())
                    std::remove(_video->generatedFramePathB.c_str());
                if (!_video->mp4Path.empty()) std::remove(_video->mp4Path.c_str());
            }
        }
        *_video = VideoJob();
    }

    // 每帧驱动视频状态机:被 update() 在末尾调用(主线程,与照片状态机同一 update() tick 内)。
    void MediaManager::updateVideoInternal()
    {
        VideoJob& v = *_video;

        if (v.phase == VideoJob::CANCELLING)
        {
            if (!v.workerDone->load()) return;
            if (v.workerJoinable && v.worker.joinable()) v.worker.join();
            v.workerJoinable = false;
            finalizeVideoCancellation();
            return;
        }

        if (v.phase == VideoJob::WAIT_A)
        {
            if (!_grabber.ready(v.snapCaptureTokenA, v.snapPathA))
            {
                if (++v.waitSnapshotTicks > kWaitSnapshotTimeoutTicks)
                {
                    hudRestore(); v.hudHidden = false; // 超时:HUD 必须复原
                    deferVideoCaptureCleanup(v.snapCaptureTokenA);
                    publishVideoFailure("A-point snapshot timeout");
                    resetVideo();
                }
                return;
            }
            // A 点快照已稳定:抓帧已完成,立即恢复 HUD(不必等到 B 点/确认/生成全部结束)。
            hudRestore(); v.hudHidden = false;
            const PhotoCameraContext completedFrameCamera = currentPhotoCameraContext();
            if (v.cinematic && !completedFrameCamera.viewTargetValid)
            {
                publishVideoFailure("cinematic A capture has no visible target");
                resetVideo();
                return;
            }
            if (v.cinematic && cinematicVideoCaptureNeedsRearm(
                    v.anchorCapture, completedFrameCamera))
            {
                PhotoCaptureRequest rebuilt;
                if (!rebuildCinematicVideoCapture(
                        v.anchorCapture, completedFrameCamera, rebuilt))
                {
                    publishVideoFailure("cinematic A capture lost its visible target");
                    resetVideo();
                    return;
                }
                v.anchorCapture = rebuilt;
                hudHide(); v.hudHidden = true; v.hudAdjustScene = true;
                v.snapCaptureTokenA = _grabber.grab(v.snapPathA);
                if (!v.snapCaptureTokenA)
                {
                    hudRestore(); v.hudHidden = false;
                    publishVideoFailure("A-point recapture busy");
                    resetVideo();
                    return;
                }
                v.waitSnapshotTicks = 0;
                return;
            }
            _grabber.cropToViewport(v.snapPathA);   // 裁掉未渲染边条
            if (v.cinematic && v.singleAnchor)
            {
                v.llaB = v.llaA;
                v.snapPathB.clear();
                if (v.cinematicSettings.motion == CINEMATIC_MOTION_ORBIT_360)
                {
                    if (!makeCinematicOneTakeOrbitPlan(
                            v.anchorCapture,
                            v.cinematicSettings.durationSeconds, v.orbitPlan))
                    {
                        publishVideoFailure("accepted orbit capture cannot form a plan");
                        resetVideo();
                        return;
                    }
                    std::ostringstream contract;
                    contract << u8"本地确定性 360° 一镜到底：固定当前画面中心，"
                             << v.orbitPlan.frames.size() << u8" 个连续渲染帧，"
                             << v.orbitPlan.framesPerSecond << u8" fps；"
                             << u8"不调用生成模型编造运镜，不允许切镜或重置机位。";
                    v.motionPrompt = contract.str();
                }
                else
                {
                    CinematicGenerationRequest request = cinematicRequestUnchecked(
                        v.anchorCapture, v.cinematicSettings);
                    v.motionPrompt = buildCinematicVideoPrompt(request);
                }
                v.phase = VideoJob::AWAIT_CONFIRM;
                OSG_NOTICE << "[AIChat] cinematic single-anchor video awaiting confirm"
                           << std::endl;
                return;
            }
            // 进入"等待用户触发 B 点"的稳态,不做任何自动跳转(beginVideoCapture 只负责起个头,
            // 真正的下一步由用户移动相机后再次调用 captureVideoEnd 触发——这里只是把"抓帧中"
            // 过渡到"抓帧已就绪"这两个瞬态/稳态分开)。
            v.phase = VideoJob::WAIT_B;
            return;
        }

        if (v.phase == VideoJob::CAPTURING_B)
        {
            if (!_grabber.ready(v.snapCaptureTokenB, v.snapPathB))
            {
                if (++v.waitSnapshotTicks > kWaitSnapshotTimeoutTicks)
                {
                    hudRestore(); v.hudHidden = false; // 超时:HUD 必须复原
                    deferVideoCaptureCleanup(v.snapCaptureTokenB);
                    publishVideoFailure("B-point snapshot timeout");
                    resetVideo();
                }
                return;
            }
            // B 点快照就绪:抓帧已完成,立即恢复 HUD。
            hudRestore(); v.hudHidden = false;
            const PhotoCameraContext completedFrameCamera = currentPhotoCameraContext();
            if (v.cinematic && !completedFrameCamera.viewTargetValid)
            {
                publishVideoFailure("cinematic B capture has no visible target");
                resetVideo();
                return;
            }
            if (v.cinematic && cinematicVideoCaptureNeedsRearm(
                    v.endCapture, completedFrameCamera))
            {
                PhotoCaptureRequest rebuilt;
                if (!rebuildCinematicVideoCapture(
                        v.endCapture, completedFrameCamera, rebuilt))
                {
                    publishVideoFailure("cinematic B capture lost its visible target");
                    resetVideo();
                    return;
                }
                v.endCapture = rebuilt;
                hudHide(); v.hudHidden = true; v.hudAdjustScene = true;
                v.snapCaptureTokenB = _grabber.grab(v.snapPathB);
                if (!v.snapCaptureTokenB)
                {
                    hudRestore(); v.hudHidden = false;
                    publishVideoFailure("B-point recapture busy");
                    resetVideo();
                    return;
                }
                v.waitSnapshotTicks = 0;
                return;
            }
            _grabber.cropToViewport(v.snapPathB);   // 裁掉未渲染边条
            // 生成视频提示词预览(用户反馈 3:改用 buildVideoPrompt——geo 上下文 + A->B 轨迹 +
            // 电影运镜语言,取代旧的纯 buildMotionPrompt 轨迹句子;buildVideoPrompt 内部仍会
            // 调 buildMotionPrompt 拼轨迹描述,不重复实现)。
            if (v.cinematic)
            {
                CinematicGenerationRequest request = cinematicRequestUnchecked(
                    v.anchorCapture, v.cinematicSettings);
                request.endAnchor = v.endCapture;
                request.hasEndAnchor = true;
                v.motionPrompt = buildCinematicVideoPrompt(request);
            }
            else
                v.motionPrompt = buildVideoPrompt(v.llaA, v.llaB, v.style);
            v.phase = VideoJob::AWAIT_CONFIRM;
            OSG_NOTICE << "[AIChat] generate_video awaiting confirm, prompt=" << v.motionPrompt << std::endl;
            return;
        }

        if (v.phase == VideoJob::CAPTURING_ORBIT)
        {
            if (v.orbitFrameIndex >= v.orbitPlan.frames.size() ||
                v.orbitFramePaths.size() != v.orbitFrameIndex + 1)
            {
                hudRestore(false); v.hudHidden = false;
                _jobs.update(v.jobId, AIJob::FAILED, 1.0f, "",
                             "deterministic orbit frame state is inconsistent");
                if (_cards) _cards->removeJob(v.jobId);
                publishVideoFailure(u8"环拍失败：逐帧状态不一致");
                resetVideo();
                return;
            }

            std::ostringstream currentName;
            currentName << v.orbitCaptureDir << "/frame_"
                        << std::setw(6) << std::setfill('0')
                        << v.orbitFrameIndex << ".png";
            const std::string currentRequestPath = currentName.str();
            if (!_grabber.ready(v.orbitCaptureToken, currentRequestPath))
            {
                if (++v.waitSnapshotTicks > kWaitSnapshotTimeoutTicks)
                {
                    hudRestore(false); v.hudHidden = false;
                    deferVideoCaptureCleanup(v.orbitCaptureToken);
                    _jobs.update(v.jobId, AIJob::FAILED, 1.0f, "",
                                 "deterministic orbit frame capture timeout");
                    if (_cards) _cards->removeJob(v.jobId);
                    publishVideoFailure(u8"环拍失败：逐帧截图超时");
                    resetVideo(false);
                }
                return;
            }

            _grabber.cropToViewport(currentRequestPath);
            ++v.orbitFrameIndex;
            const float capturedProgress = static_cast<float>(
                v.orbitFrameIndex) / static_cast<float>(v.orbitPlan.frames.size());
            _jobs.update(v.jobId, AIJob::RUNNING,
                         0.1f + 0.75f * capturedProgress, "", "");
            if (v.orbitFrameIndex < v.orbitPlan.frames.size())
            {
                std::ostringstream nextName;
                nextName << v.orbitCaptureDir << "/frame_"
                         << std::setw(6) << std::setfill('0')
                         << v.orbitFrameIndex << ".png";
                v.orbitCaptureToken = _grabber.grab(nextName.str());
                if (!v.orbitCaptureToken)
                {
                    hudRestore(false); v.hudHidden = false;
                    _jobs.update(v.jobId, AIJob::FAILED, 1.0f, "",
                                 "deterministic orbit capture busy");
                    if (_cards) _cards->removeJob(v.jobId);
                    publishVideoFailure("deterministic orbit capture busy");
                    resetVideo();
                    return;
                }
                v.orbitFramePaths.push_back(capturedPath(nextName.str()));
                v.waitSnapshotTicks = 0;
                return;
            }

            // Camera override ends before encoding. The original EarthManipulator was
            // never mutated, so the next normal frame restores the user's exact view.
            hudRestore(false); v.hudHidden = false;
            const std::vector<std::string> frames = v.orbitFramePaths;
            const int fps = v.orbitPlan.framesPerSecond;
            const std::string output = v.mp4Path;
            const int jobId = v.jobId;
            JobManager* jobs = &_jobs;
            if (v.workerJoinable && v.worker.joinable() && v.workerDone->load())
                v.worker.join();
            v.workerDone = std::make_shared<std::atomic<bool>>(false);
            v.cancelRequested = std::make_shared<std::atomic<bool>>(false);
            std::shared_ptr<std::atomic<bool>> doneFlag = v.workerDone;
            std::shared_ptr<std::atomic<bool>> cancelFlag = v.cancelRequested;
            v.worker = std::thread([frames, fps, output, jobId, jobs, doneFlag, cancelFlag]()
            {
                struct DoneSetter { std::shared_ptr<std::atomic<bool>> flag;
                    ~DoneSetter() { flag->store(true); } } done{doneFlag};
                std::string error;
                if (!encodePngSequenceToH264Mp4(
                        frames, fps, output, error, cancelFlag.get()))
                {
                    jobs->update(jobId, AIJob::FAILED, 1.0f, "",
                        error.empty() ? "native orbit encoder failed" : error);
                    return;
                }
                jobs->update(jobId, AIJob::DONE, 1.0f, output, "");
            });
            v.workerJoinable = true;
            v.phase = VideoJob::ENCODING_ORBIT;
            OSG_NOTICE << "[AIChat] deterministic orbit encoding started -> "
                       << output << std::endl;
            return;
        }

        if (v.phase == VideoJob::ENCODING_ORBIT)
        {
            AIJob snap;
            if (!_jobs.get(v.jobId, snap))
            { publishVideoFailure("deterministic orbit job disappeared"); resetVideo(); return; }
            if (snap.status == AIJob::RUNNING)
            {
                _jobs.creepProgress(v.jobId,
                    std::min(0.97f, snap.progress + 0.0008f));
                return;
            }
            if (!v.workerDone->load()) return;
            if (v.workerJoinable && v.worker.joinable()) v.worker.join();
            v.workerJoinable = false;
            if (_cards) _cards->removeJob(v.jobId);
            if (snap.status == AIJob::DONE)
            {
                if (_cards) _cards->pushPhoto(
                    snap.resultPath, u8"360° 一镜到底环拍", true);
                OSG_NOTICE << "[AIChat] deterministic orbit done -> "
                           << snap.resultPath << std::endl;
            }
            else
            {
                publishVideoFailure(u8"环拍编码失败：" + snap.error);
            }
            resetVideo();
            return;
        }

        if (v.phase == VideoJob::DOWNLOADING_FAKE)
        {
            // EARTH_AI_FAKE_MP4 路径:模拟约 100 ticks 的"生成延迟"(spec 要求),
            // 让 E2E 断言能观察到 JOB 卡的进行中状态,而不是同一帧就瞬间完成。
            ++v.fakeDelayTicks;
            float prog = 0.3f + 0.6f * std::min(1.0f, (float)v.fakeDelayTicks / 100.0f);
            _jobs.update(v.jobId, AIJob::RUNNING, prog, "", "");
            if (v.fakeDelayTicks < 100) return;

            std::string fakeMp4; fakeMp4Path(fakeMp4);
            std::string bytes;
            if (!readFileBytes(fakeMp4, bytes) || bytes.empty())
            {
                _jobs.update(v.jobId, AIJob::FAILED, 1.0f, "", "EARTH_AI_FAKE_MP4 unreadable: " + fakeMp4);
                if (_cards) _cards->removeJob(v.jobId);
                publishVideoFailure("EARTH_AI_FAKE_MP4 unreadable: " + fakeMp4);
                resetVideo();
                return;
            }
            if (!writeFileBytes(v.mp4Path, bytes))
            {
                _jobs.update(v.jobId, AIJob::FAILED, 1.0f, "", "failed to write " + v.mp4Path);
                if (_cards) _cards->removeJob(v.jobId);
                publishVideoFailure("failed to write " + v.mp4Path);
                resetVideo();
                return;
            }
            _jobs.update(v.jobId, AIJob::DONE, 1.0f, v.mp4Path, "");
            if (_cards) { _cards->removeJob(v.jobId); _cards->pushPhoto(v.mp4Path, u8"巡航视频", true); }
            OSG_NOTICE << "[AIChat] video job done -> " << v.mp4Path << std::endl;
            resetVideo();
            return;
        }

        if (v.phase == VideoJob::SUBMITTING)
        {
            AIJob snap;
            if (!_jobs.get(v.jobId, snap))
            { publishVideoFailure("video submission job disappeared"); resetVideo(); return; }
            // Omni 同步路径:worker 一步到位直接置 DONE(mp4 已写盘),这里收尾。
            if (snap.status == AIJob::DONE)
            {
                if (!v.workerDone->load()) return;
                if (v.workerJoinable && v.worker.joinable()) v.worker.join();
                v.workerJoinable = false;
                if (_cards) { _cards->removeJob(v.jobId); _cards->pushPhoto(snap.resultPath, u8"巡航视频", true); }
                OSG_NOTICE << "[AIChat] video job done -> " << snap.resultPath << std::endl;
                resetVideo();
                return;
            }
            // Omni 同步生成 30-180s,SUBMITTING 阶段爬一点进度让 Job 卡不像卡死
            // (creepProgress 锁内只在 RUNNING 时推进,不会覆盖 worker 写的终态)。
            _jobs.creepProgress(v.jobId, std::min(0.85f, snap.progress + 0.0008f));
            if (snap.status == AIJob::FAILED)
            {
                if (!v.workerDone->load()) return;
                if (v.workerJoinable && v.worker.joinable()) v.worker.join();
                if (_cards) _cards->removeJob(v.jobId);
                publishVideoFailure(snap.error);
                resetVideo();
                return;
            }
            if (snap.status == AIJob::RUNNING && snap.progress >= 0.3f && !snap.resultPath.empty())
            {
                // worker 已把 operationName 写进 resultPath(见 confirmVideo 里的注释),
                // 提交阶段的 worker 线程本身已经跑完(返回了),可以安全 join 回收。
                if (!v.workerDone->load()) return;
                if (v.workerJoinable && v.worker.joinable()) v.worker.join();
                v.workerJoinable = false;
                v.operationName = snap.resultPath;
                v.pollTicks = 0;
                v.phase = VideoJob::POLLING;
                OSG_NOTICE << "[AIChat] generate_video operation=" << v.operationName << std::endl;
            }
            return;
        }

        if (v.phase == VideoJob::POLLING)
        {
            // 每约 300 ticks(≈5-10s,取决于帧率)发起一次轮询,而不是每帧都发 HTTP 请求。
            const int kPollIntervalTicks = 300;
            const int kPollTimeoutTicks = 300 * 60;   // 300 次轮询上限(约 10 分钟),超时判失败
            ++v.pollTicks;
            ++v.ticksSinceLastPoll;

            // 若上一次轮询 worker 还没跑完,先看它是否已经把结果写进 job 了
            // (DONE/FAILED 都代表 worker 已返回,可以安全 join 回收线程)。
            if (v.pollInFlight)
            {
                AIJob snap;
                if (!_jobs.get(v.jobId, snap))
                { publishVideoFailure("video polling job disappeared"); resetVideo(); return; }
                if (snap.status == AIJob::DONE)
                {
                    if (!v.workerDone->load()) return;
                    if (v.workerJoinable && v.worker.joinable()) v.worker.join();
                    v.workerJoinable = false; v.pollInFlight = false;
                    if (_cards) { _cards->removeJob(v.jobId); _cards->pushPhoto(snap.resultPath, u8"巡航视频", true); }
                    OSG_NOTICE << "[AIChat] video job done -> " << snap.resultPath << std::endl;
                    resetVideo();
                    return;
                }
                if (snap.status == AIJob::FAILED)
                {
                    if (!v.workerDone->load()) return;
                    if (v.workerJoinable && v.worker.joinable()) v.worker.join();
                    v.workerJoinable = false; v.pollInFlight = false;
                    if (_cards) _cards->removeJob(v.jobId);
                    publishVideoFailure(snap.error);
                    resetVideo();
                    return;
                }
                // status 仍是 RUNNING 且没有失败/完成信号 -> 本轮 poll worker 要么还在飞
                // (还没 GET 完成),要么已经返回但判定"operation 仍在跑"(done:false,worker
                // lambda 里那条 "!done 直接 return" 分支,不触碰 job)。
                //
                // bug fix:这里不能再用 "!v.worker.joinable()" 当"worker 已经跑完"的信号——
                // std::thread 只要执行体还没被 join() 过,joinable() 就一直是 true,跟它的
                // 函数体是否已经跑完毫无关系(线程结束运行 ≠ 变得不可 join,那要等 std::thread
                // 对象被 join() 或 detach())。旧代码这里读到的永远是 true,pollInFlight 永远
                // 清不掉,下面 :1007 的 "if (v.pollInFlight || ...) return;" 就会永久拦住后续
                // 轮询——真实网络下第一次 GET 返回 done:false 之后,整个任务就再也不会发起
                // 第二次轮询,只能干等到 10 分钟超时(此时 Veo 那边任务其实可能已经生成完毕,
                // 白花钱)。
                //
                // 改用 worker 主动写的完成标志 pollDone(std::atomic<bool>,worker lambda 的
                // 最后一步就是把它置 true,见下方新 worker 里的 doneFlag->store(true))——
                // 无论 poll() 判定 done 还是仍在跑,只要 lambda 本身执行完毕就会置位,这才是
                // "worker 已返回、可以安全 join"的真实信号。置位后 join 回收线程、清零
                // pollInFlight 和 pollDone,下一个轮询间隔到了自然会再起一个新 worker——
                // 也就是本次修复之后轮询变成真正可重复的了(spawn → 置位 → join+清 →
                // 下一个 interval 再 spawn)。
                if (v.pollDone->load())
                {
                    if (v.workerJoinable && v.worker.joinable()) v.worker.join();
                    v.workerJoinable = false;
                    v.pollInFlight = false;
                    v.pollDone = std::make_shared<std::atomic<bool>>(true);
                    v.workerDone = v.pollDone;
                    if (v.pollTicks > kPollTimeoutTicks)
                    {
                        _jobs.update(v.jobId, AIJob::FAILED, 1.0f, "",
                                     "polling timeout (10min)");
                        publishVideoFailure("polling timeout (10min)");
                        finalizeVideoCancellation();
                        return;
                    }
                    // review:收割完当前这一轮 worker 后立刻 return,不让本 tick 继续往下走
                    // 到"到达轮询间隔就再 spawn 一个新 worker"那段——pollInFlight 刚清成
                    // false、ticksSinceLastPoll 也可能恰好已经 >= kPollIntervalTicks,不加
                    // 这个 return 就会在同一 tick 内清零又立刻重新置位,产生一个极窄的竞态
                    // 窗口。下一 tick 才重读 job 状态、决定是否该发起下一轮轮询,只是多等
                    // 一帧(可忽略的延迟),换来这个竞态彻底消失。
                    return;
                }
            }

            // 进度条在等待轮询期间缓慢爬升(0.3~0.8,阶梯式,给用户"仍在进行"的观感)。
            // bug fix:这里原先直接 _jobs.update(v.jobId, RUNNING, ..., "", "") 会把 status
            // 强行写成 RUNNING——如果恰好在这次 update() tick 与上面 pollInFlight 分支之间,
            // 后台 poll worker 刚把 job 写成 DONE/FAILED(比如轮询间隔到了、worker 已经拿到
            // 结果,但这次 tick 走到这里时上面 pollInFlight 判断还没来得及看到新状态——两次
            // 独立的加锁操作之间不是原子的),这行 creep 就会把已经完成/失败的 status 又
            // 覆盖回 RUNNING,导致 UI 卡片"完成后又变回进行中"甚至掩盖失败原因。
            // JobManager::creepProgress() 把"读 status 是否 RUNNING"和"写 progress"放进
            // 同一次加锁,对 RUNNING 之外的状态直接跳过、不做任何修改,消除这个竞态。
            float creepFrac = std::min(1.0f, (float)v.pollTicks / (float)kPollTimeoutTicks);
            _jobs.creepProgress(v.jobId, 0.3f + 0.5f * creepFrac);

            if (v.pollTicks > kPollTimeoutTicks)
            {
                _jobs.update(v.jobId, AIJob::FAILED, 1.0f, "", "polling timeout (10min)");
                publishVideoFailure("polling timeout (10min)");
                if (classifyVideoPollTimeout(v.pollInFlight) ==
                    VIDEO_POLL_TIMEOUT_ASYNC_REAP)
                {
                    v.cancelRequested->store(true);
                    v.phase = VideoJob::CANCELLING;
                }
                else finalizeVideoCancellation();
                return;
            }

            if (v.pollInFlight || v.ticksSinceLastPoll < kPollIntervalTicks) return;

            // 到达轮询间隔且没有上一次轮询还在飞行中:起一个一次性 worker 线程做本次
            // GET(不阻塞主线程),结果写回 job(DONE/FAILED);"仍在跑"(done:false)则
            // 不触碰 job 状态,只是让 worker 自然返回,下一轮再发起。
            if (v.workerJoinable && v.worker.joinable() && v.workerDone->load())
                v.worker.join();
            v.ticksSinceLastPoll = 0;
            v.pollInFlight = true;
            // 新开一轮轮询就换一个新的 pollDone(而不是复用/清零旧对象):shared_ptr 本身
            // 保证了"worker 捕获的是这一轮专属的 flag",即便未来某处逻辑变化导致上一轮的
            // flag 还有其它持有者在读,也不会跟这一轮混淆。当前 worker lambda 结束时把它
            // 置 true 就是本节点新增的"worker 已返回"信号(见上面 POLLING 分支里的用法)。
            v.pollDone = std::make_shared<std::atomic<bool>>(false);
            v.workerDone = v.pollDone;
            v.cancelRequested = std::make_shared<std::atomic<bool>>(false);
            std::string apiKey = _apiKey;
            std::string model = videoModel();
            std::string opName = v.operationName;
            std::string mp4Path = v.mp4Path;
            int jobId = v.jobId;
            JobManager* jobsPtr = &_jobs;
            std::shared_ptr<std::atomic<bool>> doneFlag = v.pollDone;
            std::shared_ptr<std::atomic<bool>> cancelFlag = v.cancelRequested;
            v.worker = std::thread([apiKey, model, opName, mp4Path, jobId, jobsPtr, doneFlag, cancelFlag]()
            {
                // RAII 收尾:无论下面哪条分支 return,都保证最后一步是把 doneFlag 置位——
                // 这是主线程判断"这次轮询 worker 已经跑完、可以安全 join"的唯一依据
                // (std::thread::joinable() 不能用来判断线程函数体是否执行完毕,见上方
                // POLLING 分支里的详细注释)。
                struct DoneSetter
                {
                    std::shared_ptr<std::atomic<bool>> flag;
                    ~DoneSetter() { flag->store(true); }
                } doneSetter{doneFlag};

                if (cancelFlag->load()) return;
                VeoVideoProvider provider(apiKey, model);
                bool done = false; std::string mp4Bytes, err;
                try { provider.poll(opName, done, mp4Bytes, err); }
                catch (const std::exception& e) { done = true; err = std::string("provider exception: ") + e.what(); }

                if (cancelFlag->load()) return;

                if (!done) return;   // 仍在跑,job 状态不变,下次轮询再试
                if (mp4Bytes.empty())
                {
                    jobsPtr->update(jobId, AIJob::FAILED, 1.0f, "", err.empty() ? "unknown poll error" : err);
                    return;
                }
                if (!writeFileBytes(mp4Path, mp4Bytes))
                {
                    jobsPtr->update(jobId, AIJob::FAILED, 1.0f, "", "failed to write " + mp4Path);
                    return;
                }
                jobsPtr->update(jobId, AIJob::DONE, 1.0f, mp4Path, "");
            });
            v.workerJoinable = true;
            return;
        }
    }
}
