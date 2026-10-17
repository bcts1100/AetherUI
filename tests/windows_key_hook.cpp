#include "platform/windows_key.hpp"
#include <iostream>
using namespace aether::win;
namespace {
constexpr UINT Tap=WM_APP+91;int taps=0,chords=0;HWND inputOwner=nullptr;
LRESULT CALLBACK procedure(HWND h,UINT m,WPARAM w,LPARAM l){if(m==Tap){++taps;return 0;}if(m==WM_HOTKEY){++chords;return 0;}return DefWindowProcW(h,m,w,l);}
void pump(){const auto end=GetTickCount64()+250;do{MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}Sleep(5);}while(GetTickCount64()<end);}
struct Key {WORD code;bool down;};
bool keys(std::initializer_list<Key> sequence){if(GetForegroundWindow()!=inputOwner){SetForegroundWindow(inputOwner);pump();}if(GetForegroundWindow()!=inputOwner){std::cerr<<"DIAG input refused after focus loss foreground="<<GetForegroundWindow()<<" owned="<<inputOwner<<'\n';return false;}std::vector<INPUT> inputs;for(auto [key,down]:sequence){INPUT i{};i.type=INPUT_KEYBOARD;i.ki.wVk=key;i.ki.dwFlags=(down?0u:KEYEVENTF_KEYUP)|((key==VK_LWIN||key==VK_RWIN)?KEYEVENTF_EXTENDEDKEY:0u);inputs.push_back(i);}return SendInput(static_cast<UINT>(inputs.size()),inputs.data(),sizeof(INPUT))==inputs.size();}
}
int main(){
 for(int key:{VK_LWIN,VK_RWIN,VK_CONTROL,VK_SHIFT,VK_MENU})if(GetAsyncKeyState(key)&0x8000){std::cerr<<"FAIL input modifier already held\n";return 1;}
 WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=procedure;wc.lpszClassName=L"Aether.WindowsKey.OwnedFixture";RegisterClassW(&wc);
 HWND h=CreateWindowW(wc.lpszClassName,L"Aether keyboard test - click this window",WS_OVERLAPPEDWINDOW|WS_VISIBLE,80,80,520,180,nullptr,nullptr,wc.hInstance,nullptr);
 if(!h)return 1;inputOwner=h;int failures=0;auto check=[&](bool ok,const char* name){std::cout<<(ok?"PASS ":"FAIL ")<<name<<'\n';if(!ok)++failures;};
 bool eligible=true;WindowsKeyHook hook;const bool installed=hook.start(h,Tap,[&]{return eligible;});const auto hookError=GetLastError();
 const bool first=RegisterHotKey(h,1,MOD_WIN|MOD_NOREPEAT,VK_F8)!=FALSE;const auto firstError=GetLastError();
 const bool second=RegisterHotKey(h,2,MOD_WIN|MOD_SHIFT|MOD_NOREPEAT,VK_F8)!=FALSE;const auto secondError=GetLastError();
 SetForegroundWindow(h);pump();
 // Windows foreground locking may require a physical click. Never send
 // global test input until the user has granted this owned window focus.
 const auto focusDeadline=GetTickCount64()+40000;
 while(installed&&first&&second&&GetForegroundWindow()!=h&&GetTickCount64()<focusDeadline)pump();
 const bool ready=installed&&first&&second&&GetForegroundWindow()==h;
 if(!ready)std::cerr<<"DIAG installed="<<installed<<" hookError="<<hookError<<" first="<<first<<" firstError="<<firstError<<" second="<<second<<" secondError="<<secondError<<" foreground="<<GetForegroundWindow()<<" owned="<<h<<'\n';
 check(ready,"owned foreground hook/hotkey fixture acquired");
 if(ready){
  const WORD windowsKeys[]{VK_LWIN,VK_RWIN};for(WORD win:windowsKeys){
   int before=taps;check(keys({{win,true},{win,false}}),"bare key input accepted");pump();check(taps==before+1&&GetForegroundWindow()==h,"bare Win posts exactly one toggle without native Start");
   int previous=chords;before=taps;check(keys({{win,true},{VK_F8,true},{VK_F8,false},{win,false}}),"native chord input accepted");pump();check(chords==previous+1&&taps==before,"native Win chord reaches registered owner without toggle");
   previous=chords;check(keys({{VK_SHIFT,true},{win,true},{VK_F8,true},{VK_F8,false},{win,false},{VK_SHIFT,false}}),"modified chord input accepted");pump();check(chords==previous+1&&taps==before,"pre-held Shift and Win chord remains native");
  }
  eligible=false;const int before=taps,previous=chords;check(keys({{VK_LWIN,true},{VK_F8,true},{VK_F8,false},{VK_LWIN,false}}),"disabled-hook chord input accepted");pump();check(chords==previous+1&&taps==before,"disabled replacement passes native chord");
  for(int key:{VK_LWIN,VK_RWIN,VK_CONTROL,VK_SHIFT,VK_MENU})check(!(GetAsyncKeyState(key)&0x8000),"no modifier left held");
 }
 hook.stop();UnregisterHotKey(h,1);UnregisterHotKey(h,2);DestroyWindow(h);return failures?1:0;
}
