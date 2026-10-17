#pragma once
#include "preset_store.hpp"
namespace aether::win {
struct SavedThemePreset {std::string id;Theme theme;std::string name;};
inline constexpr std::size_t SavedThemePresetLimit=32;
std::vector<SavedThemePreset> savedThemePresets(std::string* error=nullptr);
std::optional<SavedThemePreset> saveThemePreset(const Theme&,std::string* error=nullptr);
std::optional<SavedThemePreset> saveThemePreset(const Theme&,std::string_view name,std::string* error=nullptr);
bool renameThemePreset(std::string_view id,std::string_view name,std::string* error=nullptr);
std::optional<Theme> loadThemePreset(std::string_view id,std::string* error=nullptr);
bool removeThemePreset(std::string_view id,std::string* error=nullptr);
}
