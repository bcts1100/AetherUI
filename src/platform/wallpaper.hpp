#pragma once
#include "desktop.hpp"
#include "provider_client.hpp"
#include "core/wallpaper.hpp"
#include <memory>
namespace aether::win {
constexpr UINT WallpaperMessage=WM_APP+30;
struct WallpaperCompletion {
 std::uint64_t id=0,scope=0;WallpaperKind kind=WallpaperKind::Query;
 ProviderOutcome outcome=ProviderOutcome::Unavailable;WallpaperResult result;
 std::vector<Monitor> displays;
};
class WallpaperService {
 struct Impl;std::unique_ptr<Impl> impl_;
public:
 explicit WallpaperService(ProviderProcessOptions options={});~WallpaperService();
 WallpaperService(const WallpaperService&)=delete;WallpaperService& operator=(const WallpaperService&)=delete;
 void start(HWND receiver);void stop();
 std::uint64_t requestQuery(const std::vector<Monitor>&,std::uint64_t scope,bool inspectOnly=false);
 std::uint64_t requestApply(const std::vector<Monitor>&,std::uint64_t scope,const std::wstring& image,WallpaperFit,std::optional<std::wstring> displayName={});
 std::uint64_t requestUndo(const std::vector<Monitor>&,std::uint64_t scope);
 std::vector<WallpaperCompletion> takeCompletions();
 bool canUndo() const;
};
}
