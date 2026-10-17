#include "ui/canvas.hpp"
#include <iostream>
#include <cmath>
using namespace aether;
class Pattern final:public ui::Surface {
public:
 int mode=0;explicit Pattern(ui::Graphics& g):Surface(g){}
 void paint(ui::Canvas& c)override{auto s=c.size();c.fill(ui::box(0,0,s.width,s.height),mode==1?0xFF0000:0x0000FF);if(!mode)for(int x=0;x<static_cast<int>(s.width);x+=8)c.fill(ui::box(static_cast<float>(x),0,4,s.height),0xFFFFFF);}
};
class Glass final:public ui::Surface {
public:explicit Glass(ui::Graphics& g):Surface(g){}
 void paint(ui::Canvas& c)override{auto s=c.size();c.surface(ui::box(0,0,s.width,s.height),theme_);}
};
void settle(){auto until=GetTickCount64()+250;do{MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}Sleep(10);}while(GetTickCount64()<until);DwmFlush();}
struct Sample{double red=0,blue=0,variation=0;};
Sample sample(HWND h){RECT r{};GetWindowRect(h,&r);HDC dc=GetDC(nullptr);Sample s;double previous=0;for(int x=60;x<180;++x){auto p=GetPixel(dc,r.left+x,r.top+100);double v=GetRValue(p);s.red+=v;s.blue+=GetBValue(p);if(x>60)s.variation+=std::abs(v-previous);previous=v;}ReleaseDC(nullptr,dc);s.red/=120;s.blue/=120;s.variation/=119;return s;}
int main(){
 SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);win::ComApartment com;
 try{ui::Graphics g;Pattern pattern(g);Glass glass(g);Theme opaque;opaque.style=ThemeStyle::Classic;opaque.gradientStrength=0;opaque.surface=0x303030;opaque.borderWidth=0;Theme frosted=opaque;frosted.style=ThemeStyle::Frosted;
 if(!pattern.create(L"Aether owned blur pattern",WS_POPUP,WS_EX_TOOLWINDOW|WS_EX_TOPMOST,{120,120,600,480})||!glass.create(L"Aether owned blur probe",WS_POPUP,WS_EX_TOOLWINDOW|WS_EX_TOPMOST,{180,180,480,420}))return 2;
 pattern.setTheme(opaque);glass.setTheme(frosted);pattern.show();glass.show();settle();
 if(!glass.backdropActive()){std::cerr<<"Desktop blur unavailable on this desktop; pixel proof not obtained\n";return 3;}
 auto stripes=sample(glass.hwnd());pattern.mode=1;pattern.invalidate();settle();auto red=sample(glass.hwnd());pattern.mode=2;pattern.invalidate();settle();auto blue=sample(glass.hwnd());glass.setTheme(opaque);settle();auto flat=sample(glass.hwnd());
 std::cout<<"blur stripe variation="<<stripes.variation<<" red="<<red.red<<","<<red.blue<<" blue="<<blue.red<<","<<blue.blue<<" opaque="<<flat.red<<","<<flat.blue<<'\n';
 bool ok=stripes.variation<12&&red.red>blue.red+20&&blue.blue>red.blue+20&&!glass.backdropActive()&&std::abs(flat.red-48)<4&&std::abs(flat.blue-48)<4;
 if(!ok)std::cerr<<"FAIL: compositor did not blur and transmit the owned background, or opaque toggle failed\n";
 return ok?0:1;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 4;}
}
