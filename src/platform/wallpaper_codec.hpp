#pragma once
#include "provider_client.hpp"
#include "core/wallpaper.hpp"
namespace aether::win {
ProviderBytes encodeWallpaperRequest(const WallpaperRequest&);
std::optional<WallpaperRequest> decodeWallpaperRequest(const ProviderBytes&);
ProviderBytes encodeWallpaperResult(const WallpaperResult&);
std::optional<WallpaperResult> decodeWallpaperResult(const ProviderBytes&);
}
