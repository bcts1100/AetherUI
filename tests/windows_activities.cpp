#include "app.hpp"
#include "ui/activities.hpp"
#include "platform/provider_codec.hpp"
#include <commctrl.h>
#include <oleacc.h>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>

using namespace aether;

namespace {
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void pump(){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}}

struct Fixture {
 std::filesystem::path directory;
 Fixture(){
  directory=std::filesystem::temp_directory_path()/
   (L"AetherActivities-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
  std::filesystem::create_directories(directory);win::setConfigDirectory(directory/L"profile");
 }
 ~Fixture(){win::setConfigDirectory({});std::error_code error;std::filesystem::remove_all(directory,error);}
 std::vector<Pin> pins(int count){
  std::vector<Pin> result;
  for(int i=0;i<count;++i){
   std::ostringstream title;title<<"Application "<<std::setfill('0')<<std::setw(2)<<i;
   auto target=directory/(L"AetherActivitiesTarget-"+std::to_wstring(i)+L".exe");
   std::filesystem::copy_file(win::executablePath(),target,std::filesystem::copy_options::overwrite_existing);
   result.push_back({title.str(),win::utf8(target.wstring()),title.str()});
  }
  return result;
 }

};

struct Launcher {
 App app;ui::Graphics graphics;ui::Activities activities;HWND edit{};
 Launcher(const std::vector<Pin>& pins,win::ProviderProcessOptions providers={}):app(std::move(providers)),activities(graphics,app){
  auto config=app.config();config.hideTaskbar=false;config.pins=pins;app.apply(config);
  auto displays=win::monitors();require(!displays.empty(),"No display available for Activities regression test");
  activities.open(displays.front());pump();edit=GetDlgItem(activities.hwnd(),10);
  require(edit!=nullptr,"Activities search EDIT is missing");
 }
 std::wstring selectedName(){
  win::ComPtr<IAccessible> client;require(SUCCEEDED(AccessibleObjectFromWindow(activities.hwnd(),static_cast<DWORD>(OBJID_CLIENT),IID_PPV_ARGS(&client))),"Activities native accessibility query failed");VARIANT selection{};require(SUCCEEDED(client->get_accSelection(&selection))&&selection.vt==VT_I4,"Activities has no selected result");BSTR text{};require(SUCCEEDED(client->get_accName(selection,&text))&&text,"Selected result name unavailable");std::wstring name=text;SysFreeString(text);return name;
 }
 void key(WPARAM key){SendMessageW(edit,WM_KEYDOWN,key,0);}
 void toggleSelectedPin(){
  BYTE previous[256]{},pressed[256]{};GetKeyboardState(previous);
  std::copy(std::begin(previous),std::end(previous),std::begin(pressed));
  pressed[VK_CONTROL]|=0x80;SetKeyboardState(pressed);key('P');SetKeyboardState(previous);
 }
 LPARAM at(float x,float y){float scale=win::dpiFor(activities.hwnd())/96.f;return MAKELPARAM(static_cast<int>(x*scale),static_cast<int>(y*scale));}
 void height(int dips){
  RECT bounds{};GetWindowRect(activities.hwnd(),&bounds);
  activities.move({bounds.left,bounds.top,bounds.right,bounds.top+MulDiv(dips,win::dpiFor(activities.hwnd()),96)});pump();
 }
};

void identityMatchedCatalogUnpinsOriginalShortcut(){
 Fixture fixture;
 Pin original{"Personal Tea",win::utf8((fixture.directory/L"custom-desktop-tea.lnk").wstring()),"aumid:owned-shared-tea"};
 Pin unrelated{"Keep this pin",win::utf8((fixture.directory/L"unrelated.lnk").wstring()),"aumid:owned-unrelated"};
 win::ProviderProcessOptions provider;provider.executable=win::executablePath();provider.mode=L"pin-identity-fixture";provider.catalogTimeoutMs=2000;
 Launcher launcher({original,unrelated},provider);
 // Exercise the real Catalog and inherited provider IPC, replacing only the
 // external installed-app enumeration with a complete owned response.
 launcher.app.catalog().start(launcher.activities.hwnd());
 auto deadline=GetTickCount64()+4000;
 while(launcher.app.catalog().snapshot()->empty()&&GetTickCount64()<deadline){pump();MsgWaitForMultipleObjects(0,nullptr,FALSE,10,QS_ALLINPUT);}
 auto catalog=launcher.app.catalog().snapshot();
 require(catalog->size()==1&&(*catalog)[0].target!=win::wide(original.target)&&(*catalog)[0].identity==win::wide(original.identity),"Owned catalog did not expose the alternate-target identity match");
 launcher.activities.update();UpdateWindow(launcher.activities.hwnd());pump();
 require(launcher.selectedName()==L"Personal Tea","Identity-matched catalog entry lost its custom pinned label");
 auto description=[&](){
  win::ComPtr<IAccessible> client;require(SUCCEEDED(AccessibleObjectFromWindow(launcher.activities.hwnd(),static_cast<DWORD>(OBJID_CLIENT),IID_PPV_ARGS(&client))),"Identity-matched row lacks native accessibility");
  VARIANT selected{};require(SUCCEEDED(client->get_accSelection(&selected))&&selected.vt==VT_I4,"Identity-matched row is not selected");
  BSTR value=nullptr;require(SUCCEEDED(client->get_accDescription(selected,&value))&&value,"Identity-matched row has no native pinned description");
  std::wstring result=value;SysFreeString(value);return result;
 };
 require(description()==L"Pinned application","Alternate-target row does not expose its actual pinned presentation");
 launcher.toggleSelectedPin();
 const auto& remaining=launcher.app.config().pins;
 require(remaining.size()==1&&remaining[0]==unrelated,"Unpinning an identity-matched catalog row did not remove the original shortcut, or added a duplicate pin");
 bool first=false;auto persisted=win::loadConfig(first);
 require(persisted.pins.size()==1&&persisted.pins[0]==unrelated,"Identity-matched Unpin did not persist removal of only the original shortcut");
 launcher.activities.update();UpdateWindow(launcher.activities.hwnd());pump();
 require(description()==L"Installed application","Unpinning left the catalog row presented as pinned");
 launcher.app.catalog().stop();
}
void selectionSurvivesRefresh(){
 Fixture fixture;auto pins=fixture.pins(3);Launcher launcher(pins);
 launcher.key(VK_DOWN);launcher.activities.update();launcher.toggleSelectedPin();
 const auto& remaining=launcher.app.config().pins;
 require(remaining.size()==2,"Ctrl+P did not remove a selected pin");
 require(remaining[0].target==pins[0].target&&remaining[1].target==pins[2].target,
  "Snapshot refresh reset keyboard selection to the first app");
}
void selectionTracksStableTarget(){
 Fixture fixture;auto pins=fixture.pins(4);Launcher launcher({pins[0],pins[1],pins[2]});
 launcher.key(VK_DOWN);pins[3].name="Application 00A";
 auto config=launcher.app.config();config.pins=pins;launcher.app.apply(config);
 launcher.activities.update();launcher.toggleSelectedPin();
 const auto& remaining=launcher.app.config().pins;
 require(remaining.size()==3&&remaining[0].target==pins[0].target&&remaining[1].target==pins[2].target&&remaining[2].target==pins[3].target,
  "Snapshot reorder changed the selected app instead of retaining its target");
}
void queryResetsSelection(){
 Fixture fixture;auto pins=fixture.pins(3);Launcher launcher(pins);
 launcher.key(VK_DOWN);SetWindowTextW(launcher.edit,L"Application");launcher.toggleSelectedPin();
 const auto& remaining=launcher.app.config().pins;
 require(remaining.size()==2&&remaining[0].target==pins[1].target&&remaining[1].target==pins[2].target,
  "Editing the search query did not reset selection to the first result");
}
void clickUsesCurrentCoordinates(){
 Fixture fixture;auto pins=fixture.pins(3);Launcher launcher(pins);
 SendMessageW(launcher.activities.hwnd(),WM_MOUSEMOVE,0,launcher.at(100,160));
 SendMessageW(launcher.activities.hwnd(),WM_LBUTTONUP,0,launcher.at(100,215));
 require(!launcher.activities.visible()&&launcher.selectedName()==win::wide(pins[1].name),"Click activated the stale hovered app instead of the clicked row");
}
void shortPopupFooterDoesNotLaunch(){
 Fixture fixture;auto pins=fixture.pins(1);Launcher launcher(pins);launcher.height(200);
 SendMessageW(launcher.activities.hwnd(),WM_LBUTTONUP,0,launcher.at(100,175));
 require(launcher.activities.visible(),"Clicking the short-popup footer activated a result");
}
void partialRowDoesNotLaunch(){
 Fixture fixture;auto pins=fixture.pins(1);Launcher launcher(pins);launcher.height(220);
 // The first 51-DIP result would cross the content/footer boundary, so no row is fully visible.
 SendMessageW(launcher.activities.hwnd(),WM_LBUTTONUP,0,launcher.at(100,160));
 require(launcher.activities.visible(),"Clicking an incomplete result row activated a result");
}
void explicitWindowsEntryResetsTabAndSearch(){
 Fixture fixture;Launcher launcher(fixture.pins(2));
 auto config=launcher.app.config();config.reduceMotion=true;launcher.app.apply(config);
 SetWindowTextW(launcher.edit,L"old search");
 launcher.activities.open(win::monitors().front(),true);pump();
 auto searchName=[&]{win::ComPtr<IAccessible> client;require(SUCCEEDED(AccessibleObjectFromWindow(launcher.activities.hwnd(),static_cast<DWORD>(OBJID_CLIENT),IID_PPV_ARGS(&client))),"Overview accessibility unavailable");VARIANT child{};child.vt=VT_I4;child.lVal=1;BSTR text{};require(SUCCEEDED(client->get_accName(child,&text))&&text,"Search mode unavailable");std::wstring result=text;SysFreeString(text);return result;};
 require(searchName()==L"Search open windows"&&win::windowText(launcher.edit).empty(),"Windows-key entry did not select a fresh open-window overview");
 launcher.key(VK_ESCAPE);require(!launcher.activities.visible(),"Escape did not close open-window overview");
 launcher.activities.open(win::monitors().front());pump();
 require(searchName()==L"Search applications","Normal launcher entry retained the Windows-key tab");
}
void fullscreenBoundsAndAnimationLifetime(){
 Fixture fixture;Launcher launcher(fixture.pins(2));auto config=launcher.app.config();config.reduceMotion=true;launcher.app.apply(config);
 for(const auto& monitor:win::monitors()){
  launcher.activities.open(monitor,true);pump();RECT r{};GetWindowRect(launcher.activities.hwnd(),&r);
  require(r.left==monitor.bounds.left&&r.top==monitor.bounds.top&&r.right==monitor.bounds.right&&r.bottom==monitor.bounds.bottom,"Fullscreen overview does not cover the exact selected physical monitor");
  require(!GetPropW(launcher.activities.hwnd(),L"Aether.Overview.Animating"),"Reduced motion starts an animation timer");
  launcher.activities.dismiss();require(!launcher.activities.visible(),"Reduced motion close is not immediate");
 }
 config.reduceMotion=false;launcher.app.apply(config);launcher.activities.open(win::monitors().front(),true);
 auto settle=[&](const std::function<bool()>& done){const auto until=GetTickCount64()+1000;do{pump();if(done())return true;Sleep(5);}while(GetTickCount64()<until);return false;};
 require(settle([&]{return !GetPropW(launcher.activities.hwnd(),L"Aether.Overview.Animating");}),"Opening animation never retires");
 auto frames=GetPropW(launcher.activities.hwnd(),L"Aether.Overview.Frames");const auto idleUntil=GetTickCount64()+100;while(GetTickCount64()<idleUntil){pump();Sleep(5);}
 require(GetPropW(launcher.activities.hwnd(),L"Aether.Overview.Frames")==frames,"Settled overview keeps animating at idle");
 launcher.activities.dismiss();launcher.activities.open(win::monitors().front(),true);
 require(settle([&]{return !GetPropW(launcher.activities.hwnd(),L"Aether.Overview.Animating");})&&launcher.activities.visible(),"Rapid close/open leaves the overview stuck or hidden");
 launcher.activities.dismiss();require(settle([&]{return !launcher.activities.visible();}),"Closing animation does not hide the fullscreen surface");
 require(!GetPropW(launcher.activities.hwnd(),L"Aether.Overview.Animating"),"Hidden overview retains animation timer state");
}
void zeroRowPageDownPreservesViewport(){
 Fixture fixture;auto pins=fixture.pins(20);Launcher launcher(pins);
 RECT original{};GetWindowRect(launcher.activities.hwnd(),&original);
 launcher.key(VK_NEXT);launcher.height(200);launcher.key(VK_NEXT);
 launcher.activities.move(win::rect(original));pump();
 SendMessageW(launcher.activities.hwnd(),WM_LBUTTONUP,0,launcher.at(100,160));
 require(!launcher.activities.visible()&&launcher.selectedName()==win::wide(pins[1].name),"Page Down with no visible rows changed the stored viewport");
}
void viewportSurvivesRefresh(){
 Fixture fixture;auto pins=fixture.pins(20);Launcher launcher(pins);
 launcher.key(VK_NEXT);launcher.activities.update();pump();
 // Page Down selects one row beyond the initial viewport; the top visible row is app 1.
 SendMessageW(launcher.activities.hwnd(),WM_MOUSEMOVE,0,launcher.at(100,160));
 SendMessageW(launcher.activities.hwnd(),WM_LBUTTONUP,0,launcher.at(100,160));
 require(!launcher.activities.visible()&&launcher.selectedName()==win::wide(pins[1].name),"Snapshot refresh reset the scrolled Activities viewport");
}
}

