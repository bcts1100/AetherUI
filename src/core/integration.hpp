#pragma once
#include "model.hpp"
namespace aether {
enum class FrameMode {Follow,Light,Dark,Leave};
struct AppFrameRule {std::string executable;FrameMode mode=FrameMode::Follow;bool operator==(const AppFrameRule&)const=default;};
struct IntegrationOptions {
 bool windowsMode=false,windowsAccent=false,frames=false,terminal=false,restoreOnExit=true;
 std::string terminalFont="Cascadia Mono";int terminalSize=14,terminalOpacity=90;bool terminalBackground=false;
 int terminalDesign=0,terminalCursor=0,terminalShell=0;
 std::string terminalImage;
 std::vector<AppFrameRule> rules;
 bool operator==(const IntegrationOptions&)const=default;
};
bool darkTheme(const Theme&);IntegrationOptions parseIntegration(std::string_view);std::string serializeIntegration(const IntegrationOptions&);
std::string terminalFragment(const Theme&,const IntegrationOptions&,std::string_view windowsDirectory,std::string_view backgroundPath="");
std::string jsonString(std::string_view);std::array<std::uint32_t,16> terminalPalette(const Theme&);
inline constexpr std::array<std::string_view,11> TerminalDesignNames={"Follow desktop","Aubergine","Midnight Violet","Arctic","Pastel Mocha","Tokyo Dusk","Warm Earth","Solar Paper","Carbon","Woodland Zen","Neon Horizon"};
Theme terminalDesignTheme(const Theme&,int design);
bool validTerminalImage(std::string_view);
}
