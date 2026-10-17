#include "platform/config.hpp"
#include "platform/conpty.hpp"
#include "platform/appearance.hpp"
#include "core/integration.hpp"
#include "owned_capture.hpp"
#include <oleacc.h>
#include <functional>
#include <iostream>

using namespace aether::win;
namespace {
bool waitFor(const std::function<bool()>& predicate,int timeout=6000){
 const auto until=GetTickCount64()+timeout;
 do{MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}if(predicate())return true;Sleep(25);}while(GetTickCount64()<until);
 return false;
}
VARIANT child(long id){VARIANT v{};v.vt=VT_I4;v.lVal=id;return v;}
std::wstring value(IAccessible* a,long id){BSTR result=nullptr;std::wstring out;if(a&&SUCCEEDED(a->get_accValue(child(id),&result))&&result)out.assign(result,SysStringLen(result));SysFreeString(result);return out;}
long named(IAccessible* a,const wchar_t* label){ComPtr<IEnumVARIANT> e;if(!a||FAILED(a->QueryInterface(IID_PPV_ARGS(&e))))return 0;e->Reset();VARIANT v{};ULONG got=0;while(e->Next(1,&v,&got)==S_OK){BSTR name=nullptr;a->get_accName(v,&name);const bool match=name&&wcscmp(name,label)==0;SysFreeString(name);const long id=v.lVal;VariantClear(&v);if(match)return id;}return 0;}
struct OwnedTool {
 Handle process,job;DWORD pid{};HWND window{};bool terminal{};
 bool start(const std::filesystem::path& profile,const std::filesystem::path& directory,bool term,const wchar_t* shell=L"cmd"){
  terminal=term;auto exe=executablePath().parent_path()/L"AetherTools.exe";
  auto command=quoteArgument(exe.wstring())+(term?L" --terminal --shell "+quoteArgument(shell):L" --files")+L" --settings-dir "+quoteArgument(profile.wstring())+L" --directory "+quoteArgument(directory.wstring());
  STARTUPINFOW si{sizeof(si)};PROCESS_INFORMATION p{};if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED,nullptr,nullptr,&si,&p))return false;
  process.reset(p.hProcess);Handle thread(p.hThread);pid=p.dwProcessId;job.reset(CreateJobObjectW(nullptr,nullptr));JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if(!job||!SetInformationJobObject(job.value,JobObjectExtendedLimitInformation,&limits,sizeof(limits))||!AssignProcessToJobObject(job.value,process.value)){TerminateProcess(process.value,1);return false;}
  ResumeThread(thread.value);
  return waitFor([&]{EnumWindows([](HWND h,LPARAM data)->BOOL{auto& self=*reinterpret_cast<OwnedTool*>(data);DWORD p=0;GetWindowThreadProcessId(h,&p);if(p==self.pid&&GetPropW(h,L"Aether.Tools.Kind")==reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(self.terminal?1:2))){self.window=h;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(this));return window&&IsWindowVisible(window);});
 }
 ComPtr<IAccessible> accessible(){ComPtr<IAccessible> a;AccessibleObjectFromWindow(window,static_cast<DWORD>(OBJID_CLIENT),IID_IAccessible,reinterpret_cast<void**>(a.GetAddressOf()));return a;}
 void type(std::wstring_view text){for(auto c:text){DWORD_PTR ignored=0;SendMessageTimeoutW(window,WM_CHAR,c,0,SMTO_ABORTIFHUNG,500,&ignored);}}
 bool close(){if(!process)return true;if(terminal){type(L"exit\r");const auto shell=static_cast<DWORD>(reinterpret_cast<ULONG_PTR>(GetPropW(window,L"Aether.Terminal.ChildPid")));Handle h(OpenProcess(SYNCHRONIZE,FALSE,shell));if(h)WaitForSingleObject(h.value,4000);}PostMessageW(window,WM_CLOSE,0,0);return waitFor([&]{return WaitForSingleObject(process.value,0)==WAIT_OBJECT_0;});}
 ~OwnedTool(){if(process&&WaitForSingleObject(process.value,0)==WAIT_TIMEOUT){if(window)PostMessageW(window,WM_CLOSE,0,0);if(WaitForSingleObject(process.value,1000)!=WAIT_OBJECT_0&&job)TerminateJobObject(job.value,1);}}
};
}
int main(int argc,char** argv){
 std::cout<<std::unitbuf;ComApartment com;SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);int checks=0;
 auto check=[&](bool ok,const char* name){if(!ok)throw std::runtime_error(name);std::cout<<"PASS "<<name<<'\n';++checks;};
 try{
  const bool capture=argc>1&&std::string_view(argv[1])=="--capture";
  auto root=executablePath().parent_path()/L"test-profiles"/(L"tools-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
  auto folder=root/L"files",profile=root/L"profile";std::filesystem::create_directories(folder/L"folder");std::filesystem::create_directories(profile);writeAtomic(folder/L"example.txt","Aether Files fixture");
  OwnedTool terminal;check(terminal.start(profile,folder,true),"real terminal window opens");
  check(waitFor([&]{return GetPropW(terminal.window,L"Aether.Terminal.ChildPid")!=nullptr;}),"terminal owns a real shell");
  terminal.type(L"echo AETHER_UI_&echo ACTUAL_EXECUTION\r");
  check(waitFor([&]{auto a=terminal.accessible();return value(a.Get(),1000).find(L"AETHER_UI_\nACTUAL_EXECUTION")!=std::wstring::npos;}),"native terminal renders actual executed output through accessibility");
  if(capture){terminal.type(L"prompt aether$G&cls\r");Sleep(150);terminal.type(L"echo Aether Terminal - real CMD session\r");Sleep(100);ShowWindow(terminal.window,SW_RESTORE);SetForegroundWindow(terminal.window);Sleep(250);check(aether::test::captureOwnedClient(terminal.window,root/L"terminal.png"),"owned terminal pixels captured");}
  SendMessageW(terminal.window,WM_KEYDOWN,VK_F6,0);const auto dpi=GetDpiForWindow(terminal.window);const auto point=MAKELPARAM(MulDiv(100,dpi,96),MulDiv(100,dpi,96));SendMessageW(terminal.window,WM_LBUTTONDOWN,MK_LBUTTON,point);SendMessageW(terminal.window,WM_LBUTTONUP,0,point);
  SetWindowPos(terminal.window,nullptr,0,0,900,600,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);terminal.type(L"echo RESIZED_EXECUTION\r");
  check(waitFor([&]{auto a=terminal.accessible();return value(a.Get(),1000).find(L"RESIZED_EXECUTION")!=std::wstring::npos;}),"terminal continues rendering after resize");
  terminal.type(L"(echo FIRST_LINE&echo SECOND_LINE)>terminal-check.txt&type terminal-check.txt\r");
  check(waitFor([&]{return readText(folder/L"terminal-check.txt").find("SECOND_LINE")!=std::string::npos;}),"shell redirection and local file commands execute normally");
  for(int design=1;design<=10;++design){
   aether::IntegrationOptions options;options.terminalDesign=design;options.terminalBackground=true;options.terminalOpacity=75;options.terminalCursor=design%3;
   writeAtomic(profile/L"integration-v1.ini",aether::serializeIntegration(options));PostMessageW(terminal.window,toolsThemeMessage(),0,0);
   check(waitFor([&]{return reinterpret_cast<ULONG_PTR>(GetPropW(terminal.window,L"Aether.Terminal.Design"))==static_cast<ULONG_PTR>(design+1);}),"stock design applies to the live terminal");
   if(design==9)check(waitFor([&]{return GetPropW(terminal.window,L"Aether.Terminal.BackgroundReady")!=nullptr;},6000),"Zen image is decoded and submitted for native painting");
   terminal.type(L"echo DESIGN_"+std::to_wstring(design)+L"_RUNNING\r");
   check(waitFor([&]{auto a=terminal.accessible();return value(a.Get(),1000).find(L"DESIGN_"+std::to_wstring(design)+L"_RUNNING")!=std::wstring::npos;}),"theme switching preserves the running shell");
   if(capture){terminal.type(L"prompt aether$G&cls\r");Sleep(150);terminal.type(L"echo Aether Terminal - real CMD session\r");Sleep(100);ShowWindow(terminal.window,SW_RESTORE);SetForegroundWindow(terminal.window);Sleep(150);check(aether::test::captureOwnedClient(terminal.window,root/(L"terminal-design-"+std::to_wstring(design)+L".png")),"stock design pixels captured");}
  }
  check(terminal.close(),"terminal normal close retires owned shell");
  OwnedTool powershell;check(powershell.start(profile,folder,true,L"powershell"),"PowerShell opens in a real native terminal");
  powershell.type(L"$n=2+3; Set-Content -LiteralPath './powershell-check.txt' -Value ('RESULT_'+$n)\r");
  check(waitFor([&]{return readText(folder/L"powershell-check.txt").find("RESULT_5")!=std::string::npos;},12000),"PowerShell evaluates commands and writes real files");
  powershell.type(L"Start-Sleep -Seconds 30\r");Sleep(300);powershell.type(std::wstring(1,3));Sleep(100);
  powershell.type(L"Set-Content -LiteralPath './interrupt-check.txt' -Value 'INTERRUPTED'\r");
  check(waitFor([&]{return readText(folder/L"interrupt-check.txt").find("INTERRUPTED")!=std::string::npos;},8000),"Ctrl+C interrupts the foreground PowerShell command");
  check(powershell.close(),"PowerShell closes normally and retires its owned processes");
  std::filesystem::remove(folder/L"terminal-check.txt");std::filesystem::remove(folder/L"powershell-check.txt");std::filesystem::remove(folder/L"interrupt-check.txt");
  OwnedTool files;check(files.start(profile,folder,false),"real file browser opens");check(waitFor([&]{return reinterpret_cast<ULONG_PTR>(GetPropW(files.window,L"Aether.Files.ItemCount"))==2;}),"file browser finishes isolated local listing");
  check(waitFor([&]{auto a=files.accessible();return named(a.Get(),L"folder")&&named(a.Get(),L"example.txt");}),"real file rows are accessible");
  auto a=files.accessible();auto grid=named(a.Get(),L"Grid view");check(grid&&SUCCEEDED(a->accDoDefaultAction(child(grid))),"grid view can be invoked through native accessibility");
  check(waitFor([&]{auto b=files.accessible();return named(b.Get(),L"List view")!=0;}),"file grid state updates");
  a=files.accessible();auto directory=named(a.Get(),L"folder");check(directory&&SUCCEEDED(a->accDoDefaultAction(child(directory))),"owned child folder opens");check(waitFor([&]{return windowText(files.window).ends_with(L"folder")&&GetPropW(files.window,L"Aether.Files.ItemCount")==nullptr;}),"folder navigation completes with empty local contents");
  a=files.accessible();auto back=named(a.Get(),L"Back");check(back&&SUCCEEDED(a->accDoDefaultAction(child(back))),"Back navigation is invokable");check(waitFor([&]{return reinterpret_cast<ULONG_PTR>(GetPropW(files.window,L"Aether.Files.ItemCount"))==2;}),"Back restores the original listing");
  if(capture){ShowWindow(files.window,SW_RESTORE);SetForegroundWindow(files.window);Sleep(250);check(aether::test::captureOwnedClient(files.window,root/L"files.png"),"owned file browser pixels captured");}
  check(files.close(),"file browser normal close retires worker");std::cout<<checks<<" native tools checks passed\nEvidence: "<<utf8(root.wstring())<<'\n';return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
