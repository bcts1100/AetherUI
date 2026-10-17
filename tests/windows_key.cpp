#include "platform/windows_key.hpp"
#include <iostream>
using namespace aether::win;
int main(){
 int failures=0,checks=0;auto check=[&](bool value,const char* name){++checks;std::cout<<(value?"PASS ":"FAIL ")<<name<<'\n';if(!value)++failures;};
 for(DWORD key:{VK_LWIN,VK_RWIN}){
  WindowsTap tap;check(!tap.key(key,true),"Win down remains native");check(!tap.key(key,true),"repeat is not a toggle");check(tap.key(key,false),"bare Win release toggles");check(!tap.key(key,false),"orphan release does not toggle");
  const DWORD chords[]{'E','L',VK_TAB,VK_SHIFT,VK_CONTROL,VK_MENU,'1',VK_LEFT};
  for(DWORD chord:chords){tap.key(key,true);tap.key(chord,true);tap.key(chord,false);check(!tap.key(key,false),"Windows chord does not toggle");}
  tap.key(key,true,true);check(!tap.key(key,false),"pre-held modifier stays native");
  tap.key(key,true);tap.chord();check(!tap.key(key,false),"Windows mouse gesture stays native");
  tap.key(key,true);tap.reset();check(!tap.key(key,false),"pause/shutdown cancels pending tap");
 }
 WindowsTap both;both.key(VK_LWIN,true);both.key(VK_RWIN,true);check(!both.key(VK_LWIN,false)&&!both.key(VK_RWIN,false),"two Windows keys are a chord");
 std::cout<<checks-failures<<'/'<<checks<<" Windows-key state checks passed\n";return failures?1:0;
}
