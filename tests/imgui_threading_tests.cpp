#include <ui/ImGuiInputQueue.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#define CHECK(x) do { if (!(x)) { \
    std::cerr << "CHECK failed at " << __FILE__ << ":" << __LINE__ << ": " #x "\n"; \
    return 1; } } while (0)

int main()
{
    osgVerse::ImGuiInputQueue queue;
    std::vector<std::thread> producers;
    for (int producer = 0; producer < 4; ++producer)
    {
        producers.push_back(std::thread([producer, &queue]
        {
            for (int i = 0; i < 1000; ++i)
                queue.push(osgVerse::ImGuiInputEvent::keyEvent(
                    producer * 1000 + i, true, 0));
        }));
    }
    for (size_t i = 0; i < producers.size(); ++i) producers[i].join();
    CHECK(queue.takeAll().size() == 4000);
    CHECK(queue.takeAll().empty());
    queue.publishCapture(true, false);
    CHECK(queue.wantsMouse());
    CHECK(!queue.wantsKeyboard());

    std::ifstream sourceFile(std::string(OSGVERSE_SOURCE_DIR) + "/ui/ImGui3D.cpp");
    std::ostringstream sourceBuffer; sourceBuffer << sourceFile.rdbuf();
    const std::string source = sourceBuffer.str();
    const size_t release = source.find("void releaseOnDrawThread()");
    const size_t nextMethod = source.find("virtual bool handle(", release);
    const size_t shutdown = source.find("shutdownImGuiRendererBackend();", release);
    CHECK(release != std::string::npos);
    CHECK(nextMethod != std::string::npos);
    CHECK(shutdown != std::string::npos && shutdown < nextMethod);
    std::cout << "[OK] ImGui immutable input queue\n";
    return 0;
}
