#pragma once
#include "windows.hpp"
namespace aether::win {
enum class PreviewState {Hidden,Ready,InvalidSource,HiddenSource,MinimizedSource,Unavailable};
// Physical client coordinates, centered without cropping or changing source aspect.
RECT fitPreview(SIZE source,RECT region) noexcept;
class WindowPreview final {
 HTHUMBNAIL thumbnail_{};
 HWND owner_{},source_{};DWORD sourcePid_{};
 RECT destination_{};PreviewState state_=PreviewState::Hidden;
 BYTE opacity_=255;
 PreviewState fallback(PreviewState state) noexcept;
public:
 WindowPreview()=default;~WindowPreview(){hide();}
 WindowPreview(const WindowPreview&)=delete;WindowPreview& operator=(const WindowPreview&)=delete;
 PreviewState show(HWND destination,HWND source,DWORD expectedPid,RECT region,BYTE opacity=255) noexcept;
 void hide() noexcept;
 PreviewState state() const noexcept{return state_;}
 RECT destination() const noexcept{return destination_;}
 HTHUMBNAIL nativeHandle() const noexcept{return thumbnail_;}
};
}
