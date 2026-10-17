#include "ui/canvas.hpp"
#include "platform/windows.hpp"
#include <iostream>
// This target creates only its own temporary surface. No appbars, hooks, or taskbar changes.
class Probe final:public aether::ui::Surface {
public:
 int paints=0;explicit Probe(aether::ui::Graphics& g):Surface(g){}
 void paint(aether::ui::Canvas& c) override{
  ++paints;c.fill(aether::ui::box(0,0,300,180),0x211C28);
  c.text(L"Aether render smoke test",aether::ui::box(8,8,280,40),0xF5F1F7);
 }
};
int main(){
 aether::win::ComApartment apartment;
 try{
  aether::ui::Graphics graphics;Probe probe(graphics);
  if(!probe.create(L"Aether UI smoke",WS_POPUP,WS_EX_TOOLWINDOW,{0,0,300,180}))return 1;
  SendMessageW(probe.hwnd(),WM_PAINT,0,0);
  if(probe.paints!=1)return 2;
  probe.destroy();
  std::cout<<"Native surface creation and Direct2D/DirectWrite paint passed.\n";
  return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 3;}
}
