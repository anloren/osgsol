#include <ui/ImGui.h>
#include <imgui/imgui.h>

#include <cstdlib>
#include <iostream>
#include <string>

#define CHECK(x) do { if (!(x)) { \
    std::cerr << "CHECK failed at " << __FILE__ << ":" << __LINE__ << ": " #x "\n"; \
    return 1; } } while (0)

int main()
{
    ImGuiContext* context = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    CHECK((io.ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard) == 0);
    osgVerse::configureImGuiProductInput(io);
    CHECK((io.ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard) != 0);
    CHECK((io.ConfigFlags & ImGuiConfigFlags_NavEnableGamepad) == 0);
    ImGui::DestroyContext(context);

    const std::string path = osgVerse::defaultImGuiSettingsPath();
#if defined(_WIN32)
    const char* localAppData = std::getenv("LOCALAPPDATA");
    if (localAppData && localAppData[0])
        CHECK(path == std::string(localAppData) + "/osgVerse/imgui.ini");
    else
        CHECK(path.empty());
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    if (home && home[0])
    {
        CHECK(!path.empty());
        CHECK(path[0] == '/');
        CHECK(path.find(".app/Contents") == std::string::npos);
        CHECK(path == std::string(home) +
              "/Library/Application Support/osgVerse/imgui.ini");
    }
#else
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    const char* home = std::getenv("HOME");
    if (xdg && xdg[0])
        CHECK(path == std::string(xdg) + "/osgVerse/imgui.ini");
    else if (home && home[0])
        CHECK(path == std::string(home) + "/.config/osgVerse/imgui.ini");
    else
        CHECK(path.empty());
#endif
    std::cout << "[OK] ImGui settings path\n";
    return 0;
}
