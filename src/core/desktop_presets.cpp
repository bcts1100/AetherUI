#include "desktop_presets.hpp"
namespace aether {
std::vector<DesktopPreset> desktopPresets(){
 auto zen=preset(3);zen.name="Zen Forest";zen.background=0x101D18;zen.surface=0x1E3028;zen.raised=0x354C3F;zen.accent=0x9CCB97;zen.foreground=0xF0F6EC;zen.muted=0xB4C6B5;zen.surfaceOpacity=88;zen.topBarStyle=BarStyle::Floating;zen.dockStyle=BarStyle::Floating;
 auto ubuntu=preset(0);ubuntu.name="Ubuntu Clean";ubuntu.style=ThemeStyle::Minimal;ubuntu.topBarStyle=BarStyle::Solid;ubuntu.dockStyle=BarStyle::Floating;ubuntu.radius=12;
 auto pastel=preset(1);pastel.name="Pastel Dusk";pastel.background=0x1E1E2E;pastel.surface=0x313244;pastel.raised=0x45475A;pastel.foreground=0xCDD6F4;pastel.muted=0xBAC2DE;pastel.accent=0xCBA6F7;pastel.topBarStyle=BarStyle::Islands;pastel.dockStyle=BarStyle::Islands;
 auto neon=preset(4);neon.name="Neon Night";neon.background=0x10101C;neon.surface=0x1D1930;neon.raised=0x3C2853;neon.accent=0xF579D8;neon.foreground=0xF1EDFF;neon.muted=0xBAAED4;neon.fontFamily="Bahnschrift";neon.topBarStyle=BarStyle::Accent;neon.dockStyle=BarStyle::Floating;
 auto nord=preset(1);nord.name="Nord";nord.background=0x2E3440;nord.surface=0x3B4252;nord.raised=0x4C566A;nord.foreground=0xECEFF4;nord.muted=0xD8DEE9;nord.accent=0x88C0D0;nord.topBarStyle=BarStyle::Floating;nord.dockStyle=BarStyle::Floating;
 auto minimal=preset(4);minimal.name="Minimal Dark";minimal.topBarStyle=BarStyle::Solid;minimal.dockStyle=BarStyle::Solid;
 return {{"Zen Forest","Green woodland, quiet glass and a floating dock","papirus",zen,9},{"Ubuntu Clean","Warm palette, flat panel and colorful icons","yaru",ubuntu,1},{"Pastel Dusk","Soft purple palette and separated bar modules","tela",pastel,5},{"Neon Night","Pink accents, luminous icons and a compact dock","papirus-neon",neon,5},{"Nord","Cool blue-gray surfaces and Fluent icons","fluent",nord,4},{"Minimal Dark","Quiet solid panels and monochrome icons","papirus-mono",minimal,0}};
}
Config desktopPresetConfig(const Config& original,int index){auto presets=desktopPresets();if(index<0||index>=static_cast<int>(presets.size()))return original;auto next=original;next.theme=presets[index].theme;next.iconPack=presets[index].iconPack;next.autoWallpaperColors=false;next.animatedWallpaper=0;next.widgetsLocked=true;next.theme.topBarHeight=36;auto layout=[](DisplayOptions& d){d.edge=Edge::Bottom;d.iconSize=44;d.dockGap=12;};layout(next.defaults);for(auto& [id,d]:next.displays){(void)id;layout(d);}return next;}
}
