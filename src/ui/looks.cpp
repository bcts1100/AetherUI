#include "looks.hpp"
#include "app.hpp"
#include "extensions.hpp"
namespace aether::ui {
Looks::Looks(Graphics& graphics,App& app):Surface(graphics),app_(app){
 if(!create(L"Aether Looks",WS_OVERLAPPEDWINDOW,WS_EX_APPWINDOW,{0,0,1000,760}))throw std::runtime_error("Could not create the looks gallery.");
 accessibility_=std::make_unique<Accessibility>(hwnd_);
}
Looks::~Looks(){if(accessibility_)accessibility_->detach();destroy();}
void Looks::open(const win::Monitor& monitor){
 selected_=0;for(int i=0;i<4;++i)if(app_.config().theme==preset(5+i))selected_=i;
 move(popupBounds(monitor.work,1000,760,monitor.dpi),false);setTheme(app_.theme());BOOL dark=theme_.background<0xAAAAAA;DwmSetWindowAttribute(hwnd_,static_cast<DWMWINDOWATTRIBUTE>(20),&dark,sizeof(dark));show(true);UpdateWindow(hwnd_);focus(0);
}
void Looks::update(){if(theme_!=app_.theme()){setTheme(app_.theme());BOOL dark=theme_.background<0xAAAAAA;DwmSetWindowAttribute(hwnd_,static_cast<DWMWINDOWATTRIBUTE>(20),&dark,sizeof(dark));}if(visible())invalidate();}
void Looks::button(Canvas& c,D2D1_RECT_F rect,const std::wstring& name,std::function<void()> action,bool enabled){
 c.fill(rect,enabled?theme_.surface:theme_.background,8);c.text(name,rect,enabled?theme_.foreground:theme_.muted,12,true,1);
 auto node=accessibleNode(hwnd_,static_cast<long>(actions_.size()+1),name,ROLE_SYSTEM_PUSHBUTTON,STATE_SYSTEM_FOCUSABLE|(enabled?0:STATE_SYSTEM_UNAVAILABLE),rect,drawingScale_);
 node.invoke=[this,enabled,action=std::move(action)]{if(visible()&&enabled)action();};node.focus=[this,index=static_cast<int>(actions_.size())]{if(visible()){focused_=index;SetFocus(hwnd_);invalidate();}};
 actions_.push_back(std::move(node));rects_.push_back(rect);
}
void Looks::paint(Canvas& c){
 RECT client{};GetClientRect(hwnd_,&client);drawingScale_=std::clamp(std::min(client.right/scale()/900.f,client.bottom/scale()/650.f),.25f,1.f);c.setScale(drawingScale_);
 const auto size=c.size();const float width=size.width,height=size.height;actions_.clear();rects_.clear();
 c.text(L"Make your desktop yours",box(24,15,width-48,36),theme_.foreground,24,true);
 c.text(L"Choose a look to preview. Apply changes only the palette.",box(24,53,width-48,24),theme_.muted,12);
 const float card=(width-84)/4;
 for(int i=0;i<4;++i){const auto t=preset(5+i);const float x=24+i*(card+12);
  c.setTheme(t);c.fill(box(x,88,card,132),t.background,14);c.fill(box(x+12,100,card-24,18),t.surface,5);
  c.fill(box(x+24,132,card-48,35),t.raised,8);c.circle(x+35,149,5,t.accent);c.text(L"Aa",box(x+52,132,card-75,35),t.foreground,16,true);
  c.text(win::wide(t.name),box(x+12,181,card-24,24),t.foreground,12,true);
  if(selected_==i)c.stroke(box(x,88,card,132),t.accent,14,2);
  auto rect=box(x,88,card,132);auto node=accessibleNode(hwnd_,i+1,L"Preview "+win::wide(t.name),ROLE_SYSTEM_LISTITEM,STATE_SYSTEM_FOCUSABLE|STATE_SYSTEM_SELECTABLE|(selected_==i?STATE_SYSTEM_SELECTED:0),rect,drawingScale_);
  node.invoke=[this,i]{if(visible()){selected_=i;notice_.clear();invalidate();}};node.focus=[this,i]{if(visible()){focused_=i;SetFocus(hwnd_);invalidate();}};actions_.push_back(std::move(node));rects_.push_back(rect);
 }
 const auto t=preset(5+selected_);c.setTheme(t);const float previewHeight=height-385;
 c.fill(box(24,236,width-48,previewHeight),t.background,14);c.fill(box(38,250,width-76,28),t.surface,8);
 c.text(L"Activities",box(52,250,120,28),t.foreground,11,true);c.text(L"Desktop preview",box(width/2-85,250,170,28),t.muted,11,false,1);
 c.fill(box(48,295,238,100),t.surface,12);c.text(L"Today",box(64,304,205,20),t.muted,11,true);c.text(L"10:42",box(64,330,205,36),t.foreground,28,true);c.text(L"A calmer place to work",box(64,369,205,18),t.muted,11);
 const float rightWidth=width-386;c.fill(box(314,295,rightWidth,std::max(70.f,previewHeight-111)),t.raised,12);c.text(L"Your apps and widgets",box(336,305,rightWidth-44,28),t.foreground,17,true);
 const float half=(rightWidth-54)/2;c.fill(box(332,347,half,98),t.surface,10);c.fill(box(344+half,347,half,98),t.surface,10);
 c.glyph(L'\uE189',box(345,359,25,25),t.accent,20);c.text(L"Media widget",box(380,357,half-57,24),t.foreground,13,true);c.text(L"Your music here",box(347,389,half-28,23),t.muted,11);
 c.glyph(L'\uE892',box(347,417,24,20),t.muted,13);c.glyph(L'\uE768',box(385,417,24,20),t.accent,15);c.glyph(L'\uE893',box(423,417,24,20),t.muted,13);
 c.glyph(L'\uE706',box(357+half,359,25,25),t.accent,20);c.text(L"Seattle",box(390+half,357,half-57,24),t.foreground,13,true);c.text(L"Forecasts from NWS",box(359+half,389,half-28,23),t.muted,11);
 if(previewHeight>330){c.text(L"A space for notes and the tools you use every day",box(336,459,rightWidth-44,26),t.muted,11);}
 const float dockX=width/2-136,dockY=236+previewHeight-52;c.fill(box(dockX,dockY,272,38),t.surface,14);
 for(int i=0;i<6;++i){c.fill(box(dockX+14+i*42.f,dockY+7,26,24),i==2?t.accent:t.raised,7);if(i==2)c.fill(box(dockX+21+i*42.f,dockY+33,12,2),t.accent,1);}
 c.setTheme(theme_);const float row=height-130;
 button(c,box(24,row,150,36),L"Apply look",[this]{apply();},app_.config().theme!=t);
 button(c,box(186,row,150,36),L"Undo last apply",[this]{undo();},undo_.available(app_.config().theme));
 button(c,box(348,row,width-372,36),L"Use matching desk widgets",[this]{addWidgets(false);});
 button(c,box(24,row+46,width-230,36),L"Add media, notes, resources + Seattle weather",[this]{addWidgets(true);});
 button(c,box(width-194,row+46,170,36),L"Manage widgets",[this]{app_.openExtensions();});
 c.text(notice_.empty()?L"Weather uses NWS when enabled. Frosted panels use desktop blur where available.":notice_,box(24,height-34,width-48,24),theme_.muted,11);
 if(GetFocus()==hwnd_&&focused_>=0&&static_cast<size_t>(focused_)<rects_.size())c.stroke(rects_[static_cast<size_t>(focused_)],theme_.foreground,8,2);
 accessibility_->snapshot(L"Aether Looks",ROLE_SYSTEM_PANE,actions_,focused_>=0?focused_+1:0,selected_+1);
}
void Looks::apply(){if(!visible())return;const auto next=preset(5+selected_);if(next==app_.config().theme)return;undo_.record(app_.config().theme,next);auto config=app_.config();config.theme=next;app_.apply(std::move(config));notice_=L"Look applied. Your layout, pins and taskbar setting are preserved.";update();}
void Looks::undo(){if(!visible())return;auto previous=undo_.take(app_.config().theme);if(!previous){notice_=L"The palette changed elsewhere; Undo will not overwrite it.";invalidate();return;}auto config=app_.config();config.theme=*previous;app_.apply(std::move(config));notice_=L"Previous look restored.";update();}
void Looks::addWidgets(bool productivity){
 if(!visible())return;const auto package=productivity?productivityExtension():sleekDeskExtension(selected_);auto& runtime=app_.extensions();std::string error;auto id=runtime.install(package,&error);bool ok=id.has_value();std::vector<std::string> previous;
 if(ok&&!productivity){for(const auto& entry:runtime.library().entries())if(entry.enabled&&entry.manifest.id!=package.id){for(int i=0;i<4;++i)if(entry.manifest.id==sleekDeskExtension(i).id)previous.push_back(entry.storageId);}
  for(const auto& other:previous)if(!runtime.setEnabled(other,false,&error)){ok=false;break;}
 }
 if(ok)ok=runtime.setEnabled(*id,true,&error);
 if(!ok)for(const auto& other:previous){std::string rollback;runtime.setEnabled(other,true,&rollback);if(!rollback.empty())error+=". "+rollback;}
 notice_=ok?L"Widgets enabled. Drag their headers to arrange them; Manage widgets can disable them.":win::wide(error);invalidate();
}
void Looks::focus(int delta){UpdateWindow(hwnd_);if(actions_.empty())return;focused_=focused_<0?0:(focused_+delta+static_cast<int>(actions_.size()))%static_cast<int>(actions_.size());SetFocus(hwnd_);invalidate();}
void Looks::click(float x,float y){x/=drawingScale_;y/=drawingScale_;for(size_t i=0;i<rects_.size();++i)if(contains(rects_[i],x,y)){focused_=static_cast<int>(i);if(actions_[i].invoke)actions_[i].invoke();invalidate();return;}}
LRESULT Looks::event(UINT message,WPARAM w,LPARAM l,bool& handled){handled=true;switch(message){case WM_GETMINMAXINFO:{auto* info=reinterpret_cast<MINMAXINFO*>(l);info->ptMinTrackSize={MulDiv(640,win::dpiFor(hwnd_),96),MulDiv(500,win::dpiFor(hwnd_),96)};return 0;}case WM_GETOBJECT:UpdateWindow(hwnd_);return accessibility_->object(w,l,handled);case WM_CLOSE:hide();return 0;case WM_SETFOCUS:case WM_KILLFOCUS:invalidate();return 0;case WM_KEYDOWN:if(w==VK_TAB){focus(GetKeyState(VK_SHIFT)<0?-1:1);return 0;}if((w==VK_LEFT||w==VK_RIGHT)&&focused_>=0&&focused_<4){selected_=(selected_+(w==VK_LEFT?3:1))%4;focused_=selected_;notice_.clear();invalidate();return 0;}if(w==VK_RETURN||w==VK_SPACE){if(focused_>=0&&static_cast<size_t>(focused_)<actions_.size()&&actions_[static_cast<size_t>(focused_)].invoke)actions_[static_cast<size_t>(focused_)].invoke();return 0;}if(w==VK_ESCAPE){hide();return 0;}break;default:break;}handled=false;return 0;}
}
