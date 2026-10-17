#pragma once
#include "windows.hpp"
#include <cstdint>
namespace aether::win {
// Epoch is parent-owned and never persisted. Birth protects against PID reuse.
struct WindowMetadataRequest {
 std::uint64_t hwnd=0,birth=0,epoch=0;DWORD pid=0;
 bool operator==(const WindowMetadataRequest&) const=default;
};
struct WindowMetadata {
 WindowMetadataRequest token;std::wstring identity;
 bool desktopKnown=false,onCurrentDesktop=true;
};
struct LaunchRequest {
 std::wstring target;std::uint64_t owner=0,foreground=0;DWORD ownerPid=0;bool activate=false;
};
}