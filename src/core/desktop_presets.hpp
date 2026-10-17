#pragma once
#include "model.hpp"
namespace aether {
struct DesktopPreset {std::string name,description,iconPack;Theme theme;int background=0;};
std::vector<DesktopPreset> desktopPresets();
Config desktopPresetConfig(const Config&,int index);
}
