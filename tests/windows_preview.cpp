#include <iostream>
#include "platform/window_preview.hpp"
#include <functional>

using namespace aether;
namespace {
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void pump(){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}}
bool same(RECT a,RECT b){return a.left==b.left&&a.top==b.top&&a.right==b.right&&a.bottom==b.bottom;}
struct NativeWindow {
 HWND hwnd{};
 explicit NativeWindow(int width=640,int height=360,HWND parent=nullptr){
  hwnd=CreateWindowExW(WS_EX_TOOLWINDOW,L"STATIC",L"Aether owned preview fixture",
   parent?WS_CHILD|WS_VISIBLE:WS_POPUP|WS_VISIBLE,40,60,width,height,parent,nullptr,GetModuleHandleW(nullptr),nullptr);
  require(hwnd!=nullptr,"Could not create owned native preview fixture");
  UpdateWindow(hwnd);pump();DwmFlush();
 }
 ~NativeWindow(){if(hwnd)DestroyWindow(hwnd);}
 NativeWindow(const NativeWindow&)=delete;NativeWindow& operator=(const NativeWindow&)=delete;
 DWORD pid() const {DWORD result=0;GetWindowThreadProcessId(hwnd,&result);return result;}
};
void released(HTHUMBNAIL handle,const char* message){SIZE size{};require(FAILED(DwmQueryThumbnailSourceSize(handle,&size)),message);}
void aspectFitsLandscapeAndPortrait(){
 require(same(win::fitPreview({1600,900},{10,20,410,320}),{10,57,410,282}),"Landscape preview was stretched or not centered");
 require(same(win::fitPreview({900,1600},{10,20,410,320}),{126,20,294,320}),"Portrait preview was stretched or not centered");
 require(same(win::fitPreview({400,300},{10,20,410,320}),{10,20,410,320}),"Matching aspect preview did not use available region");
 require(same(win::fitPreview({0,900},{10,20,410,320}),{}),"Zero-size source produced a drawable destination");
 require(same(win::fitPreview({1600,900},{10,20,10,320}),{}),"Empty region produced a drawable destination");
}
void registersOnlySelectedOwnedSource(){
 NativeWindow destination(500,400),source;
 win::WindowPreview preview;
 require(preview.show(destination.hwnd,source.hwnd,source.pid(),{10,20,410,320})==win::PreviewState::Ready,"Visible selected window did not register a native DWM preview");
 require(preview.nativeHandle()!=nullptr,"Ready preview does not own a DWM registration");
 SIZE size{};require(SUCCEEDED(DwmQueryThumbnailSourceSize(preview.nativeHandle(),&size)),"Owned DWM handle is invalid");
 require(size.cx==640&&size.cy==360,"DWM thumbnail does not belong to selected source");
 auto handle=preview.nativeHandle();
 require(preview.show(destination.hwnd,source.hwnd,source.pid(),{20,30,420,330})==win::PreviewState::Ready,"Destination move made selected preview unavailable");
 require(preview.nativeHandle()==handle,"Unchanged source was needlessly registered again");
 require(same(preview.destination(),{20,67,420,292}),"Native thumbnail destination does not aspect-fit the region");
}
void hiddenPreviewReleasesRegistration(){
 NativeWindow destination(500,400),source;win::WindowPreview preview;
 require(preview.show(destination.hwnd,source.hwnd,source.pid(),{0,0,400,300})==win::PreviewState::Ready,"Hide fixture did not register");
 auto handle=preview.nativeHandle();preview.hide();
 require(preview.state()==win::PreviewState::Hidden&&preview.nativeHandle()==nullptr,"Hide kept ownership of a preview");
 released(handle,"Hide did not unregister the real DWM thumbnail");preview.hide();
}
void hiddenDestinationReleasesRegistration(){
 NativeWindow destination(500,400),source;win::WindowPreview preview;
 require(preview.show(destination.hwnd,source.hwnd,source.pid(),{0,0,400,300})==win::PreviewState::Ready,"Destination-hide fixture did not register");
 auto handle=preview.nativeHandle();ShowWindow(destination.hwnd,SW_HIDE);
 require(preview.show(destination.hwnd,source.hwnd,source.pid(),{0,0,400,300})==win::PreviewState::Hidden,"Hidden destination did not suspend the preview");
 require(preview.nativeHandle()==nullptr,"Hidden destination retained a registration");released(handle,"Destination hide did not unregister DWM thumbnail");
}
void wrongPidReleasesWithoutPreviewing(){
 NativeWindow destination(500,400),source;win::WindowPreview preview;
 require(preview.show(destination.hwnd,source.hwnd,source.pid(),{0,0,400,300})==win::PreviewState::Ready,"PID fixture did not register");
 auto handle=preview.nativeHandle();
 require(preview.show(destination.hwnd,source.hwnd,source.pid()+1,{0,0,400,300})==win::PreviewState::InvalidSource,"Mismatched HWND/PID was accepted as selected source");
 require(preview.nativeHandle()==nullptr,"Mismatched source kept previous registration");released(handle,"Mismatched PID did not unregister prior thumbnail");
 require(preview.show(destination.hwnd,source.hwnd,0,{0,0,400,300})==win::PreviewState::InvalidSource,"Zero PID was accepted");
}
void hiddenSourcesFallbackAndMinimizedSourcesPreview(){
 NativeWindow destination(500,400),source;win::WindowPreview preview;
 require(preview.show(destination.hwnd,source.hwnd,source.pid(),{0,0,400,300})==win::PreviewState::Ready,"Source-hide fixture did not register");
 auto handle=preview.nativeHandle();ShowWindow(source.hwnd,SW_HIDE);
 require(preview.show(destination.hwnd,source.hwnd,source.pid(),{0,0,400,300})==win::PreviewState::HiddenSource,"Hidden source has no explicit fallback");
 require(preview.nativeHandle()==nullptr,"Hidden source retained thumbnail ownership");released(handle,"Hidden source did not release DWM registration");
 ShowWindow(source.hwnd,SW_SHOWNOACTIVATE);ShowWindow(source.hwnd,SW_MINIMIZE);pump();
 require(IsIconic(source.hwnd)!=FALSE,"Owned source could not be minimized");
 require(preview.show(destination.hwnd,source.hwnd,source.pid(),{0,0,400,300})==win::PreviewState::Ready,"Already minimized source did not provide a native preview");
 require(preview.nativeHandle()!=nullptr&&IsIconic(source.hwnd),"Preview restored a minimized source");
}
void closedAndNonTopLevelSourcesAreRejected(){
 NativeWindow destination(500,400),source;win::WindowPreview preview;auto pid=source.pid();
 require(preview.show(destination.hwnd,source.hwnd,pid,{0,0,400,300})==win::PreviewState::Ready,"Closed-source fixture did not register");
 auto handle=preview.nativeHandle();auto closed=source.hwnd;DestroyWindow(source.hwnd);source.hwnd=nullptr;
 require(preview.show(destination.hwnd,closed,pid,{0,0,400,300})==win::PreviewState::InvalidSource,"Closed selected HWND was accepted");
 require(preview.nativeHandle()==nullptr,"Closed selected source kept a thumbnail");released(handle,"Closed source registration was retained");
 NativeWindow child(200,100,destination.hwnd);
 require(preview.show(destination.hwnd,child.hwnd,child.pid(),{0,0,400,300})==win::PreviewState::InvalidSource,"Child window was accepted as a top-level source");
 require(preview.show(destination.hwnd,destination.hwnd,destination.pid(),{0,0,400,300})==win::PreviewState::InvalidSource,"Activities could preview itself");
}
void sourceSelectionUsesNewWindow(){
 NativeWindow destination(500,400),landscape,portrait(180,320);win::WindowPreview preview;
 require(preview.show(destination.hwnd,landscape.hwnd,landscape.pid(),{10,20,410,320})==win::PreviewState::Ready,"First selected source did not register");
 require(preview.show(destination.hwnd,portrait.hwnd,portrait.pid(),{10,20,410,320})==win::PreviewState::Ready,"Changed selected source did not register");
 SIZE size{};require(SUCCEEDED(DwmQueryThumbnailSourceSize(preview.nativeHandle(),&size))&&size.cx==180&&size.cy==320,"Selection retained the previous source thumbnail");
 require(same(preview.destination(),{126,20,294,320}),"Changed selection retained the prior aspect ratio");
}
void destructionReleasesNativeOwnership(){
 NativeWindow destination(500,400),source;HTHUMBNAIL handle{};
 {win::WindowPreview preview;require(preview.show(destination.hwnd,source.hwnd,source.pid(),{0,0,400,300})==win::PreviewState::Ready,"Destructor fixture did not register");handle=preview.nativeHandle();}
 released(handle,"Preview destruction did not release DWM ownership");
}
void invalidDestinationAndEmptyRegionRelease(){
 NativeWindow destination(500,400),source,child(100,100,destination.hwnd);win::WindowPreview preview;
 require(preview.show(destination.hwnd,source.hwnd,source.pid(),{0,0,400,300})==win::PreviewState::Ready,"Invalid-region fixture did not register");
 auto handle=preview.nativeHandle();
 require(preview.show(destination.hwnd,source.hwnd,source.pid(),{0,0,0,300})==win::PreviewState::Unavailable,"Empty destination has no fallback");
 require(preview.nativeHandle()==nullptr,"Empty destination retained ownership");released(handle,"Empty region did not release prior thumbnail");
 require(preview.show(child.hwnd,source.hwnd,source.pid(),{0,0,100,100})==win::PreviewState::Unavailable,"Child destination was accepted");
 require(preview.nativeHandle()==nullptr,"Invalid destination created a thumbnail");
 auto shell=GetShellWindow();DWORD shellPid=0;if(shell)GetWindowThreadProcessId(shell,&shellPid);
 require(shell&&shellPid!=GetCurrentProcessId(),"No foreign shell destination available for ownership test");
 require(preview.show(shell,source.hwnd,source.pid(),{0,0,100,100})==win::PreviewState::Unavailable,"Foreign destination passed process ownership validation");
 require(preview.nativeHandle()==nullptr,"Foreign destination retained thumbnail ownership");
}
LRESULT CALLBACK paintedSource(HWND h,UINT m,WPARAM w,LPARAM l){
 if(m==WM_PAINT){PAINTSTRUCT paint{};auto dc=BeginPaint(h,&paint);RECT r{};GetClientRect(h,&r);auto left=r;left.right=r.right/2;auto right=r;right.left=left.right;
  auto red=CreateSolidBrush(RGB(235,45,20)),blue=CreateSolidBrush(RGB(20,80,235));FillRect(dc,&left,red);FillRect(dc,&right,blue);DeleteObject(red);DeleteObject(blue);EndPaint(h,&paint);return 0;}
 return DefWindowProcW(h,m,w,l);
}
void minimizedPreviewContainsSourcePixelsAndRetainsAnimationHandle(){
 WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=paintedSource;wc.lpszClassName=L"Aether.MinimizedPreview.PaintFixture";
 require(RegisterClassW(&wc)!=0||GetLastError()==ERROR_CLASS_ALREADY_EXISTS,"Painted source class unavailable");
 NativeWindow destination(500,400),source;
 DestroyWindow(source.hwnd);source.hwnd=CreateWindowExW(WS_EX_TOOLWINDOW,wc.lpszClassName,L"Owned red blue preview source",WS_POPUP|WS_VISIBLE,60,60,640,360,nullptr,nullptr,wc.hInstance,nullptr);
 require(source.hwnd!=nullptr,"Painted source unavailable");UpdateWindow(source.hwnd);pump();DwmFlush();
 SetWindowPos(destination.hwnd,HWND_TOPMOST,800,100,500,400,SWP_NOACTIVATE);UpdateWindow(destination.hwnd);pump();DwmFlush();
 win::WindowPreview preview;
 const RECT region{20,30,420,330};
 require(preview.show(destination.hwnd,source.hwnd,source.pid(),region)==win::PreviewState::Ready,"Painted source registration failed");
 const auto original=preview.nativeHandle();ShowWindow(source.hwnd,SW_SHOWMINNOACTIVE);pump();DwmFlush();
 WINDOWPLACEMENT before{sizeof(before)};require(GetWindowPlacement(source.hwnd,&before)&&IsIconic(source.hwnd),"Painted source not minimized");
 const auto foreground=GetForegroundWindow();
 for(BYTE opacity:{BYTE{0},BYTE{64},BYTE{128},BYTE{255}}){
  require(preview.show(destination.hwnd,source.hwnd,source.pid(),region,opacity)==win::PreviewState::Ready,"Minimized opacity transition lost preview");
  require(preview.nativeHandle()==original,"Minimized opacity transition re-registered its thumbnail");
 }
 auto pixelsMatch=[&]{DwmFlush();RECT r{};GetWindowRect(destination.hwnd,&r);auto fitted=preview.destination();auto dc=GetDC(nullptr);
  auto left=GetPixel(dc,r.left+fitted.left+(fitted.right-fitted.left)/4,r.top+(fitted.top+fitted.bottom)/2);
  auto right=GetPixel(dc,r.left+fitted.left+3*(fitted.right-fitted.left)/4,r.top+(fitted.top+fitted.bottom)/2);ReleaseDC(nullptr,dc);
  return left!=CLR_INVALID&&right!=CLR_INVALID&&GetRValue(left)>GetBValue(left)+100&&GetBValue(right)>GetRValue(right)+100;
 };
 auto deadline=GetTickCount64()+2000;while(!pixelsMatch()&&GetTickCount64()<deadline){pump();Sleep(20);}
 require(pixelsMatch(),"Minimized thumbnail does not contain the source's red/blue content");
 preview.hide();released(original,"Minimized preview hide retained native ownership");
 require(preview.show(destination.hwnd,source.hwnd,source.pid(),region)==win::PreviewState::Ready,"Opening a fresh preview of an already minimized source failed");
 require(pixelsMatch(),"Fresh minimized thumbnail lost retained source pixels");
 WINDOWPLACEMENT after{sizeof(after)};require(GetWindowPlacement(source.hwnd,&after)&&IsIconic(source.hwnd)&&same(before.rcNormalPosition,after.rcNormalPosition)&&before.showCmd==after.showCmd,"Preview changed source placement or restored it");
 require(GetForegroundWindow()==foreground,"Preview changed foreground activation");
 auto minimized=preview.nativeHandle();ShowWindow(source.hwnd,SW_SHOWNOACTIVATE);pump();
 require(preview.show(destination.hwnd,source.hwnd,source.pid(),region)==win::PreviewState::Ready&&preview.nativeHandle()==minimized,"Source restore churned its preview registration");
}
}
int main(){
 SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
 std::vector<std::pair<const char*,std::function<void()>>> tests={
  {"aspect fits landscape and portrait",aspectFitsLandscapeAndPortrait},
  {"registers only selected owned source",registersOnlySelectedOwnedSource},
  {"hidden preview releases registration",hiddenPreviewReleasesRegistration},
  {"hidden destination releases registration",hiddenDestinationReleasesRegistration},
  {"wrong PID releases without previewing",wrongPidReleasesWithoutPreviewing},
  {"hidden sources fallback and minimized sources preview",hiddenSourcesFallbackAndMinimizedSourcesPreview},
  {"closed and non-top-level sources are rejected",closedAndNonTopLevelSourcesAreRejected},
  {"source selection uses new window",sourceSelectionUsesNewWindow},
  {"destruction releases native ownership",destructionReleasesNativeOwnership},
  {"invalid destination and empty region release",invalidDestinationAndEmptyRegionRelease},
  {"minimized preview pixels and animation handle",minimizedPreviewContainsSourcePixelsAndRetainsAnimationHandle}};
 int failures=0;for(const auto& [name,test]:tests)try{test();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& error){++failures;std::cerr<<"FAIL "<<name<<": "<<error.what()<<'\n';}
 return failures?1:0;
}



