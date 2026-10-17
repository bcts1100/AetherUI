#include "marketplace.hpp"
namespace aether::win {
const std::vector<CatalogPackage>& bundledCatalog(){static const std::vector<CatalogPackage> catalog={
 {"graphite","Graphite","Quiet charcoal surfaces and precise highlights.","Appearance only",PackageKind::Theme,5},
 {"linen","Linen","Warm light surfaces and soft typography.","Appearance only",PackageKind::Theme,6},
 {"aurora-look","Aurora glass","Cool translucent surfaces with real desktop blur.","Appearance and Windows compositor",PackageKind::Theme,7},
 {"ember","Ember","Warm accents for a darker workspace.","Appearance only",PackageKind::Theme,8},
 {"frosted","Frosted glass","Translucent native panels; opaque fallback when unavailable.","Appearance and Windows compositor",PackageKind::Theme,3},
 {"desk","Essential desk","Clock, focused application and three app shortcuts.","Clock and current application title; shortcuts launch on click",PackageKind::Widgets,0},
 {"productivity","Productivity","Media, battery, resources, local notes and Seattle weather.","Windows media session; system resources; local note; Seattle forecast from NWS",PackageKind::Widgets,1},
 {"live-aurora","Aurora ribbons","Slow teal ribbons across every connected display.","Owned desktop surfaces; no network or audio",PackageKind::Wallpaper,1},
 {"live-nebula","Nebula","Violet and blue motion across a midnight canvas.","Owned desktop surfaces; no network or audio",PackageKind::Wallpaper,2},
 {"live-dunes","Ember dunes","Warm flowing ribbons with muted sunset colors.","Owned desktop surfaces; no network or audio",PackageKind::Wallpaper,3}};return catalog;}
namespace {
bool known(std::string_view id){return std::any_of(bundledCatalog().begin(),bundledCatalog().end(),[&](const auto& p){return p.id==id;});}
std::vector<InstalledPackage> list(const detail::PresetStore& store){std::vector<InstalledPackage> result;for(const auto& receipt:store.ids()){auto text=store.read(receipt,nullptr);if(!text)continue;auto ini=detail::parsePresetIni(*text,nullptr);if(!ini||ini->size()!=1)continue;auto section=ini->find("AetherPackage");if(section==ini->end()||section->second.size()!=2)continue;auto& values=section->second;if(!values.contains("version")||values.at("version")!="1"||!values.contains("id")||!known(values.at("id")))continue;result.push_back({receipt,values.at("id")});}return result;}
}
std::vector<InstalledPackage> installedPackages(std::string* error){detail::PresetStore store(L"marketplace",L".aetherpackage",false,error);return store?list(store):std::vector<InstalledPackage>{};}
std::optional<InstalledPackage> installPackage(std::string_view id,std::string* error){
 if(!known(id)){if(error)*error="Unknown bundled package.";return {};}
 detail::PresetStore store(L"marketplace",L".aetherpackage",true,error);if(!store)return {};
 for(const auto& p:list(store))if(p.id==id)return p;
 auto receipt=store.create("[AetherPackage]\nversion=1\nid="+std::string(id)+"\n",error);return receipt?std::optional<InstalledPackage>({*receipt,std::string(id)}):std::nullopt;
}
bool removePackage(std::string_view receipt,std::string* error){detail::PresetStore store(L"marketplace",L".aetherpackage",false,error);if(!store)return false;auto packages=list(store);if(std::none_of(packages.begin(),packages.end(),[&](const auto& p){return p.receipt==receipt;})){if(error)*error="Package receipt is missing or invalid.";return false;}return store.remove(receipt,error);}
}
