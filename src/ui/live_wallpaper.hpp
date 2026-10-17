#pragma once
#include "canvas.hpp"
#include "platform/desktop.hpp"
namespace aether::ui {
// Procedural, muted backgrounds. Every HWND and timer belongs to this process;
// exiting (including a crash) uncovers the unchanged Windows wallpaper.
class LiveWallpaper {
 class Scene;Graphics& graphics_;std::vector<std::unique_ptr<Scene>> scenes_;
 std::vector<win::Monitor> displays_;int preset_=0;bool still_=false;std::shared_ptr<const win::IconPixels> image_;HWND host_=nullptr;
 std::wstring notice_;ULONGLONG retryAfter_=0;int attemptedPreset_=0;
public:
 explicit LiveWallpaper(Graphics&);~LiveWallpaper();
 bool update(int preset,const std::vector<win::Monitor>&,const std::vector<win::Window>&,bool paused,HWND ownedTestHost=nullptr,std::shared_ptr<const win::IconPixels> image={},bool still=false);
 void stop();std::size_t count()const{return scenes_.size();}
 std::uint64_t frames()const;
 std::vector<Rect> surfaceBounds()const;
 const std::wstring& notice()const{return notice_;}
};
}
