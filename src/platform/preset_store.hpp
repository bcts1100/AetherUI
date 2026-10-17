#pragma once
#include "config.hpp"
namespace aether::win {
// Names are data, never paths. Limit to 80 UTF-16 code units / 320 UTF-8 bytes.
std::optional<std::string> normalizePresetName(std::string_view,std::string* error=nullptr);
namespace detail {
using PresetIni=std::map<std::string,std::map<std::string,std::string>>;
std::string presetEncode(std::string_view);
std::optional<std::string> presetDecode(std::string_view);
std::optional<PresetIni> parsePresetIni(std::string_view,std::string* error);
class PresetStore {
 std::filesystem::path directory_;std::wstring extension_;Handle lock_;
 std::filesystem::path path(std::string_view) const;
public:
 PresetStore(const wchar_t* folder,const wchar_t* extension,bool create,std::string* error);
 explicit operator bool() const{return static_cast<bool>(lock_);}
 std::vector<std::string> ids() const;
 std::optional<std::string> read(std::string_view,std::string* error) const;
 std::optional<std::string> create(std::string_view,std::string* error) const;
 bool replace(std::string_view,std::string_view,std::string* error) const;
 bool remove(std::string_view,std::string* error) const;
};
}
}
