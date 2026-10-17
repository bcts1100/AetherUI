#include "theme_library.hpp"
namespace aether::win {
namespace {
std::string serialize(const Theme& theme,std::string_view name){return serializeTheme(theme)+"\n[preset]\nname="+detail::presetEncode(name)+"\n";}
std::optional<SavedThemePreset> read(const detail::PresetStore& store,std::string_view id,std::string* error){
 auto text=store.read(id,error);if(!text)return {};
 auto theme=parseTheme(*text,error);if(!theme)return {};
 auto ini=detail::parsePresetIni(*text,error);if(!ini)return {};
 std::string name;
 if(auto metadata=ini->find("preset");metadata!=ini->end()){
  if(metadata->second.size()!=1||!metadata->second.contains("name")){if(error)*error="Palette name metadata is malformed.";return {};}
  auto decoded=detail::presetDecode(metadata->second.at("name"));if(!decoded){if(error)*error="Palette name encoding is malformed.";return {};}
  auto normalized=normalizePresetName(*decoded,error);if(!normalized)return {};name=*normalized;
 }else{
  // Old files remain valid: theme.name supplies the label, tokens stay unchanged.
  auto legacy=normalizePresetName(theme->name);name=legacy?*legacy:"Palette "+std::string(id.substr(7));
 }
 if(error)error->clear();return SavedThemePreset{std::string(id),*theme,std::move(name)};
}
std::vector<SavedThemePreset> list(const detail::PresetStore& store){
 std::vector<SavedThemePreset> result;for(const auto& id:store.ids()){auto item=read(store,id,nullptr);if(item)result.push_back(std::move(*item));if(result.size()==SavedThemePresetLimit)break;}return result;
}
}
std::vector<SavedThemePreset> savedThemePresets(std::string* error){detail::PresetStore store(L"theme-presets",L".aethertheme",false,error);return store?list(store):std::vector<SavedThemePreset>{};}
std::optional<SavedThemePreset> saveThemePreset(const Theme& theme,std::string* error){return saveThemePreset(theme,theme.name,error);}
std::optional<SavedThemePreset> saveThemePreset(const Theme& theme,std::string_view name,std::string* error){
 auto normalized=normalizePresetName(name,error);if(!normalized)return {};
 detail::PresetStore store(L"theme-presets",L".aethertheme",true,error);if(!store)return {};
 if(list(store).size()>=SavedThemePresetLimit){if(error)*error="Your profile has 32 saved palettes. Remove one before saving another.";return {};}
 auto text=serialize(theme,*normalized);auto tokens=parseTheme(text,error);if(!tokens)return {};
 auto id=store.create(text,error);if(!id)return {};return SavedThemePreset{*id,*tokens,*normalized};
}
std::optional<Theme> loadThemePreset(std::string_view id,std::string* error){
 detail::PresetStore store(L"theme-presets",L".aethertheme",false,error);if(!store)return {};
 auto item=read(store,id,error);return item?std::optional<Theme>(item->theme):std::nullopt;
}
bool renameThemePreset(std::string_view id,std::string_view name,std::string* error){
 auto normalized=normalizePresetName(name,error);if(!normalized)return false;
 detail::PresetStore store(L"theme-presets",L".aethertheme",false,error);if(!store)return false;
 auto item=read(store,id,error);return item&&store.replace(id,serialize(item->theme,*normalized),error);
}
bool removeThemePreset(std::string_view id,std::string* error){detail::PresetStore store(L"theme-presets",L".aethertheme",false,error);return store&&store.remove(id,error);}
}
