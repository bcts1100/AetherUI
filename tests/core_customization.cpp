#include "core/model.hpp"
#include <iostream>
#include <stdexcept>
using namespace aether;
namespace {void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}bool token(std::string_view text,std::string_view value){return text.find(value)!=text.npos;}}
int main(){try{
 auto old=parseSettings("[theme]\nname=Legacy\naccent=123456\n[layout]\niconSize=56\n[pin:0]\nname=Tea\ntarget=C:%5CTea.exe\nidentity=tea\n");
 auto legacy=serializeSettings(old);check(old.theme.name=="Legacy"&&old.theme.accent==0x123456&&old.defaults.iconSize==56&&old.pins[0].identity=="tea","legacy settings changed identity or palette");
 check(preset(0).name=="Ubuntu"&&preset(1).name=="Midnight"&&preset(2).name=="Paper","legacy preset indices changed");
 check(preset(3).name=="Frosted"&&preset(4).name=="Minimal","new Frosted and Minimal presets are unavailable");
 auto modern=parseSettings("[theme]\nfontFamily=Calibri\nfontScale=125\nborderWidth=3\ngradientStrength=80\ndockHighlight=40\nstyle=frosted\nindicator=line\n[behavior]\nreduceMotion=1\n[layout]\nclock=0\nfocusedTitle=0\nstatus=0\n[pin:0]\nname=Renamed Tea\ntarget=C:%5CTea.exe\nidentity=tea\niconPath=C:%5CTea.png\niconRevision=42\n");
 auto text=serializeSettings(modern);
 for(auto value:{"fontFamily=Calibri\n","fontScale=125\n","borderWidth=3\n","gradientStrength=80\n","dockHighlight=40\n","style=frosted\n","indicator=line\n","reduceMotion=1\n","clock=0\n","focusedTitle=0\n","status=0\n","iconPath=C:\\Tea.png\n","iconRevision=42\n"})check(token(text,value),"new customization setting did not survive load/save");
 auto grouped=groupDock(modern.pins,{{"Tea window","C:\\Tea.exe","tea","monitor",true}},"monitor",true);
 check(grouped.size()==1&&grouped[0].tasks.size()==1&&grouped[0].app.name=="Renamed Tea"&&grouped[0].app.target=="C:\\Tea.exe"&&grouped[0].app.identity=="tea","decorative metadata changed launch/group identity");
 auto bounded=serializeSettings(parseSettings("[theme]\nfontFamily=Unsafe%0Afont\nfontScale=900\nborderWidth=-9\ngradientStrength=900\ndockHighlight=-2\nstyle=arbitrary\nindicator=arbitrary\n[pin:0]\nname=Bad icon\ntarget=C:%5CTea.exe\niconPath=%5C%5Cserver%5Cimage.png\n"));
 for(auto value:{"fontFamily=Segoe%20UI\n","fontScale=125\n","borderWidth=0\n","gradientStrength=100\n","dockHighlight=0\n","style=classic\n","indicator=dots\n","iconPath=\n"})check(token(bounded,value),"unsafe or out-of-range appearance data was not normalized");
 auto exported=serializeTheme(modern.theme);auto imported=parseTheme(exported);check(imported&&serializeTheme(*imported)==exported,"theme export lost appearance tokens");
 auto invalidTheme=exported+"\n[theme]\nstyle=unknown\n";check(!parseTheme(invalidTheme),"Imported theme accepts unknown style tokens");
 std::cout<<"PASS legacy profiles, style bounds, theme export and pin identity\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
