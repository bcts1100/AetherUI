#include "window_preview.hpp"
#include <cstdint>
namespace aether::win {
RECT fitPreview(SIZE source,RECT region) noexcept{
 const auto width=static_cast<std::int64_t>(region.right)-region.left;
 const auto height=static_cast<std::int64_t>(region.bottom)-region.top;
 if(source.cx<=0||source.cy<=0||width<=0||height<=0)return {};
 std::int64_t fitWidth=width,fitHeight=height;
 if(static_cast<std::int64_t>(source.cx)*height>static_cast<std::int64_t>(source.cy)*width)
  fitHeight=std::max<std::int64_t>(1,width*source.cy/source.cx);
 else fitWidth=std::max<std::int64_t>(1,height*source.cx/source.cy);
 auto left=static_cast<std::int64_t>(region.left)+(width-fitWidth)/2;
 auto top=static_cast<std::int64_t>(region.top)+(height-fitHeight)/2;
 return {static_cast<LONG>(left),static_cast<LONG>(top),static_cast<LONG>(left+fitWidth),static_cast<LONG>(top+fitHeight)};
}
void WindowPreview::hide() noexcept{
 if(thumbnail_)DwmUnregisterThumbnail(thumbnail_);
 thumbnail_=nullptr;owner_=nullptr;source_=nullptr;sourcePid_=0;destination_={};state_=PreviewState::Hidden;
}
PreviewState WindowPreview::fallback(PreviewState state) noexcept{hide();state_=state;return state_;}
PreviewState WindowPreview::show(HWND destination,HWND source,DWORD expectedPid,RECT region,BYTE opacity) noexcept{
 DWORD ownerPid=0;
 if(!IsWindow(destination)||GetAncestor(destination,GA_ROOT)!=destination||
  !GetWindowThreadProcessId(destination,&ownerPid)||ownerPid!=GetCurrentProcessId())return fallback(PreviewState::Unavailable);
 if(!IsWindowVisible(destination))return fallback(PreviewState::Hidden);
 RECT client{},bounded{};
 if(!GetClientRect(destination,&client)||!IntersectRect(&bounded,&region,&client))return fallback(PreviewState::Unavailable);
 if(!windowAlive(source,expectedPid)||source==destination||GetAncestor(source,GA_ROOT)!=source)return fallback(PreviewState::InvalidSource);
 if(!IsWindowVisible(source))return fallback(PreviewState::HiddenSource);
 DWORD cloaked=0;
 if(SUCCEEDED(DwmGetWindowAttribute(source,DWMWA_CLOAKED,&cloaked,sizeof(cloaked)))&&cloaked)return fallback(PreviewState::HiddenSource);
 BOOL composition=FALSE;
 // DWM can retain a source's last composed image while it is minimized. Keep
 // that relationship through minimize/restore; never wake or capture the app.
 const auto unavailable=IsIconic(source)?PreviewState::MinimizedSource:PreviewState::Unavailable;
 if(FAILED(DwmIsCompositionEnabled(&composition))||!composition)return fallback(unavailable);
 if(thumbnail_&&(owner_!=destination||source_!=source||sourcePid_!=expectedPid))hide();
 const bool fresh=thumbnail_==nullptr;
 if(fresh){
  HTHUMBNAIL next{};
  if(!windowAlive(source,expectedPid))return fallback(PreviewState::InvalidSource);
  if(FAILED(DwmRegisterThumbnail(destination,source,&next))||!next)return fallback(unavailable);
  thumbnail_=next;owner_=destination;source_=source;sourcePid_=expectedPid;
 }
 SIZE sourceSize{};
 if(FAILED(DwmQueryThumbnailSourceSize(thumbnail_,&sourceSize))||sourceSize.cx<=0||sourceSize.cy<=0)return fallback(unavailable);
 const auto fitted=fitPreview(sourceSize,bounded);
 if(!windowAlive(source,expectedPid))return fallback(PreviewState::InvalidSource);
 if(fresh||!EqualRect(&fitted,&destination_)||opacity_!=opacity){
  DWM_THUMBNAIL_PROPERTIES properties{};
  properties.dwFlags=DWM_TNP_RECTDESTINATION|DWM_TNP_VISIBLE|DWM_TNP_OPACITY|DWM_TNP_SOURCECLIENTAREAONLY;
  properties.rcDestination=fitted;properties.fVisible=TRUE;properties.opacity=opacity;properties.fSourceClientAreaOnly=FALSE;
  if(FAILED(DwmUpdateThumbnailProperties(thumbnail_,&properties)))return fallback(unavailable);
 }
 if(!windowAlive(source,expectedPid))return fallback(PreviewState::InvalidSource);
 destination_=fitted;opacity_=opacity;state_=PreviewState::Ready;return state_;
}
}
