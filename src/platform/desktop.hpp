#pragma once
#include "windows.hpp"
#include "shell_types.hpp"
#include <memory>
#include <set>
namespace aether::win {
struct Monitor {HMONITOR handle{};std::wstring name;Rect bounds,work;int dpi=96;bool primary=false;bool operator==(const Monitor&) const = default;};
struct Window {HWND hwnd{};DWORD pid{};std::wstring title,path,identity,display;bool active=false,fullscreen=false;Rect bounds{};bool minimized=false;std::uint64_t processBirth=0;bool operator==(const Window&) const = default;};
std::vector<Monitor> monitors();
std::vector<Window> windows();
struct WindowScanStats {std::uint64_t snapshots=0,pathQueries=0,identityQueries=0,pathCacheHits=0,identityCacheHits=0;double totalMilliseconds=0;};
class WindowCollector {
 struct Impl;std::unique_ptr<Impl> impl_;WindowScanStats stats_;
public:
 WindowCollector();~WindowCollector();
 WindowCollector(const WindowCollector&)=delete;WindowCollector& operator=(const WindowCollector&)=delete;
 std::vector<Window> snapshot(bool reconcile=false);
 void invalidateIdentity(HWND hwnd);
 // Lifecycle destruction/recreation drops prior-window identity and desktop membership.
 void forgetWindow(HWND hwnd);
 std::vector<WindowMetadataRequest> takeMetadataRequests(std::size_t maximum=128);
 bool acceptMetadata(const std::vector<WindowMetadata>&);
 void rejectMetadata(const std::vector<WindowMetadataRequest>&);
 const WindowScanStats& statistics() const{return stats_;}
};
void activate(const Window& w,bool minimizeActive=false);
void snapActive(int zone);
class WindowEvents {
 std::vector<HWINEVENTHOOK> hooks_;
 static HWND receiver_;
 static RefreshGate gate_;
 static std::set<HWND> tracked_;
 static std::set<HWND> invalidated_;
 static std::set<HWND> moving_;
 static std::set<HWND> moveEnds_;
 static std::set<HWND> lifecycle_;
 static void CALLBACK callback(HWINEVENTHOOK,DWORD,HWND,LONG,LONG,DWORD,DWORD);
public:
 bool start(HWND receiver);void stop();~WindowEvents(){stop();}
 std::vector<HWND> takeIdentityInvalidations();
 bool moving() const{return !moving_.empty();}
 std::vector<HWND> takeUserMoveEnds();
 std::vector<HWND> takeLifecycleChanges();
 void acknowledge(){gate_.consume();}
 void acknowledge(const std::vector<Window>& snapshot){gate_.consume();tracked_.clear();for(const auto& window:snapshot)tracked_.insert(window.hwnd);}
 void pause(bool value){gate_.setPaused(value);if(value){invalidated_.clear();moving_.clear();moveEnds_.clear();lifecycle_.clear();}}
};
class Appbar {
 HWND owner_{},reservation_{};bool registered_=false,busy_=false;UINT edge_=ABE_TOP;Rect area_{};int pixels_=0;
 static LRESULT CALLBACK procedure(HWND,UINT,WPARAM,LPARAM);
public:
 Appbar()=default;Appbar(const Appbar&)=delete;Appbar& operator=(const Appbar&)=delete;
 ~Appbar(){detach();}
 void attach(HWND h,UINT edge);Rect position(Rect screen,int thickness);void detach();
 bool registered() const{return registered_;}
};
}
