#include <ui/ImGui.h>

#include <cstdlib>
#include <iostream>
#include <string>

#define CHECK(x) do { if (!(x)) { \
    std::cerr << "CHECK failed at " << __FILE__ << ":" << __LINE__ << ": " #x "\n"; \
    return 1; } } while (0)

int main()
{
    const std::string path = osgVerse::defaultImGuiSettingsPath();
    const char* home = std::getenv("HOME");
    if (home && home[0])
    {
        CHECK(!path.empty());
        CHECK(path[0] == '/');
        CHECK(path.find(".app/Contents") == std::string::npos);
#if defined(__APPLE__)
        CHECK(path == std::string(home) +
              "/Library/Application Support/osgVerse/imgui.ini");
#endif
    }
    std::cout << "[OK] ImGui settings path\n";
    return 0;
}
