#include "core/model.hpp"
#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace aether;
#define CHECK(x) do {if(!(x)) throw std::runtime_error(#x);} while(false)
int main(){
 std::vector<std::pair<const char*,std::function<void()>>> tests={
 {"default Ubuntu colors",[]{CHECK(preset(0).accent==0xE97843);}},
 {"distinct presets",[]{CHECK(preset(1).background!=preset(0).background);CHECK(preset(2).foreground!=preset(0).foreground);}},
 {"config numeric clamping",[]{auto c=parseSettings("[layout]\niconSize=900\n[theme]\nradius=-9\n");CHECK(c.defaults.iconSize==72);CHECK(c.theme.radius==0);}},
 {"malformed numeric fallback",[]{auto c=parseSettings("[layout]\niconSize=potato\n");CHECK(c.defaults.iconSize==48);}},
 {"reject trailing numeric garbage",[]{auto c=parseSettings("[layout]\niconSize=60oops\n");CHECK(c.defaults.iconSize==48);}},
 {"new profiles request native taskbar hiding",[]{CHECK(Config{}.hideTaskbar);CHECK(parseSettings("").hideTaskbar);CHECK(parseSettings("[layout]\niconSize=56\n").hideTaskbar);}},
 {"explicit native taskbar preference survives loading",[]{CHECK(!parseSettings("[behavior]\nhideTaskbar=0\n").hideTaskbar);CHECK(!parseSettings("[behavior]\nhideTaskbar=false\n").hideTaskbar);CHECK(parseSettings("[behavior]\nhideTaskbar=1\n").hideTaskbar);CHECK(parseSettings("[behavior]\nhideTaskbar=true\n").hideTaskbar);}},
 {"explicit native taskbar opt out survives persistence",[]{Config c;c.hideTaskbar=false;CHECK(!parseSettings(serializeSettings(c)).hideTaskbar);}},
 {"invalid boolean is safe",[]{auto c=parseSettings("[behavior]\nhideTaskbar=maybe\n");CHECK(!c.hideTaskbar);CHECK(!parseSettings("[behavior]\nhideTaskbar=\n").hideTaskbar);}},
 {"dock gap persists for layout and display overrides",[]{auto c=parseSettings("[layout]\ndockGap=18\n[display:second]\ndockGap=4\n");CHECK(c.defaults.dockGap==18);CHECK(c.forDisplay("second").dockGap==4);CHECK(c.forDisplay("missing").dockGap==18);auto roundtrip=parseSettings(serializeSettings(c));CHECK(roundtrip.defaults.dockGap==18);CHECK(roundtrip.forDisplay("second").dockGap==4);}},
 {"dock gap clamps bounds and rejects numeric garbage",[]{CHECK(parseSettings("[layout]\ndockGap=-8\n").defaults.dockGap==0);CHECK(parseSettings("[layout]\ndockGap=300\n").defaults.dockGap==32);CHECK(parseSettings("[layout]\ndockGap=7oops\n").defaults.dockGap==10);auto c=parseSettings("[layout]\ndockGap=18\n[display:second]\ndockGap=bad\n");CHECK(c.forDisplay("second").dockGap==18);}},
 {"saved layout clamps out of range dock gaps",[]{Config c;c.defaults.dockGap=300;c.displays["second"].dockGap=-9;auto roundtrip=parseSettings(serializeSettings(c));CHECK(roundtrip.defaults.dockGap==32);CHECK(roundtrip.forDisplay("second").dockGap==0);}},
 {"window previews preference parses and survives persistence",[]{auto c=parseSettings("[behavior]\nwindowPreviews=false\n");CHECK(!c.windowPreviews);CHECK(!parseSettings(serializeSettings(c)).windowPreviews);CHECK(parseSettings("[behavior]\nwindowPreviews=1\n").windowPreviews);CHECK(parseSettings("[behavior]\nwindowPreviews=maybe\n").windowPreviews);}},
 {"old settings retain dock spacing and previews",[]{auto c=parseSettings("[layout]\niconSize=56\n");auto r=dockGeometry({0,0,1920,1080},c.defaults.edge,4,c.defaults.iconSize,96,c.defaults.dockGap);CHECK(r.bottom==1070);CHECK(c.windowPreviews);}},
 {"roundtrip Unicode pins",[]{Config c;c.pins={{"\xE8\x8C\xB6","C:\\Tea Shop\\tea.exe","tea"}};CHECK(parseSettings(serializeSettings(c)).pins==c.pins);}},
 {"roundtrip escaped config values",[]{Config c;c.pins={{"a=b%\nq","C:\\apps\\x.exe","ab"}};CHECK(parseSettings(serializeSettings(c)).pins==c.pins);}},
 {"per display override",[]{Config c;auto d=c.defaults;d.edge=Edge::Left;c.displays["\\\\.\\DISPLAY2"]=d;auto out=parseSettings(serializeSettings(c));CHECK(out.forDisplay("\\\\.\\DISPLAY2").edge==Edge::Left);CHECK(out.forDisplay("missing").edge==Edge::Bottom);}},
 {"unknown entries ignored",[]{auto c=parseSettings("[script]\nexec=bad\n[layout]\nedge=right\n");CHECK(c.defaults.edge==Edge::Right);}},
 {"theme roundtrip",[]{auto t=preset(1);CHECK(parseTheme(serializeTheme(t)).value()==t);}},
 {"theme must identify schema",[]{CHECK(!parseTheme("[theme]\naccent=123456\n"));}},
 {"invalid theme color rejected",[]{auto s=serializeTheme(preset(0));s+="accent=oops\n";CHECK(!parseTheme(s));}},
 {"oversized theme rejected",[]{CHECK(!parseTheme(std::string(70000,'x')));}},
 {"search exact beats prefix",[]{CHECK(searchScore(L"tea",L"Tea")>searchScore(L"tea",L"Tea Shop"));}},
 {"search prefix beats substring",[]{CHECK(searchScore(L"code",L"Code Editor")>searchScore(L"code",L"Visual Code"));}},
 {"search tokens in any order",[]{CHECK(searchScore(L"studio visual",L"Visual Studio")>0);}},
 {"search missing token",[]{CHECK(searchScore(L"unicorn",L"Windows Terminal")==0);}},
 {"search case insensitive",[]{CHECK(searchScore(L"TERM",L"terminal")>0);}},
 {"empty query matches",[]{CHECK(searchScore(L"",L"Anything")>0);}},
 {"negative coordinate monitor",[]{auto r=dockGeometry({-1920,0,0,1080},Edge::Bottom,8,48,96);CHECK(r.left>=-1920);CHECK(r.right<=0);CHECK(r.bottom<1080);}},
 {"portrait left dock bounded",[]{auto r=dockGeometry({0,0,1080,1920},Edge::Left,60,72,144);CHECK(r.top>=0);CHECK(r.bottom<=1920);CHECK(r.right<=1080);}},
 {"high DPI scales dock",[]{auto a=dockGeometry({0,0,3840,2160},Edge::Bottom,6,48,96);auto b=dockGeometry({0,0,3840,2160},Edge::Bottom,6,48,192);CHECK(b.height()==2*a.height());}},
 {"right edge remains inside work",[]{auto r=dockGeometry({100,40,1500,900},Edge::Right,4,48,96);CHECK(r.right<1500);CHECK(r.left>100);}},
 {"tiny geometry safe",[]{auto r=dockGeometry({0,0,90,100},Edge::Bottom,100,72,192);CHECK(r.width()>0);CHECK(r.width()<=90);CHECK(r.height()<=100);}},
 {"paging includes every item",[]{int next=0,seen=0;do{auto p=pageFor(61,next,500,64);CHECK(p.count>0);seen+=p.count;if(!p.next)break;next=p.offset+p.count;}while(seen<100);CHECK(seen==61);}},
 {"paging uses all slots when every group fits",[]{auto p=pageFor(5,0,500,64);CHECK(p.count==5);CHECK(!p.previous);CHECK(!p.next);}},
 {"tiny paging scales controls inside its surface",[]{auto p=pageFor(9,0,120,80);CHECK(p.count==1);CHECK(p.slotSize>0);CHECK(p.slotSize<80);CHECK((p.count+1+(p.previous?1:0)+(p.next?1:0))*p.slotSize+p.padding<=120.01f);}},
 {"tiny paging keeps middle and last page reachable",[]{auto middle=pageFor(9,4,120,80);CHECK(middle.offset==4);CHECK(middle.previous);CHECK(middle.next);CHECK(middle.slotSize>0);CHECK((middle.count+3)*middle.slotSize+middle.padding<=120.01f);auto last=pageFor(9,999,120,80);CHECK(last.offset==8);CHECK(last.count==1);CHECK(last.previous);CHECK(!last.next);}},
 {"paging clamps offset",[]{auto p=pageFor(9,999,500,64);CHECK(p.offset<9);CHECK(p.count>0);}},
 {"empty paging",[]{auto p=pageFor(0,0,200,64);CHECK(p.count==0);CHECK(!p.next);}},
 {"adaptive geometry uses compact mixed DPI dimensions",[]{auto p=pageFor(9,4,120,80);auto r=dockGeometry({-420,0,0,1800},Edge::Bottom,p,72,288);CHECK(r.left==-390);CHECK(r.right==-30);CHECK(r.height()==120);CHECK(r.bottom==1770);}},
 {"zero length paging draws no unreachable items",[]{auto p=pageFor(9,4,0,80);CHECK(p.count==0);CHECK(p.capacity==0);CHECK(!p.previous);CHECK(!p.next);}},
 {"two groups use compact space without paging",[]{auto p=pageFor(2,0,180,64);CHECK(p.count==2);CHECK(!p.next);CHECK(3*p.slotSize+p.padding<=180.01f);}},
 {"nearest edge",[]{CHECK(nearestEdge({-1920,0,0,1080},-1919,400)==Edge::Left);CHECK(nearestEdge({0,0,1920,1080},1900,400)==Edge::Right);CHECK(nearestEdge({0,0,1920,1080},800,1075)==Edge::Bottom);}},
 {"snap left half",[]{auto r=snapRect({-1920,40,0,1080},0,8);CHECK(r.left==-1912);CHECK(r.right<=-960);CHECK(r.top==48);}},
 {"snap zones remain within work",[]{for(int z=0;z<7;++z){auto r=snapRect({100,40,1500,900},z,8);CHECK(r.left>=100);CHECK(r.top>=40);CHECK(r.right<=1500);CHECK(r.bottom<=900);CHECK(r.width()>0);}}},
 {"config input capped",[]{auto c=parseSettings(std::string(1100000,'x'));CHECK(c.pins.empty());}},
 {"invalid edge fallback",[]{CHECK(parseSettings("[layout]\nedge=up\n").defaults.edge==Edge::Bottom);}},
 {"booleans roundtrip",[]{Config c;c.hideTaskbar=true;c.hideFullscreen=false;c.clock24=true;c.defaults.autoHide=true;auto r=parseSettings(serializeSettings(c));CHECK(r.hideTaskbar);CHECK(!r.hideFullscreen);CHECK(r.clock24);CHECK(r.defaults.autoHide);}},
 };

 tests.insert(tests.end(),{
 {"pin survives without open windows",[]{auto g=groupDock({{"Tea","C:/Tea.exe","tea"}},{},"a",true);CHECK(g.size()==1);CHECK(g[0].pinIndex==0);CHECK(g[0].tasks.empty());}},
 {"same app windows grouped",[]{auto g=groupDock({},{{"One","C:/a.exe","a","x",false},{"Two","C:/a.exe","a","x",true}},"x",true);CHECK(g.size()==1);CHECK(g[0].tasks.size()==2);CHECK(g[0].active);}},
 {"monitor filtering keeps pins",[]{auto g=groupDock({{"A","C:/a.exe","a"}},{{"A","C:/a.exe","a","other",true}},"here",true);CHECK(g.size()==1);CHECK(g[0].tasks.empty());}},
 {"all monitors grouping",[]{auto g=groupDock({},{{"A","a","a","other",true}},"here",false);CHECK(g.size()==1);}},
 {"path fallback matches pinned executable",[]{auto g=groupDock({{"A","C:/a.exe",""}},{{"A","c:/A.exe","aumid:one","x",true}},"x",true);CHECK(g.size()==1);CHECK(g[0].pinIndex==0);CHECK(g[0].tasks.size()==1);}},
 {"pinned order is preserved",[]{auto g=groupDock({{"B","b","b"},{"A","a","a"}},{{"A","a","a","x",true}},"x",true);CHECK(g.size()==2);CHECK(g[0].app.name=="B");CHECK(g[1].tasks.size()==1);}},
 {"stable ordering for unpinned tasks",[]{auto g=groupDock({},{{"Z","z","z","x",false},{"A","a","a","x",false}},"x",true);CHECK(g.size()==2);CHECK(g[0].app.name=="A");}},
 {"distinct app identities sharing a host stay separate",[]{auto g=groupDock({},{{"Mail","C:/Windows/host.exe","aumid:mail","x",false},{"Photos","C:/Windows/host.exe","aumid:photos","x",true}},"x",true);CHECK(g.size()==2);CHECK(g[0].tasks.size()==1);CHECK(g[1].tasks.size()==1);}},
 {"shortcut identity joins a running executable",[]{auto g=groupDock({{"Tea","C:/Menu/Tea.lnk","c:/apps/tea.exe"}},{{"Draft","C:\\Apps\\TEA.exe","c:\\apps\\tea.exe","x",true}},"x",true);CHECK(g.size()==1);CHECK(g[0].tasks.size()==1);CHECK(g[0].app.name=="Tea");}},
 {"shortcut executable fallback joins a window with an application ID",[]{auto g=groupDock({{"Tea","C:/Menu/Tea.lnk","c:/apps/tea.exe"}},{{"Draft","C:/Apps/Tea.exe","aumid:contoso.tea","x",true}},"x",true);CHECK(g.size()==1);CHECK(g[0].tasks.size()==1);CHECK(g[0].app.name=="Tea");}},
 {"packaged host labels use application titles",[]{auto g=groupDock({},{{"Mail","C:/Windows/System32/ApplicationFrameHost.exe","aumid:mail","x",false},{"Photos","C:/Windows/System32/ApplicationFrameHost.exe","aumid:photos","x",true}},"x",true);CHECK(g.size()==2);CHECK(g[0].app.name=="Mail");CHECK(g[1].app.name=="Photos");}},
 {"shared host title fallback remains stable across ordering",[]{auto a=groupDock({},{{"Inbox","C:/Windows/System32/RuntimeBroker.exe","aumid:mail","x",true},{"Draft","C:/Windows/System32/RuntimeBroker.exe","aumid:mail","x",false}},"x",true);auto b=groupDock({},{{"Draft","C:/Windows/System32/RuntimeBroker.exe","aumid:mail","x",false},{"Inbox","C:/Windows/System32/RuntimeBroker.exe","aumid:mail","x",true}},"x",true);CHECK(a.size()==1);CHECK(a[0].app.name=="Draft");CHECK(b[0].app.name=="Draft");}},
 {"unpinned labels survive foreground window reordering",[]{auto a=groupDock({},{{"Draft - Tea","C:/Apps/Tea.exe","tea","x",false},{"Inbox - Tea","C:/Apps/Tea.exe","tea","x",true}},"x",true);auto b=groupDock({},{{"Inbox - Tea","C:/Apps/Tea.exe","tea","x",true},{"Draft - Tea","C:/Apps/Tea.exe","tea","x",false}},"x",true);CHECK(a[0].app.name=="Tea");CHECK(b[0].app.name=="Tea");}},
 {"equal app labels have deterministic identity order",[]{auto a=groupDock({},{{"Editor","C:/b/Editor.exe","b","x",false},{"Editor","C:/a/Editor.exe","a","x",false}},"x",true);CHECK(a.size()==2);CHECK(a[0].app.identity=="a");CHECK(a[1].app.identity=="b");}},
 {"empty selection",[]{CHECK(advanceSelection(0,1,0)==-1);}},
 {"selection bounds",[]{CHECK(advanceSelection(0,-1,5)==0);CHECK(advanceSelection(4,1,5)==4);CHECK(advanceSelection(2,1,5)==3);}},
 {"pin reorder",[]{Config c;c.pins={{"A","a","a"},{"B","b","b"},{"C","c","c"}};CHECK(reorderPin(c,0,2));CHECK(c.pins[2].name=="A");CHECK(c.pins[0].name=="B");}},
 {"pin lookup follows its target after reorder",[]{Config c;c.pins={{"A","C:\\Apps\\A.exe","a"},{"B","C:/Apps/B.exe","b"},{"C","C:/Apps/C.exe","c"}};auto saved=c.pins[0];CHECK(reorderPin(c,0,2));CHECK(findPinByTarget(c,saved)==2);CHECK(findPinByTarget(c,{"Renamed","c:/apps/a.EXE","different"})==2);}},
 {"removed target cannot select a former index neighbor",[]{Config c;c.pins={{"A","a","a"},{"B","b","b"}};auto saved=c.pins[0];c.pins.erase(c.pins.begin());CHECK(!findPinByTarget(c,saved));CHECK(!findPinByTarget(c,{"Empty","","a"}));}},
 {"invalid pin reorder unchanged",[]{Config c;CHECK(!reorderPin(c,1,0));}},
 {"popup on negative display",[]{auto r=popupBounds({-1920,40,0,1080},720,600,144);CHECK(r.left>=-1920);CHECK(r.right<=0);CHECK(r.top>=40);CHECK(r.height()>0);}},
 {"popup constrained on small screen",[]{auto r=popupBounds({0,0,500,400},900,700,192);CHECK(r.width()>0);CHECK(r.width()<=500);CHECK(r.height()<=400);}},
 {"event burst coalesces",[]{RefreshGate g;CHECK(g.request());CHECK(!g.request());g.consume();CHECK(g.request());}},
 {"locked session drops events",[]{RefreshGate g;g.setPaused(true);CHECK(!g.request());g.setPaused(false);CHECK(g.request());}}
 });
 tests.insert(tests.end(),{
 {"radius scales exactly once",[]{CHECK(scaledRadius(16,144)==24);CHECK(scaledRadius(16,192)==32);}},
 {"radius clamps user setting before scaling",[]{CHECK(scaledRadius(90,192)==56);CHECK(scaledRadius(-1,192)==0);}},
 {"icons downscale to bounded pixel storage",[]{auto s=boundedIconSize(512,512);CHECK(s.width==128);CHECK(s.height==128);}},
 {"nonsquare icon preserves aspect",[]{auto s=boundedIconSize(256,128);CHECK(s.width==128);CHECK(s.height==64);}},
 {"small icon is not enlarged",[]{auto s=boundedIconSize(32,32);CHECK(s.width==32);CHECK(s.height==32);}},
 {"zero size icon rejected",[]{auto s=boundedIconSize(0,32);CHECK(s.width==0);CHECK(s.height==0);}},
 {"live helper without ack is not ready",[]{CHECK(!recoveryReady(true,false));}},
 {"ack plus live process permits recovery",[]{CHECK(recoveryReady(true,true));CHECK(!recoveryReady(false,true));}}
 });
 int failures=0; for(auto& [name,test]:tests){try{test();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& e){++failures;std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';}}
 std::cout<<tests.size()-failures<<"/"<<tests.size()<<" passed\n";return failures?1:0;
}
