#include "platform/windows.hpp"
#include "platform/desktop.hpp"
#include <oleacc.h>
#include <iostream>
#include <array>
#include <wincodec.h>
using namespace aether;
namespace {
void pump(unsigned milliseconds=250){const auto until=GetTickCount64()+milliseconds;do{MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}Sleep(5);}while(GetTickCount64()<until);}
bool key(HWND owner,WORD code){if(GetForegroundWindow()!=owner)return false;INPUT inputs[2]{};for(auto& i:inputs){i.type=INPUT_KEYBOARD;i.ki.wVk=code;i.ki.dwFlags=(code==VK_LWIN||code==VK_RWIN)?KEYEVENTF_EXTENDEDKEY:0;}inputs[1].ki.dwFlags|=KEYEVENTF_KEYUP;return SendInput(2,inputs,sizeof(INPUT))==2;}
bool chord(HWND owner,WORD code){if(GetForegroundWindow()!=owner)return false;INPUT inputs[4]{};for(auto& input:inputs)input.type=INPUT_KEYBOARD;inputs[0].ki.wVk=VK_CONTROL;inputs[1].ki.wVk=code;inputs[2].ki.wVk=code;inputs[2].ki.dwFlags=KEYEVENTF_KEYUP;inputs[3].ki.wVk=VK_CONTROL;inputs[3].ki.dwFlags=KEYEVENTF_KEYUP;const auto sent=SendInput(4,inputs,sizeof(INPUT));if(sent>0&&sent<4){INPUT releases[2]{inputs[2],inputs[3]};SendInput(2,releases,sizeof(INPUT));}return sent==4;}
std::wstring controlName(HWND owner,long id){win::ComPtr<IAccessible> access;if(FAILED(AccessibleObjectFromWindow(owner,static_cast<DWORD>(OBJID_CLIENT),IID_PPV_ARGS(&access))))return {};VARIANT child{};child.vt=VT_I4;child.lVal=id;BSTR text{};if(FAILED(access->get_accName(child,&text))||!text)return {};std::wstring result=text;SysFreeString(text);return result;}
HWND overview(DWORD root){struct Scan{DWORD root;HWND result=nullptr;} scan{root};EnumWindows([](HWND h,LPARAM p)->BOOL{auto& s=*reinterpret_cast<Scan*>(p);DWORD pid=0;GetWindowThreadProcessId(h,&pid);if(pid==s.root&&win::windowText(h)==L"Aether Activities"){s.result=h;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&scan));return scan.result;}
bool windowsMode(HWND h){win::ComPtr<IAccessible> access;if(FAILED(AccessibleObjectFromWindow(h,static_cast<DWORD>(OBJID_CLIENT),IID_PPV_ARGS(&access))))return false;VARIANT child{};child.vt=VT_I4;child.lVal=1;BSTR value{};if(FAILED(access->get_accName(child,&value))||!value)return false;bool ok=std::wstring_view(value)==L"Search open windows";SysFreeString(value);return ok;}
bool snapshot(HWND h,const std::filesystem::path& path){
 if(GetForegroundWindow()!=h)return false;RECT r{};GetWindowRect(h,&r);const int width=r.right-r.left,height=r.bottom-r.top;
 HDC screen=GetDC(nullptr),memory=CreateCompatibleDC(screen);HBITMAP bitmap=CreateCompatibleBitmap(screen,width,height);auto old=SelectObject(memory,bitmap);
 const bool captured=BitBlt(memory,0,0,width,height,screen,r.left,r.top,SRCCOPY|CAPTUREBLT)!=FALSE;SelectObject(memory,old);DeleteDC(memory);ReleaseDC(nullptr,screen);
 win::ComPtr<IWICImagingFactory> factory;win::ComPtr<IWICBitmap> image;win::ComPtr<IWICStream> stream;win::ComPtr<IWICBitmapEncoder> encoder;win::ComPtr<IWICBitmapFrameEncode> frame;WICPixelFormatGUID format=GUID_WICPixelFormat24bppBGR;
 bool ok=captured&&SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)))&&SUCCEEDED(factory->CreateBitmapFromHBITMAP(bitmap,nullptr,WICBitmapIgnoreAlpha,&image))&&SUCCEEDED(factory->CreateStream(&stream))&&SUCCEEDED(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE))&&SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder))&&SUCCEEDED(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache))&&SUCCEEDED(encoder->CreateNewFrame(&frame,nullptr))&&SUCCEEDED(frame->Initialize(nullptr))&&SUCCEEDED(frame->SetSize(static_cast<UINT>(width),static_cast<UINT>(height)))&&SUCCEEDED(frame->SetPixelFormat(&format))&&SUCCEEDED(frame->WriteSource(image.Get(),nullptr))&&SUCCEEDED(frame->Commit())&&SUCCEEDED(encoder->Commit());DeleteObject(bitmap);return ok;
}
}
int main(int argc,char** argv){
 SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
 win::ComApartment com;HWND controller=FindWindowW(L"Aether.Controller.v3",L"Aether UI Controller");DWORD root=0;GetWindowThreadProcessId(controller,&root);
 win::Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,root));wchar_t image[32768]{};DWORD size=32768;
 if(!process||!QueryFullProcessImageNameW(process.value,0,image,&size)||std::filesystem::path(image)!=win::executablePath().parent_path()/L"AetherUI.exe"||reinterpret_cast<ULONG_PTR>(GetPropW(controller,L"Aether.LifecyclePhase"))!=1){std::cerr<<"FAIL expected ready staged Aether root\n";return 1;}
 const bool showOnly=argc==3&&std::string_view(argv[1])=="--show-only";
 if(argc==2&&std::string_view(argv[1])=="--close-only"){HWND activity=overview(root);if(activity&&IsWindowVisible(activity)){PostMessageW(activity,WM_CLOSE,0,0);pump(800);}return activity&&IsWindowVisible(activity)?1:0;}
 for(int k:{VK_CONTROL,VK_SHIFT,VK_MENU,VK_LWIN,VK_RWIN})if(GetAsyncKeyState(k)&0x8000){std::cerr<<"FAIL external modifier held\n";return 1;}
 WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"Aether.WindowsKey.AppFixture";RegisterClassW(&wc);
 HWND fixture=CreateWindowW(wc.lpszClassName,L"Aether Windows-key app test",WS_OVERLAPPEDWINDOW|WS_VISIBLE,80,80,520,180,nullptr,nullptr,wc.hInstance,nullptr);if(!fixture)return 1;
 ShowWindow(fixture,SW_SHOW);SetForegroundWindow(fixture);pump();int failures=0;auto check=[&](bool ok,const char* name){std::cout<<(ok?"PASS ":"FAIL ")<<name<<'\n';if(!ok)++failures;return ok;};
 if(check(GetForegroundWindow()==fixture,"owned input fixture acquired")){
  for(WORD win:std::array<WORD,2>{VK_LWIN,VK_RWIN}){
   SetForegroundWindow(fixture);pump();if(!check(key(fixture,win),"Windows tap delivered to owned fixture"))break;pump(800);HWND activity=overview(root);
   if(!check(activity&&IsWindowVisible(activity)&&GetForegroundWindow()==activity&&windowsMode(activity),"real Aether opens focused Open windows overview"))break;
   MONITORINFO monitor{sizeof(monitor)};RECT bounds{};GetMonitorInfoW(MonitorFromWindow(activity,MONITOR_DEFAULTTONEAREST),&monitor);GetWindowRect(activity,&bounds);check(EqualRect(&bounds,&monitor.rcMonitor)!=FALSE,"overview fills exact physical monitor bounds");
   check(GetPropW(activity,L"Aether.Overview.Animating")==nullptr,"opening animation settles without a persistent timer");
   auto frames=GetPropW(activity,L"Aether.Overview.Frames");pump(120);check(GetPropW(activity,L"Aether.Overview.Frames")==frames,"settled actual overview has no idle animation frames");
   if(showOnly){ShowWindow(fixture,SW_HIDE);pump(400);check(snapshot(activity,win::wide(argv[2])),"owned overview visual capture saved");DestroyWindow(fixture);return failures?1:0;}
   if(argc==2&&win==VK_LWIN)check(snapshot(activity,win::wide(argv[1])),"owned overview visual capture saved");
   const bool groupedBefore=GetPropW(activity,L"Aether.Overview.Grouped")!=nullptr;
   if(!check(chord(activity,'G'),"Ctrl+G delivered to owned overview"))break;pump(400);check((GetPropW(activity,L"Aether.Overview.Grouped")!=nullptr)!=groupedBefore,"Ctrl+G changes grouping");
   if(!check(chord(activity,'G'),"grouping reset delivered"))break;pump(400);check((GetPropW(activity,L"Aether.Overview.Grouped")!=nullptr)==groupedBefore,"grouping preference restored");
   const auto monitorBefore=controlName(activity,20);if(!check(chord(activity,'M'),"Ctrl+M delivered to owned overview"))break;pump(400);check(!monitorBefore.empty()&&controlName(activity,20)!=monitorBefore,"Ctrl+M changes monitor filter");
   bool filtersReset=true;for(std::size_t i=0;i<win::monitors().size();++i){filtersReset=chord(activity,'M')&&filtersReset;pump(350);}check(filtersReset&&controlName(activity,20)==monitorBefore,"monitor filter restored after complete cycle");
   check(key(activity,VK_RIGHT),"empty overview accepts arrow navigation");pump();
   if(!check(key(activity,win),"second Windows tap delivered to owned overview"))break;pump(500);check(!IsWindowVisible(activity),"second Windows tap closes real Activities");
  }
  SetForegroundWindow(fixture);pump();if(check(key(fixture,VK_LWIN),"Escape-cycle Windows tap delivered")){pump(800);HWND activity=overview(root);if(check(activity&&IsWindowVisible(activity)&&GetForegroundWindow()==activity,"Escape-cycle overview owns input")){check(key(activity,VK_ESCAPE),"Escape delivered only to owned Activities");pump(500);check(!IsWindowVisible(activity),"Escape closes real Activities");}}
 }
 HWND activity=overview(root);if(activity&&IsWindowVisible(activity)&&GetForegroundWindow()==activity){key(activity,VK_ESCAPE);pump();}
 for(int k:{VK_CONTROL,VK_SHIFT,VK_MENU,VK_LWIN,VK_RWIN})check(!(GetAsyncKeyState(k)&0x8000),"no modifier left held");
 check(WaitForSingleObject(process.value,0)==WAIT_TIMEOUT,"same real Aether process remains live");DestroyWindow(fixture);return failures?1:0;
}
