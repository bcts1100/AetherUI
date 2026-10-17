#pragma once
#include "windows.hpp"
namespace aether::win {
enum class BackdropState {Opaque,Blur,Unavailable};
// Compatibility entry point is resolved dynamically; only owned HWNDs change.
BackdropState applyBackdrop(HWND,bool enabled) noexcept;
}
