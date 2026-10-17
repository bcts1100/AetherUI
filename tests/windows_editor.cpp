#include "app.hpp"
#include "ui/editor.hpp"
#include <commctrl.h>
#include <iostream>
#include <winioctl.h>
#include <cstring>
namespace {
std::wstring requestedPresetName;bool acceptPresetName=true;UINT_PTR presetDialogTimer=0;
void CALLBACK presetNameTimer(HWND,UINT,UINT_PTR timer,DWORD){
 EnumThreadWindows(GetCurrentThreadId(),[](HWND window,LPARAM)->BOOL{
  if(!IsWindowVisible(window)||!GetDlgItem(window,101))return TRUE;
  SetDlgItemTextW(window,101,requestedPresetName.c_str());
  PostMessageW(window,WM_COMMAND,acceptPresetName?IDOK:IDCANCEL,0);
  return FALSE;
 },0);
 // Leave the timer active until the modal is found; the caller always cancels it.
 (void)timer;
}
bool directoryJunction(const std::filesystem::path& link,const std::filesystem::path& target){
 struct MountPointHeader {DWORD tag;WORD length,reserved,substituteOffset,substituteLength,printOffset,printLength;};
 static_assert(sizeof(MountPointHeader)==16);
 std::wstring substitute=L"\\??\\"+target.wstring();
 auto print=target.wstring();
 if(substitute.size()+print.size()+2>16000)return false;
 MountPointHeader header{};header.tag=IO_REPARSE_TAG_MOUNT_POINT;
 header.substituteLength=static_cast<WORD>(substitute.size()*sizeof(wchar_t));
 header.printOffset=static_cast<WORD>(header.substituteLength+sizeof(wchar_t));
 header.printLength=static_cast<WORD>(print.size()*sizeof(wchar_t));
 std::wstring paths=substitute;paths.push_back(L'\0');paths+=print;paths.push_back(L'\0');
 header.length=static_cast<WORD>(8+paths.size()*sizeof(wchar_t));
 std::vector<BYTE> buffer(sizeof(header)+paths.size()*sizeof(wchar_t));
 std::memcpy(buffer.data(),&header,sizeof(header));std::memcpy(buffer.data()+sizeof(header),paths.data(),paths.size()*sizeof(wchar_t));
 if(!CreateDirectoryW(link.c_str(),nullptr))return false;
 aether::win::Handle directory(CreateFileW(link.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr));
 DWORD written=0;bool created=directory&&DeviceIoControl(directory.value,FSCTL_SET_REPARSE_POINT,buffer.data(),static_cast<DWORD>(buffer.size()),nullptr,0,&written,nullptr);
 directory.reset();if(!created)RemoveDirectoryW(link.c_str());return created;
}
}
int main(){
 using namespace aether;
 SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
 win::ComApartment apartment;
 auto directory=win::executablePath().parent_path()/L"test-profiles"/(L"editor-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
 win::setConfigDirectory(directory);Config fixtureSettings;fixtureSettings.hideTaskbar=false;win::saveConfig(fixtureSettings);
 INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES|ICC_WIN95_CLASSES};InitCommonControlsEx(&controls);
 App app;ui::Graphics graphics;ui::Editor editor(graphics,app);
 auto displays=win::monitors();if(displays.empty())return 1;
 int failures=0,checks=0;
 auto check=[&](bool ok,const char* detail){++checks;if(!ok){++failures;std::cerr<<detail<<"\n";}};
 auto paint=[&]{editor.update();UpdateWindow(editor.hwnd());};
 editor.open(displays.front());UpdateWindow(editor.hwnd());
 for(int i=0;i<13;++i)SendMessageW(editor.hwnd(),WM_KEYDOWN,VK_TAB,0);
 SendMessageW(editor.hwnd(),WM_KEYDOWN,VK_SPACE,0);
 check(app.config().theme==preset(1),"Keyboard cannot select the Midnight theme.");
 bool first=false;auto saved=win::loadConfig(first);
 check(saved.theme==preset(1)&&!saved.hideTaskbar,"Keyboard theme selection was not safely persisted.");
 SendMessageW(editor.hwnd(),WM_KEYDOWN,VK_ESCAPE,0);
 check(!editor.visible(),"Escape did not hide Desktop Studio.");
 editor.open(displays.front());
 int dpi=win::dpiFor(editor.hwnd());RECT client{};float factor=1;
 auto resize=[&](int width,int height){SetWindowPos(editor.hwnd(),nullptr,0,0,MulDiv(width,dpi,96),MulDiv(height,dpi,96),SWP_NOMOVE|SWP_NOZORDER);GetClientRect(editor.hwnd(),&client);factor=std::min(1.f,client.right/(dpi/96.f)/730.f)*(dpi/96.f);paint();};
 auto at=[&](float x,float y){return MAKELPARAM(static_cast<int>(x*factor),static_cast<int>(y*factor));};
 auto click=[&](float x,float y){SendMessageW(editor.hwnd(),WM_LBUTTONUP,0,at(x,y));paint();};
 auto namedClick=[&](const wchar_t* name,bool accept,float x,float y){
  requestedPresetName=name;acceptPresetName=accept;
  presetDialogTimer=SetTimer(nullptr,0,20,presetNameTimer);
  click(x,y);KillTimer(nullptr,presetDialogTimer);presetDialogTimer=0;
 };
 resize(620,700);
 click(80,164);click(632,370);
 check(app.config().defaults.edge==Edge::Right,"Compact Desktop Studio cannot reach the right-edge setting.");
 editor.open(displays.front());resize(730,460);
 click(80,125);
 for(int i=0;i<12;++i)SendMessageW(editor.hwnd(),WM_KEYDOWN,VK_TAB,0);
 for(int i=0;i<3;++i)SendMessageW(editor.hwnd(),WM_KEYDOWN,VK_NEXT,0);
 SendMessageW(editor.hwnd(),WM_KEYDOWN,VK_RETURN,0);
 check(app.config().theme==preset(1),"Enter applied a focused control hidden by scrolling.");

 // A control must change the selected profile and survive a real disk reload.
 resize(730,900);click(80,164);
 click(548,465);
 check(app.config().defaults.dockGap==12,"Dock gap control did not change the default profile.");
 saved=win::loadConfig(first);
 check(saved.defaults.dockGap==12,"Dock gap changed in memory but was not persisted.");
 for(int i=0;i<20;++i)click(548,465);
 check(app.config().defaults.dockGap==32,"Dock gap increase exceeded or missed the upper bound.");
 for(int i=0;i<20;++i)click(410,465);
 check(app.config().defaults.dockGap==0,"Dock gap decrease exceeded or missed the lower bound.");
 click(80,203);click(480,385);
 check(!app.config().windowPreviews,"Window previews preference did not toggle off.");
 saved=win::loadConfig(first);
 check(!saved.windowPreviews&&!saved.hideTaskbar,"Preview preference was not persisted with taskbar hiding disabled.");

 // Copy uses the selected display, touches only connected profiles and leaves defaults intact.
 auto source=win::utf8(displays.front().name);auto config=app.config();
 config.defaults=DisplayOptions{};DisplayOptions profile;
 profile.edge=Edge::Left;profile.iconSize=64;profile.dockGap=22;profile.topBar=false;
 profile.autoHide=true;profile.monitorOnly=false;profile.reserveDock=false;
 config.displays[source]=profile;
 DisplayOptions disconnected;disconnected.edge=Edge::Right;disconnected.dockGap=30;
 config.displays["disconnected-test-display"]=disconnected;app.apply(config);
 click(80,164);
 Rect bounds=displays.front().bounds;
 for(const auto& display:displays){bounds.left=std::min(bounds.left,display.bounds.left);bounds.top=std::min(bounds.top,display.bounds.top);bounds.right=std::max(bounds.right,display.bounds.right);bounds.bottom=std::max(bounds.bottom,display.bounds.bottom);}
 float mapWidth=client.right/(dpi/96.f)/std::min(1.f,client.right/(dpi/96.f)/730.f)-278;
 float mapScale=std::min(mapWidth/std::max(1,bounds.width()),84.f/std::max(1,bounds.height()));
 float origin=234+(mapWidth-bounds.width()*mapScale)/2;
 const auto& firstDisplay=displays.front();
 click(origin+(firstDisplay.bounds.left-bounds.left+firstDisplay.bounds.width()/2.f)*mapScale,
  147+(firstDisplay.bounds.top-bounds.top+firstDisplay.bounds.height()/2.f)*mapScale);
 click(315,323);
 bool copied=true;for(const auto& display:displays){auto name=win::utf8(display.name);copied=copied&&app.config().displays.contains(name)&&app.config().forDisplay(name)==profile;}
 check(copied,"Copy display profile did not apply the selected layout to all connected displays.");
 check(app.config().defaults==DisplayOptions{}&&app.config().forDisplay("disconnected-test-display")==disconnected,"Copy display profile changed defaults or a disconnected override.");
 saved=win::loadConfig(first);bool copyPersisted=true;
 for(const auto& display:displays)copyPersisted=copyPersisted&&saved.forDisplay(win::utf8(display.name))==profile;
 check(copyPersisted,"Copied display profiles did not survive reload.");
 click(570,276);
 check(!app.config().displays.contains(source)&&app.config().forDisplay(source)==DisplayOptions{},"Reset override did not restore default inheritance.");
 saved=win::loadConfig(first);
 check(!saved.displays.contains(source)&&saved.forDisplay("disconnected-test-display")==disconnected,"Reset override was not isolated and persisted.");

 // Generated filenames keep user palette names out of filesystem paths.
 auto custom=preset(2);custom.name="Evening desk / ../ [palette]";custom.accent=0x123456;custom.radius=22;
 config=app.config();config.theme=custom;app.apply(config);click(80,125);
 namedClick(L"Cancelled palette",false,315,642);
 check(win::savedThemePresets().empty(),"Cancelling a palette name created a saved preset.");
 namedClick(L"Evening Desk",true,315,642);
 auto namedThemes=win::savedThemePresets();
 check(namedThemes.size()==1&&namedThemes.front().name=="Evening Desk","Native palette name input was not persisted.");
 auto library=directory/L"theme-presets",one=library/L"preset-000001.aethertheme";
 auto stored=parseTheme(win::readText(one,65536));
 check(stored&&*stored==custom,"Save palette did not create an exact data-only preset inside the settings profile.");
 std::filesystem::create_directories(library);
 win::writeAtomic(library/L"preset-000003.aethertheme","[AetherTheme]\nversion=1\n[theme]\naccent=not-color\n");
 win::writeAtomic(library/L"ignored.cmd","Do not execute or load this file.");
 config=app.config();config.theme=preset(0);app.apply(config);paint();
 click(330,736);
 check(app.config().theme==custom,"Saved palette row did not apply its persisted colors and radius.");
 saved=win::loadConfig(first);check(saved.theme==custom,"Applying a saved palette was not persisted.");
 // Tab reaches the offscreen saved-palette rename action on a compact window.
 resize(730,460);click(80,125);
 for(int n=0;n<27;++n)SendMessageW(editor.hwnd(),WM_KEYDOWN,VK_TAB,0);
 requestedPresetName=L"Renamed Desk";acceptPresetName=true;presetDialogTimer=SetTimer(nullptr,0,20,presetNameTimer);
 SendMessageW(editor.hwnd(),WM_KEYDOWN,VK_RETURN,0);KillTimer(nullptr,presetDialogTimer);presetDialogTimer=0;paint();
 namedThemes=win::savedThemePresets();
 check(namedThemes.size()==1&&namedThemes.front().name=="Renamed Desk"&&namedThemes.front().theme==custom,"Native palette rename changed theme values or failed to save its name.");
 resize(730,900);click(80,125);click(665,736);
 check(!std::filesystem::exists(one),"Delete palette did not remove the selected preset.");
 config=app.config();config.theme=preset(1);app.apply(config);paint();click(330,736);
 check(app.config().theme==preset(1),"Malformed saved palette was exposed as an applicable theme.");
 check(win::readText(library/L"ignored.cmd")=="Do not execute or load this file.","Saved palette actions touched an unrelated script file.");

 // Complete named layouts apply through the real editor while retaining unrelated state.
 click(80,164);
 SendMessageW(editor.hwnd(),WM_MOUSEWHEEL,MAKEWPARAM(0,-720),0);paint();
 float layoutScroll=std::min(330.f,std::max(0.f,1560.f-(client.bottom/factor-151.f)));
 namedClick(L"Cancelled layout",false,315,961-layoutScroll);
 check(win::savedLayoutPresets().empty(),"Cancelling the native layout name created a preset.");
 auto originalLayout=app.config();
 namedClick(L"Studio Work",true,315,961-layoutScroll);
 auto layouts=win::savedLayoutPresets();
 check(layouts.size()==1&&layouts.front().name=="Studio Work"&&layouts.front().defaults==originalLayout.defaults,"Native layout save failed to persist its name or defaults.");
 config=app.config();config.defaults.edge=Edge::Right;config.defaults.dockGap=8;config.theme=preset(1);config.clock24=true;config.windowPreviews=false;
 auto disconnectedCurrent=config.forDisplay("disconnected-test-display");disconnectedCurrent.dockGap=14;config.displays["disconnected-test-display"]=disconnectedCurrent;app.apply(config);
 SendMessageW(editor.hwnd(),WM_MOUSEWHEEL,MAKEWPARAM(0,-240),0);paint();
 layoutScroll=std::min(layoutScroll+110.f,std::max(0.f,1622.f-(client.bottom/factor-151.f)));
 float layoutRow=1144-layoutScroll;
 click(330,layoutRow);
 check(app.config().defaults==originalLayout.defaults&&app.config().theme==preset(1)&&app.config().clock24&&!app.config().windowPreviews,"Native layout apply altered theme/behavior or failed to restore defaults.");
 check(app.config().forDisplay("disconnected-test-display")==disconnectedCurrent,"Native layout apply overwrote disconnected display settings.");
 saved=win::loadConfig(first);check(saved.defaults==originalLayout.defaults&&!saved.hideTaskbar,"Applied layout was not persisted safely.");
 namedClick(L"Renamed Work",true,570,layoutRow);
 layouts=win::savedLayoutPresets();
 check(layouts.size()==1&&layouts.front().name=="Renamed Work","Native layout rename was not persisted.");
 click(665,layoutRow);check(win::savedLayoutPresets().empty(),"Native layout Remove did not delete its own preset.");

 // A reparse directory must never redirect palette writes outside the library.
 auto linkedProfile=directory/L"linked-profile",outside=directory/L"outside-library";
 std::filesystem::create_directories(linkedProfile);std::filesystem::create_directories(outside);
 auto linkedLibrary=linkedProfile/L"theme-presets";
 win::writeAtomic(outside/L"sentinel.txt","Preserve the junction target.");
 bool junction=directoryJunction(linkedLibrary,outside);
 check(junction,"Could not create the owned NTFS junction fixture.");
 if(junction){
  auto attributes=GetFileAttributesW(linkedLibrary.c_str());
  check(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_REPARSE_POINT),"Owned junction did not have the reparse attribute.");
  win::setConfigDirectory(linkedProfile);editor.open(displays.front());resize(730,900);paint();click(80,125);namedClick(L"Linked palette",true,315,642);
  check(!std::filesystem::exists(outside/L"preset-000001.aethertheme"),"Saved palette followed a reparse directory and wrote outside its library.");
  check(RemoveDirectoryW(linkedLibrary.c_str())!=FALSE,"Could not remove the owned junction itself.");
  check(win::readText(outside/L"sentinel.txt")=="Preserve the junction target.","Junction cleanup changed its target sentinel.");
  win::setConfigDirectory(directory);
 }
 std::cout<<"Desktop Studio: "<<(checks-failures)<<"/"<<checks<<" checks passed.\n";
 return failures?2:0;
}
