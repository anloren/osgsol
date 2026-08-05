#include <applications/earth_explorer/ai_media.h>
#include <applications/earth_explorer/cinematic_video_encoder.h>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <type_traits>

#define CHECK(x) do { if (!(x)) { \
    std::cerr << "CHECK failed at " << __FILE__ << ":" << __LINE__ << ": " #x << "\n"; \
    return 1; } } while (0)

static std::string readSourceFile(const std::string& relative)
{
    std::ifstream input(std::string(OSGVERSE_SOURCE_DIR) + "/" + relative);
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

static std::string extractFunctionBody(const std::string& source,
                                       const std::string& signature)
{
    size_t signaturePos = source.find(signature);
    if (signaturePos == std::string::npos) return std::string();
    size_t openingBrace = source.find('{', signaturePos + signature.size());
    if (openingBrace == std::string::npos) return std::string();

    int depth = 1;
    bool lineComment = false, blockComment = false;
    bool stringLiteral = false, charLiteral = false, escaped = false;
    for (size_t i = openingBrace + 1; i < source.size(); ++i)
    {
        char c = source[i];
        char next = (i + 1 < source.size()) ? source[i + 1] : '\0';
        if (lineComment)
        {
            if (c == '\n') lineComment = false;
            continue;
        }
        if (blockComment)
        {
            if (c == '*' && next == '/') { blockComment = false; ++i; }
            continue;
        }
        if (stringLiteral || charLiteral)
        {
            if (escaped) { escaped = false; continue; }
            if (c == '\\') { escaped = true; continue; }
            if ((stringLiteral && c == '"') || (charLiteral && c == '\''))
            { stringLiteral = false; charLiteral = false; }
            continue;
        }
        if (c == '/' && next == '/') { lineComment = true; ++i; continue; }
        if (c == '/' && next == '*') { blockComment = true; ++i; continue; }
        if (c == '"') { stringLiteral = true; continue; }
        if (c == '\'') { charLiteral = true; continue; }
        if (c == '{') ++depth;
        else if (c == '}' && --depth == 0)
            return source.substr(openingBrace + 1, i - openingBrace - 1);
    }
    return std::string();
}

static size_t countOccurrences(const std::string& text, const std::string& token)
{
    size_t count = 0, pos = 0;
    while ((pos = text.find(token, pos)) != std::string::npos)
    {
        ++count;
        pos += token.size();
    }
    return count;
}

static earthai::VideoUiDispatchResult dispatchResult(
    earthai::VideoUiRequest::Kind kind, bool succeeded,
    const std::string& error = std::string())
{
    earthai::VideoUiDispatchResult result;
    result.dispatched = true;
    result.kind = kind;
    result.succeeded = succeeded;
    result.error = error;
    return result;
}

int main()
{
    using namespace earthai;
    typedef bool (*OrbitEncoderSignature)(
        const std::vector<std::string>&, int, const std::string&, std::string&,
        const std::atomic<bool>*);
    static_assert(std::is_same<decltype(&encodePngSequenceToH264Mp4),
                  OrbitEncoderSignature>::value,
                  "orbit encoder declaration must retain its cancellation argument");

    CHECK(classifyVideoPollHttp(false, 0) == VIDEO_POLL_RETRY);
    CHECK(classifyVideoPollHttp(true, 429) == VIDEO_POLL_RETRY);
    CHECK(classifyVideoPollHttp(true, 500) == VIDEO_POLL_RETRY);
    CHECK(classifyVideoPollHttp(true, 503) == VIDEO_POLL_RETRY);
    CHECK(classifyVideoPollHttp(true, 599) == VIDEO_POLL_RETRY);
    CHECK(classifyVideoPollHttp(true, 600) == VIDEO_POLL_TERMINAL_ERROR);
    CHECK(classifyVideoPollHttp(true, 400) == VIDEO_POLL_TERMINAL_ERROR);
    CHECK(classifyVideoPollHttp(true, 401) == VIDEO_POLL_TERMINAL_ERROR);
    CHECK(classifyVideoPollHttp(true, 200) == VIDEO_POLL_PARSE_BODY);

    // A screenshot request remains independently observable after a video job has been
    // cancelled.  Cancelling before the render callback must make that callback a no-op;
    // cancelling after the callback has begun permits the in-flight write to finish, then
    // exposes a terminal state for deferred cleanup.
    SnapshotCaptureController cancelledBeforeCallback("before.png");
    cancelledBeforeCallback.cancel();
    CHECK(cancelledBeforeCallback.beginCallback() == SNAPSHOT_CAPTURE_SKIP_CANCELLED);
    CHECK(cancelledBeforeCallback.terminal());
    CHECK(!cancelledBeforeCallback.delegateStarted());

    SnapshotCaptureController cancelledDuringWrite("during.png");
    CHECK(cancelledDuringWrite.beginCallback() == SNAPSHOT_CAPTURE_WRITE_DELEGATE);
    CHECK(cancelledDuringWrite.delegateStarted());
    cancelledDuringWrite.cancel();
    CHECK(!cancelledDuringWrite.terminal());
    cancelledDuringWrite.completeCallback(true);
    CHECK(cancelledDuringWrite.terminal());
    CHECK(cancelledDuringWrite.cancelled());

    SnapshotCaptureController accepted("accepted.png");
    CHECK(accepted.beginCallback() == SNAPSHOT_CAPTURE_WRITE_DELEGATE);
    accepted.completeCallback(true);
    CHECK(accepted.terminal());
    CHECK(!accepted.cancelled());

    // SnapshotGrabber must not replace a cancelled request while the one-shot OSG callback
    // still owns it; otherwise the old callback can write a late file to a now-untracked path.
    SnapshotCaptureSlot captureSlot;
    CHECK(captureSlot.begin("first.png"));
    captureSlot.cancel();
    CHECK(!captureSlot.begin("second.png"));
    std::shared_ptr<SnapshotCaptureController> firstCapture = captureSlot.active();
    CHECK(firstCapture.get() != NULL);
    CHECK(firstCapture->beginCallback() == SNAPSHOT_CAPTURE_SKIP_CANCELLED);
    CHECK(firstCapture->terminal());
    CHECK(captureSlot.begin("second.png"));

    // Photo and video share one render callback. A cancelled video request keeps the shared
    // slot reserved until its no-op callback is terminal; the video token, not the slot's
    // later active request, is what a deferred video cleanup may cancel.
    SnapshotCaptureSlot sharedCaptureSlot;
    CHECK(sharedCaptureSlot.begin("video-a.png"));
    std::shared_ptr<SnapshotCaptureController> videoToken = sharedCaptureSlot.active();
    videoToken->cancel();
    CHECK(!sharedCaptureSlot.begin("photo-after-video-cancel.png"));
    CHECK(videoToken->beginCallback() == SNAPSHOT_CAPTURE_SKIP_CANCELLED);
    CHECK(videoToken->terminal());
    CHECK(sharedCaptureSlot.begin("photo-after-video-terminal.png"));

    SnapshotCaptureSlot photoFirstSlot;
    CHECK(photoFirstSlot.begin("photo-active.png"));
    CHECK(!photoFirstSlot.begin("video-while-photo-active.png"));

    SnapshotCaptureSlot terminalVideoThenPhotoSlot;
    CHECK(terminalVideoThenPhotoSlot.begin("video-terminal.png"));
    std::shared_ptr<SnapshotCaptureController> terminalVideoToken =
        terminalVideoThenPhotoSlot.active();
    CHECK(terminalVideoToken->beginCallback() == SNAPSHOT_CAPTURE_WRITE_DELEGATE);
    terminalVideoToken->completeCallback(true);
    CHECK(terminalVideoThenPhotoSlot.begin("newer-photo.png"));
    std::shared_ptr<SnapshotCaptureController> newerPhotoToken =
        terminalVideoThenPhotoSlot.active();
    terminalVideoToken->cancel();
    CHECK(!newerPhotoToken->cancelled());

    // A timeout can retire a never-started callback without wedging the shared slot. Its late
    // callback must still be a no-op, while the newly admitted route keeps its own token.
    SnapshotCaptureSlot timeoutSlot;
    CHECK(timeoutSlot.begin("timed-out-video.png"));
    std::shared_ptr<SnapshotCaptureController> timedOutVideo = timeoutSlot.active();
    CHECK(timedOutVideo->retireIfNotStarted());
    CHECK(timedOutVideo->terminal());
    CHECK(!timedOutVideo->completedSuccessfully());
    CHECK(timeoutSlot.begin("photo-after-timeout.png"));
    std::shared_ptr<SnapshotCaptureController> photoAfterTimeout = timeoutSlot.active();
    CHECK(timedOutVideo->beginCallback() == SNAPSHOT_CAPTURE_SKIP_CANCELLED);
    CHECK(!photoAfterTimeout->cancelled());

    SnapshotCaptureController captureSuccess("successful.png");
    CHECK(captureSuccess.beginCallback() == SNAPSHOT_CAPTURE_WRITE_DELEGATE);
    captureSuccess.completeCallback(true);
    CHECK(captureSuccess.completedSuccessfully());

    // File-size stability is token-local. Interleaving A/B observations must neither borrow
    // the other's size nor declare either request stable on its first observation.
    SnapshotCaptureController stableA("stable-a.png");
    SnapshotCaptureController stableB("stable-b.png");
    CHECK(!stableA.observeFileSize("stable-a.png", 41));
    CHECK(!stableB.observeFileSize("stable-b.png", 99));
    CHECK(stableA.observeFileSize("stable-a.png", 41));
    CHECK(stableB.observeFileSize("stable-b.png", 99));

    SnapshotCaptureController equalFirstA("equal-a.png");
    SnapshotCaptureController equalFirstB("equal-b.png");
    CHECK(!equalFirstA.observeFileSize("equal-a.png", 128));
    CHECK(!equalFirstB.observeFileSize("equal-b.png", 128));
    CHECK(equalFirstA.observeFileSize("equal-a.png", 128));
    CHECK(equalFirstB.observeFileSize("equal-b.png", 128));

    SnapshotCaptureController pathBound("path-bound.png");
    CHECK(!pathBound.observeFileSize("wrong-path.png", 88));
    CHECK(!pathBound.observeFileSize("path-bound.png", 88));
    CHECK(pathBound.observeFileSize("path-bound.png", 88));

    VideoUiRequestQueue queue;
    VideoUiRequest first; first.kind = VideoUiRequest::Begin;
    first.lla = osg::Vec3d(1.0, 2.0, 3.0);
    VideoUiRequest second; second.kind = VideoUiRequest::Cancel;
    queue.push(first);
    queue.push(second);

    std::vector<VideoUiRequest> drained = queue.drain();
    CHECK(drained.size() == 2);
    CHECK(drained[0].kind == VideoUiRequest::Begin);
    CHECK(drained[0].lla == osg::Vec3d(1.0, 2.0, 3.0));
    CHECK(drained[1].kind == VideoUiRequest::Cancel);
    CHECK(queue.drain().empty());

    CinematicUiRequestQueue cinematicQueue;
    CinematicUiRequest imageRequest;
    imageRequest.settings = defaultImageCinematicSettings();
    imageRequest.settings.era = CINEMATIC_ERA_1920S;
    imageRequest.settings.userPrompt = "Victoria Harbour";
    CinematicUiRequest videoRequest;
    videoRequest.settings = defaultVideoCinematicSettings();
    videoRequest.settings.motion = CINEMATIC_MOTION_ORBIT_360;
    cinematicQueue.push(imageRequest);
    cinematicQueue.push(videoRequest);
    const std::vector<CinematicUiRequest> cinematicDrained =
        cinematicQueue.drain();
    CHECK(cinematicDrained.size() == 2);
    CHECK(cinematicDrained[0].settings.mediaKind == CINEMATIC_IMAGE);
    CHECK(cinematicDrained[0].settings.userPrompt == "Victoria Harbour");
    CHECK(cinematicDrained[1].settings.motion == CINEMATIC_MOTION_ORBIT_360);
    CHECK(cinematicQueue.drain().empty());

    // Pure reducer behavior: failures replace stale text, no-request publications retain it,
    // and every successful request boundary (plus Cancel) clears it.
    std::string commandError = "stale error";
    commandError = reduceVideoCommandError(
        commandError, dispatchResult(VideoUiRequest::Begin, false,
                                     "video capture is not idle"));
    CHECK(commandError == "video capture is not idle");

    MediaManager::VideoUiSnapshot firstPublished, secondPublished;
    firstPublished.commandError = commandError;
    commandError = reduceVideoCommandError(commandError, VideoUiDispatchResult());
    secondPublished.commandError = commandError;
    CHECK(firstPublished.commandError == "video capture is not idle");
    CHECK(secondPublished.commandError == firstPublished.commandError);

    commandError = reduceVideoCommandError(
        commandError, dispatchResult(VideoUiRequest::CaptureEnd, false,
                                     "video is not waiting for B"));
    CHECK(commandError == "video is not waiting for B");
    commandError = reduceVideoCommandError(
        commandError, dispatchResult(VideoUiRequest::Confirm, false,
                                     "provider rejected request"));
    CHECK(commandError == "provider rejected request");
    firstPublished.commandError = commandError;
    commandError = reduceVideoCommandError(commandError, VideoUiDispatchResult());
    secondPublished.commandError = commandError;
    CHECK(firstPublished.commandError == "provider rejected request");
    CHECK(secondPublished.commandError == firstPublished.commandError);

    commandError = reduceVideoCommandError(
        commandError, dispatchResult(VideoUiRequest::Confirm, true));
    CHECK(commandError.empty());
    commandError = "old confirm failure";
    commandError = reduceVideoCommandError(
        commandError, dispatchResult(VideoUiRequest::Begin, true));
    CHECK(commandError.empty());
    commandError = "old begin failure";
    commandError = reduceVideoCommandError(
        commandError, dispatchResult(VideoUiRequest::CaptureEnd, true));
    CHECK(commandError.empty());
    commandError = "old capture failure";
    commandError = reduceVideoCommandError(
        commandError, dispatchResult(VideoUiRequest::Cancel, true));
    CHECK(commandError.empty());

    // Direct FRAME-owner calls do not create UI command failures. A failed owner call must keep
    // the existing UI error, while every successful owner boundary clears it before publication.
    std::string ownerError = "old UI failure";
    CHECK(reduceVideoOwnerCommandError(ownerError, false) == ownerError);
    CHECK(reduceVideoOwnerCommandError(ownerError, true).empty());

    // Cancellation is a FRAME-owner state transition, not a synchronous wait.  A live
    // encoder/HTTP worker must be reaped later; pre-worker capture can reset immediately.
    CHECK(classifyVideoCancellation(false) == VIDEO_CANCEL_RESET_NOW);
    CHECK(classifyVideoCancellation(true) == VIDEO_CANCEL_ASYNC_REAP);

    // Timeout after a done:false poll is terminal immediately; timeout while a GET is live
    // requests asynchronous cancellation and must not block the FRAME owner.
    CHECK(classifyVideoPollTimeout(false) == VIDEO_POLL_TIMEOUT_FINALIZE_NOW);
    CHECK(classifyVideoPollTimeout(true) == VIDEO_POLL_TIMEOUT_ASYNC_REAP);

    // Local video failures remain visible even when the optional chat core is absent.
    VideoStatusBanner banner;
    banner = reduceVideoStatusBanner(banner, VIDEO_STATUS_FAILURE,
                                     "local encoder failed");
    CHECK(banner.visible);
    CHECK(banner.message == "local encoder failed");
    CHECK(reduceVideoStatusBanner(banner, VIDEO_STATUS_NO_CHANGE).message ==
          "local encoder failed");
    CHECK(reduceVideoStatusBanner(banner, VIDEO_STATUS_SUCCESS).message.empty());
    banner = reduceVideoStatusBanner(banner, VIDEO_STATUS_FAILURE,
                                     "snapshot timeout");
    CHECK(reduceVideoStatusBanner(banner, VIDEO_STATUS_DISMISS).visible == false);

    const std::string ui = readSourceFile("applications/earth_explorer/ai_ui.cpp");
    const std::string uiHeader = readSourceFile("applications/earth_explorer/ai_ui.h");
    const std::string media = readSourceFile("applications/earth_explorer/ai_media.cpp");
    const std::string mediaHeader = readSourceFile("applications/earth_explorer/ai_media.h");
    const std::string encoderHeader = readSourceFile(
        "applications/earth_explorer/cinematic_video_encoder.h");
    const std::string encoderStub = readSourceFile(
        "applications/earth_explorer/cinematic_video_encoder.cpp");
    const std::string encoderMac = readSourceFile(
        "applications/earth_explorer/cinematic_video_encoder.mm");
    const std::string setup = readSourceFile("applications/earth_explorer/ai_setup.cpp");
    const std::string earthMain = readSourceFile(
        "applications/earth_explorer/earth_main.cpp");
    CHECK(!ui.empty() && !uiHeader.empty() && !media.empty() && !mediaHeader.empty());
    CHECK(!setup.empty() && !earthMain.empty());
    CHECK(!encoderHeader.empty() && !encoderStub.empty() && !encoderMac.empty());

    // Header, non-mac fallback, and macOS implementation must all expose the same
    // cancellation-aware five-argument encoder ABI.
    CHECK(encoderHeader.find("const std::atomic<bool>* cancelRequested") !=
          std::string::npos);
    CHECK(encoderStub.find("const std::atomic<bool>* cancelRequested") !=
          std::string::npos);
    CHECK(encoderMac.find("const std::atomic<bool>* cancelRequested") !=
          std::string::npos);
    CHECK(encoderStub.find("deterministic H.264 orbit encoding cancelled") !=
          std::string::npos);
    CHECK(encoderMac.find("std::remove(outputPath.c_str());") != std::string::npos);

    // The final draw callback is shared by photo and video. MediaManager therefore owns one
    // SnapshotGrabber/ScreenCaptureHandler coordinator, rather than two independently safe-
    // looking handlers that can replace each other's pending operation.
    CHECK(countOccurrences(mediaHeader, "SnapshotGrabber _grabber;") == 1);
    CHECK(mediaHeader.find("_videoGrabber") == std::string::npos);
    CHECK(countOccurrences(media, "_grabber(captureCamera)") == 1);
    CHECK(countOccurrences(media, "new GenerationScreenCaptureHandler(") == 1);
    CHECK(media.find("snapCaptureTokenA") != std::string::npos);
    CHECK(media.find("snapCaptureTokenB") != std::string::npos);
    CHECK(media.find("orbitCaptureToken") != std::string::npos);
    CHECK(mediaHeader.find("_lastSize") == std::string::npos);
    CHECK(mediaHeader.find("observeFileSize") != std::string::npos);
    const std::string snapshotReady = extractFunctionBody(
        media, "bool SnapshotGrabber::ready(");
    const std::string snapshotRetire = extractFunctionBody(
        media, "void SnapshotGrabber::retire(");
    const std::string snapshotTerminalReaper = extractFunctionBody(
        media, "void SnapshotGrabber::reapTerminalGeneration()");
    const std::string snapshotGrab = extractFunctionBody(
        media, "std::shared_ptr<SnapshotCaptureController> SnapshotGrabber::grab(");
    const std::string snapshotConstructor = extractFunctionBody(
        media, "SnapshotGrabber::SnapshotGrabber(osg::Camera* captureCamera)");
    const std::string dispatcherDraw = extractFunctionBody(
        media, "virtual void operator()(osg::RenderInfo& renderInfo) const");
    CHECK(snapshotReady.find("capture->completedSuccessfully()") !=
          std::string::npos);
    CHECK(snapshotReady.find("capture->requestedPath() != pngPath") !=
          std::string::npos);
    CHECK(snapshotRetire.find("capture->retireIfNotStarted()") !=
          std::string::npos);
    // OSG RenderStage reads Camera::_finalDrawCallback without a synchronization boundary.
    // The dispatcher is therefore installed exactly once during MediaManager construction;
    // runtime FRAME paths publish/revoke generations atomically and never read/write Camera's
    // callback pointer.
    CHECK(mediaHeader.find("CaptureGeneration") != std::string::npos);
    CHECK(mediaHeader.find("GenerationDispatcher") != std::string::npos);
    CHECK(mediaHeader.find("_dispatcherInstalled") != std::string::npos);
    CHECK(mediaHeader.find("osg::observer_ptr<osg::Camera> _captureCamera") !=
          std::string::npos);
    CHECK(mediaHeader.find("_timeoutRetainedGenerations") == std::string::npos);
    CHECK(mediaHeader.find("_reapableGenerations") != std::string::npos);
    CHECK(countOccurrences(media, "setFinalDrawCallback(") == 1);
    CHECK(countOccurrences(media, "getFinalDrawCallback(") == 1);
    CHECK(snapshotConstructor.find("_captureCamera->setFinalDrawCallback(_dispatcher.get())") !=
          std::string::npos);
    CHECK(snapshotConstructor.find("_captureCamera->getFinalDrawCallback()") !=
          std::string::npos);
    CHECK(media.find(": _captureCamera(captureCamera)") !=
          std::string::npos);
    CHECK(snapshotConstructor.find("_dispatcherInstalled = true") != std::string::npos);
    CHECK(snapshotConstructor.find("_viewer->getCamera") == std::string::npos);
    CHECK(snapshotRetire.find("_dispatcher->revoke(_activeGeneration)") !=
          std::string::npos);
    CHECK(snapshotRetire.find("removeCallbackFromViewer") == std::string::npos);
    CHECK(snapshotGrab.find("new GenerationScreenCaptureHandler(") !=
          std::string::npos);
    // Completed one-shot captures and timeout generations are released once no invocation is
    // active. A render traversal that loaded before revoke owns its own shared_ptr, so a local
    // 8 s / 24 fps orbit cannot retain 192 WindowCaptureCallback ContextData buffers forever.
    CHECK(snapshotGrab.find("_timeoutRetainedGenerations.push_back(_activeGeneration)") ==
          std::string::npos);
    CHECK(snapshotRetire.find("_timeoutRetainedGenerations") == std::string::npos);
    CHECK(snapshotRetire.find("_reapableGenerations.push_back(_activeGeneration)") !=
          std::string::npos);
    CHECK(snapshotGrab.find("setCaptureOperation") == std::string::npos);
    CHECK(snapshotGrab.find("captureNextFrame") == std::string::npos);
    CHECK(snapshotGrab.find("addEventHandler") == std::string::npos);
    // WriteToFile discards writeImageFile's return value. A stale expected artifact must be
    // deleted before arming; deletion failure makes the request terminal and returns null.
    CHECK(snapshotGrab.find("const std::string actualPath = capturedPath(pngPath)") !=
          std::string::npos);
    CHECK(snapshotGrab.find("std::remove(actualPath.c_str()) != 0") !=
          std::string::npos);
    CHECK(snapshotGrab.find("token->retireIfNotStarted()") != std::string::npos);
    // numFrames=0 keeps the one-generation handler from self-removing the permanent camera
    // dispatcher. The FRAME owner only revokes the generation after terminal publication.
    CHECK(snapshotGrab.find("GenerationScreenCaptureHandler(operation.get(), 0)") !=
          std::string::npos);
    CHECK(snapshotReady.find("capture->terminal()") != std::string::npos);
    CHECK(snapshotReady.find("reapTerminalGeneration()") != std::string::npos);
    CHECK(snapshotGrab.find("reapTerminalGeneration()") != std::string::npos);
    // A failed dispatcher install does not start a token and cannot leave stale retention.
    CHECK(snapshotGrab.find("_timeoutRetainedGenerations.push_back(generation)") ==
          std::string::npos);
    CHECK(snapshotGrab.find("_dispatcher->publish(generation)") != std::string::npos);
    CHECK(snapshotGrab.find("setFinalDrawCallback") == std::string::npos);
    CHECK(snapshotGrab.find("getFinalDrawCallback") == std::string::npos);
    const std::string cropToViewport = extractFunctionBody(
        media, "void SnapshotGrabber::cropToViewport(");
    CHECK(cropToViewport.find("_captureCamera.valid()") != std::string::npos);
    CHECK(cropToViewport.find("_captureCamera->getViewport()") != std::string::npos);
    CHECK(cropToViewport.find("_viewer") == std::string::npos);
    // A callback that was claimed before timeout can complete after the media job resets. The
    // unconditional FRAME reaper revokes its generation and drops timeout and normal
    // generations only once the render invocation is quiescent.
    CHECK(mediaHeader.find("void reapTerminalGeneration()") != std::string::npos);
    CHECK(snapshotTerminalReaper.find("_activeGeneration->token->terminal()") !=
          std::string::npos);
    CHECK(snapshotTerminalReaper.find("_dispatcher->revoke(_activeGeneration)") !=
          std::string::npos);
    CHECK(snapshotTerminalReaper.find("_activeGeneration.reset()") != std::string::npos);
    // The dispatcher atomically owns a shared generation, so a render traversal either sees no
    // capture or keeps the immutable generation alive through its complete callback invocation.
    CHECK(media.find("struct SnapshotGrabber::GenerationDispatcher") != std::string::npos);
    CHECK(media.find("std::atomic_load_explicit(&_generation") != std::string::npos);
    CHECK(media.find("std::atomic_store_explicit(&_generation") != std::string::npos);
    CHECK(media.find("std::atomic_compare_exchange_strong_explicit(") != std::string::npos);
    CHECK(media.find("std::atomic<unsigned int> _inFlight") != std::string::npos);
    CHECK(snapshotTerminalReaper.find("quiescent()") != std::string::npos);
    CHECK(snapshotTerminalReaper.find("_reapableGenerations.push_back") !=
          std::string::npos);
    CHECK(snapshotTerminalReaper.find("_timeoutRetainedGenerations") ==
          std::string::npos);
    CHECK(dispatcherDraw.find("std::atomic_load_explicit(&_generation") !=
          std::string::npos);
    size_t chainedFinalCallback = dispatcherDraw.find("_previousCallback");
    size_t generationLoad = dispatcherDraw.find("std::atomic_load_explicit(&_generation");
    CHECK(chainedFinalCallback != std::string::npos);
    CHECK(chainedFinalCallback < generationLoad);
    CHECK(dispatcherDraw.find("getFinalDrawCallback") == std::string::npos);
    CHECK(dispatcherDraw.find("setFinalDrawCallback") == std::string::npos);

    // Architectural regression guard: a live worker cancellation only transitions to async
    // reaping. resetVideo checks workerDone before its sole join, so FRAME/ESC never waits for
    // a provider HTTP request or native encoder still in flight.
    const std::string cancelVideoAsync = extractFunctionBody(
        media, "void MediaManager::cancelVideo()");
    const std::string resetVideo = extractFunctionBody(
        media, "void MediaManager::resetVideo(bool removeOrbitArtifacts)");
    CHECK(cancelVideoAsync.find("VIDEO_CANCEL_ASYNC_REAP") != std::string::npos);
    CHECK(cancelVideoAsync.find("VideoJob::CANCELLING") != std::string::npos);
    CHECK(resetVideo.find("if (!_video->workerDone->load())") != std::string::npos);
    CHECK(resetVideo.find("_video->worker.join()") != std::string::npos);
    CHECK(resetVideo.find("workerDone->load())") <
          resetVideo.find("_video->worker.join()"));
    CHECK(setup.find("class VideoEscapeCancelHandler") != std::string::npos);
    CHECK(setup.find("VideoUiRequest::Cancel") != std::string::npos);
    // ImGui items need an active surface. Keep the persistent no-core status inside the
    // command-bar Begin/End block rather than emitting widgets after End().
    const size_t statusWidget = ui.find("视频状态：%s");
    const size_t commandEnd = ui.find("    ImGui::End();", statusWidget);
    CHECK(statusWidget != std::string::npos);
    CHECK(commandEnd != std::string::npos);
    CHECK(statusWidget < commandEnd);
    // UI disclosure and the request URL must resolve the same process-stable image model.
    CHECK(mediaHeader.find("resolvedCinematicImageModel") != std::string::npos);
    CHECK(media.find("const std::string& kImageModel = resolvedCinematicImageModel()") !=
          std::string::npos);
    CHECK(countOccurrences(ui, "media->imageModelLabel()") >= 2);
    CHECK(ui.find(u8"付费提交：图像首帧模型") != std::string::npos);
    CHECK(ui.find(u8"Nano Banana 2 首帧") == std::string::npos);
    CHECK(ui.find("AUDIT_VIDEO_RUNNING") != std::string::npos);
    CHECK(ui.find("AUDIT_VIDEO_FAILURE") != std::string::npos);
    CHECK(ui.find("AUDIT_ACTION_VIDEO_STOP") != std::string::npos);
    CHECK(ui.find("AUDIT_ACTION_VIDEO_STATUS_DISMISS") != std::string::npos);
    CHECK(ui.find("AUDIT_ACTION_LOCAL_360_START") != std::string::npos);
    CHECK(ui.find("localConfirmationVisible") != std::string::npos);
    CHECK(ui.find("CINEMATIC_MOTION_ORBIT_360") != std::string::npos);
    const std::string finalizer = extractFunctionBody(
        media, "void MediaManager::finalizeVideoCancellation()");
    const std::string deferredReaper = extractFunctionBody(
        media, "void MediaManager::reapDeferredCaptureCleanups()");
    CHECK(finalizer.find("existing.status == AIJob::RUNNING") != std::string::npos);
    CHECK(finalizer.find("\"cancelled\"") != std::string::npos);
    CHECK(finalizer.find("deferCancelledVideoCaptureCleanup()") != std::string::npos);
    CHECK(finalizer.find("resetVideo(false)") != std::string::npos);
    CHECK(deferredReaper.find("!cleanup.capture->terminal()") != std::string::npos);
    CHECK(deferredReaper.find("cancelled capture cleanup failed: ") != std::string::npos);
    CHECK(finalizer.find("generatedFramePathA") == std::string::npos);
    const std::string deferredVideoCleanup = extractFunctionBody(
        media, "void MediaManager::deferVideoCaptureCleanup(");
    CHECK(deferredVideoCleanup.find("generatedFramePathA") != std::string::npos);
    CHECK(deferredVideoCleanup.find("generatedFramePathB") != std::string::npos);
    CHECK(resetVideo.find("existing.status == AIJob::DONE") != std::string::npos);
    CHECK(resetVideo.find("generatedFramePathA") != std::string::npos);
    CHECK(resetVideo.find("_video->mp4Path") != std::string::npos);

    const std::string videoPoll = extractFunctionBody(
        media, "void VeoVideoProvider::poll(");
    CHECK(!videoPoll.empty());
    const std::string retryPoll = extractFunctionBody(
        videoPoll, "if (disposition == VIDEO_POLL_RETRY)");
    const std::string terminalPoll = extractFunctionBody(
        videoPoll, "if (disposition == VIDEO_POLL_TERMINAL_ERROR)");
    const std::string malformedPoll = extractFunctionBody(
        videoPoll, "if (!perr.empty() || !v.is<picojson::object>())");
    size_t request = videoPoll.find("httpRequestRetry(req)");
    size_t classification = videoPoll.find("classifyVideoPollHttp((bool)resp");
    size_t retryBranch = videoPoll.find("if (disposition == VIDEO_POLL_RETRY)");
    size_t terminalBranch = videoPoll.find(
        "if (disposition == VIDEO_POLL_TERMINAL_ERROR)");
    size_t parseBody = videoPoll.find("picojson::parse(v, resp->body)");
    CHECK(request < classification && classification < retryBranch);
    CHECK(retryBranch < terminalBranch && terminalBranch < parseBody);
    CHECK(videoPoll.find("resp ? (int)resp->status_code : 0") != std::string::npos);
    CHECK(videoPoll.find("if (!resp || resp->status_code != 200)") == std::string::npos);
    CHECK(retryPoll.find("done = false;") != std::string::npos);
    CHECK(retryPoll.find("err.clear();") != std::string::npos);
    CHECK(retryPoll.find("done = true;") == std::string::npos);
    CHECK(retryPoll.find("return;") != std::string::npos);
    CHECK(terminalPoll.find("done = true;") != std::string::npos);
    CHECK(terminalPoll.find("err = \"HTTP \"") != std::string::npos);
    CHECK(terminalPoll.find("return;") != std::string::npos);
    CHECK(malformedPoll.find("err = \"bad json: \"") != std::string::npos);
    CHECK(malformedPoll.find("done = true;") != std::string::npos);
    CHECK(malformedPoll.find("return;") != std::string::npos);

    const std::string update = extractFunctionBody(media, "void MediaManager::update()");
    const std::string snapshotGetter = extractFunctionBody(
        media, "MediaManager::VideoUiSnapshot MediaManager::videoUiSnapshot() const");
    const std::string hudHide = extractFunctionBody(
        media, "void MediaManager::hudHide(bool adjustScene)");
    const std::string hudRestore = extractFunctionBody(
        media, "void MediaManager::hudRestore(bool adjustScene)");
    const std::string isHudHidden = extractFunctionBody(mediaHeader, "bool isHudHidden() const");
    const std::string draw = extractFunctionBody(ui, "void AIChatUI::draw(");
    const std::string beginVideo = extractFunctionBody(
        media, "bool MediaManager::beginVideoCapture(");
    const std::string captureEnd = extractFunctionBody(
        media, "bool MediaManager::captureVideoEnd(");
    const std::string confirmVideo = extractFunctionBody(
        media, "picojson::value MediaManager::confirmVideo()");
    const std::string cancelVideo = extractFunctionBody(
        media, "void MediaManager::cancelVideo()");
    const std::string updateVideo = extractFunctionBody(
        media, "void MediaManager::updateVideoInternal()");
    const std::string ownerResult = extractFunctionBody(
        media, "void MediaManager::applyVideoOwnerCommandResult(bool succeeded)");
    const std::string frameHandle = extractFunctionBody(setup, "virtual bool handle(");
    const std::string viewerFrame = extractFunctionBody(
        earthMain, "void frame(double simulationTime = USE_REFERENCE_TIME) override");
    CHECK(!update.empty() && !snapshotGetter.empty() && !hudHide.empty());
    CHECK(!hudRestore.empty() && !isHudHidden.empty() && !draw.empty());
    CHECK(!beginVideo.empty() && !captureEnd.empty() && !confirmVideo.empty());
    CHECK(!cancelVideo.empty() && !updateVideo.empty() && !ownerResult.empty());
    CHECK(!frameHandle.empty() && !viewerFrame.empty());
    // The only Camera callback installation happens while configureAIChat constructs
    // MediaManager: before its FRAME handler is registered and before Earth enters run().
    // This is the safe initialization boundary for the process-stable dispatcher.
    size_t mediaManagerConstruction = setup.find("new earthai::MediaManager(");
    size_t frameHandlerRegistration = setup.find("viewer.addEventHandler(new AIFrameHandler(");
    size_t configureAI = earthMain.find("configureAIChat(aiDeps)");
    size_t viewerRun = earthMain.find("viewerResult = viewer.run()");
    CHECK(mediaManagerConstruction != std::string::npos);
    CHECK(frameHandlerRegistration != std::string::npos);
    CHECK(mediaManagerConstruction < frameHandlerRegistration);
    CHECK(configureAI != std::string::npos && viewerRun != std::string::npos);
    CHECK(configureAI < viewerRun);
    CHECK(setup.find("deps.captureCamera") != std::string::npos);
    CHECK(earthMain.find("aiDeps.captureCamera = cameras[3]") != std::string::npos);
    CHECK(beginVideo.find("_video->artifactId") != std::string::npos);
    CHECK(beginVideo.find("++_cinematicRequestSerial") != std::string::npos);
    CHECK(confirmVideo.find("generatedFramePathA") != std::string::npos);
    CHECK(confirmVideo.find("generatedFramePathB") != std::string::npos);

    // Runtime structure: request dispatch and both state machines reach exactly one publication
    // epilogue, which copies the FRAME-owned persistent command error.
    CHECK(update.find("_videoRequests.drain()") != std::string::npos);
    CHECK(update.find("_cinematicRequests.drain()") != std::string::npos);
    const std::string drainStatement =
        "std::vector<VideoUiRequest> requests = _videoRequests.drain();";
    size_t firstUpdateCode = update.find_first_not_of(" \t\r\n");
    CHECK(firstUpdateCode != std::string::npos);
    CHECK(update.compare(firstUpdateCode, drainStatement.size(), drainStatement) == 0);
    CHECK(update.find("std::string commandError;") == std::string::npos);
    CHECK(update.find("_videoCommandError = reduceVideoCommandError(") != std::string::npos);
    CHECK(update.find("snapshot.commandError = _videoCommandError;") != std::string::npos);
    CHECK(countOccurrences(update, "_videoSnapshot = snapshot;") == 1);
    CHECK(update.find("return;") == std::string::npos);
    size_t videoUpdate = update.find("updateVideoInternal()");
    size_t photoUpdate = update.find("updatePhotoInternal()");
    size_t publication = update.find("_videoSnapshot = snapshot;");
    CHECK(videoUpdate < photoUpdate && photoUpdate < publication);
    CHECK(update.find("reapDeferredCaptureCleanups()") != std::string::npos);
    CHECK(update.find("_grabber.reapTerminalGeneration()") != std::string::npos);
    CHECK(update.find("_grabber.reapTerminalGeneration()") <
          update.find("reapDeferredCaptureCleanups()"));
    // Timeout cleanup must own the exact token and late artifact paths before state reset for
    // photo, A, B, and orbit routes; deferred reaping waits for a claimed callback terminal.
    const std::string photoUpdateBody = extractFunctionBody(
        media, "void MediaManager::updatePhotoInternal()");
    CHECK(mediaHeader.find("void deferCaptureCleanup(") != std::string::npos);
    CHECK(photoUpdateBody.find("deferCaptureCleanup(_photoCaptureToken") !=
          std::string::npos);
    CHECK(updateVideo.find("deferVideoCaptureCleanup(v.snapCaptureTokenA)") !=
          std::string::npos);
    CHECK(updateVideo.find("deferVideoCaptureCleanup(v.snapCaptureTokenB)") !=
          std::string::npos);
    CHECK(updateVideo.find("deferVideoCaptureCleanup(v.orbitCaptureToken)") !=
          std::string::npos);

    // Getter must return only the locked published value, never derive from live VideoJob state.
    CHECK(snapshotGetter.find("return _videoSnapshot;") != std::string::npos);
    CHECK(snapshotGetter.find("_video->") == std::string::npos);
    CHECK(snapshotGetter.find("*_video") == std::string::npos);
    CHECK(snapshotGetter.find("videoPhase(") == std::string::npos);
    CHECK(snapshotGetter.find("pendingVideoInfo(") == std::string::npos);

    // Draw traversal reads one snapshot. New image/video starts use the unified cinematic value
    // queue; Finish-B / Confirm / Cancel remain explicit video state-machine requests. Live
    // mutations stay in FRAME-owned MediaManager / main-thread tool paths.
    CHECK(countOccurrences(draw, "media->videoUiSnapshot()") == 1);
    CHECK(draw.find("media->enqueueCinematicRequest(request)") != std::string::npos);
    CHECK(draw.find("request.kind = earthai::VideoUiRequest::CaptureEnd") != std::string::npos);
    CHECK(draw.find("request.kind = earthai::VideoUiRequest::Confirm") != std::string::npos);
    CHECK(draw.find("request.kind = earthai::VideoUiRequest::Cancel") != std::string::npos);
    CHECK(draw.find("media->beginVideoCapture(") == std::string::npos);
    CHECK(draw.find("media->captureVideoEnd(") == std::string::npos);
    CHECK(draw.find("media->confirmVideo(") == std::string::npos);
    CHECK(draw.find("media->cancelVideo(") == std::string::npos);
    CHECK(draw.find("media->videoPhase(") == std::string::npos);
    CHECK(draw.find("media->pendingVideoInfo(") == std::string::npos);
    CHECK(draw.find("media->startCinematicImageJob(") == std::string::npos);
    CHECK(draw.find("media->beginCinematicVideoCapture(") == std::string::npos);
    CHECK(setup.find("mediaVideoPtr->beginVideoCapture(") != std::string::npos);
    CHECK(setup.find("mediaVideoPtr->captureVideoEnd(") != std::string::npos);
    CHECK(frameHandle.find("_media->confirmVideo(") != std::string::npos);
    size_t mainThreadDrain = frameHandle.find("_core->drainMainThread()");
    size_t mediaUpdate = frameHandle.find("_media->update()");
    CHECK(mainThreadDrain != std::string::npos && mediaUpdate != std::string::npos);
    CHECK(mainThreadDrain < mediaUpdate);

    // Direct owner methods explicitly preserve errors on failure and clear them on success.
    CHECK(ownerResult.find("_videoCommandError = reduceVideoOwnerCommandError(") !=
          std::string::npos);
    CHECK(countOccurrences(beginVideo, "applyVideoOwnerCommandResult(false);") == 3);
    CHECK(countOccurrences(beginVideo, "applyVideoOwnerCommandResult(true);") == 1);
    CHECK(beginVideo.find(
        "!frozenCapture && !_routeCapabilities.canGeneratePointToPoint()") !=
          std::string::npos);
    CHECK(beginVideo.find("applyVideoOwnerCommandResult(false);") <
          beginVideo.find("return false;"));
    CHECK(beginVideo.rfind("applyVideoOwnerCommandResult(true);") <
          beginVideo.rfind("return true;"));

    CHECK(countOccurrences(captureEnd, "applyVideoOwnerCommandResult(false);") == 3);
    CHECK(countOccurrences(captureEnd, "applyVideoOwnerCommandResult(true);") == 1);
    CHECK(captureEnd.find("applyVideoOwnerCommandResult(false);") <
          captureEnd.find("return false;"));
    CHECK(captureEnd.rfind("applyVideoOwnerCommandResult(true);") <
          captureEnd.rfind("return true;"));

    CHECK(countOccurrences(confirmVideo, "applyVideoOwnerCommandResult(false);") == 4);
    CHECK(countOccurrences(confirmVideo, "applyVideoOwnerCommandResult(true);") == 1);
    size_t firstConfirmFailure = confirmVideo.find("applyVideoOwnerCommandResult(false);");
    size_t firstConfirmReturn = confirmVideo.find("return picojson::value(err);");
    size_t secondConfirmFailure = confirmVideo.find(
        "applyVideoOwnerCommandResult(false);", firstConfirmFailure + 1);
    size_t secondConfirmReturn = confirmVideo.find(
        "return picojson::value(err);", firstConfirmReturn + 1);
    size_t thirdConfirmFailure = confirmVideo.find(
        "applyVideoOwnerCommandResult(false);", secondConfirmFailure + 1);
    size_t thirdConfirmReturn = confirmVideo.find(
        "return picojson::value(err);", secondConfirmReturn + 1);
    size_t confirmSuccess = confirmVideo.find("applyVideoOwnerCommandResult(true);");
    size_t confirmSuccessReturn = confirmVideo.rfind("return picojson::value(r);");
    CHECK(firstConfirmFailure < firstConfirmReturn);
    CHECK(firstConfirmReturn < secondConfirmFailure && secondConfirmFailure < secondConfirmReturn);
    CHECK(secondConfirmReturn < thirdConfirmFailure &&
          thirdConfirmFailure < thirdConfirmReturn);
    CHECK(thirdConfirmReturn < confirmSuccess && confirmSuccess < confirmSuccessReturn);

    // Re-arming belongs only to the immutable cinematic capture contract.  Ordinary
    // generate_video keeps its legacy A/B capture path, while local orbit planning waits
    // for the final accepted A frame.
    CHECK(countOccurrences(updateVideo,
        "v.cinematic && cinematicVideoCaptureNeedsRearm(") == 2);
    CHECK(updateVideo.find("makeCinematicOneTakeOrbitPlan(") != std::string::npos);

    CHECK(countOccurrences(cancelVideo, "applyVideoOwnerCommandResult(true);") == 1);
    CHECK(cancelVideo.find("applyVideoOwnerCommandResult(true);") <
          cancelVideo.find("if (_video->phase == VideoJob::IDLE) return;"));

    // Draw/FRAME HUD sharing uses atomic paths and cannot decrement below zero.  Scene
    // adjustment has a separate reference count: deterministic local recording hides only UI,
    // preserving the exact visible sun/time/labels while photo reference captures keep their
    // established fill-light behavior.
    CHECK(mediaHeader.find("std::atomic<int> _hudHideCount;") != std::string::npos);
    CHECK(mediaHeader.find(
        "std::atomic<int> _captureSceneAdjustmentCount;") != std::string::npos);
    CHECK(isHudHidden.find("_hudHideCount.load()") != std::string::npos);
    CHECK(hudHide.find("_hudHideCount.fetch_add(") != std::string::npos);
    CHECK(hudHide.find("if (!adjustScene) return;") != std::string::npos);
    CHECK(hudHide.find("if (!adjustScene) return;") <
          hudHide.find("_captureSceneAdjustmentCount.fetch_add("));
    CHECK(hudRestore.find("_hudHideCount.compare_exchange_") != std::string::npos);
    CHECK(hudRestore.find("if (!adjustScene) return;") != std::string::npos);
    CHECK(hudRestore.find("if (!adjustScene) return;") <
          hudRestore.find("_captureSceneAdjustmentCount.compare_exchange_"));
    CHECK(hudRestore.find("--_hudHideCount") == std::string::npos);
    CHECK(hudRestore.find("_hudHideCount--") == std::string::npos);
    CHECK(update.find("_captureSceneAdjustmentCount.load() > 0") !=
          std::string::npos);
    CHECK(update.find("_hudHideCount.load() > 0) applyFillLight()") ==
          std::string::npos);
    CHECK(confirmVideo.find("hudHide(false);") != std::string::npos);
    CHECK(cancelVideo.find("finalizeVideoCancellation();") != std::string::npos);
    CHECK(countOccurrences(updateVideo, "hudRestore(false);") >= 3);

    // The application owns the orbit pose after manipulator update and before cull/draw.  This
    // ordering is the invariant that prevents the manipulator from overwriting a planned frame
    // and prevents culling from using the previous camera matrix.
    const size_t advance = viewerFrame.find("advance(simulationTime)");
    const size_t event = viewerFrame.find("eventTraversal()");
    const size_t traversalUpdate = viewerFrame.find("updateTraversal()");
    const size_t cameraOverride = viewerFrame.find("_beforeRendering()");
    const size_t rendering = viewerFrame.find("renderingTraversals()");
    const size_t doneGuard = viewerFrame.find("if (_done) return;");
    const size_t firstFrame = viewerFrame.find("if (_firstFrame)");
    const size_t viewerInit = viewerFrame.find("viewerInit()");
    const size_t realize = viewerFrame.find("realize()");
    const size_t finishFirstFrame = viewerFrame.find("_firstFrame = false");
    CHECK(doneGuard != std::string::npos);
    CHECK(doneGuard < firstFrame && firstFrame < viewerInit);
    CHECK(viewerInit < realize && realize < finishFirstFrame);
    CHECK(finishFirstFrame < advance);
    CHECK(advance < event && event < traversalUpdate);
    CHECK(traversalUpdate < cameraOverride && cameraOverride < rendering);
    CHECK(earthMain.find("aiMedia->applyDeterministicVideoCamera()") !=
          std::string::npos);

    CHECK(uiHeader.find("_videoConfirmError") == std::string::npos);
    std::cout << "[OK] media UI reducer, request queue, and publication wiring\n";
    return 0;
}
