#include "editor.hpp"
#include "app.hpp"
#include "platform/appearance.hpp"
#include "platform/dialogs.hpp"
#include <commdlg.h>
namespace aether::ui {
void Editor::terminalSettings(Canvas& c,float y){
 const float width=c.size().width-244,half=(width-12)/2;
 auto options=app_.appearanceBridge().options();
 auto update=[this](IntegrationOptions next){try{app_.appearanceBridge().configure(std::move(next),app_.theme());}catch(const std::exception& e){win::message(hwnd_,win::wide(e.what()),MB_OK|MB_ICONERROR);}invalidate();};
 button(c,box(220,y,half,36),L"Open Aether Terminal",[this]{app_.appearanceBridge().launchTools(hwnd_,true);});
 bool pinned=std::any_of(app_.config().pins.begin(),app_.config().pins.end(),[](const auto& p){return p.target=="aether://terminal";});
 button(c,box(232+half,y,half,36),pinned?L"Pinned to dock":L"Pin Aether Terminal",[this]{auto config=app_.config();if(std::none_of(config.pins.begin(),config.pins.end(),[](const auto& p){return p.target=="aether://terminal";})&&config.pins.size()<128){config.pins.push_back({"Aether Terminal","aether://terminal","aether.terminal"});app_.apply(std::move(config));}},pinned);
 c.text(L"DEFAULT SHELL",box(222,y+58,width,22),theme_.muted,10,true);
 const wchar_t* shells[]={L"PowerShell",L"Command Prompt",L"WSL / Ubuntu"};
 for(int n=0;n<3;++n)button(c,box(220+n*(width+12)/3,y+90,(width-24)/3,34),shells[n],[options,update,n]()mutable{options.terminalShell=n;update(options);},options.terminalShell==n);
 c.text(L"STOCK DESIGNS",box(222,y+146,width,24),theme_.muted,10,true);
 button(c,box(220,y+180,width,32),L"Follow desktop palette",[options,update]()mutable{options.terminalDesign=0;update(options);},options.terminalDesign==0);
 for(int n=1;n<=10;++n){
  auto t=terminalDesignTheme(app_.theme(),n);float x=220+((n-1)%2)*(half+12),row=y+226+((n-1)/2)*92.f;
  auto bounds=box(x,row,half,80);c.fill(bounds,t.background,9);
  c.ellipse(x+half*.83f,row+50,half*.26f,23,t.accent,.16f,true);
  c.text(win::wide(TerminalDesignNames[n]),box(x+12,row+5,half-24,26),t.foreground,13,true);
  c.text(L"aether ~ $ echo hello",box(x+12,row+35,half-24,24),t.accent,11);
  if(options.terminalDesign==n)c.stroke(bounds,theme_.accent,9,2);
  action(bounds,[options,update,n]()mutable{options.terminalDesign=n;options.terminalBackground=true;update(options);},L"Terminal design: "+win::wide(TerminalDesignNames[n]),ROLE_SYSTEM_LISTITEM,STATE_SYSTEM_FOCUSABLE|STATE_SYSTEM_SELECTABLE|(options.terminalDesign==n?STATE_SYSTEM_SELECTED:0));
 }
 c.text(L"TEXT & CURSOR",box(222,y+708,width,24),theme_.muted,10,true);
 button(c,box(220,y+746,width,36),L"Font: "+win::wide(options.terminalFont),[this,options,update]()mutable{
  LOGFONTW font{};font.lfWeight=400;auto family=win::wide(options.terminalFont);std::copy_n(family.data(),std::min(family.size(),static_cast<std::size_t>(LF_FACESIZE-1)),font.lfFaceName);
  CHOOSEFONTW picker{sizeof(picker)};picker.hwndOwner=hwnd_;picker.lpLogFont=&font;picker.Flags=CF_SCREENFONTS|CF_FIXEDPITCHONLY|CF_INITTOLOGFONTSTRUCT|CF_FORCEFONTEXIST|CF_NOSIZESEL;
  app_.beginInteraction();bool chosen=ChooseFontW(&picker)!=FALSE;app_.endInteraction();if(chosen){options.terminalFont=win::utf8(font.lfFaceName);update(options);}
 },false,L"Choose terminal monospace font");
 button(c,box(220,y+794,half,34),L"Smaller text",[options,update]()mutable{options.terminalSize=std::max(10,options.terminalSize-1);update(options);});
 button(c,box(232+half,y+794,half,34),L"Larger text ("+std::to_wstring(options.terminalSize)+L" pt)",[options,update]()mutable{options.terminalSize=std::min(24,options.terminalSize+1);update(options);});
 const wchar_t* cursors[]={L"Outline block",L"Underline",L"Bar"};
 for(int n=0;n<3;++n)button(c,box(220+n*(width+12)/3,y+840,(width-24)/3,34),cursors[n],[options,update,n]()mutable{options.terminalCursor=n;update(options);},options.terminalCursor==n);
 toggle(c,y+896,L"Background artwork",L"Static stock art or your image. No animation timer or pointer parallax.",options.terminalBackground,[options,update]()mutable{options.terminalBackground=!options.terminalBackground;update(options);});
 button(c,box(220,y+980,half,34),L"Choose background image…",[this,options,update]()mutable{
  COMDLG_FILTERSPEC filter{L"Local image",L"*.png;*.jpg;*.jpeg"};app_.beginInteraction();auto path=win::chooseFile(hwnd_,false,L"Terminal background",&filter,1);app_.endInteraction();if(path&&validTerminalImage(win::utf8(path->wstring()))){options.terminalImage=win::utf8(path->wstring());options.terminalBackground=true;update(options);}
 });
 button(c,box(232+half,y+980,half,34),L"Use stock background",[options,update]()mutable{options.terminalImage.clear();options.terminalBackground=true;update(options);});
 button(c,box(220,y+1028,half,34),L"Less tint",[options,update]()mutable{options.terminalOpacity=std::max(50,options.terminalOpacity-5);update(options);});
 button(c,box(232+half,y+1028,half,34),L"More tint ("+std::to_wstring(options.terminalOpacity)+L"%)",[options,update]()mutable{options.terminalOpacity=std::min(100,options.terminalOpacity+5);update(options);});
 c.text(options.terminalImage.empty()?L"Stock artwork follows the selected design.":win::wide(options.terminalImage),box(222,y+1075,width,48),theme_.muted,11);
 toggle(c,y+1140,L"Also theme Windows Terminal profiles",L"Optional fragment: PowerShell, CMD and WSL; existing default profile is preserved.",options.terminal,[options,update]()mutable{options.terminal=!options.terminal;update(options);});
 c.text(L"Commands run in real shells. WSL needs an installed distribution. Ctrl+Shift+C/V copies/pastes, Ctrl+Tab switches tabs, F6 focuses toolbar controls. Theme changes keep sessions running.",box(222,y+1232,width,90),theme_.muted,12);
}
}
