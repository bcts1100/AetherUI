#include "platform/config.hpp"
#include <iostream>
int main() {
 using namespace aether;
 win::ComApartment com;
 auto profile = win::executablePath().parent_path() / L"test-profiles" / (L"config-" + std::to_wstring(GetCurrentProcessId()));
 SetEnvironmentVariableW(L"AETHER_UI_SETTINGS_DIR", profile.c_str());
 if (win::configDirectory() != profile) {std::cerr << "Explicit settings directory was ignored.\n"; return 1;}
 Config config; config.theme=preset(1); config.clock24=true; config.pins={{"Tea", "C:/Tea.exe", "tea"}};
 win::saveConfig(config);
 bool first=true; auto loaded=win::loadConfig(first);
 if(first || loaded.theme!=config.theme || loaded.pins!=config.pins || !loaded.clock24) return 2;
 config.theme=preset(2); win::saveConfig(config);
 if(win::readText(profile/L"settings-v3.ini.bak").empty()) return 3;
 win::writeAtomic(profile/L"settings-v3.ini", "[layout]\niconSize=corrupt\n");
 loaded=win::loadConfig(first); if(first || loaded.defaults.iconSize!=48) return 4;
 std::cout << "Isolated settings, atomic save, backup and corrupt fallback passed.\n";
 return 0;
}