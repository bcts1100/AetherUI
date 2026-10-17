#include "backdrop.hpp"
namespace aether::win {
BackdropState applyBackdrop(HWND window,bool enabled) noexcept {
 DWORD owner=0;GetWindowThreadProcessId(window,&owner);
 if(!window||owner!=GetCurrentProcessId())return BackdropState::Unavailable;
 BOOL composition=FALSE;if(FAILED(DwmIsCompositionEnabled(&composition))||!composition)return enabled?BackdropState::Unavailable:BackdropState::Opaque;
 HIGHCONTRASTW contrast{sizeof(contrast)};
 const bool blocked=enabled&&(!SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(contrast),&contrast,0)||(contrast.dwFlags&HCF_HIGHCONTRASTON));
 // Desktop blur on Windows 10 has no supported DwmEnableBlurBehindWindow
 // implementation. Resolve the compositor's compatibility ABI at runtime;
 // failure always retains an opaque render target, never an invisible panel.
 struct Accent {int state,flags;DWORD tint;int animation;};
 struct Attribute {int attribute;void* data;SIZE_T bytes;};
 using SetAttribute=BOOL(WINAPI*)(HWND,Attribute*);
 const auto set=reinterpret_cast<SetAttribute>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"SetWindowCompositionAttribute"));
 if(!set)return enabled?BackdropState::Unavailable:BackdropState::Opaque;
 Accent accent{enabled&&!blocked?3:0,0,0,0};Attribute attribute{19,&accent,sizeof(accent)};
 if(!set(window,&attribute))return enabled?BackdropState::Unavailable:BackdropState::Opaque;
 return blocked?BackdropState::Unavailable:enabled?BackdropState::Blur:BackdropState::Opaque;
}
}
