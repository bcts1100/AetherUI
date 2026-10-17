#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <dwmapi.h>
#include <wrl/client.h>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include <cwchar>
#include <cstddef>
#include <iterator>
#include <utility>
#include "core/model.hpp"
namespace aether::win {
using Microsoft::WRL::ComPtr;
constexpr UINT RefreshMessage=WM_APP+11, CatalogMessage=WM_APP+12,
 IconsMessage=WM_APP+13, ApplyMessage=WM_APP+14, AppbarMessage=WM_APP+15,
 TrayMessage=WM_APP+16, RelayoutMessage=WM_APP+17, OpenEditorMessage=WM_APP+18, ShellMessage=WM_APP+19,PluginDataMessage=WM_APP+20,StatusMessage=WM_APP+21;
struct Handle {
 HANDLE value=nullptr;
 Handle()=default;explicit Handle(HANDLE v):value(v){}
 ~Handle(){reset();}
 Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
 Handle(Handle&& h) noexcept:value(h.value){h.value=nullptr;}
 Handle& operator=(Handle&& h) noexcept{if(this!=&h){reset();value=h.value;h.value=nullptr;}return *this;}
 void reset(HANDLE h=nullptr){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);value=h;}
 explicit operator bool() const{return value&&value!=INVALID_HANDLE_VALUE;}
};
struct ComApartment {
 HRESULT result;
 explicit ComApartment(DWORD mode=COINIT_APARTMENTTHREADED):result(CoInitializeEx(nullptr,mode)){}
 ~ComApartment(){if(SUCCEEDED(result))CoUninitialize();}
 ComApartment(const ComApartment&)=delete;ComApartment& operator=(const ComApartment&)=delete;
};
inline Rect rect(RECT r){return {static_cast<int>(r.left),static_cast<int>(r.top),static_cast<int>(r.right),static_cast<int>(r.bottom)};}
inline RECT native(Rect r){return {r.left,r.top,r.right,r.bottom};}
std::wstring wide(std::string_view text);
std::string utf8(std::wstring_view text);
std::wstring lower(std::wstring text);
std::filesystem::path executablePath();
std::wstring windowText(HWND hwnd);
std::wstring processPath(DWORD pid);
bool windowAlive(HWND hwnd,DWORD pid);
bool openTarget(HWND owner,const std::wstring& target);
void roundedWindow(HWND hwnd,int radius);
int dpiFor(HWND hwnd);
void message(HWND owner,const std::wstring& text,UINT flags=MB_OK|MB_ICONINFORMATION);
}
