#include "app.hpp"
#include "owned_capture.hpp"
#include "ui/marketplace.hpp"
#include "ui/extensions.hpp"
#include <iostream>
using namespace aether;
namespace{
App* app=nullptr;ui::Marketplace* market=nullptr;UINT_PTR timer=0;bool failed=false;
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void invoke(const wchar_t* name){UpdateWindow(market->hwnd());win::ComPtr<IAccessible> tree;check(SUCCEEDED(AccessibleObjectFromWindow(market->hwnd(),static_cast<DWORD>(OBJID_CLIENT),IID_IAccessible,reinterpret_cast<void**>(tree.GetAddressOf()))),"MSAA unavailable");long count=0;tree->get_accChildCount(&count);for(long i=1;i<=count;++i){VARIANT child{};child.vt=VT_I4;child.lVal=i;BSTR text=nullptr;tree->get_accName(child,&text);bool match=text&&std::wstring(text)==name;SysFreeString(text);if(match){check(SUCCEEDED(tree->accDoDefaultAction(child)),"action rejected");UpdateWindow(market->hwnd());return;}}throw std::runtime_error("marketplace action missing");}
void CALLBACK tick(HWND,UINT,UINT_PTR,DWORD){if(!app||!app->controller())return;KillTimer(nullptr,timer);try{
 ui::Graphics g;ui::Marketplace gallery(g,*app);market=&gallery;gallery.open(app->display());const auto before=app->config();const auto originalEntries=app->extensions().library().entries();
 auto pictures=win::executablePath().parent_path()/L"test-artifacts";std::filesystem::create_directories(pictures);auto picture=pictures/(L"marketplace-"+std::to_wstring(GetCurrentProcessId())+L".png");UpdateWindow(gallery.hwnd());if(test::captureOwnedClient(gallery.hwnd(),picture))std::cout<<"Native marketplace capture: "<<picture.string()<<'\n';
 invoke(L"Graphite");invoke(L"Install package");check(app->config().theme==before.theme,"Install applied without consent");check(win::installedPackages().size()==1,"missing receipt");invoke(L"Apply / enable");check(app->config().theme==preset(5)&&!app->config().hideTaskbar,"theme apply changed taskbar preference");invoke(L"Remove package");check(win::installedPackages().empty(),"theme receipt survived removal");
 invoke(L"Essential desk");invoke(L"Install package");check(app->extensions().widgetCount()==0,"widget install auto-enabled");invoke(L"Apply / enable");check(app->extensions().widgetCount()==5,"widget package not enabled");invoke(L"Disable");check(app->extensions().widgetCount()==0,"widget disable failed");invoke(L"Apply / enable");invoke(L"Remove package");check(app->extensions().widgetCount()==0&&app->extensions().library().entries()==originalEntries,"widget package removal left ownership");
 std::string error;check(!win::installPackage("../unknown",&error),"unknown package accepted");check(!win::removePackage("../outside",&error),"unsafe receipt accepted");
 for(const auto& p:win::bundledCatalog()){auto first=win::installPackage(p.id,&error);auto again=win::installPackage(p.id,&error);check(first&&again&&first->receipt==again->receipt,"duplicate install changed receipt");}
 auto installed=win::installedPackages();check(installed.size()==win::bundledCatalog().size(),"catalog persistence incomplete");for(const auto& p:installed)check(win::removePackage(p.receipt,&error),"catalog remove failed");check(win::installedPackages().empty(),"receipts leaked");market=nullptr;std::cout<<"PASS native marketplace install/apply/disable/remove, duplicate persistence and path validation\n";
 }catch(const std::exception& e){failed=true;market=nullptr;std::cerr<<"FAIL "<<e.what()<<'\n';}app->quit();}
}
int main(){if(FindWindowW(L"Aether.Controller.v3",L"Aether UI Controller"))return 2;SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);win::ComApartment com;auto profile=win::executablePath().parent_path()/L"test-profiles"/(L"market-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));std::filesystem::create_directories(profile);win::setConfigDirectory(profile);Config c;c.hideTaskbar=false;c.defaults.dock=false;c.defaults.topBar=false;c.defaults.reserveDock=false;win::saveConfig(c);App instance;app=&instance;timer=SetTimer(nullptr,0,100,tick);int result=instance.run();app=nullptr;return failed||result?1:0;}
