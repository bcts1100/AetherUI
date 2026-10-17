#include "layout_library.hpp"
#include "preset_store.hpp"
#include "core/tiling.hpp"
#include <set>
#include <charconv>
#include <sstream>
namespace aether::win {
namespace {
void fail(std::string* error,const char* message){if(error)*error=message;}
bool validDisplayName(std::string_view name){
 if(name.empty()||name.size()>256||!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,name.data(),static_cast<int>(name.size()),nullptr,0))return false;
 for(unsigned char c:name)if(c<32||c==127)return false;return true;
}
bool validOptions(const DisplayOptions& options){return (options.edge==Edge::Bottom||options.edge==Edge::Left||options.edge==Edge::Right)&&options.iconSize>=32&&options.iconSize<=72&&options.dockGap>=0&&options.dockGap<=32;}
void writeOptions(std::ostream& out,const DisplayOptions& options){
 out<<"edge="<<edgeName(options.edge)<<"\niconSize="<<options.iconSize<<"\ndockGap="<<options.dockGap<<"\ntopBar="<<options.topBar<<"\ndock="<<options.dock<<"\nautoHide="<<options.autoHide<<"\nmonitorOnly="<<options.monitorOnly<<"\nreserveDock="<<options.reserveDock<<"\nclock="<<options.clock<<"\nfocusedTitle="<<options.focusedTitle<<"\nstatus="<<options.status<<'\n';
}
std::string serialize(const SavedLayoutPreset& preset){
 std::ostringstream out;out<<"; Complete layout profile, data only\n[AetherLayout]\nversion=3\n[preset]\nname="<<detail::presetEncode(preset.name)<<"\n[layout]\n";writeOptions(out,preset.defaults);
 for(const auto& [name,options]:preset.displays){out<<"\n[display:"<<detail::presetEncode(name)<<"]\n";writeOptions(out,options);}Config tiling;tiling.tilingDefaults=preset.tilingDefaults;tiling.tilingDisplays=preset.tilingDisplays;tiling.snapLayouts=preset.snapLayouts;out<<serializeTilingSettings(tiling);return out.str();
}
std::optional<DisplayOptions> parseOptions(const std::map<std::string,std::string>& values,bool modern){
 if(values.size()!=(modern?11u:8u))return {};
 for(const char* key:{"edge","iconSize","dockGap","topBar","dock","autoHide","monitorOnly","reserveDock"})if(!values.contains(key))return {};
 DisplayOptions options;const auto& edge=values.at("edge");if(edge=="left")options.edge=Edge::Left;else if(edge=="right")options.edge=Edge::Right;else if(edge!="bottom")return {};
 auto number=[&](const char* key,int& value){const auto& text=values.at(key);auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),value);return error==std::errc{}&&end==text.data()+text.size();};
 if(!number("iconSize",options.iconSize)||!number("dockGap",options.dockGap)||!validOptions(options))return {};
 auto flag=[&](const char* key,bool& value){const auto& text=values.at(key);if(text!="0"&&text!="1")return false;value=text=="1";return true;};
 if(!flag("topBar",options.topBar)||!flag("dock",options.dock)||!flag("autoHide",options.autoHide)||!flag("monitorOnly",options.monitorOnly)||!flag("reserveDock",options.reserveDock))return {};
 if(modern){for(const char* key:{"clock","focusedTitle","status"})if(!values.contains(key))return {};if(!flag("clock",options.clock)||!flag("focusedTitle",options.focusedTitle)||!flag("status",options.status))return {};}
 return options;
}
bool validLayout(const SavedLayoutPreset& preset,std::string* error){
 Config tiling;tiling.tilingDefaults=preset.tilingDefaults;tiling.tilingDisplays=preset.tilingDisplays;tiling.snapLayouts=preset.snapLayouts;
 if(!validTilingConfig(tiling)||!validOptions(preset.defaults)||preset.displays.size()>32){fail(error,"Layouts require valid defaults and at most 32 display overrides.");return false;}
 for(const auto& [name,options]:preset.displays)if(!validDisplayName(name)||!validOptions(options)){fail(error,"Layout contains an invalid display identifier or option.");return false;}return true;
}
std::optional<SavedLayoutPreset> read(const detail::PresetStore& store,std::string_view id,std::string* error){
 auto text=store.read(id,error);if(!text)return {};auto ini=detail::parsePresetIni(*text,error);if(!ini)return {};
 auto bad=[&]()->std::optional<SavedLayoutPreset>{fail(error,"Expected a complete AetherLayout version=1, version=2 or version=3 file with valid layout data.");return {};};
 if(!ini->contains("AetherLayout")||ini->at("AetherLayout").size()!=1||!ini->at("AetherLayout").contains("version")||(ini->at("AetherLayout").at("version")!="1"&&ini->at("AetherLayout").at("version")!="2"&&ini->at("AetherLayout").at("version")!="3")||!ini->contains("preset")||ini->at("preset").size()!=1||!ini->at("preset").contains("name")||!ini->contains("layout"))return bad();
 auto decoded=detail::presetDecode(ini->at("preset").at("name"));if(!decoded)return bad();auto name=normalizePresetName(*decoded,error);if(!name)return {};
 bool modern=ini->at("AetherLayout").at("version")=="3";auto defaults=parseOptions(ini->at("layout"),modern);if(!defaults)return bad();SavedLayoutPreset result{std::string(id),*name,*defaults,{}};
 for(const auto& [section,values]:*ini){
  if(section=="AetherLayout"||section=="preset"||section=="layout")continue;
  if(section=="tiling"&&ini->at("AetherLayout").at("version")!="1")continue;
  if(section.rfind("display:",0)!=0||result.displays.size()>=32)return bad();
  auto display=detail::presetDecode(std::string_view(section).substr(8));auto options=parseOptions(values,modern);
  if(!display||!validDisplayName(*display)||!options||result.displays.contains(*display))return bad();result.displays.emplace(*display,*options);
 }
 if(ini->at("AetherLayout").at("version")!="1"){
  if(!ini->contains("tiling"))return bad();std::ostringstream textOut;textOut<<"[tiling]\n";for(const auto& [key,value]:ini->at("tiling"))textOut<<key<<'='<<value<<'\n';Config config;
  if(!parseTilingSettings(config,textOut.str()))return bad();result.tilingDefaults=config.tilingDefaults;result.tilingDisplays=std::move(config.tilingDisplays);result.snapLayouts=std::move(config.snapLayouts);
 }
 if(!validLayout(result,error))return {};if(error)error->clear();return result;
}
std::vector<SavedLayoutPreset> list(const detail::PresetStore& store){std::vector<SavedLayoutPreset> result;for(const auto& id:store.ids()){auto preset=read(store,id,nullptr);if(preset)result.push_back(std::move(*preset));if(result.size()==SavedLayoutPresetLimit)break;}return result;}
}
std::vector<SavedLayoutPreset> savedLayoutPresets(std::string* error){detail::PresetStore store(L"layout-presets",L".aetherlayout",false,error);return store?list(store):std::vector<SavedLayoutPreset>{};}
std::optional<SavedLayoutPreset> saveLayoutPreset(const Config& config,std::string_view name,std::string* error){
 auto normalized=normalizePresetName(name,error);if(!normalized)return {};SavedLayoutPreset preset{{},*normalized,config.defaults,config.displays,config.tilingDefaults,config.tilingDisplays,config.snapLayouts};if(!validLayout(preset,error))return {};
 detail::PresetStore store(L"layout-presets",L".aetherlayout",true,error);if(!store)return {};
 if(list(store).size()>=SavedLayoutPresetLimit){fail(error,"Your profile has 32 saved layouts. Remove one before saving another.");return {};}
 auto id=store.create(serialize(preset),error);if(!id)return {};preset.id=*id;return preset;
}
std::optional<SavedLayoutPreset> loadLayoutPreset(std::string_view id,std::string* error){detail::PresetStore store(L"layout-presets",L".aetherlayout",false,error);return store?read(store,id,error):std::nullopt;}
bool renameLayoutPreset(std::string_view id,std::string_view name,std::string* error){
 auto normalized=normalizePresetName(name,error);if(!normalized)return false;detail::PresetStore store(L"layout-presets",L".aetherlayout",false,error);if(!store)return false;
 auto preset=read(store,id,error);if(!preset)return false;preset->name=*normalized;return store.replace(id,serialize(*preset),error);
}
bool removeLayoutPreset(std::string_view id,std::string* error){detail::PresetStore store(L"layout-presets",L".aetherlayout",false,error);return store&&store.remove(id,error);}
Config applyLayoutPreset(const Config& current,const SavedLayoutPreset& preset,const std::vector<std::string>& connected,std::string* error){
 Config result=current;if(error)error->clear();if(!validLayout(preset,error))return result;
 result.defaults=preset.defaults;
 for(const auto& name:connected){auto saved=preset.displays.find(name);if(saved==preset.displays.end())result.displays.erase(name);else result.displays[name]=saved->second;}
 // Preserve disconnected values already in this profile. Seed absent saved values
 // for later reconnection without overwriting intentional current preferences.
 for(const auto& [name,options]:preset.displays)if(!result.displays.contains(name))result.displays.emplace(name,options);
  result.tilingDefaults=preset.tilingDefaults;result.snapLayouts=preset.snapLayouts;
 std::set<std::string> connectedNames(connected.begin(),connected.end());
 for(const auto& name:connected){auto saved=preset.tilingDisplays.find(name);if(saved==preset.tilingDisplays.end())result.tilingDisplays.erase(name);else result.tilingDisplays[name]=saved->second;}
 // Disconnected custom profiles retain their geometry even when a saved layout
 // reuses their ID; create a bounded local ID and remap only the retained policy.
 for(auto& [name,policy]:result.tilingDisplays)if(!connectedNames.contains(name)&&policy.layoutId!="balanced"&&policy.layoutId!="master"){
  auto old=findSnapLayout(current,policy.layoutId);if(!old){if(error)*error="Disconnected tiling profile references a missing layout.";result=current;return result;}
  auto existing=findSnapLayout(result,policy.layoutId);if(existing&&*existing==*old)continue;
  SnapLayout retained=*old;if(existing){unsigned index=1;do{retained.id="retained-"+std::to_string(index++);}while(findSnapLayout(result,retained.id)&&index<1000);}
  if(result.snapLayouts.size()>=SnapLayoutLimit){if(error)*error="This layout cannot preserve disconnected custom zones within the 16-layout limit.";result=current;return result;}
  result.snapLayouts.push_back(retained);policy.layoutId=retained.id;
 }
 for(const auto& [name,policy]:preset.tilingDisplays)if(!result.tilingDisplays.contains(name))result.tilingDisplays.emplace(name,policy);
 if(result.displays.size()>32||!validTilingConfig(result)){fail(error,"This layout would exceed 32 display overrides. Reset unused overrides first.");result=current;}
 return result;
}
}
