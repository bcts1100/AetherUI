#include "app.hpp"
#include "ui/extensions.hpp"
#include <iostream>
#include <oleacc.h>
namespace {
std::vector<HWND> ownedWidgets(){std::vector<HWND> result;EnumThreadWindows(GetCurrentThreadId(),[](HWND hwnd,LPARAM data)->BOOL{auto name=aether::win::windowText(hwnd);if(name.starts_with(L"Aether extension: "))reinterpret_cast<std::vector<HWND>*>(data)->push_back(hwnd);return TRUE;},reinterpret_cast<LPARAM>(&result));return result;}
}
int main(){
 using namespace aether;win::ComApartment com;auto directory=win::executablePath().parent_path()/L"test-profiles"/(L"extension-runtime-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));std::filesystem::create_directories(directory/L"extensions");win::setConfigDirectory(directory);
 // Literal owned installed data tests the real runtime without affecting the user's profile.
 win::writeAtomic(directory/L"extensions"/L"preset-000001.aetherplugin","[AetherPlugin]\nversion=1\n[plugin]\nid=clock-fixture\nname=Clock fixture\n[widget:clock]\ntype=clock\nlabel=Native fixture clock\n[AetherInstallation]\nversion=1\nenabled=1\norder=0\n");
 App app;ui::Graphics graphics;ui::ExtensionRuntime runtime(graphics,app);int checks=0,failures=0;auto check=[&](bool ok,const char* why){++checks;if(!ok){++failures;std::cerr<<why<<'\n';}};std::string error;
 runtime.pause(true);check(runtime.start(&error),"Runtime must load an owned installed profile.");
 auto windows=ownedWidgets();check(runtime.widgetCount()==1&&windows.size()==1,"Enabled native clock must own exactly one real Surface HWND.");
 check(std::none_of(windows.begin(),windows.end(),[](HWND hwnd){return IsWindowVisible(hwnd)!=FALSE;}),"Session pause must keep all owned extension HWNDs hidden.");
 win::ComPtr<IAccessible> retained;
 if(!windows.empty()){auto h=windows.front();HRESULT hr=AccessibleObjectFromWindow(h,static_cast<DWORD>(OBJID_CLIENT),IID_IAccessible,reinterpret_cast<void**>(retained.GetAddressOf()));check(SUCCEEDED(hr)&&retained,"Native widget must publish a lifetime-safe accessible client.");if(retained){VARIANT child{};child.vt=VT_I4;child.lVal=1;BSTR name=nullptr;auto named=retained->get_accName(child,&name);check(SUCCEEDED(named)&&name&&std::wstring(name).find(L"Native fixture clock")!=std::wstring::npos,"Widget accessibility must expose manifest label and native status.");SysFreeString(name);}}
 check(runtime.setEnabled("preset-000001",false,&error),"Disable must persist before destroying native widgets.");
 check(runtime.widgetCount()==0&&ownedWidgets().empty()&&std::none_of(windows.begin(),windows.end(),[](HWND hwnd){return IsWindow(hwnd)!=FALSE;}),"Disable must destroy native windows rather than leave hidden inactive resources.");
 if(retained){long count=0;check(retained->get_accChildCount(&count)==CO_E_OBJNOTCONNECTED,"Retained accessible widget reference must disconnect after disable.");}
 win::ExtensionLibrary reload;check(reload.load(&error)&&reload.entries().size()==1&&!reload.entries()[0].enabled,"Disabled runtime state must survive a real profile reload.");
 check(runtime.setEnabled("preset-000001",true,&error)&&runtime.widgetCount()==1,"Explicit enable must create the native surface again.");
 windows=ownedWidgets();runtime.update();check(runtime.widgetCount()==1&&ownedWidgets()==windows,"An ordinary status update must retain HWND identity without recreating native resources.");
 runtime.shutdown();check(runtime.widgetCount()==0&&ownedWidgets().empty(),"Shutdown must destroy all extension surfaces.");runtime.update();check(runtime.widgetCount()==0,"Update after shutdown cannot restart widgets.");
 // The manager uses the actual App-owned runtime, native focus navigation and MSAA actions.
 auto& managed=app.extensions();managed.pause(true);check(managed.start(&error),"Manager runtime must load without showing fixture widgets.");
 win::ComPtr<IAccessible> managerReference;
 {
  ui::Extensions manager(graphics,app);check(manager.hwnd()&&IsWindow(manager.hwnd()),"Extension manager must own a real native Surface.");
  if(manager.hwnd()){
   auto monitor=app.display();manager.move(popupBounds(monitor.work,760,620,monitor.dpi),false);manager.show(false);InvalidateRect(manager.hwnd(),nullptr,FALSE);UpdateWindow(manager.hwnd());
   SendMessageW(manager.hwnd(),WM_KEYDOWN,VK_TAB,0);SendMessageW(manager.hwnd(),WM_KEYDOWN,VK_TAB,0);SendMessageW(manager.hwnd(),WM_KEYDOWN,VK_RETURN,0);UpdateWindow(manager.hwnd());
   check(managed.library().entries().size()==4,"Keyboard Tab/Enter must reach Add examples and persist native packages.");
   auto hr=AccessibleObjectFromWindow(manager.hwnd(),static_cast<DWORD>(OBJID_CLIENT),IID_IAccessible,reinterpret_cast<void**>(managerReference.GetAddressOf()));check(SUCCEEDED(hr)&&managerReference,"Extension manager must publish an accessible native client.");
   bool named=false,disabled=false;if(managerReference){long count=0;managerReference->get_accChildCount(&count);for(long i=1;i<=count;++i){VARIANT child{};child.vt=VT_I4;child.lVal=i;BSTR name=nullptr;if(SUCCEEDED(managerReference->get_accName(child,&name))&&name){std::wstring label(name);named=named||label.find(L"Clock fixture")!=std::wstring::npos;if(label==L"Disable Clock fixture")disabled=SUCCEEDED(managerReference->accDoDefaultAction(child));}SysFreeString(name);}}
   check(named&&disabled&&managed.widgetCount()==0,"Accessible package name and Disable action must destroy its native widget.");
   long enableId=0,removeId=0;if(managerReference){long count=0;managerReference->get_accChildCount(&count);for(long i=1;i<=count;++i){VARIANT child{};child.vt=VT_I4;child.lVal=i;BSTR name=nullptr;if(SUCCEEDED(managerReference->get_accName(child,&name))&&name){std::wstring label(name);if(label==L"Enable Clock fixture")enableId=i;if(label==L"Remove Clock fixture")removeId=i;}SysFreeString(name);}}
   check(enableId!=0&&removeId!=0,"Visible fixture must publish the retained Enable and Remove actions before it is hidden.");
   auto beforeHidden=managed.library().entries();auto fixtureFile=directory/L"extensions"/L"preset-000001.aetherplugin";auto beforeBytes=win::readText(fixtureFile);manager.hide();
   if(managerReference&&enableId&&removeId){VARIANT child{};child.vt=VT_I4;child.lVal=enableId;managerReference->accDoDefaultAction(child);child.lVal=removeId;managerReference->accDoDefaultAction(child);}
   check(managed.library().entries()==beforeHidden&&managed.widgetCount()==0,"Retained hidden-manager MSAA actions cannot enable or remove packages.");
   check(std::filesystem::exists(fixtureFile)&&win::readText(fixtureFile)==beforeBytes,"Retained hidden-manager actions cannot change or delete the saved profile file.");
  }
 }
 if(managerReference){long count=0;check(managerReference->get_accChildCount(&count)==CO_E_OBJNOTCONNECTED,"Manager accessibility must disconnect after its native surface is destroyed.");}
 managed.shutdown();check(ownedWidgets().empty(),"App-owned manager runtime shutdown must release all hidden fixture surfaces.");
 std::cout<<"Extensions runtime: "<<(checks-failures)<<"/"<<checks<<" checks passed.\n";return failures?2:0;
}