#pragma once
#include "desktop.hpp"
namespace aether::win {
struct Workspace {GUID id{};std::wstring name;bool current=false;};
struct WorkspaceAppearance {int background=0;std::wstring image;bool operator==(const WorkspaceAppearance&)const=default;};
constexpr UINT WorkspaceChangedMessage=WM_APP+44;
// Shell-owned virtual desktops; no foreign windows are hidden by Aether.
// The internal ABI is admitted only on Windows 10 19041--19045.
class Workspaces final {
 struct Impl;std::unique_ptr<Impl> impl_;
public:
 Workspaces();~Workspaces();
 Workspaces(const Workspaces&)=delete;Workspaces& operator=(const Workspaces&)=delete;
 bool refresh();bool available()const;const std::vector<Workspace>& items()const;
 const std::wstring& notice()const;
 bool create();bool switchTo(const GUID&);bool move(const Window&,const GUID&);
 // Names/order belong to this Aether profile, keyed by native desktop GUID.
 bool rename(const GUID&,std::wstring_view);bool reorder(const GUID&,int position);
 bool setAppearance(const GUID&,WorkspaceAppearance);WorkspaceAppearance appearance(const GUID&)const;
 bool watch(HWND receiver);void unwatch();
 // Only desktops created through this instance can be removed. Used by owned tests.
 bool removeCreated(const GUID&,const GUID& fallback);
};
bool requestWindowClose(const Window&) noexcept;
std::wstring overviewAppKey(const Window&);
}
