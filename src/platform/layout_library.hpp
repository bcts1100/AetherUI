#pragma once
#include "config.hpp"
namespace aether::win {
struct SavedLayoutPreset {std::string id,name;DisplayOptions defaults;std::map<std::string,DisplayOptions> displays;TilingOptions tilingDefaults;std::map<std::string,TilingOptions> tilingDisplays;std::vector<SnapLayout> snapLayouts;};
inline constexpr std::size_t SavedLayoutPresetLimit=32;
std::vector<SavedLayoutPreset> savedLayoutPresets(std::string* error=nullptr);
std::optional<SavedLayoutPreset> saveLayoutPreset(const Config&,std::string_view name,std::string* error=nullptr);
std::optional<SavedLayoutPreset> loadLayoutPreset(std::string_view id,std::string* error=nullptr);
bool renameLayoutPreset(std::string_view id,std::string_view name,std::string* error=nullptr);
bool removeLayoutPreset(std::string_view id,std::string* error=nullptr);
// Complete connected profile: replace defaults, apply saved connected overrides or
// restore inheritance. Keep current disconnected preferences; seed missing ones.
// Preserve theme, pins and behavior, including the native taskbar preference.
Config applyLayoutPreset(const Config&,const SavedLayoutPreset&,const std::vector<std::string>& connected,std::string* error=nullptr);
}
