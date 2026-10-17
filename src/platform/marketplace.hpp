#pragma once
#include "preset_store.hpp"
namespace aether::win {
enum class PackageKind {Theme,Widgets,Wallpaper};
struct CatalogPackage{std::string id,name,description,permissions;PackageKind kind;int preset;};
struct InstalledPackage{std::string receipt,id;};
const std::vector<CatalogPackage>& bundledCatalog();
std::vector<InstalledPackage> installedPackages(std::string* error=nullptr);
std::optional<InstalledPackage> installPackage(std::string_view id,std::string* error=nullptr);
bool removePackage(std::string_view receipt,std::string* error=nullptr);
}
