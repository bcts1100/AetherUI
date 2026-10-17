#include "core/model.hpp"
#include "core/tiling.hpp"
#include <iostream>
#include <stdexcept>
using namespace aether;
namespace {
int checks=0;
void check(bool value,const char* label){++checks;if(!value)throw std::runtime_error(label);}
}
int main(){try{
 Config old=parseSettings("[AetherSettings]\nversion=3\n[layout]\niconSize=64\n");
 check(!old.tilingDefaults.enabled&&old.tilingForDisplay("unknown").layoutId=="balanced","old settings must default auto tiling off");
 auto zone=zoneRect({-1920,40,0,1080},{0,0,5000,10000},8,144,100,100);
 check(zone&&zone->left==-1908&&zone->top==52&&zone->right==-972&&zone->bottom==1068,"normalized negative origin DPI geometry");
 check(!zoneRect({0,0,100,80},{0,0,10000,10000},8,96,100,80),"min size refuses impossible zone");
 check(!zoneRect({0,0,1920,1080},{-1,0,10000,10000},0,96),"invalid normalized zone rejected");
 SnapLayout custom{"custom-1","Three zones",{{0,0,5000,10000},{5000,0,10000,5000},{5000,5000,10000,10000}},12};
 check(validSnapLayout(custom),"valid custom snap layout");
 auto overlapping=custom;overlapping.zones.push_back({0,0,10000,10000});check(!validSnapLayout(overlapping),"overlap rejected");
 Config c;c.snapLayouts.push_back(custom);c.tilingDefaults={true,"custom-1"};c.tilingDisplays["portrait"]={false,"master"};
 auto round=parseSettings(serializeSettings(c));
 check(round.snapLayouts==c.snapLayouts&&round.tilingDefaults==c.tilingDefaults&&round.tilingDisplays==c.tilingDisplays,"tiling settings roundtrip");
 auto malformed=serializeSettings(c);auto badZone=malformed.find("layout0.zone0=0,0,5000,10000");check(badZone!=malformed.npos,"serialized custom zone present");malformed.replace(badZone,std::string("layout0.zone0=0,0,5000,10000").size(),"layout0.zone0=0,0,0,10000");auto refused=parseSettings(malformed);check(!refused.tilingDefaults.enabled&&refused.snapLayouts.empty(),"malformed custom geometry fails closed");
 auto unknown=parseSettings(serializeSettings(c)+"unsafeCommand=run\n");check(!unknown.tilingDefaults.enabled&&unknown.snapLayouts.empty(),"unknown tiling data fails closed");
 auto duplicate=parseSettings(serializeSettings(c)+"\n[tiling]\nenabled=1\n");check(!duplicate.tilingDefaults.enabled,"duplicate tiling sections fail closed");
 auto invalidName=custom;invalidName.name="bad\nname";check(!validSnapLayout(invalidName),"control characters rejected in custom names");invalidName.name=std::string("\xC0\x80",2);check(!validSnapLayout(invalidName),"invalid UTF8 name rejected");
 auto reserved=custom;reserved.id="balanced";check(!validSnapLayout(reserved),"builtin IDs reserved");reserved.id="../bad";check(!validSnapLayout(reserved),"path IDs rejected");
 Config bounded;for(int n=0;n<16;++n){SnapLayout layout{"custom-"+std::to_string(n),"Bounded custom",{},0};for(int z=0;z<12;++z)layout.zones.push_back({z*10000/12,0,(z+1)*10000/12,10000});bounded.snapLayouts.push_back(layout);}for(int n=0;n<32;++n)bounded.tilingDisplays["display"+std::to_string(n)]={false,"custom-0"};auto all=parseSettings(serializeSettings(bounded));check(all.snapLayouts==bounded.snapLayouts&&all.tilingDisplays==bounded.tilingDisplays,"maximum bounded tiling config roundtrip");
 bounded.tilingDisplays["overflow"]={true,"balanced"};check(!validTilingConfig(bounded)&&!parseSettings(serializeSettings(bounded)).tilingDefaults.enabled,"over-limit profile fails closed");
 check(round.tilingForDisplay("portrait").enabled==false&&round.tilingForDisplay("another").enabled,"display policy inheritance");
 auto broken=parseSettings("[tiling]\nenabled=1\nlayoutId=missing\n");check(!broken.tilingDefaults.enabled&&broken.tilingDefaults.layoutId=="balanced","missing custom layout fails closed");
 for(auto work:std::vector<Rect>{{-1080,-1920,0,0},{0,40,1920,1040},{100,100,700,900}}){
  for(int count=1;count<=12;++count){auto grid=tileRects(work,count,*findSnapLayout(Config{},"balanced"),192,10,10);
   check(grid.size()==static_cast<std::size_t>(count),"balanced grid count");for(auto r:grid)check(r.left>=work.left&&r.top>=work.top&&r.right<=work.right&&r.bottom<=work.bottom&&r.width()>0&&r.height()>0,"tiles inside work area");
  }
 }
 std::vector<Rect> monitors{{-1920,0,0,1080},{0,0,1920,1080},{0,-1920,1080,0}};
 check(adjacentMonitorIndex(monitors,1,MoveDirection::Left)==0,"directional negative-origin neighbor");
 check(adjacentMonitorIndex(monitors,1,MoveDirection::Up)==2,"portrait monitor above");
 check(!adjacentMonitorIndex(monitors,0,MoveDirection::Left),"missing directional neighbor");
 auto moved=transferRect({-1920,0,0,1040},{0,0,1080,1880},{-1700,80,-900,680},96,144,100,100);
 check(moved&&moved->left>=0&&moved->top>=0&&moved->right<=1080&&moved->bottom<=1880,"monitor move clamp DPI scaled size");
 check(!transferRect({0,0,800,600},{0,0,100,100},{0,0,400,300},96,96,300,200),"monitor move min size refusal");
 check(tileRects({0,0,200,100},65,*findSnapLayout(Config{},"balanced"),96).empty(),"bounded tile count");
 std::cout<<checks<<" tiling checks passed\n";return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
