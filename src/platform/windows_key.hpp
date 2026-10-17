#pragma once
#include "windows.hpp"
#include <functional>
namespace aether::win {
// Tracks only modifier/chord state; never retains typed keys or text.
class WindowsTap {
 unsigned held_=0;bool chord_=false;
public:
 bool key(DWORD key,bool down,bool modified=false) noexcept {
  const unsigned bit=key==VK_LWIN?1u:key==VK_RWIN?2u:0u;
  if(!bit){if(down&&held_)chord_=true;return false;}
  if(down){if(!(held_&bit)){if(!held_)chord_=modified;else chord_=true;held_|=bit;}return false;}
  if(!(held_&bit))return false;
  held_&=~bit;const bool tap=!held_&&!chord_;if(!held_)chord_=false;return tap;
 }
 void chord() noexcept {if(held_)chord_=true;}
 void reset() noexcept {held_=0;chord_=false;}
};
class WindowsKeyHook {
 static WindowsKeyHook* active_;
 HHOOK keyboard_=nullptr,mouse_=nullptr;HWND receiver_=nullptr;UINT message_=0;
 WindowsTap tap_;std::function<bool()> enabled_;
 static LRESULT CALLBACK keyboard(int,WPARAM,LPARAM);
 static LRESULT CALLBACK mouse(int,WPARAM,LPARAM);
public:
 ~WindowsKeyHook(){stop();}
 bool start(HWND,UINT,std::function<bool()>);
 void stop() noexcept;
};
}
