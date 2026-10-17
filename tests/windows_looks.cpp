#include "app.hpp"
#include "ui/looks.hpp"
#include "ui/extensions.hpp"
#include "platform/provider_codec.hpp"
#include <oleacc.h>
#include <wincodec.h>
#include <iostream>
using namespace aether;
namespace {
struct Fixture {App* app=nullptr;std::unique_ptr<ui::Looks> gallery;std::unique_ptr<ui::Graphics> graphics;std::string toolsId;int phase=0,checks=0,failures=0;ULONGLONG deadline=0;UINT_PTR timer=0;std::filesystem::path pictures;bool dialogBlockedMutation=false;};
Fixture* fixture=nullptr;
void check(bool condition,const char* message){++fixture->checks;if(!condition){++fixture->failures;std::cerr<<message<<'\n';}}
VARIANT child(long id){VARIANT v{};v.vt=VT_I4;v.lVal=id;return v;}
win::ComPtr<IAccessible> tree(HWND window){win::ComPtr<IAccessible> value;if(FAILED(AccessibleObjectFromWindow(window,static_cast<DWORD>(OBJID_CLIENT),IID_IAccessible,reinterpret_cast<void**>(value.GetAddressOf()))))throw std::runtime_error("Looks MSAA unavailable");return value;}
void invoke(ui::Looks& gallery,const wchar_t* name,bool allowDisabled=false){gallery.update();UpdateWindow(gallery.hwnd());auto value=tree(gallery.hwnd());long count=0;value->get_accChildCount(&count);for(long i=1;i<=count;++i){BSTR label=nullptr;value->get_accName(child(i),&label);const bool found=label&&std::wstring(label)==name;SysFreeString(label);if(found){if(FAILED(value->accDoDefaultAction(child(i)))&&!allowDisabled)throw std::runtime_error("Looks action rejected");UpdateWindow(gallery.hwnd());return;}}throw std::runtime_error("Looks action missing");}
HWND widgetWindow(const wchar_t* title){HWND found=nullptr;std::pair<const wchar_t*,HWND*> context{title,&found};EnumThreadWindows(GetCurrentThreadId(),[](HWND window,LPARAM data)->BOOL{auto* pair=reinterpret_cast<std::pair<const wchar_t*,HWND*>*>(data);if(win::windowText(window)==pair->first){*pair->second=window;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&context));return found;}
void CALLBACK saveNote(HWND,UINT,UINT_PTR,DWORD){
 EnumThreadWindows(GetCurrentThreadId(),[](HWND window,LPARAM)->BOOL{if(win::windowText(window)!=L"Quick note"||!GetDlgItem(window,101))return TRUE;
  std::string error;fixture->dialogBlockedMutation=!fixture->app->extensions().setEnabled(fixture->toolsId,false,&error);
  SetDlgItemTextW(window,101,L"Owned quick note saved locally");PostMessageW(window,WM_COMMAND,IDOK,0);return FALSE;},0);
}
bool capture(HWND window,const std::filesystem::path& path){
 RECT client{};GetClientRect(window,&client);POINT origin{};ClientToScreen(window,&origin);const int width=client.right,height=client.bottom;if(width<1||height<1)return false;
 for(int y:{height/4,height/2,3*height/4})for(int x:{width/4,width/2,3*width/4}){auto top=WindowFromPoint({origin.x+x,origin.y+y});if(top!=window&&GetAncestor(top,GA_ROOT)!=window)return false;}
 DwmFlush();HDC screen=GetDC(nullptr),memory=CreateCompatibleDC(screen);if(!screen||!memory){if(memory)DeleteDC(memory);if(screen)ReleaseDC(nullptr,screen);return false;}
 BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;void* pixels=nullptr;auto bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&pixels,nullptr,0);if(!bitmap){DeleteDC(memory);ReleaseDC(nullptr,screen);return false;}
 auto previous=SelectObject(memory,bitmap);const bool copied=BitBlt(memory,0,0,width,height,screen,origin.x,origin.y,SRCCOPY|CAPTUREBLT)!=FALSE;
 bool saved=false;if(copied){auto* data=static_cast<BYTE*>(pixels);for(size_t i=3;i<static_cast<size_t>(width)*height*4;i+=4)data[i]=255;
  win::ComPtr<IWICImagingFactory> factory;win::ComPtr<IWICStream> stream;win::ComPtr<IWICBitmapEncoder> encoder;win::ComPtr<IWICBitmapFrameEncode> frame;win::ComPtr<IPropertyBag2> properties;
  if(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)))&&SUCCEEDED(factory->CreateStream(&stream))&&SUCCEEDED(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE))&&SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder))&&SUCCEEDED(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache))&&SUCCEEDED(encoder->CreateNewFrame(&frame,&properties))&&SUCCEEDED(frame->Initialize(properties.Get()))&&SUCCEEDED(frame->SetSize(static_cast<UINT>(width),static_cast<UINT>(height)))){
   auto format=GUID_WICPixelFormat32bppBGRA;if(SUCCEEDED(frame->SetPixelFormat(&format))&&IsEqualGUID(format,GUID_WICPixelFormat32bppBGRA)&&SUCCEEDED(frame->WritePixels(static_cast<UINT>(height),static_cast<UINT>(width*4),static_cast<UINT>(width*height*4),data))&&SUCCEEDED(frame->Commit())&&SUCCEEDED(encoder->Commit()))saved=true;
  }
 }
 SelectObject(memory,previous);DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(nullptr,screen);return saved;
}
void finish(){if(fixture->timer){KillTimer(nullptr,fixture->timer);fixture->timer=0;}fixture->app->quit();}
void CALLBACK tick(HWND,UINT,UINT_PTR,DWORD){
 if(!fixture||!fixture->app->controller())return;
 try{auto& app=*fixture->app;
  if(fixture->phase==0){fixture->phase=1;fixture->graphics=std::make_unique<ui::Graphics>();fixture->gallery=std::make_unique<ui::Looks>(*fixture->graphics,app);fixture->gallery->open(app.display());const auto before=app.config();
   const wchar_t* names[]={L"Preview Aether Graphite",L"Preview Aether Linen",L"Preview Aether Aurora",L"Preview Aether Ember"};
   for(int i=0;i<4;++i){invoke(*fixture->gallery,names[i]);check(app.config().theme==before.theme,"Preview changed the desktop before Apply");
    if(capture(fixture->gallery->hwnd(),fixture->pictures/(L"look-"+std::to_wstring(i)+L".png")))std::cout<<"Native preview PNG saved for look "<<i<<'\n';else std::cout<<"Native preview capture occluded/unavailable for look "<<i<<"; no pixel acceptance claimed\n";}
   invoke(*fixture->gallery,L"Apply look");check(app.config().theme==preset(8)&&app.config().pins==before.pins&&app.config().hideTaskbar==before.hideTaskbar,"Apply changed unrelated preferences");
   invoke(*fixture->gallery,L"Undo last apply");check(app.config().theme==before.theme,"Undo did not restore the prior look");
   invoke(*fixture->gallery,L"Preview Aether Graphite");invoke(*fixture->gallery,L"Apply look");auto external=app.config();external.theme=preset(2);app.apply(external);invoke(*fixture->gallery,L"Undo last apply",true);check(app.config().theme==external.theme,"Undo overwrote a subsequent external palette");
   invoke(*fixture->gallery,L"Use matching desk widgets");invoke(*fixture->gallery,L"Preview Aether Linen");invoke(*fixture->gallery,L"Use matching desk widgets");
   check(app.extensions().widgetCount()==5,"Switching matching looks stacked two desk widget sets");
   invoke(*fixture->gallery,L"Add media, notes, resources + Seattle weather");
   check(app.extensions().widgetCount()==10,"Integrated gallery did not create ten matching/productivity widgets");
   for(const auto& entry:app.extensions().library().entries())if(entry.manifest.id=="aether-productivity")fixture->toolsId=entry.storageId;
   check(!fixture->toolsId.empty(),"Productivity package missing");fixture->deadline=GetTickCount64()+6000;return;
  }
  if(fixture->phase==1){auto snapshot=app.extensions().data().snapshot();if(!snapshot.mediaAvailable||!snapshot.weatherCached){if(GetTickCount64()>fixture->deadline){check(false,"Visible typed widgets did not receive their owned fake provider data");finish();}return;}
   fixture->phase=2;check(snapshot.media.find(L"Owned track")!=snapshot.media.npos,"Media data came from the wrong provider");
   auto note=widgetWindow(L"Aether extension: Productivity / Quick note");check(note&&IsWindowVisible(note),"Note widget not visible");if(!note){finish();return;}auto noteTree=tree(note);const auto timer=SetTimer(nullptr,0,20,saveNote);noteTree->accDoDefaultAction(child(4));KillTimer(nullptr,timer);
   check(fixture->dialogBlockedMutation,"An active note dialog allowed destruction of its owner");
   auto& entries=app.extensions().library().entries();auto tools=std::find_if(entries.begin(),entries.end(),[](const auto& entry){return entry.manifest.id=="aether-productivity";});check(tools!=entries.end()&&std::any_of(tools->manifest.widgets.begin(),tools->manifest.widgets.end(),[](const auto& widget){return widget.kind==WidgetKind::Note&&widget.text=="Owned quick note saved locally";}),"Note text was not committed through the owned library");
   std::string error;check(app.extensions().setEnabled(fixture->toolsId,false,&error),"Productivity disable failed");long count=0;check(noteTree->get_accChildCount(&count)==CO_E_OBJNOTCONNECTED,"Retained note accessibility survived owner destruction");check(!app.extensions().data().command(L"toggle"),"Disabled media accepted a late action");
   win::ExtensionLibrary reload;check(reload.load(&error),"Package reload failed");check(std::any_of(reload.entries().begin(),reload.entries().end(),[](const auto& entry){return entry.manifest.id=="aether-productivity"&&!entry.enabled&&std::any_of(entry.manifest.widgets.begin(),entry.manifest.widgets.end(),[](const auto& widget){return widget.kind==WidgetKind::Note&&widget.text=="Owned quick note saved locally";});}),"Note/state did not survive disk reload");finish();
  }
 }catch(const std::exception& error){check(false,error.what());finish();}
}
}
int wmain(int argc,wchar_t** argv){
 if(argc==7)return win::runProviderHost(argc,argv,[](win::ProviderOperation operation,const win::ProviderBytes&,std::wstring_view){if(operation==win::ProviderOperation::MediaQuery)return win::ProviderWireReply{0,win::encodeProviderTarget(L"playing\nOwned track\nOwned artist\nowned-session")};if(operation==win::ProviderOperation::SeattleForecast)return win::ProviderWireReply{0,win::encodeProviderTarget(L"55F - owned weather fixture\nNo network request")};if(operation==win::ProviderOperation::Catalog)return win::ProviderWireReply{0,win::encodeApplications({})};return win::ProviderWireReply{1,{}};});
 if(FindWindowW(L"Aether.Controller.v3",L"Aether UI Controller")){std::cerr<<"Close Aether before the isolated gallery fixture\n";return 2;}
 SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);win::ComApartment apartment;Fixture state;fixture=&state;
 auto profile=win::executablePath().parent_path()/L"test-profiles"/(L"looks-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));std::filesystem::create_directories(profile);win::setConfigDirectory(profile);
 auto config=Config{};config.hideTaskbar=false;config.defaults.dock=false;config.defaults.topBar=false;config.defaults.reserveDock=false;config.pins={{"Owned pin","C:\\AetherOwnedNeverLaunch.exe","owned-fixture"}};win::saveConfig(config);
 state.pictures=win::executablePath().parent_path()/L"test-artifacts"/(L"looks-"+std::to_wstring(GetCurrentProcessId()));std::filesystem::create_directories(state.pictures);
 win::ProviderProcessOptions fake;fake.executable=win::executablePath();fake.mode=L"owned-looks";App app(fake,{},fake);state.app=&app;state.timer=SetTimer(nullptr,0,50,tick);
 const auto result=app.run();state.gallery.reset();state.graphics.reset();std::cout<<state.checks-state.failures<<'/'<<state.checks<<" native gallery/widget checks passed; appExit="<<result<<"; pictures="<<state.pictures.string()<<'\n';fixture=nullptr;return result||state.failures?1:0;
}
