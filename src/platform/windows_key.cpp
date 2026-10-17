#include "windows_key.hpp"
namespace aether::win {
WindowsKeyHook* WindowsKeyHook::active_=nullptr;
namespace {
constexpr ULONG_PTR ReplayTag=0x4145544857494E;
bool modified() noexcept {
 for(int key:{VK_CONTROL,VK_MENU,VK_SHIFT,VK_LBUTTON,VK_RBUTTON,VK_MBUTTON,VK_XBUTTON1,VK_XBUTTON2})if(GetAsyncKeyState(key)&0x8000)return true;
 return false;
}
}
bool WindowsKeyHook::start(HWND receiver,UINT message,std::function<bool()> enabled){
 stop();if(active_)return false;receiver_=receiver;message_=message;enabled_=std::move(enabled);active_=this;
 keyboard_=SetWindowsHookExW(WH_KEYBOARD_LL,keyboard,GetModuleHandleW(nullptr),0);
 mouse_=SetWindowsHookExW(WH_MOUSE_LL,mouse,GetModuleHandleW(nullptr),0);
 if(!keyboard_||!mouse_){stop();return false;}return true;
}
void WindowsKeyHook::stop() noexcept {
 if(keyboard_)UnhookWindowsHookEx(keyboard_);if(mouse_)UnhookWindowsHookEx(mouse_);
 keyboard_=mouse_=nullptr;if(active_==this)active_=nullptr;receiver_=nullptr;enabled_={};tap_.reset();
}
LRESULT CALLBACK WindowsKeyHook::keyboard(int code,WPARAM message,LPARAM data){
 auto* self=active_;if(code!=HC_ACTION||!self)return CallNextHookEx(nullptr,code,message,data);
 const auto& key=*reinterpret_cast<const KBDLLHOOKSTRUCT*>(data);
 if(key.dwExtraInfo==ReplayTag)return CallNextHookEx(nullptr,code,message,data);
 if(!self->enabled_||!self->enabled_()){self->tap_.reset();return CallNextHookEx(nullptr,code,message,data);}
 const bool down=message==WM_KEYDOWN||message==WM_SYSKEYDOWN;
 if(self->tap_.key(key.vkCode,down,modified())){
  // Deliver the real Win down unchanged, preserving all native chords. For a
  // bare release only, finish with Ctrl-down / Win-up / Ctrl-up so Explorer
  // observes a chord rather than opening Start. Tagged replay cannot recurse.
  INPUT replay[3]{};for(auto& input:replay){input.type=INPUT_KEYBOARD;input.ki.dwExtraInfo=ReplayTag;}
  replay[0].ki.wVk=VK_LCONTROL;
  replay[1].ki.wVk=static_cast<WORD>(key.vkCode);replay[1].ki.dwFlags=KEYEVENTF_KEYUP|KEYEVENTF_EXTENDEDKEY;
  replay[2].ki.wVk=VK_LCONTROL;replay[2].ki.dwFlags=KEYEVENTF_KEYUP;
  const UINT sent=SendInput(3,replay,sizeof(INPUT));
  if(sent==3){PostMessageW(self->receiver_,self->message_,0,0);return 1;}
  // Input restrictions fail open. If a partial batch was accepted, release
  // our synthetic modifier; the original physical Win release still passes.
  if(sent)SendInput(1,&replay[2],sizeof(INPUT));
 }
 return CallNextHookEx(nullptr,code,message,data);
}
LRESULT CALLBACK WindowsKeyHook::mouse(int code,WPARAM message,LPARAM data){
 if(code==HC_ACTION&&active_&&message!=WM_MOUSEMOVE)active_->tap_.chord();
 return CallNextHookEx(nullptr,code,message,data);
}
}
