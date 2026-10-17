#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <oleauto.h>
#include <exdisp.h>
#include <shlobj.h>
#include <shldisp.h>
#include <servprov.h>
#include <wrl/client.h>
#include <filesystem>
#include <array>
#include <iostream>
#include <string>
// Local validation helper. Uses the existing desktop automation object as in:
// https://learn.microsoft.com/en-us/windows/win32/shell/samples-execinexplorer
// It only submits an explicit launch request. Taskbar acceptance must still
// independently prove that its detached sentinel is outside every job.
namespace {
using Microsoft::WRL::ComPtr;
namespace fs=std::filesystem;
struct Apartment {
 HRESULT result=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED|COINIT_DISABLE_OLE1DDE);
 ~Apartment(){if(SUCCEEDED(result))CoUninitialize();}
};
struct Variant {
 VARIANT value{};
 Variant(){VariantInit(&value);}
 explicit Variant(const std::wstring& text):Variant(){value.vt=VT_BSTR;value.bstrVal=SysAllocStringLen(text.data(),static_cast<UINT>(text.size()));}
 ~Variant(){VariantClear(&value);}
 Variant(const Variant&)=delete;Variant& operator=(const Variant&)=delete;
 bool allocated()const{return value.vt!=VT_BSTR||value.bstrVal!=nullptr;}
};
bool samePath(const fs::path& first,const fs::path& second){return _wcsicmp(first.c_str(),second.c_str())==0;}
bool within(const fs::path& path,const fs::path& root){
 auto component=path.begin();
 for(const auto& required:root){
  if(component==path.end()||!samePath(*component,required))return false;
  ++component;
 }
 return true;
}
bool ordinaryComponents(const fs::path& absolute){
 if(!absolute.is_absolute())return false;
 auto prefix=absolute.root_path();
 for(const auto& part:absolute.relative_path()){
  prefix/=part;const auto attributes=GetFileAttributesW(prefix.c_str());
  if(attributes==INVALID_FILE_ATTRIBUTES||(attributes&FILE_ATTRIBUTE_REPARSE_POINT))return false;
 }
 return true;
}
fs::path canonicalOrdinary(const fs::path& absolute){
 if(!ordinaryComponents(absolute))return {};
 std::error_code error;auto result=fs::canonical(absolute,error);
 return !error&&ordinaryComponents(result)?result:fs::path{};
}
fs::path modulePath(){
 std::array<wchar_t,32768> path{};const auto count=GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));
 return count&&count<path.size()?fs::path(std::wstring(path.data(),count)):fs::path{};
}
fs::path stagedRoot(){
 auto directory=canonicalOrdinary(modulePath()).parent_path();
 while(!directory.empty()){
  const auto handoff=GetFileAttributesW((directory/L"CODEX_HANDOFF.md").c_str());
  if(samePath(directory.filename(),fs::path(L"AetherUI"))
   &&samePath(directory.parent_path().filename(),fs::path(L"AetherUI-v03"))
   &&handoff!=INVALID_FILE_ATTRIBUTES&&!(handoff&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)))return directory;
  const auto parent=directory.parent_path();if(parent==directory)break;directory=parent;
 }
 return {};
}
fs::path systemCommand(){
 std::array<wchar_t,32768> directory{};const auto count=GetSystemDirectoryW(directory.data(),static_cast<UINT>(directory.size()));
 return count&&count<directory.size()?canonicalOrdinary(fs::path(std::wstring(directory.data(),count))/L"cmd.exe"):fs::path{};
}
int failed(const wchar_t* stage,HRESULT result){
 std::wcerr<<L"Desktop launch failed at "<<stage<<L"; HRESULT=0x"<<std::hex<<static_cast<unsigned long>(result)<<std::dec<<std::endl;return 4;
}
bool genuineDesktop(HWND window,DWORD& pid){
 pid=0;if(!window||!GetWindowThreadProcessId(window,&pid)||!pid)return false;
 const auto process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid);
 if(!process)return false;
 std::array<wchar_t,32768> actual{},directory{};DWORD length=static_cast<DWORD>(actual.size());
 const auto windowsCount=GetWindowsDirectoryW(directory.data(),static_cast<UINT>(directory.size()));
 const bool queried=QueryFullProcessImageNameW(process,0,actual.data(),&length)!=FALSE;
 const bool alive=WaitForSingleObject(process,0)==WAIT_TIMEOUT;CloseHandle(process);
 return queried&&alive&&windowsCount&&windowsCount<directory.size()
  &&samePath(fs::path(std::wstring(actual.data(),length)),fs::path(std::wstring(directory.data(),windowsCount))/L"explorer.exe");
}
int submit(const fs::path& binary,const std::wstring& arguments,const fs::path& directory){
 Apartment apartment;if(FAILED(apartment.result))return failed(L"COM apartment initialization",apartment.result);
 ComPtr<IShellWindows> windows;
 auto result=CoCreateInstance(CLSID_ShellWindows,nullptr,CLSCTX_LOCAL_SERVER,IID_PPV_ARGS(windows.GetAddressOf()));
 if(FAILED(result))return failed(L"existing shell-window collection",result);
 Variant desktop,empty;desktop.value.vt=VT_I4;desktop.value.lVal=CSIDL_DESKTOP;long legacyWindow=0;
 ComPtr<IDispatch> desktopDispatch;
 result=windows->FindWindowSW(&desktop.value,&empty.value,SWC_DESKTOP,&legacyWindow,SWFO_NEEDDISPATCH,desktopDispatch.GetAddressOf());
 if(FAILED(result)||!desktopDispatch)return failed(L"existing desktop lookup",FAILED(result)?result:E_NOINTERFACE);
 ComPtr<IServiceProvider> services;result=desktopDispatch.As(&services);
 if(FAILED(result))return failed(L"desktop service provider",result);
 ComPtr<IShellBrowser> browser;
 result=services->QueryService(SID_STopLevelBrowser,IID_PPV_ARGS(browser.GetAddressOf()));
 if(FAILED(result))return failed(L"existing desktop browser",result);
 ComPtr<IShellView> view;result=browser->QueryActiveShellView(view.GetAddressOf());
 if(FAILED(result)||!view)return failed(L"existing desktop view",FAILED(result)?result:E_NOINTERFACE);
 HWND window=nullptr;result=view->GetWindow(&window);DWORD explorerPid=0;
 if(FAILED(result)||!genuineDesktop(window,explorerPid))return failed(L"desktop Explorer identity",FAILED(result)?result:E_ACCESSDENIED);
 ComPtr<IDispatch> viewDispatch;
 result=view->GetItemObject(SVGIO_BACKGROUND,IID_PPV_ARGS(viewDispatch.GetAddressOf()));
 if(FAILED(result))return failed(L"desktop background automation",result);
 ComPtr<IShellFolderViewDual> folderView;result=viewDispatch.As(&folderView);
 if(FAILED(result))return failed(L"desktop folder automation",result);
 ComPtr<IDispatch> application;result=folderView->get_Application(application.GetAddressOf());
 if(FAILED(result))return failed(L"existing desktop application",result);
 ComPtr<IShellDispatch2> shell;result=application.As(&shell);
 if(FAILED(result))return failed(L"desktop launch interface",result);
 Variant file(binary.native()),parameters(arguments),workingDirectory(directory.native()),verb(std::wstring(L"open")),show;
 show.value.vt=VT_I4;show.value.lVal=SW_HIDE;
 if(!file.allocated()||!parameters.allocated()||!workingDirectory.allocated()||!verb.allocated())return failed(L"launch argument allocation",E_OUTOFMEMORY);
 result=shell->ShellExecute(file.value.bstrVal,parameters.value,workingDirectory.value,verb.value,show.value);
 if(FAILED(result))return failed(L"desktop ShellExecute request",result);
 std::wcout<<L"Existing desktop launch request accepted; Explorer PID="<<explorerPid<<L"; binary="<<binary.native()
  <<L". Actual acceptance must still verify independent recovery ownership."<<std::endl;
 return 0;
}
}
int wmain(int argc,wchar_t** argv){
 std::wstring binaryText,arguments,directoryText;bool hasBinary=false,hasArguments=false,hasDirectory=false;
 for(int i=1;i<argc;++i){
  const std::wstring option=argv[i];
  if(i+1==argc){std::wcerr<<L"Each launcher option requires an explicit value."<<std::endl;return 2;}
  if(option==L"--binary"&&!hasBinary){binaryText=argv[++i];hasBinary=true;}
  else if(option==L"--arguments"&&!hasArguments){arguments=argv[++i];hasArguments=true;}
  else if(option==L"--directory"&&!hasDirectory){directoryText=argv[++i];hasDirectory=true;}
  else{std::wcerr<<L"Unknown or repeated launcher option."<<std::endl;return 2;}
 }
 if(!hasBinary||!hasArguments||!hasDirectory||arguments.size()>32760){
  std::wcerr<<L"Usage: AetherDesktopLauncher --binary <absolute file> --arguments <explicit argument string> --directory <absolute staged directory>"<<std::endl;return 2;
 }
 try{
  const auto stage=stagedRoot(),binary=canonicalOrdinary(fs::path(binaryText)),directory=canonicalOrdinary(fs::path(directoryText));
  const auto binaryAttributes=binary.empty()?INVALID_FILE_ATTRIBUTES:GetFileAttributesW(binary.c_str());
  const auto directoryAttributes=directory.empty()?INVALID_FILE_ATTRIBUTES:GetFileAttributesW(directory.c_str());
  const auto command=systemCommand();
  if(stage.empty()||binary.empty()||directory.empty()||!within(directory,stage)
   ||binaryAttributes==INVALID_FILE_ATTRIBUTES||(binaryAttributes&FILE_ATTRIBUTE_DIRECTORY)
   ||directoryAttributes==INVALID_FILE_ATTRIBUTES||!(directoryAttributes&FILE_ATTRIBUTE_DIRECTORY)
   ||(!within(binary,stage)&&(command.empty()||!samePath(binary,command)))){
   std::wcerr<<L"Launcher admission rejected: require ordinary existing staged paths, or the actual System32 cmd.exe with a staged working directory."<<std::endl;return 3;
  }
  return submit(binary,arguments,directory);
 }catch(const std::exception& error){
  std::cerr<<"Launcher path admission failed: "<<error.what()<<std::endl;return 3;
 }
}