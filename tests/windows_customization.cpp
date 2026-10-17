#include "app.hpp"
#include "ui/editor.hpp"
#include "ui/activities.hpp"
#include "ui/panel.hpp"
#include "platform/provider_sources.hpp"
#include "platform/provider_codec.hpp"
#include "platform/layout_library.hpp"
#include <oleacc.h>
#include <wincodec.h>
#include <iostream>
using namespace aether;
namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
VARIANT child(long id){VARIANT v{};v.vt=VT_I4;v.lVal=id;return v;}
win::ComPtr<IAccessible> tree(HWND window){win::ComPtr<IAccessible> value;check(SUCCEEDED(AccessibleObjectFromWindow(window,static_cast<DWORD>(OBJID_CLIENT),IID_IAccessible,reinterpret_cast<void**>(value.GetAddressOf()))),"Studio accessibility unavailable");return value;}
long find(IAccessible* value,std::wstring_view label){long count=0;value->get_accChildCount(&count);std::vector<VARIANT> items(static_cast<std::size_t>(count));long fetched=0;AccessibleChildren(value,0,count,items.data(),&fetched);long found=-1;for(long i=0;i<fetched;++i){auto& item=items[static_cast<std::size_t>(i)];if(item.vt==VT_I4){BSTR name=nullptr;value->get_accName(item,&name);if(name&&std::wstring_view(name,SysStringLen(name))==label)found=item.lVal;SysFreeString(name);}VariantClear(&item);}return found;}
void invoke(ui::Editor& editor,const wchar_t* name){
 editor.update();editor.invalidate();UpdateWindow(editor.hwnd());auto value=tree(editor.hwnd());auto id=find(value.Get(),name);check(id>0,"Customization control absent from native accessibility tree");
 check(SUCCEEDED(value->accSelect(SELFLAG_TAKEFOCUS,child(id))),"Customization control cannot receive keyboard focus");UpdateWindow(editor.hwnd());
 VARIANT state{},focus{};value->get_accState(child(id),&state);value->get_accFocus(&focus);long x=0,y=0,width=0,height=0;value->accLocation(&x,&y,&width,&height,child(id));RECT client{};GetClientRect(editor.hwnd(),&client);POINT origin{};ClientToScreen(editor.hwnd(),&origin);
 std::cout<<"FOCUS "<<win::utf8(name)<<" id="<<id<<" focus="<<(focus.vt==VT_I4?focus.lVal:0)<<" state="<<(state.vt==VT_I4?state.lVal:0)<<" location="<<x-origin.x<<","<<y-origin.y<<","<<width<<","<<height<<" client="<<client.right<<","<<client.bottom<<" dpi="<<win::dpiFor(editor.hwnd())<<'\n';
 VariantClear(&state);VariantClear(&focus);SendMessageW(editor.hwnd(),WM_KEYDOWN,VK_RETURN,0);editor.update();UpdateWindow(editor.hwnd());
}
void pump(DWORD milliseconds){auto until=GetTickCount64()+milliseconds;do{MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}if(GetTickCount64()>=until)break;MsgWaitForMultipleObjectsEx(0,nullptr,10,QS_ALLINPUT,MWMO_INPUTAVAILABLE);}while(true);}
int appearanceTicks=0;
LRESULT CALLBACK observeTimer(HWND window,UINT message,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR){if(message==WM_TIMER)++appearanceTicks;return DefSubclassProc(window,message,w,l);}
void CALLBACK renameDialog(HWND,UINT,UINT_PTR,DWORD){EnumThreadWindows(GetCurrentThreadId(),[](HWND window,LPARAM)->BOOL{if(win::windowText(window)!=L"Rename pinned app")return TRUE;SetDlgItemTextW(window,101,L"Tea personal label");PostMessageW(window,WM_COMMAND,IDOK,0);return FALSE;},0);}
void png(const std::filesystem::path& path,unsigned width,unsigned height,BYTE red=200){
 win::ComPtr<IWICImagingFactory> factory;check(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory))),"WIC fixture factory");win::ComPtr<IWICStream> stream;factory->CreateStream(&stream);check(SUCCEEDED(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE)),"WIC fixture stream");win::ComPtr<IWICBitmapEncoder> encoder;factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder);check(SUCCEEDED(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache)),"WIC fixture encoder");win::ComPtr<IWICBitmapFrameEncode> frame;encoder->CreateNewFrame(&frame,nullptr);frame->Initialize(nullptr);frame->SetSize(width,height);WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;frame->SetPixelFormat(&format);std::vector<BYTE> row(static_cast<std::size_t>(width)*4);for(unsigned i=0;i<width;++i){row[i*4]=12;row[i*4+1]=60;row[i*4+2]=red;row[i*4+3]=255;}for(unsigned i=0;i<height;++i)check(SUCCEEDED(frame->WritePixels(1,width*4,static_cast<UINT>(row.size()),row.data())),"WIC fixture pixels");check(SUCCEEDED(frame->Commit())&&SUCCEEDED(encoder->Commit()),"WIC fixture commit");
}
}
int main(){try{
 SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);win::ComApartment com;INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
 auto directory=win::executablePath().parent_path()/L"test-profiles"/(L"customization-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));std::filesystem::create_directories(directory);win::setConfigDirectory(directory);Config fixtureSettings;fixtureSettings.hideTaskbar=false;win::saveConfig(fixtureSettings);
 auto valid=directory/L"wide.png";png(valid,256,128);auto reply=win::runProviderOperation(static_cast<win::ProviderOperation>(7),win::encodeProviderTarget(valid.wstring()));auto pixels=win::decodeIcon(reply.bytes);check(reply.status==0&&pixels&&pixels->width==128&&pixels->height==64&&pixels->bytes[2]==200,"Actual image provider did not decode/downscale owned PNG");
 auto huge=directory/L"huge.png";png(huge,4097,1);check(win::runProviderOperation(static_cast<win::ProviderOperation>(7),win::encodeProviderTarget(huge.wstring())).status!=0,"Image provider accepted oversized source dimensions");
 win::writeAtomic(directory/L"malformed.png","not an image");check(win::runProviderOperation(static_cast<win::ProviderOperation>(7),win::encodeProviderTarget((directory/L"malformed.png").wstring())).status!=0,"Image provider accepted malformed image");
 win::writeAtomic(directory/L"oversized.png",std::string(8*1024*1024+1,'x'));check(win::runProviderOperation(static_cast<win::ProviderOperation>(7),win::encodeProviderTarget((directory/L"oversized.png").wstring())).status!=0,"Image provider accepted oversized file budget");
 auto custom=parseSettings("[layout]\nclock=0\nfocusedTitle=0\nstatus=0\n");auto layout=win::saveLayoutPreset(custom,"Custom visibility");check(layout.has_value(),"Customized layout preset cannot save");auto stored=win::loadLayoutPreset(layout->id);check(stored.has_value(),"Customized layout preset cannot load");Config restored;restored.defaults=stored->defaults;auto layoutText=serializeSettings(restored);check(layoutText.find("clock=0\n")!=std::string::npos&&layoutText.find("focusedTitle=0\n")!=std::string::npos&&layoutText.find("status=0\n")!=std::string::npos,"Named layout preset lost per-display top-bar visibility");
 auto legacyText="[AetherLayout]\nversion=1\n[preset]\nname=Legacy\n[layout]\nedge=bottom\niconSize=48\ndockGap=10\ntopBar=1\ndock=1\nautoHide=0\nmonitorOnly=1\nreserveDock=1\n";win::writeAtomic(directory/L"layout-presets"/L"preset-000080.aetherlayout",legacyText);auto oldLayout=win::loadLayoutPreset("preset-000080");check(oldLayout&&oldLayout->defaults==DisplayOptions{},"Legacy eight-field layout lost its inherited visibility defaults");
 App app;ui::Graphics graphics;ui::Editor editor(graphics,app);auto displays=win::monitors();check(!displays.empty(),"No native test display");editor.open(displays.front());UpdateWindow(editor.hwnd());
 invoke(editor,L"Frosted glass");check(app.config().theme.name=="Frosted","Frosted control did not apply native theme");
 invoke(editor,L"Minimal");check(app.config().theme.name=="Minimal","Minimal control did not apply native theme");
 invoke(editor,L"Increase text scale");check(serializeSettings(app.config()).find("fontScale=105\n")!=std::string::npos,"Native text scale control did not change persisted setting");
 invoke(editor,L"Reduce motion");check(serializeSettings(app.config()).find("reduceMotion=1\n")!=std::string::npos,"Reduce-motion control did not persist");
 bool first=false;auto loaded=win::loadConfig(first);check(serializeSettings(loaded).find("fontScale=105\n")!=std::string::npos&&!loaded.hideTaskbar,"Customization disk reload changed setting or taskbar gate");
 invoke(editor,L"Layout & displays");
 auto visibility=[&](const char* control){auto profile=app.config().forDisplay(win::utf8(displays.front().name));std::cout<<"VISIBILITY "<<control<<" defaults="<<app.config().defaults.clock<<","<<app.config().defaults.focusedTitle<<","<<app.config().defaults.status<<" selected="<<profile.clock<<","<<profile.focusedTitle<<","<<profile.status<<" overrides="<<app.config().displays.size()<<'\n';};
 visibility("before");invoke(editor,L"Show clock");visibility("clock");invoke(editor,L"Show focused title");visibility("title");invoke(editor,L"Show status");visibility("status");
 check(!app.config().defaults.clock&&!app.config().defaults.focusedTitle&&!app.config().defaults.status,"Native display controls did not change top-bar visibility");
 loaded=win::loadConfig(first);check(!loaded.defaults.clock&&!loaded.defaults.focusedTitle&&!loaded.defaults.status,"Top-bar visibility failed disk reload");
 auto config=app.config();config.pins={{"Tea fixture",win::utf8(win::executablePath().wstring()),"tea-fixture",win::utf8(valid.wstring()),1}};app.apply(config);
 invoke(editor,L"Pinned apps");auto renameTimer=SetTimer(nullptr,0,20,renameDialog);invoke(editor,L"Rename Tea fixture");KillTimer(nullptr,renameTimer);
 check(app.config().pins[0].name=="Tea personal label"&&app.config().pins[0].target==config.pins[0].target&&app.config().pins[0].identity=="tea-fixture","Native pinned-app rename changed launch/group identity");
 invoke(editor,L"Reset icon for Tea personal label");check(app.config().pins[0].iconPath.empty()&&app.config().pins[0].target==config.pins[0].target&&app.config().pins[0].identity=="tea-fixture","Native icon reset changed launch/group identity");
 {
  win::IconCache cache;cache.start(editor.hwnd());Pin decorated{"Owned icon",win::utf8(win::executablePath().wstring()),"owned",win::utf8(valid.wstring()),1};
  auto await=[&](const Pin& pin){std::shared_ptr<const win::IconPixels> image;auto deadline=GetTickCount64()+3500;while(GetTickCount64()<deadline){image=cache.request(pin);if(image&&image->width==128&&image->height==64)break;pump(10);}return image;};
  auto original=await(decorated);check(original&&original->width==128&&original->height==64&&original->bytes[2]==200,"Actual isolated provider/cache did not deliver custom PNG");
  png(valid,256,128,30);decorated.iconRevision=2;auto changed=await(decorated);check(changed&&changed->width==128&&changed->height==64&&changed->bytes[2]==30&&changed->serial!=original->serial,"Re-selecting the same image path reused stale cached override");
  decorated.iconPath=win::utf8((directory/L"malformed.png").wstring());decorated.iconRevision=3;
  std::shared_ptr<const win::IconPixels> fallback;auto deadline=GetTickCount64()+3500;while(GetTickCount64()<deadline){fallback=cache.request(decorated);if(fallback)break;pump(10);}check(fallback&&fallback->width<=128&&fallback->height<=128,"Malformed decorative image did not fall back to a native app icon");cache.stop();
 }
 {
  config=app.config();config.reduceMotion=false;config.defaults.reserveDock=false;config.defaults.autoHide=false;config.windowPreviews=false;app.apply(config);
  ui::Panel dock(graphics,app,displays.front(),false);dock.setGroups(groupDock(config.pins,{},win::utf8(displays.front().name),true));dock.layout(displays.front(),false);UpdateWindow(dock.hwnd());pump(0);
  SetWindowSubclass(dock.hwnd(),observeTimer,78,0);RECT before{},after{},client{};GetWindowRect(dock.hwnd(),&before);GetClientRect(dock.hwnd(),&client);
  BOOL motion=FALSE;SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&motion,0);appearanceTicks=0;SendMessageW(dock.hwnd(),WM_MOUSEMOVE,0,MAKELPARAM(client.right*3/4,client.bottom/2));TRACKMOUSEEVENT cancelTrack{sizeof(cancelTrack),TME_CANCEL|TME_LEAVE,dock.hwnd(),0};TrackMouseEvent(&cancelTrack);MSG leaveMessage{};while(PeekMessageW(&leaveMessage,dock.hwnd(),WM_MOUSELEAVE,WM_MOUSELEAVE,PM_REMOVE)){}pump(240);GetWindowRect(dock.hwnd(),&after);
  check(EqualRect(&before,&after)!=FALSE,"Visual dock easing changed physical hit/window geometry");
  if(motion)check(appearanceTicks>0&&appearanceTicks<18,"Enabled dock easing had no bounded transient frames");
  int settled=appearanceTicks;pump(130);check(appearanceTicks==settled,"Dock easing retained a timer after rest");
  SendMessageW(dock.hwnd(),WM_MOUSELEAVE,0,0);dock.hide();settled=appearanceTicks;pump(200);check(appearanceTicks==settled,"Hidden dock retained its appearance timer");
  dock.show();config.reduceMotion=true;app.apply(config);dock.layout(displays.front(),false);UpdateWindow(dock.hwnd());pump(0);settled=appearanceTicks;SendMessageW(dock.hwnd(),WM_MOUSEMOVE,0,MAKELPARAM(client.right*3/4,client.bottom/2));TrackMouseEvent(&cancelTrack);while(PeekMessageW(&leaveMessage,dock.hwnd(),WM_MOUSELEAVE,WM_MOUSELEAVE,PM_REMOVE)){}pump(200);check(appearanceTicks==settled,"Reduce motion allowed appearance timer frames");RemoveWindowSubclass(dock.hwnd(),observeTimer,78);
 }
 ui::Activities activities(graphics,app);activities.open(displays.front());auto edit=GetDlgItem(activities.hwnd(),10);SetWindowTextW(edit,L"carets");SendMessageW(edit,EM_SETSEL,3,3);SendMessageW(edit,WM_KEYDOWN,VK_LEFT,0);DWORD start=0,end=0;SendMessageW(edit,EM_GETSEL,reinterpret_cast<WPARAM>(&start),reinterpret_cast<LPARAM>(&end));check(start==2&&end==2,"Customized native search font broke native caret delegation");
 win::setConfigDirectory({});std::cout<<"PASS owned WIC image bounds and actual native Studio customization/caret\n";return 0;
}catch(const std::exception& e){win::setConfigDirectory({});std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
