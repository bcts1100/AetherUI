#include "ui/dock_flyout.hpp"
#include "ui/dock_flyout_geometry.hpp"
#include <functional>
#include <iostream>
#include <initializer_list>

using namespace aether;
namespace {
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void pump(){MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}}
struct NativeWindow {
 HWND hwnd{};
 explicit NativeWindow(const wchar_t* name=L"Owned dock window",int width=640,int height=360){
  hwnd=CreateWindowExW(WS_EX_TOOLWINDOW,L"STATIC",name,WS_POPUP|WS_VISIBLE,40,60,width,height,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
  require(hwnd!=nullptr,"Could not create owned source window");UpdateWindow(hwnd);pump();DwmFlush();
 }
 ~NativeWindow(){if(hwnd)DestroyWindow(hwnd);}
 win::Window record() const {DWORD pid=0;GetWindowThreadProcessId(hwnd,&pid);return {hwnd,pid,win::windowText(hwnd),win::executablePath().wstring(),L"fixture",L"fixture-display"};}
};
void minimizeOwned(std::initializer_list<HWND> windows){
 for(auto window:windows)ShowWindow(window,SW_MINIMIZE);pump();
 for(auto window:windows)require(IsIconic(window)!=FALSE,"Owned activation fixture did not minimize");
}
void requireRestoredOnly(HWND expected,std::initializer_list<HWND> others,const char* message){
 // Synthetic test input may be denied foreground ownership. Restoring exactly the
 // selected HWND proves activation without depending on global desktop focus.
 const auto deadline=GetTickCount64()+1000;
 do{
  pump();bool selected=IsWindow(expected)&&IsWindowVisible(expected)&&!IsIconic(expected);
  bool untouched=std::all_of(others.begin(),others.end(),[](HWND window){return IsWindow(window)&&IsIconic(window);});
  if(selected&&untouched)return;Sleep(10);
 }while(GetTickCount64()<deadline);
 std::cerr<<"activation diagnostic expected="<<reinterpret_cast<std::uintptr_t>(expected)<<" expectedMinimized="<<IsIconic(expected)<<" focusId="<<(GetFocus()?GetDlgCtrlID(GetFocus()):-1);
 for(auto window:others)std::cerr<<" other="<<reinterpret_cast<std::uintptr_t>(window)<<" minimized="<<IsIconic(window);std::cerr<<'\n';
 require(false,message);
}
struct Popup {
 ui::Graphics graphics;win::IconCache icons;ui::DockFlyout flyout{graphics,icons};win::Monitor monitor;
 Popup(std::function<void()> onClosed={}):flyout(graphics,icons,std::move(onClosed)){auto monitors=win::monitors();require(!monitors.empty(),"No monitor available");monitor=monitors.front();}
 void open(const std::vector<win::Window>& windows,bool previews=true,bool focus=false){
  auto area=monitor.work;Rect anchor{area.left+100,area.bottom-70,area.left+154,area.bottom-16};
  flyout.open(monitor,{"Owned app",win::utf8(win::executablePath().wstring()),"fixture"},anchor,Edge::Bottom,windows,Theme{},previews,focus);pump();DwmFlush();
 }
 void key(WPARAM key){SendMessageW(GetFocus()?GetFocus():flyout.hwnd(),WM_KEYDOWN,key,0);pump();}
 HWND card(int slot){return GetDlgItem(flyout.hwnd(),100+slot);}
};
void geometryStaysInsidePhysicalWorkAreas(){
 for(auto edge:{Edge::Bottom,Edge::Left,Edge::Right})for(int dpi:{96,144,192}){
  Rect work{-1920,-400,0,680};Rect anchor=edge==Edge::Bottom?Rect{-1000,600,-940,660}:edge==Edge::Left?Rect{-1910,-100,-1850,-40}:Rect{-70,-100,-10,-40};
  auto layout=ui::dockFlyoutGeometry(work,anchor,edge,dpi,40);
  require(layout.bounds.left>=work.left&&layout.bounds.top>=work.top&&layout.bounds.right<=work.right&&layout.bounds.bottom<=work.bottom,"Flyout escaped negative-origin physical work area");
  require(layout.capacity>0&&layout.capacity<=6,"Flyout has an unbounded thumbnail page");
  if(edge==Edge::Bottom)require(layout.bounds.bottom<=anchor.top,"Bottom flyout did not open above the dock");
  if(edge==Edge::Left)require(layout.bounds.left>=anchor.right,"Left flyout did not open right of the dock");
  if(edge==Edge::Right)require(layout.bounds.right<=anchor.left,"Right flyout did not open left of the dock");
 }
 auto tiny=ui::dockFlyoutGeometry({-100,0,140,260},{-40,215,10,255},Edge::Bottom,192,8);
 require(tiny.bounds.left>=-100&&tiny.bounds.right<=140&&tiny.bounds.top>=0&&tiny.bounds.bottom<=260,"Constrained high-DPI flyout escaped work bounds");
 require(tiny.capacity>=1&&tiny.capacity<=6,"Constrained flyout lost its usable window list");
 auto scaled=ui::dockFlyoutGeometry({0,0,1920,1080},{800,1000,850,1050},Edge::Bottom,144,1);
 auto normal=ui::dockFlyoutGeometry({0,0,1920,1080},{800,1000,850,1050},Edge::Bottom,96,1);
 require(scaled.bounds.width()>normal.bounds.width(),"Flyout physical size ignored target monitor DPI");
}
void emptyAndInvalidGroupsDoNotOpen(){
 Popup popup;popup.open({});require(!popup.flyout.visible(),"Empty pinned app opened a preview flyout");
 NativeWindow source;auto invalid=source.record();++invalid.pid;popup.open({invalid});
 require(!popup.flyout.visible()&&popup.flyout.registeredPreviewCount()==0,"Wrong PID source was admitted to dock group");
}
void opensBoundedPageAndAccessibleTitles(){
 Popup popup;std::vector<std::unique_ptr<NativeWindow>> sources;std::vector<win::Window> windows;
 for(int i=0;i<9;++i){sources.push_back(std::make_unique<NativeWindow>((L"Dock title "+std::to_wstring(i)).c_str()));windows.push_back(sources.back()->record());}
 popup.open(windows);
 require(popup.flyout.visible(),"Live group did not open its native flyout");
 require(popup.flyout.registeredPreviewCount()>0&&popup.flyout.registeredPreviewCount()<=6,"Live page registrations are missing or unbounded");
 require(popup.card(0)&&win::windowText(popup.card(0)).find(L"Dock title 0")!=std::wstring::npos,"Native accessible title button is missing");
 require(GetDlgItem(popup.flyout.hwnd(),2)&&IsWindowEnabled(GetDlgItem(popup.flyout.hwnd(),2)),"Overflow has no next-page action");
 SendMessageW(GetDlgItem(popup.flyout.hwnd(),2),BM_CLICK,0,0);pump();
 require(popup.flyout.registeredPreviewCount()<=6&&win::windowText(popup.card(0)).find(L"Dock title 0")==std::wstring::npos,"Paging retained first-page windows or leaked registrations");
}
void configOffKeepsOnlyNativeWindowList(){
 NativeWindow source;Popup popup;popup.open({source.record()},false,true);
 require(popup.flyout.visible()&&popup.card(0),"Disabled previews removed the keyboard window list");
 require(popup.flyout.registeredPreviewCount()==0,"Disabled config still registered a DWM thumbnail");
 popup.flyout.update({source.record()},Theme{},true);require(popup.flyout.registeredPreviewCount()==1,"Enabling previews did not register visible current group");
 popup.flyout.update({source.record()},Theme{},false);require(popup.flyout.registeredPreviewCount()==0,"Turning previews off retained native thumbnail ownership");
}
void switchesClickedWindowAndReleases(){
 NativeWindow first(L"First switch target"),second(L"Second switch target");minimizeOwned({first.hwnd,second.hwnd});Popup popup;popup.open({first.record(),second.record()},true,true);
 require(IsIconic(first.hwnd)&&IsIconic(second.hwnd)&&popup.flyout.registeredPreviewCount()==2,"Click fixture did not retain both minimized targets and native previews");
 SendMessageW(popup.card(1),BM_CLICK,0,0);pump();
 require(!popup.flyout.visible()&&popup.flyout.registeredPreviewCount()==0,"Switch kept the flyout or thumbnail ownership");
 requireRestoredOnly(second.hwnd,{first.hwnd},"Click did not restore only the requested group HWND");
}
void focusedActionButtonsHonorEnter(){
 Popup popup;std::vector<std::unique_ptr<NativeWindow>> sources;std::vector<win::Window> windows;
 for(int i=0;i<9;++i){sources.push_back(std::make_unique<NativeWindow>((L"Action window "+std::to_wstring(i)).c_str()));windows.push_back(sources.back()->record());}
 popup.open(windows,true,true);auto next=GetDlgItem(popup.flyout.hwnd(),2);require(IsWindowEnabled(next)!=FALSE,"Paging fixture has no enabled next action");
 SetFocus(next);popup.key(VK_RETURN);
 require(popup.flyout.visible()&&win::windowText(popup.card(0)).find(L"Action window 0")==std::wstring::npos,"Enter on Next activated a window instead of paging");
 SetFocus(GetDlgItem(popup.flyout.hwnd(),1));popup.key(VK_RETURN);
 require(popup.flyout.visible()&&win::windowText(popup.card(0)).find(L"Action window 0")!=std::wstring::npos,"Enter on Previous activated a window instead of paging");
 ShowWindow(sources[0]->hwnd,SW_MINIMIZE);pump();popup.flyout.update(windows,Theme{},true);SetFocus(GetDlgItem(popup.flyout.hwnd(),3));popup.key(VK_RETURN);
 require(!popup.flyout.visible()&&popup.flyout.registeredPreviewCount()==0,"Enter on Close did not dismiss and release");
 require(IsIconic(sources[0]->hwnd)!=FALSE,"Enter on Close restored the selected source window");
}
void titleRefreshRetainsFocusedActions(){
 Popup popup;std::vector<std::unique_ptr<NativeWindow>> sources;std::vector<win::Window> windows;
 for(int i=0;i<9;++i){sources.push_back(std::make_unique<NativeWindow>((L"Focus window "+std::to_wstring(i)).c_str()));ShowWindow(sources.back()->hwnd,SW_MINIMIZE);windows.push_back(sources.back()->record());}pump();
 auto allMinimized=[&](){return std::all_of(sources.begin(),sources.end(),[](const auto& source){return IsIconic(source->hwnd)!=FALSE;});};
 require(allMinimized(),"Action focus fixture did not minimize every owned source");popup.open(windows,true,true);
 auto refreshFocused=[&](int id,const char* message){
  auto control=GetDlgItem(popup.flyout.hwnd(),id);require(IsWindowVisible(control)&&IsWindowEnabled(control),"Refresh fixture action is unavailable");SetFocus(control);
  windows[0].title+=L" updated";popup.flyout.update(windows,Theme{},true);pump();
  if(GetFocus()!=control)std::cerr<<"action focus diagnostic expectedId="<<id<<" actualId="<<(GetFocus()?GetDlgCtrlID(GetFocus()):-1)<<" foregroundOwn="<<(GetForegroundWindow()==popup.flyout.hwnd())<<'\n';
  require(GetFocus()==control,message);popup.key(VK_RETURN);
 };
 refreshFocused(2,"Title refresh stole focus from Next windows");
 require(popup.flyout.visible()&&allMinimized()&&win::windowText(popup.card(0)).find(L"Focus window 0")==std::wstring::npos,"Refreshed Next Enter did not page without activating a source");
 refreshFocused(1,"Title refresh stole focus from Previous windows");
 require(popup.flyout.visible()&&allMinimized()&&win::windowText(popup.card(0)).find(L"Focus window 0")!=std::wstring::npos,"Refreshed Previous Enter did not return to the first page");
 refreshFocused(3,"Title refresh stole focus from Close window list");
 require(!popup.flyout.visible()&&allMinimized()&&popup.flyout.registeredPreviewCount()==0,"Refreshed Close Enter activated a source or retained popup ownership");
}
void tabReachesNativeFooter(){
 NativeWindow source;Popup popup;popup.open({source.record()},false,true);auto card=popup.card(0),close=GetDlgItem(popup.flyout.hwnd(),3);SetFocus(card);popup.key(VK_TAB);require(GetFocus()==close,"Tab did not reach native Close button");
 BYTE previous[256]{},pressed[256]{};GetKeyboardState(previous);std::copy(std::begin(previous),std::end(previous),std::begin(pressed));pressed[VK_SHIFT]|=0x80;SetKeyboardState(pressed);popup.key(VK_TAB);SetKeyboardState(previous);require(GetFocus()==card,"Shift+Tab did not return from footer to card");
}
void spaceRetainsNativeButtonAction(){
 NativeWindow source;Popup popup;popup.open({source.record()},true,true);auto close=GetDlgItem(popup.flyout.hwnd(),3);SetFocus(close);
 SendMessageW(close,WM_KEYDOWN,VK_SPACE,0);SendMessageW(close,WM_KEYUP,VK_SPACE,0);pump();
 require(!popup.flyout.visible()&&popup.flyout.registeredPreviewCount()==0,"Space no longer invokes the focused native Close button");
}
void enterUsesRetainedHwndSelection(){
 NativeWindow first(L"First selection"),second(L"Retained selection"),third(L"Third selection");minimizeOwned({first.hwnd,second.hwnd,third.hwnd});Popup popup;popup.open({first.record(),second.record(),third.record()},true,true);
 require(IsIconic(first.hwnd)&&IsIconic(second.hwnd)&&IsIconic(third.hwnd)&&popup.flyout.registeredPreviewCount()==3,"Enter fixture did not retain minimized targets and native previews");
 popup.key(VK_RIGHT);auto beforeFocus=GetFocus()?GetDlgCtrlID(GetFocus()):-1;
 popup.flyout.update({third.record(),first.record(),second.record()},Theme{},true);pump();auto afterFocus=GetFocus()?GetDlgCtrlID(GetFocus()):-1;
 bool selectedCardFocused=GetFocus()==popup.card(2);
 if(!selectedCardFocused)std::cerr<<"selection focus diagnostic beforeId="<<beforeFocus<<" afterId="<<afterFocus<<" expectedId=102 foregroundOwn="<<(GetForegroundWindow()==popup.flyout.hwnd())<<'\n';
 popup.key(VK_RETURN);
 requireRestoredOnly(second.hwnd,{first.hwnd,third.hwnd},"Refresh and Enter did not restore only the retained selected HWND");
 require(selectedCardFocused,"Refresh retained the HWND but not its native focused card");
 require(!popup.flyout.visible()&&popup.flyout.registeredPreviewCount()==0,"Enter activation did not close and release");
}
void closeRaceFallsBackAndEmptyGroupCloses(){
 NativeWindow first(L"Closed source"),second(L"Surviving source");Popup popup;auto gone=first.record();popup.open({gone,second.record()},true,true);
 DestroyWindow(first.hwnd);first.hwnd=nullptr;popup.flyout.update({gone,second.record()},Theme{},true);pump();
 require(popup.flyout.visible()&&popup.flyout.registeredPreviewCount()==1,"Closing selected source did not retain the remaining current group");
 require(win::windowText(popup.card(0)).find(L"Surviving source")!=std::wstring::npos,"Closed HWND remained in accessible window actions");
 DestroyWindow(second.hwnd);second.hwnd=nullptr;popup.flyout.update({},Theme{},true);
 require(!popup.flyout.visible()&&popup.flyout.registeredPreviewCount()==0,"Empty group retained a popup or registrations");
}
void minimizedSourceHasPreviewAndReadableState(){
 NativeWindow source(L"Minimized target");ShowWindow(source.hwnd,SW_MINIMIZE);pump();Popup popup;popup.open({source.record()});
 require(popup.flyout.visible()&&popup.flyout.registeredPreviewCount()==1,"Minimized source lost its native preview");
 require(IsIconic(source.hwnd),"Dock preview restored a minimized source");
 require(win::windowText(popup.card(0)).find(L"Minimized")!=std::wstring::npos,"Minimized window has no readable accessible state");
}
void visibleHideNotifiesOnce(){
 NativeWindow source;int closes=0;Popup popup([&]{++closes;});popup.open({source.record()});
 popup.flyout.close();popup.flyout.close();require(closes==1,"Close callback did not match one visible-to-hidden transition");
 popup.open({source.record()});ShowWindow(popup.flyout.hwnd(),SW_HIDE);pump();
 require(closes==2,"External hide did not notify dock auto-hide owner");
 popup.open({source.record()},true,true);popup.key(VK_ESCAPE);
 require(closes==3,"Escape did not notify dock auto-hide owner");
}
void escapeHotkeyOwnershipAndFocusedFallback(){
 NativeWindow source;Popup popup;popup.open({source.record()});
 bool acquired=RegisterHotKey(source.hwnd,99,MOD_NOREPEAT,VK_ESCAPE)!=FALSE;
 if(acquired)UnregisterHotKey(source.hwnd,99);
 require(!acquired,"Visible hover flyout did not reserve temporary Escape dismissal");
 popup.flyout.close();require(RegisterHotKey(source.hwnd,99,MOD_NOREPEAT,VK_ESCAPE)!=FALSE,"Closed flyout retained global Escape ownership");
 struct EscapeOwner {HWND hwnd;~EscapeOwner(){UnregisterHotKey(hwnd,99);}} owner{source.hwnd};
 popup.open({source.record()},true,true);popup.key(VK_ESCAPE);
 require(!popup.flyout.visible()&&popup.flyout.registeredPreviewCount()==0,"Unavailable global Escape broke focused keyboard dismissal");
}
void escapeHideAndGroupChangeCleanUp(){
 NativeWindow first(L"Old group"),second(L"Current group");Popup popup;popup.open({first.record()},true,true);popup.key(VK_ESCAPE);
 require(!popup.flyout.visible()&&popup.flyout.registeredPreviewCount()==0,"Escape retained native registrations");
 popup.open({first.record()});ShowWindow(popup.flyout.hwnd(),SW_HIDE);pump();
 require(popup.flyout.registeredPreviewCount()==0,"External hide retained live thumbnails");
 popup.open({first.record()});popup.open({second.record()});
 require(popup.flyout.registeredPreviewCount()==1&&win::windowText(popup.card(0)).find(L"Current group")!=std::wstring::npos,"Group change retained old group sources");
 SendMessageW(popup.flyout.hwnd(),WM_CLOSE,0,0);require(!popup.flyout.visible()&&popup.flyout.registeredPreviewCount()==0,"Close did not release the flyout");
}
}
int main(){
 SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);win::ComApartment apartment;
 std::vector<std::pair<const char*,std::function<void()>>> tests={
  {"geometry stays inside physical work areas",geometryStaysInsidePhysicalWorkAreas},
  {"empty and invalid groups do not open",emptyAndInvalidGroupsDoNotOpen},
  {"bounded page and accessible titles",opensBoundedPageAndAccessibleTitles},
  {"config off keeps native window list",configOffKeepsOnlyNativeWindowList},
  {"click switches requested window",switchesClickedWindowAndReleases},
  {"Enter retains selected HWND",enterUsesRetainedHwndSelection},
  {"focused action buttons honor Enter",focusedActionButtonsHonorEnter},
  {"title refresh retains focused actions",titleRefreshRetainsFocusedActions},
  {"Space retains native button action",spaceRetainsNativeButtonAction},
  {"Tab reaches native footer",tabReachesNativeFooter},
  {"window close race and empty group",closeRaceFallsBackAndEmptyGroupCloses},
  {"minimized source preview and readable state",minimizedSourceHasPreviewAndReadableState},
  {"Escape hide and group replacement",escapeHideAndGroupChangeCleanUp},
  {"visible hide notifies once",visibleHideNotifiesOnce},
  {"Escape ownership and focused fallback",escapeHotkeyOwnershipAndFocusedFallback}};
 int failures=0;for(const auto& [name,test]:tests)try{test();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& e){++failures;std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';}
 return failures?1:0;
}


