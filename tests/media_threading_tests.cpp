#include <applications/earth_explorer/ai_media.h>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

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

    const std::string ui = readSourceFile("applications/earth_explorer/ai_ui.cpp");
    const std::string uiHeader = readSourceFile("applications/earth_explorer/ai_ui.h");
    const std::string media = readSourceFile("applications/earth_explorer/ai_media.cpp");
    const std::string mediaHeader = readSourceFile("applications/earth_explorer/ai_media.h");
    const std::string setup = readSourceFile("applications/earth_explorer/ai_setup.cpp");
    CHECK(!ui.empty() && !uiHeader.empty() && !media.empty() && !mediaHeader.empty());

    const std::string update = extractFunctionBody(media, "void MediaManager::update()");
    const std::string snapshotGetter = extractFunctionBody(
        media, "MediaManager::VideoUiSnapshot MediaManager::videoUiSnapshot() const");
    const std::string hudHide = extractFunctionBody(media, "void MediaManager::hudHide()");
    const std::string hudRestore = extractFunctionBody(media, "void MediaManager::hudRestore()");
    const std::string isHudHidden = extractFunctionBody(mediaHeader, "bool isHudHidden() const");
    const std::string draw = extractFunctionBody(ui, "void AIChatUI::draw(");
    CHECK(!update.empty() && !snapshotGetter.empty() && !hudHide.empty());
    CHECK(!hudRestore.empty() && !isHudHidden.empty() && !draw.empty());

    // Runtime structure: request dispatch and both state machines reach exactly one publication
    // epilogue, which copies the FRAME-owned persistent command error.
    CHECK(update.find("_videoRequests.drain()") != std::string::npos);
    CHECK(update.find("std::string commandError;") == std::string::npos);
    CHECK(update.find("_videoCommandError = reduceVideoCommandError(") != std::string::npos);
    CHECK(update.find("snapshot.commandError = _videoCommandError;") != std::string::npos);
    CHECK(countOccurrences(update, "_videoSnapshot = snapshot;") == 1);
    CHECK(update.find("return;") == std::string::npos);
    size_t videoUpdate = update.find("updateVideoInternal()");
    size_t photoUpdate = update.find("updatePhotoInternal()");
    size_t publication = update.find("_videoSnapshot = snapshot;");
    CHECK(videoUpdate < photoUpdate && photoUpdate < publication);

    // Getter must return only the locked published value, never derive from live VideoJob state.
    CHECK(snapshotGetter.find("return _videoSnapshot;") != std::string::npos);
    CHECK(snapshotGetter.find("_video->") == std::string::npos);
    CHECK(snapshotGetter.find("*_video") == std::string::npos);
    CHECK(snapshotGetter.find("videoPhase(") == std::string::npos);
    CHECK(snapshotGetter.find("pendingVideoInfo(") == std::string::npos);

    // Draw traversal reads one snapshot and enqueues all four request kinds; live mutations stay
    // in FRAME-owned MediaManager / main-thread tool paths.
    CHECK(countOccurrences(draw, "media->videoUiSnapshot()") == 1);
    CHECK(draw.find("request.kind = earthai::VideoUiRequest::Begin") != std::string::npos);
    CHECK(draw.find("request.kind = earthai::VideoUiRequest::CaptureEnd") != std::string::npos);
    CHECK(draw.find("request.kind = earthai::VideoUiRequest::Confirm") != std::string::npos);
    CHECK(draw.find("request.kind = earthai::VideoUiRequest::Cancel") != std::string::npos);
    CHECK(draw.find("media->beginVideoCapture(") == std::string::npos);
    CHECK(draw.find("media->captureVideoEnd(") == std::string::npos);
    CHECK(draw.find("media->confirmVideo(") == std::string::npos);
    CHECK(draw.find("media->cancelVideo(") == std::string::npos);
    CHECK(setup.find("mediaVideoPtr->beginVideoCapture(") != std::string::npos);
    CHECK(setup.find("mediaVideoPtr->captureVideoEnd(") != std::string::npos);

    // Draw/FRAME HUD sharing uses only the atomic paths and cannot decrement below zero.
    CHECK(mediaHeader.find("std::atomic<int> _hudHideCount;") != std::string::npos);
    CHECK(isHudHidden.find("_hudHideCount.load()") != std::string::npos);
    CHECK(hudHide.find("_hudHideCount.fetch_add(") != std::string::npos);
    CHECK(hudRestore.find("_hudHideCount.compare_exchange_") != std::string::npos);
    CHECK(hudRestore.find("--_hudHideCount") == std::string::npos);
    CHECK(hudRestore.find("_hudHideCount--") == std::string::npos);

    CHECK(uiHeader.find("_videoConfirmError") == std::string::npos);
    std::cout << "[OK] media UI reducer, request queue, and publication wiring\n";
    return 0;
}
