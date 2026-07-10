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

    const std::string ui = readSourceFile("applications/earth_explorer/ai_ui.cpp");
    const std::string media = readSourceFile("applications/earth_explorer/ai_media.cpp");
    CHECK(ui.find("enqueueVideoRequest(") != std::string::npos);
    CHECK(ui.find("media->beginVideoCapture(") == std::string::npos);
    CHECK(ui.find("media->captureVideoEnd(") == std::string::npos);
    CHECK(ui.find("media->confirmVideo(") == std::string::npos);
    CHECK(ui.find("media->cancelVideo(") == std::string::npos);
    CHECK(media.find("_videoRequests.drain()") != std::string::npos);
    CHECK(media.find("videoUiSnapshot() const") != std::string::npos);
    std::cout << "[OK] media UI request queue\n";
    return 0;
}