int wmain(int argc,wchar_t** argv){
 if(argc>1&&std::wstring_view(argv[1])==L"--provider-channel")
  return win::runProviderHost(argc,argv,[](win::ProviderOperation operation,const win::ProviderBytes& payload,std::wstring_view mode)->win::ProviderWireReply{
   if(operation==win::ProviderOperation::Quit)return {};
   if(operation==win::ProviderOperation::Catalog&&payload.empty()&&mode==L"pin-identity-fixture")
    return {0,win::encodeApplications({{L"Installed Tea",L"C:\\AetherOwnedStartMenu\\alternate-tea.lnk",L"aumid:owned-shared-tea"}})};
   return {1,{}};
  });
 SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
 win::ComApartment apartment;
 INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES|ICC_WIN95_CLASSES};InitCommonControlsEx(&controls);
 std::vector<std::pair<const char*,std::function<void()>>> tests={
  {"explicit Windows entry resets tab and search",explicitWindowsEntryResetsTabAndSearch},
  {"fullscreen bounds and animation lifetime",fullscreenBoundsAndAnimationLifetime},
  {"identity-matched catalog Unpin removes original shortcut",identityMatchedCatalogUnpinsOriginalShortcut},
  {"selection survives refresh",selectionSurvivesRefresh},
  {"selection tracks stable target",selectionTracksStableTarget},
  {"query resets selection",queryResetsSelection},
  {"click uses current coordinates",clickUsesCurrentCoordinates},
  {"viewport survives refresh",viewportSurvivesRefresh},
  {"short-popup footer does not launch",shortPopupFooterDoesNotLaunch},
  {"partial row does not launch",partialRowDoesNotLaunch},
  {"zero-row Page Down preserves viewport",zeroRowPageDownPreservesViewport}};
 int failures=0;
 for(const auto& [name,test]:tests)try{test();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& error){++failures;std::cerr<<"FAIL "<<name<<": "<<error.what()<<'\n';}
 return failures?1:0;
}




