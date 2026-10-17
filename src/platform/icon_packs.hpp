#pragma once
#include "config.hpp"
#include <map>
namespace aether::win {
struct IconPack {std::string id,name,source,license;std::filesystem::path directory;std::map<std::string,std::string> icons;};
std::string iconAlias(std::string_view);
std::optional<IconPack> readIconPack(const std::filesystem::path&,std::string* error=nullptr);
std::vector<IconPack> iconPacks();
std::optional<std::string> importIconPack(const std::filesystem::path&,std::string* error=nullptr);
std::filesystem::path packIcon(const IconPack&,const Pin&);
}
