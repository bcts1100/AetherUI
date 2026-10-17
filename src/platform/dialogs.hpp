#pragma once
#include "windows.hpp"
#include <optional>
namespace aether::win {
std::optional<std::string> editNote(HWND owner,std::string_view initial,const wchar_t* title=L"Quick note",const wchar_t* hint=L"Saved locally in this profile. Up to 300 characters.",int limit=300);
std::optional<std::filesystem::path> chooseFile(HWND owner,bool save,const wchar_t* title,
 const COMDLG_FILTERSPEC* filters,UINT filterCount,const wchar_t* defaultName=nullptr,
 const wchar_t* defaultExtension=nullptr);
}
