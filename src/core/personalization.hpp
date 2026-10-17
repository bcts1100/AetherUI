#pragma once
#include "model.hpp"
#include <span>
#include <array>
namespace aether {
struct ModuleBox {TopModule module;Rect bounds;int zone;};
std::vector<ModuleBox> topModuleBoxes(const DisplayOptions&,BarStyle,int width,int height,int padding);
bool moveTopModule(DisplayOptions&,TopModule,int zone,std::optional<TopModule> before={});
Theme withPaletteColors(const Theme& base,const Theme& palette);
double contrastRatio(std::uint32_t,std::uint32_t);
std::array<std::uint8_t,4> colorRefBgra(std::uint32_t);
std::vector<Theme> wallpaperPalettes(const Theme&,std::span<const std::uint8_t> bgra,unsigned width,unsigned height);
std::vector<Rect> barShapes(BarStyle,bool top,int width,int height,int padding,int slots=1,bool vertical=false,int slotSize=56);
}
