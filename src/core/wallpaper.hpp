#pragma once
#include "model.hpp"
#include <string>
namespace aether {
constexpr std::size_t WallpaperMonitorLimit=16,WallpaperPathLimit=4096,WallpaperDeviceLimit=1024;
enum class WallpaperFit:std::uint32_t {Center=0,Tile=1,Stretch=2,Fit=3,Fill=4,Span=5};
enum class WallpaperKind:std::uint32_t {Query=0,Apply=1,Undo=2};
enum class WallpaperStatus:std::uint32_t {Ok=0,Unavailable=1,Invalid=2,Stale=3,Failed=4,Partial=5,Uncertain=6};
struct WallpaperDisplay {std::wstring device;Rect bounds;bool operator==(const WallpaperDisplay&) const=default;};
struct WallpaperMonitor {WallpaperDisplay display;std::wstring path;bool operator==(const WallpaperMonitor&) const=default;};
struct WallpaperSnapshot {std::vector<WallpaperMonitor> monitors;WallpaperFit fit=WallpaperFit::Fill;std::uint32_t color=0;bool slideshow=false;bool operator==(const WallpaperSnapshot&) const=default;};
struct WallpaperRequest {
 WallpaperKind kind=WallpaperKind::Query;std::vector<WallpaperDisplay> displays;
 std::wstring image,selectedDevice;WallpaperFit fit=WallpaperFit::Fill;
 std::optional<WallpaperSnapshot> before,expected;
};
struct WallpaperResult {
 WallpaperKind kind=WallpaperKind::Query;WallpaperStatus status=WallpaperStatus::Unavailable;
 std::optional<WallpaperSnapshot> before,after;std::wstring message;
};
class WallpaperBackend {
public:
 virtual ~WallpaperBackend()=default;
 virtual std::optional<WallpaperSnapshot> query()=0;
 virtual bool restorable(const WallpaperSnapshot&){return true;}
 virtual bool position(WallpaperFit)=0;
 virtual bool background(std::uint32_t)=0;
 virtual bool image(const std::wstring& device,const std::wstring& path)=0;
};
bool validWallpaperPath(std::wstring_view,bool allowEmpty=false);
bool validWallpaperSnapshot(const WallpaperSnapshot&);
bool validWallpaperRequest(const WallpaperRequest&);
bool sameWallpaperTopology(const std::vector<WallpaperDisplay>&,const WallpaperSnapshot&);
bool sameWallpaperState(const WallpaperSnapshot&,const WallpaperSnapshot&);
std::optional<std::vector<WallpaperDisplay>> mapWallpaperDisplays(const WallpaperSnapshot&,const std::vector<Rect>&);
WallpaperResult executeWallpaper(WallpaperBackend&,const WallpaperRequest&);
}
