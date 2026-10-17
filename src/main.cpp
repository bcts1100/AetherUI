#include "app.hpp"
#include <commctrl.h>
int WINAPI wWinMain(HINSTANCE,HINSTANCE,LPWSTR,int){
 using namespace aether;
 // Supported on the minimum Windows 10 1809 target; no virtualized coordinates.
 SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
 win::ComApartment apartment;
 INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES|ICC_WIN95_CLASSES};InitCommonControlsEx(&controls);
 int count=0;LPWSTR* args=CommandLineToArgvW(GetCommandLineW(),&count);
 std::wstring command;bool explicitSettings=false;
 try{
  for(int i=1;args&&i<count;++i){
   if(wcscmp(args[i],L"--settings-dir")==0){if(++i>=count)throw std::runtime_error("--settings-dir requires an absolute directory.");win::setConfigDirectory(args[i]);explicitSettings=true;}
   else if(wcscmp(args[i],L"--quit")==0||wcscmp(args[i],L"--restore")==0)command=args[i];
   else throw std::runtime_error("Unknown argument. Supported: --settings-dir <absolute path>, --quit, --restore.");
  }
 }catch(const std::exception& e){if(args)LocalFree(args);win::message(nullptr,win::wide(e.what()),MB_OK|MB_ICONERROR);return 1;}
 if(args)LocalFree(args);
 HWND existing=FindWindowW(L"Aether.Controller.v3",L"Aether UI Controller");
 if(command==L"--quit"){if(existing)PostMessageW(existing,WM_CLOSE,0,0);return 0;}
 if(command==L"--restore"){if(existing)PostMessageW(existing,win::ApplyMessage,0,0);return 0;}
 win::Handle singleton(CreateMutexW(nullptr,FALSE,L"Local\\AetherUI.v3.SingleInstance"));
 if(!singleton){win::message(nullptr,L"Could not create the Aether instance lock.",MB_OK|MB_ICONERROR);return 1;}
 if(GetLastError()==ERROR_ALREADY_EXISTS){if(explicitSettings){win::message(nullptr,L"Aether UI is already running. Close it before starting this settings profile.",MB_OK|MB_ICONINFORMATION);return 2;}if(existing){DWORD pid=0;GetWindowThreadProcessId(existing,&pid);AllowSetForegroundWindow(pid);PostMessageW(existing,win::OpenEditorMessage,0,0);}return 0;}
 try{
  if(FAILED(apartment.result))throw std::runtime_error("Windows COM initialization failed.");
  App app;return app.run();
 }catch(const std::exception& e){win::log(e.what());win::message(nullptr,win::wide(e.what()),MB_OK|MB_ICONERROR);return 1;}
 catch(...){win::message(nullptr,L"Aether could not start. The recovery helper will restore any taskbar state it owns.",MB_OK|MB_ICONERROR);return 2;}
}
