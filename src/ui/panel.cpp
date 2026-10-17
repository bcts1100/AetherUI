#include "panel.hpp"
#include "app.hpp"
#include <commctrl.h>
#include <cmath>
#include "core/personalization.hpp"
namespace aether::ui {
namespace {
constexpr int ActivitiesItem=-1,PreviousItem=-2,NextItem=-3,NoItem=-99;
constexpr UINT_PTR PreviewHoverTimer=3,PreviewLeaveTimer=4,AppearanceTimer=5;
constexpr std::uint64_t AppearanceDuration=160;
constexpr UINT ReconcileHoverMessage=WM_APP+64;
struct InteractionScope {
 App& app;
 explicit InteractionScope(App& value):app(value){app.beginInteraction();}
 ~InteractionScope(){app.endInteraction();}
};
struct PopupMenu {
 HMENU value=CreatePopupMenu();
 ~PopupMenu(){if(value)DestroyMenu(value);}
};
std::wstring appKey(const Pin& app){
 auto key=win::lower(win::wide(app.identity.empty()?app.target:app.identity));
 std::replace(key.begin(),key.end(),L'\\',L'/');return key;
}
std::wstring menuTitle(std::wstring_view source){
 std::wstring title;auto limit=std::min<std::size_t>(source.size(),120);
 for(std::size_t i=0;i<limit;++i){auto c=source[i];if(c<32)c=L' ';if(c==L'&')title+=L'&';title+=c;}
 if(source.size()>limit)title+=L"\u2026";return title;
}
}
Panel::Panel(Graphics& g,App& app,const win::Monitor& monitor,bool top)
 :Surface(g,FontPart::Dock),app_(app),monitor_(monitor),top_(top){
 if(!create(top?L"Aether Top Bar":L"Aether Dock",WS_POPUP,
  WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_TOPMOST,{0,0,100,40}))
  throw std::runtime_error("Could not create desktop panel.");
 if(!top_){
  tooltip_=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP,
   CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,hwnd_,nullptr,GetModuleHandleW(nullptr),nullptr);
  if(tooltip_){
   TOOLINFOW t{};t.cbSize=sizeof(t);t.hwnd=hwnd_;t.uId=reinterpret_cast<UINT_PTR>(hwnd_);
   t.uFlags=TTF_IDISHWND|TTF_SUBCLASS;t.lpszText=const_cast<wchar_t*>(L"Aether Dock");
   SendMessageW(tooltip_,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&t));
   SendMessageW(tooltip_,TTM_SETMAXTIPWIDTH,0,320);
   SendMessageW(tooltip_,TTM_SETDELAYTIME,TTDT_INITIAL,500);
  }
 }
}
Panel::~Panel(){cancelAnimation();cancelPreviewTimers();if(previewPin_)app_.closeDockPreview();appbar_.detach();if(tooltip_)DestroyWindow(tooltip_);tooltip_=nullptr;destroy();}
void Panel::setGroups(std::vector<DockGroup> groups){
 std::optional<Pin> hovered;
 if(hover_>=0&&static_cast<std::size_t>(hover_)<groups_.size())hovered=groups_[static_cast<std::size_t>(hover_)].app;
 bool changed=groups.size()!=groups_.size();
 if(!changed)for(std::size_t i=0;i<groups.size();++i){
  if(groups[i].app!=groups_[i].app||groups[i].active!=groups_[i].active||groups[i].tasks.size()!=groups_[i].tasks.size()){changed=true;break;}
 }
 // Task indices track the fresh snapshot even when drawing is unchanged.
 groups_=std::move(groups);
 if(hovered){
  auto key=appKey(*hovered);setHover(NoItem);
  for(std::size_t i=0;i<groups_.size();++i)if(appKey(groups_[i].app)==key){setHover(static_cast<int>(i));break;}
 }
 tip(hoverText());
 if(changed){cancelAnimation();if(!top_)hoverDirty_=true;hits_.clear();invalidate();}
}
void Panel::layout(const win::Monitor& m,bool suppressed){
 auto oldOptions=options_;auto oldTheme=theme_;auto oldPage=page_;
 monitor_=m;options_=app_.config().forDisplay(win::utf8(m.name));theme_=app_.theme();
 suppressed_=suppressed;
 if(oldOptions!=options_||oldTheme!=theme_||app_.config().reduceMotion)cancelAnimation();
 if(suppressed||(top_?!options_.topBar:!options_.dock)){
  cancelAnimation();cancelPreviewTimers();if(previewPin_)app_.closeDockPreview();previewPin_.reset();
  appbar_.detach();hide();placedValid_=false;KillTimer(hwnd_,2);return;
 }
 int dpi=std::clamp(m.dpi,48,480);auto px=[&](int n){return MulDiv(n,dpi,96);};
 if(top_){
  appbar_.attach(hwnd_,ABE_TOP);auto r=appbar_.position(m.bounds,px(std::clamp(theme_.topBarHeight,28,64)));
  if(!appbar_.registered())r={m.bounds.left,m.bounds.top,m.bounds.right,m.bounds.top+px(std::clamp(theme_.topBarHeight,28,64))};
  full_=r;bool moved=!placedValid_||placed_!=r;if(moved){move(r);placed_=r;placedValid_=true;}if(moved||oldOptions!=options_||oldTheme!=theme_)invalidate();return;
 }
 bool reserve=options_.reserveDock&&!options_.autoHide;
 Rect work=m.work;
 if(reserve){
  UINT edge=options_.edge==Edge::Left?ABE_LEFT:options_.edge==Edge::Right?ABE_RIGHT:ABE_BOTTOM;
  appbar_.attach(hwnd_,edge);
  work=appbar_.position(m.bounds,px(options_.iconSize+24+2*options_.dockGap));
  if(!appbar_.registered())work=m.work;
 }else{
  appbar_.detach();MONITORINFO info{};info.cbSize=sizeof(info);
  if(GetMonitorInfoW(m.handle,&info))work=win::rect(info.rcWork);
 }
 int available=options_.edge==Edge::Bottom?work.width():work.height();
 int availableDips=std::max(1,MulDiv(available,96,dpi)-20);
 page_=pageFor(static_cast<int>(groups_.size()),page_.offset,availableDips,options_.iconSize+8);
 full_=dockGeometry(work,options_.edge,page_,options_.iconSize,dpi,options_.dockGap);
 bool pageChanged=oldPage.offset!=page_.offset||oldPage.count!=page_.count||oldPage.previous!=page_.previous||oldPage.next!=page_.next||oldPage.slotSize!=page_.slotSize||oldPage.padding!=page_.padding;
 if(pageChanged){hoverDirty_=true;hits_.clear();if(hover_>=0&&(hover_<page_.offset||hover_>=page_.offset+page_.count))setHover(NoItem);tip(hoverText());}
 if(!options_.autoHide)collapsed_=false;
 collapse(collapsed_);
 if(pageChanged||oldOptions!=options_){hoverDirty_=true;if(previewPin_){cancelPreviewTimers();closeForLayout();previewPin_.reset();}}
 if(options_.autoHide&&!collapsed_)SetTimer(hwnd_,2,1400,nullptr);
 if(pageChanged||oldOptions!=options_||oldTheme!=theme_)invalidate();
}
void Panel::collapse(bool value){
 if(top_||suppressed_)return;bool wasCollapsed=collapsed_;collapsed_=value&&options_.autoHide;
 Rect r=full_;
 if(collapsed_){
  cancelAnimation();int thickness=std::max(3,MulDiv(3,monitor_.dpi,96));
  MONITORINFO info{};info.cbSize=sizeof(info);Rect work=monitor_.work;
  if(GetMonitorInfoW(monitor_.handle,&info))work=win::rect(info.rcWork);
  if(options_.edge==Edge::Bottom){r.bottom=work.bottom;r.top=r.bottom-thickness;}
  else if(options_.edge==Edge::Left){r.left=work.left;r.right=r.left+thickness;}
  else {r.right=work.right;r.left=r.right-thickness;}
 }
 int radius=collapsed_?0:scaledRadius(theme_.radius,monitor_.dpi);
 bool moved=!placedValid_||r!=placed_;
 if(moved){move(r);placed_=r;placedValid_=true;}
 if(moved||radius!=regionRadius_){win::roundedWindow(hwnd_,collapsed_?0:theme_.radius);regionRadius_=radius;barRegions_.clear();}
 if(moved||wasCollapsed!=collapsed_){hoverDirty_=true;if(previewPin_){cancelPreviewTimers();closeForLayout();previewPin_.reset();}invalidate();}
}
int Panel::hit(float x,float y) const{for(const auto& h:hits_)if(contains(h.rect,x,y))return h.item;return NoItem;}
void Panel::paintBar(Canvas& c){
 const auto size=c.size();const auto style=collapsed_?BarStyle::Solid:top_&&arranging_?BarStyle::Solid:top_?theme_.topBarStyle:theme_.dockStyle;std::vector<D2D1_RECT_F> shapes;
 if(!top_&&style==BarStyle::Islands){
  const bool vertical=options_.edge!=Edge::Bottom;const int slots=page_.count+1+(page_.previous?1:0)+(page_.next?1:0);const float length=vertical?size.height:size.width,cross=vertical?size.width:size.height;
  const float padding=std::min(page_.padding,std::max(0.f,length-slots)),slot=std::min({page_.slotSize,(length-padding)/slots,cross});const float start=(length-slots*slot)/2,inset=static_cast<float>(std::min(theme_.barPadding,std::max(0,static_cast<int>((cross-24)/2))));
  for(int n=0;n<slots;++n){const float gap=std::min(2.f,slot/8);shapes.push_back(vertical?box(inset,start+n*slot+gap,size.width-2*inset,slot-2*gap):box(start+n*slot+gap,inset,slot-2*gap,size.height-2*inset));}
 }else for(auto r:barShapes(style,top_,static_cast<int>(std::lround(size.width)),static_cast<int>(std::lround(size.height)),theme_.barPadding))shapes.push_back(D2D1::RectF(static_cast<float>(r.left),static_cast<float>(r.top),static_cast<float>(r.right),static_cast<float>(r.bottom)));
 const int radius=style==BarStyle::Solid||style==BarStyle::Accent||(top_&&style==BarStyle::Classic)?0:theme_.radius;
 std::vector<Rect> physical;for(const auto& r:shapes)physical.push_back({static_cast<int>(std::lround(r.left*scale())),static_cast<int>(std::lround(r.top*scale())),static_cast<int>(std::lround(r.right*scale())),static_cast<int>(std::lround(r.bottom*scale()))});
 const int pxRadius=scaledRadius(radius,monitor_.dpi);if(physical!=barRegions_||pxRadius!=barRegionRadius_){
  auto region=CreateRectRgn(0,0,0,0);bool valid=region!=nullptr;for(const auto& r:physical){auto part=radius?CreateRoundRectRgn(r.left,r.top,r.right,r.bottom,pxRadius*2,pxRadius*2):CreateRectRgn(r.left,r.top,r.right,r.bottom);if(!part){valid=false;break;}if(CombineRgn(region,region,part,RGN_OR)==ERROR)valid=false;DeleteObject(part);if(!valid)break;}
  if(valid&&SetWindowRgn(hwnd_,region,FALSE)){barRegions_=physical;barRegionRadius_=pxRadius;}else if(region)DeleteObject(region);
 }
 SetPropW(hwnd_,L"Aether.Bar.Design",reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(style)));SetPropW(hwnd_,L"Aether.Bar.Shapes",reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(shapes.size())));
 for(const auto& r:shapes){if(top_&&style==BarStyle::Classic&&theme_.style==ThemeStyle::Classic)c.fill(r,theme_.background);else c.surface(r,theme_,static_cast<float>(radius));}
 if(style==BarStyle::Accent){if(!top_&&options_.edge!=Edge::Bottom)c.fill(box(options_.edge==Edge::Left?0:size.width-3,0,3,size.height),theme_.accent);else c.fill(box(0,size.height-3,size.width,3),theme_.accent);}
}
void Panel::paint(Canvas& c){
 auto size=c.size();float width=size.width,height=size.height;hits_.clear();
 if(top_){
  paintBar(c);moduleBoxes_=topModuleBoxes(options_,theme_.topBarStyle,static_cast<int>(width),static_cast<int>(height),theme_.barPadding);
  SetPropW(hwnd_,L"Aether.Modules.Arranging",reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(arranging_)));
  for(const auto& module:moduleBoxes_){auto r=D2D1::RectF(static_cast<float>(module.bounds.left),static_cast<float>(module.bounds.top),static_cast<float>(module.bounds.right),static_cast<float>(module.bounds.bottom));const int id=static_cast<int>(module.module);auto property=L"Aether.Module."+std::to_wstring(id);SetPropW(hwnd_,property.c_str(),reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(module.bounds.left+1)));c.pushClip(r);
   if(arranging_)c.stroke(r,theme_.accent,5,moduleDrag_&&*moduleDrag_==module.module?2.f:1.f,.8f);
   switch(module.module){
   case TopModule::Activities:c.fontPart(FontPart::Activities);c.grid(box(r.left+5,r.top,24,r.bottom-r.top),theme_.accent);c.text(L"Activities",box(r.left+35,r.top,r.right-r.left-35,r.bottom-r.top),theme_.foreground,13,true);break;
   case TopModule::Title:c.fontPart(FontPart::WindowTitle);if(arranging_)c.text(L"Focused window",r,theme_.muted,12);else for(const auto& window:app_.windows())if(window.active){c.text(window.title,r,theme_.muted,12);break;}break;
   case TopModule::Clock:c.fontPart(FontPart::Clock);c.text(app_.status().clock,r,theme_.foreground,12,true,1);break;
   case TopModule::Status:c.fontPart(FontPart::Status);c.glyph(L'\uE701',box(r.left,r.top,24,r.bottom-r.top),theme_.muted,15);c.glyph(app_.status().muted?L'\uE74F':L'\uE767',box(r.left+27,r.top,24,r.bottom-r.top),theme_.foreground,16);c.text(app_.status().power,box(r.left+54,r.top,std::max(1.f,r.right-r.left-54),r.bottom-r.top),theme_.muted,11,false,1);break;
   case TopModule::Settings:c.glyph(L'\uE713',r,theme_.foreground,16);break;
   }c.popClip();
  }
  if(moduleDrag_){const float x=moduleDragX_<width/3?0:moduleDragX_>width*2/3?width*2/3:width/3;c.stroke(box(x+2,1,width/3-4,height-2),theme_.accent,4,2);}
  return;
 }
 if(collapsed_){paintBar(c);c.fill(box(0,0,width,height),theme_.accent);return;}
 paintBar(c);
 bool vertical=options_.edge!=Edge::Bottom;
 int slots=page_.count+1+(page_.previous?1:0)+(page_.next?1:0);
 float length=vertical?height:width,cross=vertical?width:height;
 float padding=std::min(page_.padding,std::max(0.f,length-slots));
 float slot=std::min({page_.slotSize,(length-padding)/slots,cross});
 if(slot<=0)return;
 float inset=std::min(4.f,slot*.1f),icon=slot-2*inset;
 float along=(length-slots*slot)/2;
 auto draw=[&](int id){
  auto r=vertical?box((width-slot)/2,along,slot,slot):box(along,(height-slot)/2,slot,slot);
  hits_.push_back({r,id});
  bool active=id>=0&&static_cast<std::size_t>(id)<groups_.size()&&groups_[static_cast<std::size_t>(id)].active;
  float highlight=std::max(hoverAmount(id),active?.65f:0.f)*std::clamp(theme_.dockHighlight,0,100)/100.f;
  if(highlight>0)c.fill(r,theme_.raised,std::min(slot/4,std::max(4.f,static_cast<float>(theme_.radius)-5)),highlight);
  auto ir=box(r.left+inset,r.top+inset,icon,icon);
  if(id==ActivitiesItem){if(icon>=32)c.grid(ir,theme_.foreground);else c.glyph(L'\uE71D',ir,theme_.foreground,std::max(1,static_cast<int>(icon*.7f)));}
  else if(id==PreviousItem||id==NextItem){
   wchar_t glyph=vertical?(id==PreviousItem?L'\uE70E':L'\uE70D'):(id==PreviousItem?L'\uE76B':L'\uE76C');
   c.glyph(glyph,ir,theme_.muted,std::max(1,static_cast<int>(std::min(22.f,icon*.7f))));
  }else if(id>=0&&static_cast<std::size_t>(id)<groups_.size()){
   const auto& item=groups_[static_cast<std::size_t>(id)];
   c.icon(app_.icons().request(item.app),ir,theme_.muted);
   int dots=std::min(3,static_cast<int>(item.tasks.size()));
   float spacing=std::min(6.f,slot/5),radius=std::min(1.7f,slot/10);
   auto tint=active?theme_.accent:theme_.muted;
   float markerX=vertical?(options_.edge==Edge::Left?std::min(3.f,width/2):width-std::min(3.f,width/2)):(r.left+r.right)/2;
   float markerY=vertical?(r.top+r.bottom)/2:height-std::min(4.f,height/2);
   if(dots&&theme_.indicator==IndicatorStyle::Line){
    float half=std::min(active?10.f:6.f,slot/4);if(vertical)c.line(markerX,markerY-half,markerX,markerY+half,tint,2);else c.line(markerX-half,markerY,markerX+half,markerY,tint,2);
   }else for(int d=0;d<dots;++d){
    float offset=(d-(dots-1)/2.f)*spacing;float x=markerX+(vertical?0:offset),y=markerY+(vertical?offset:0);
    if(theme_.indicator==IndicatorStyle::Square)c.fill(box(x-radius,y-radius,radius*2,radius*2),tint,.5f);else c.circle(x,y,radius,tint);
   }
  }
  along+=slot;
 };
 draw(ActivitiesItem);if(page_.previous)draw(PreviousItem);
 for(int i=page_.offset;i<page_.offset+page_.count;++i)draw(i);
 if(page_.next)draw(NextItem);
 // Reconcile only after dirty geometry/group hit rectangles have been rebuilt.
 // Posting keeps popup changes outside the active Direct2D paint transaction.
 if(hoverDirty_){hoverDirty_=false;if(!hoverSyncPosted_){hoverSyncPosted_=PostMessageW(hwnd_,ReconcileHoverMessage,0,0)!=FALSE;if(!hoverSyncPosted_)hoverDirty_=true;}}
}
float Panel::hoverAmount(int id) const{
 if(!animationArmed_)return id==hover_?1.f:0.f;
 auto found=hoverFrom_.find(id);float start=found==hoverFrom_.end()?0.f:found->second;
 float progress=std::clamp(static_cast<float>(GetTickCount64()-animationStart_)/static_cast<float>(AppearanceDuration),0.f,1.f);
 float eased=1.f-(1.f-progress)*(1.f-progress)*(1.f-progress);
 return start+((id==animationTarget_?1.f:0.f)-start)*eased;
}
void Panel::cancelAnimation(){if(hwnd_)KillTimer(hwnd_,AppearanceTimer);animationArmed_=false;hoverFrom_.clear();}
void Panel::setHover(int next,bool animate){
 if(next==hover_)return;BOOL windowsMotion=FALSE;
 bool enabled=animate&&!top_&&!suppressed_&&!collapsed_&&visible()&&!app_.config().reduceMotion&&
  SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&windowsMotion,0)&&windowsMotion;
 std::map<int,float> from;
 if(enabled){
  for(const auto& [id,value]:hoverFrom_){(void)value;float current=hoverAmount(id);if(current>.002f&&from.size()<128)from.emplace(id,current);}
  float previous=hoverAmount(hover_);if(previous>.002f&&from.size()<128)from[hover_]=previous;
 }
 cancelAnimation();hover_=next;
 if(enabled&&!from.empty()&&next!=NoItem){hoverFrom_=std::move(from);animationTarget_=next;animationStart_=GetTickCount64();animationArmed_=SetTimer(hwnd_,AppearanceTimer,16,nullptr)!=0;if(!animationArmed_)hoverFrom_.clear();}
 else if(enabled&&next!=NoItem){hoverFrom_={};animationTarget_=next;animationStart_=GetTickCount64();animationArmed_=SetTimer(hwnd_,AppearanceTimer,16,nullptr)!=0;}
 else if(enabled&&!from.empty()){hoverFrom_=std::move(from);animationTarget_=NoItem;animationStart_=GetTickCount64();animationArmed_=SetTimer(hwnd_,AppearanceTimer,16,nullptr)!=0;if(!animationArmed_)hoverFrom_.clear();}
}
void Panel::tip(const std::wstring& text){
 if(!tooltip_)return;
 bool liveGroup=hover_>=0&&static_cast<std::size_t>(hover_)<groups_.size()&&!groups_[static_cast<std::size_t>(hover_)].tasks.empty();
 bool suppressed=liveGroup||app_.dockPreviewVisible();
 // A running group uses the flyout; suppress competing native tooltip bubbles
 // throughout its hover and while the flyout is visible.
 SendMessageW(tooltip_,TTM_ACTIVATE,suppressed?FALSE:TRUE,0);
 if(suppressed)SendMessageW(tooltip_,TTM_POP,0,0);
 if(tooltipText_==text)return;tooltipText_=text;
 TOOLINFOW t{};t.cbSize=sizeof(t);t.hwnd=hwnd_;t.uId=reinterpret_cast<UINT_PTR>(hwnd_);
 t.lpszText=tooltipText_.data();SendMessageW(tooltip_,TTM_UPDATETIPTEXTW,0,reinterpret_cast<LPARAM>(&t));
}
std::wstring Panel::hoverText() const{
 if(hover_>=0&&static_cast<std::size_t>(hover_)<groups_.size()){
  const auto& group=groups_[static_cast<std::size_t>(hover_)];auto text=win::wide(group.app.name);
  if(group.tasks.size()>1)text+=L"  |  "+std::to_wstring(group.tasks.size())+L" windows";
  for(auto index:group.tasks)if(index<app_.windows().size()){
   const auto& window=app_.windows()[index];
   if(group.tasks.size()==1||window.active){if(!window.title.empty()&&window.title!=text)text+=L"\n"+window.title;break;}
  }
  return text;
 }
 return hover_==ActivitiesItem?L"Activities  |  Ctrl+Alt+Space":hover_==PreviousItem?L"Previous apps":hover_==NextItem?L"More apps":L"Ctrl-drag to move dock";
}
void Panel::cancelHoverTimer(){KillTimer(hwnd_,PreviewHoverTimer);hoverTimerArmed_=false;}
void Panel::cancelPreviewTimers(){cancelHoverTimer();KillTimer(hwnd_,PreviewLeaveTimer);}
void Panel::refreshHover(){
 if(top_||suppressed_||collapsed_||!visible())return;
 POINT point{};RECT bounds{};
 if(!GetCursorPos(&point)||!GetWindowRect(hwnd_,&bounds)||!PtInRect(&bounds,point)||WindowFromPoint(point)!=hwnd_)return;
 // Eligibility can recover without a mouse move after a menu/modal scope or
 // window-state change. Rebuild hit rectangles before rearming; dismissal is
 // still enforced by pointer(). No idle timer or repeated Shell scan is added.
 hoverDirty_=true;invalidate();
}
void Panel::closeForLayout(){closingForLayout_=true;app_.closeDockPreview();closingForLayout_=false;}
void Panel::reconcileHover(){
 hoverSyncPosted_=false;
 if(hoverDirty_||top_||suppressed_||collapsed_||dragging_||!app_.dockPreviewAllowed(monitor_.name))return;
 POINT point{};RECT bounds{};if(!GetCursorPos(&point)||!GetWindowRect(hwnd_,&bounds))return;
 if(!PtInRect(&bounds,point)){cancelHoverTimer();if(hover_!=NoItem){setHover(NoItem);tip(hoverText());invalidate();}return;}
 ScreenToClient(hwnd_,&point);pointer(point.x/scale(),point.y/scale());
}
void Panel::previewClosed(){
 if(top_)return;
 POINT closeCursor{};auto anchor=win::native(previewAnchor_);
 if(!closingForLayout_&&previewOwned_&&previewPin_&&GetCursorPos(&closeCursor)&&PtInRect(&anchor,closeCursor)){dismissedPin_=previewPin_;dismissedPoint_=closeCursor;}
 previewOwned_=false;cancelPreviewTimers();tip(hoverText());
 if(options_.autoHide&&!suppressed_&&!dragging_){
  POINT point{};RECT rect{};if(GetCursorPos(&point)&&GetWindowRect(hwnd_,&rect)&&!PtInRect(&rect,point))SetTimer(hwnd_,2,650,nullptr);
 }
}
Rect Panel::previewAnchor(int item) const {
 auto found=std::find_if(hits_.begin(),hits_.end(),[&](const Hit& h){return h.item==item;});
 if(found==hits_.end())return {};
 float s=scale();POINT point{static_cast<LONG>(std::floor(found->rect.left*s)),static_cast<LONG>(std::floor(found->rect.top*s))};
 ClientToScreen(hwnd_,&point);return {point.x,point.y,point.x+static_cast<int>(std::ceil((found->rect.right-found->rect.left)*s)),point.y+static_cast<int>(std::ceil((found->rect.bottom-found->rect.top)*s))};
}
void Panel::pointer(float x,float y){
 if(top_){if(moduleDrag_){moduleDragX_=x;invalidate();}return;}
 KillTimer(hwnd_,2);
 if(collapsed_){collapse(false);return;}
 KillTimer(hwnd_,PreviewLeaveTimer);
 POINT point{};bool havePoint=GetCursorPos(&point)!=FALSE;
 if(dismissedPin_&&havePoint&&(point.x!=dismissedPoint_.x||point.y!=dismissedPoint_.y))dismissedPin_.reset();
 int next=hit(x,y);
 bool liveGroup=next>=0&&static_cast<std::size_t>(next)<groups_.size()&&!groups_[static_cast<std::size_t>(next)].tasks.empty();
 if(next!=hover_){
  cancelHoverTimer();
  bool samePreview=liveGroup&&previewPin_&&appKey(*previewPin_)==appKey(groups_[static_cast<std::size_t>(next)].app)&&previewAnchor(next)==previewAnchor_;
  if(app_.dockPreviewVisible()&&!samePreview){app_.closeDockPreview();previewPin_.reset();}
  setHover(next,true);tip(hoverText());invalidate();
 }
 bool dismissed=liveGroup&&dismissedPin_&&appKey(*dismissedPin_)==appKey(groups_[static_cast<std::size_t>(next)].app);
 if(liveGroup&&!dragging_&&!hoverTimerArmed_&&!dismissed&&!app_.dockPreviewVisible()&&app_.dockPreviewAllowed(monitor_.name))
  hoverTimerArmed_=SetTimer(hwnd_,PreviewHoverTimer,350,nullptr)!=0;
}
void Panel::leave(){if(!top_){
 // A queued leave from an earlier tracking generation can arrive after the
 // pointer has returned. Trust the actual owned hit before cancelling dwell;
 // no further mouse move is guaranteed for a stationary pointer.
 POINT actual{};RECT bounds{};
 if(!suppressed_&&!collapsed_&&GetCursorPos(&actual)&&GetWindowRect(hwnd_,&bounds)&&PtInRect(&bounds,actual)&&WindowFromPoint(actual)==hwnd_){
  TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,hwnd_,0};tracking_=TrackMouseEvent(&track)!=FALSE;
  refreshHover();return;
 }
 POINT point{};if(dismissedPin_&&GetCursorPos(&point)&&(point.x!=dismissedPoint_.x||point.y!=dismissedPoint_.y))dismissedPin_.reset();
 cancelHoverTimer();if(hover_!=NoItem){setHover(NoItem,true);invalidate();}
 if(app_.dockPreviewVisible())SetTimer(hwnd_,PreviewLeaveTimer,350,nullptr);
 if(options_.autoHide)SetTimer(hwnd_,2,650,nullptr);
}}
void Panel::turnPage(int direction){
 if(page_.capacity<=0||(direction<0&&!page_.previous)||(direction>0&&!page_.next))return;
 cancelPreviewTimers();app_.closeDockPreview();previewPin_.reset();
 page_.offset=std::max(0,page_.offset+direction*page_.capacity);hits_.clear();setHover(NoItem);tip(hoverText());
 layout(monitor_,suppressed_);invalidate();
}
void Panel::click(float x,float y){
 cancelPreviewTimers();app_.closeDockPreview();previewPin_.reset();
 if(top_){if(arranging_)return;for(const auto& item:moduleBoxes_)if(contains(D2D1::RectF(static_cast<float>(item.bounds.left),static_cast<float>(item.bounds.top),static_cast<float>(item.bounds.right),static_cast<float>(item.bounds.bottom)),x,y)){if(item.module==TopModule::Activities)app_.openActivities(monitor_.name);else if(item.module==TopModule::Settings)app_.openEditor();else app_.openQuick(monitor_.name);return;}return;}
 int item=hit(x,y);
 if(item==ActivitiesItem)app_.openActivities(monitor_.name);
 else if(item==PreviousItem)turnPage(-1);else if(item==NextItem)turnPage(1);
 else if(item>=0&&static_cast<std::size_t>(item)<groups_.size())app_.activateGroup(groups_[static_cast<std::size_t>(item)],(GetKeyState(VK_SHIFT)&0x8000)!=0);
}
void Panel::context(POINT point){
 cancelPreviewTimers();app_.closeDockPreview();previewPin_.reset();
 InteractionScope interaction(app_);
 if(top_){PopupMenu menu;if(!menu.value)return;AppendMenuW(menu.value,MF_STRING,1,arranging_?L"Finish arranging modules":L"Arrange top bar modules");AppendMenuW(menu.value,MF_STRING,2,L"Reset module placement on this monitor");AppendMenuW(menu.value,MF_STRING,3,L"Desktop Studio");AppendMenuW(menu.value,MF_STRING,4,L"More Aether actions…");SetForegroundWindow(hwnd_);auto choice=TrackPopupMenuEx(menu.value,TPM_RETURNCMD|TPM_NONOTIFY|TPM_RIGHTBUTTON,point.x,point.y,hwnd_,nullptr);PostMessageW(hwnd_,WM_NULL,0,0);if(choice==1){arranging_=!arranging_;moduleDrag_.reset();invalidate();}else if(choice==2){auto config=app_.config();auto key=win::utf8(monitor_.name);auto options=config.forDisplay(key);options.moduleOrder={0,1,2,3,4};options.moduleZones={0,0,1,2,2};config.displays[key]=options;app_.apply(std::move(config));}else if(choice==3)app_.openEditor();else if(choice==4)app_.globalMenu(hwnd_,point);return;}
 POINT local=point;ScreenToClient(hwnd_,&local);int id=hit(local.x/scale(),local.y/scale());
 if(id<0||static_cast<std::size_t>(id)>=groups_.size()){app_.globalMenu(hwnd_,point);return;}
 const auto group=groups_[static_cast<std::size_t>(id)];auto anchor=previewAnchor(id);
 struct Choice {win::Window window;bool restore;};std::vector<Choice> choices;
 // Snapshot HWND and PID before entering the menu's nested message loop.
 for(auto index:group.tasks)if(index<app_.windows().size()&&choices.size()<100){
  const auto& window=app_.windows()[index];choices.push_back({window,IsZoomed(window.hwnd)||IsIconic(window.hwnd)});
 }
 PopupMenu menu;if(!menu.value)return;
 auto label=menuTitle(win::wide(group.app.name));if(label.empty())label=L"Application";
 AppendMenuW(menu.value,MF_STRING|MF_DISABLED,0,label.c_str());AppendMenuW(menu.value,MF_SEPARATOR,0,nullptr);
 UINT launchFlags=MF_STRING|(group.app.target.empty()?MF_GRAYED:0);
 AppendMenuW(menu.value,launchFlags,1,L"Open new window");
 AppendMenuW(menu.value,launchFlags,2,group.pinIndex>=0?L"Unpin from dock":L"Pin to dock");
 if(group.pinIndex>=0){
  auto pin=findPinByTarget(app_.config(),group.app);
  AppendMenuW(menu.value,MF_STRING|(!pin||*pin==0?MF_GRAYED:0),3,L"Move pin earlier");
  AppendMenuW(menu.value,MF_STRING|(!pin||*pin+1>=app_.config().pins.size()?MF_GRAYED:0),4,L"Move pin later");
 }
 if(!choices.empty()){AppendMenuW(menu.value,MF_STRING,5,L"Show open windows");AppendMenuW(menu.value,MF_SEPARATOR,0,nullptr);}
 for(std::size_t i=0;i<choices.size();++i){
  auto submenu=CreatePopupMenu();if(!submenu)continue;
  UINT base=100+static_cast<UINT>(i)*4;
  AppendMenuW(submenu,MF_STRING,base,L"Switch to window");
  AppendMenuW(submenu,MF_STRING,base+1,L"Minimize");
  AppendMenuW(submenu,MF_STRING,base+2,choices[i].restore?L"Restore":L"Maximize");
  AppendMenuW(submenu,MF_SEPARATOR,0,nullptr);AppendMenuW(submenu,MF_STRING,base+3,L"Close window");
  auto title=menuTitle(choices[i].window.title);if(title.empty())title=L"Window "+std::to_wstring(i+1);
  UINT flags=MF_POPUP|MF_STRING|(choices[i].window.active?MF_CHECKED:0);
  if(!AppendMenuW(menu.value,flags,reinterpret_cast<UINT_PTR>(submenu),title.c_str()))DestroyMenu(submenu);
 }
 SetForegroundWindow(hwnd_);UINT command=TrackPopupMenuEx(menu.value,TPM_RETURNCMD|TPM_NONOTIFY|TPM_RIGHTBUTTON,point.x,point.y,hwnd_,nullptr);
 PostMessageW(hwnd_,WM_NULL,0,0);
 if(command==1){DockGroup launch;launch.app=group.app;app_.activateGroup(launch,true);}
 else if(command>=2&&command<=4){
  // Pin ordering can change while a menu is open; resolve the target now.
  auto pin=findPinByTarget(app_.config(),group.app);
  if(command==2){if(group.pinIndex>=0){if(pin)app_.removePin(*pin);}else if(!pin)app_.togglePin(group.app);}
  else if(command==3&&pin&&*pin>0)app_.movePin(*pin,*pin-1);
  else if(command==4&&pin&&*pin+1<app_.config().pins.size())app_.movePin(*pin,*pin+1);
 }else if(command==5){
  if(anchor.width()>0&&anchor.height()>0){previewPin_=group.app;previewAnchor_=anchor;app_.openDockPreview(monitor_.name,group.app,anchor,true);previewOwned_=app_.dockPreviewVisible();}
 }else if(command>=100){
  std::size_t index=(command-100)/4;UINT action=(command-100)%4;
  if(index<choices.size()){
   const auto& choice=choices[index];const auto& window=choice.window;
   if(win::windowAlive(window.hwnd,window.pid))switch(action){
    case 0:win::activate(window);break;
    case 1:ShowWindowAsync(window.hwnd,SW_MINIMIZE);break;
    case 2:ShowWindowAsync(window.hwnd,choice.restore?SW_RESTORE:SW_MAXIMIZE);break;
    case 3:PostMessageW(window.hwnd,WM_CLOSE,0,0);break;
   }
  }
 }
}
LRESULT Panel::event(UINT m,WPARAM w,LPARAM l,bool& handled){
 handled=true;
 switch(m){
 case WM_SHOWWINDOW:if(!w)cancelAnimation();break;
 case WM_WINDOWPOSCHANGED:if(reinterpret_cast<WINDOWPOS*>(l)->flags&SWP_HIDEWINDOW)cancelAnimation();break;
 case WM_DESTROY:cancelAnimation();cancelPreviewTimers();return 0;
 case WM_SETTINGCHANGE:cancelAnimation();invalidate();break;
 case WM_MOUSEACTIVATE:return top_&&arranging_?MA_ACTIVATE:MA_NOACTIVATE;
 case WM_CLOSE:app_.quit();return 0;
 case win::AppbarMessage:if(w==ABN_POSCHANGED)app_.scheduleLayout();return 0;
 case WM_DPICHANGED:placedValid_=false;app_.scheduleLayout();break;
  case ReconcileHoverMessage:reconcileHover();return 0;
  case WM_TIMER:
  if(w==AppearanceTimer){if(!animationArmed_||!visible()||suppressed_||collapsed_||app_.config().reduceMotion||GetTickCount64()-animationStart_>=AppearanceDuration)cancelAnimation();invalidate();return 0;}
  if(w==PreviewHoverTimer){
   cancelHoverTimer();
   if(!top_&&!suppressed_&&!collapsed_&&!dragging_&&app_.dockPreviewAllowed(monitor_.name)&&hover_>=0&&static_cast<std::size_t>(hover_)<groups_.size()){
    const auto& group=groups_[static_cast<std::size_t>(hover_)];auto anchor=previewAnchor(hover_);POINT point{};auto rect=win::native(anchor);
    if(!group.tasks.empty()&&GetCursorPos(&point)&&PtInRect(&rect,point)){previewPin_=group.app;previewAnchor_=anchor;app_.openDockPreview(monitor_.name,group.app,anchor);previewOwned_=app_.dockPreviewVisible();tip(hoverText());}
   }
   return 0;
  }
  if(w==PreviewLeaveTimer){
   KillTimer(hwnd_,PreviewLeaveTimer);POINT point{};RECT rect{};GetCursorPos(&point);GetWindowRect(hwnd_,&rect);
   if(!PtInRect(&rect,point)&&!app_.dockPreviewContainsPointer()){app_.closeDockPreview();previewPin_.reset();}
   return 0;
  }
  if(w==2){KillTimer(hwnd_,2);POINT p{};RECT r{};GetCursorPos(&p);GetWindowRect(hwnd_,&r);if(!PtInRect(&r,p)&&!dragging_&&!app_.dockPreviewVisible())collapse(true);return 0;}break;
 case WM_MOUSEWHEEL:if(!top_){turnPage(GET_WHEEL_DELTA_WPARAM(w)>0?-1:1);return 0;}break;
 case WM_KEYDOWN:if(top_&&arranging_&&w==VK_ESCAPE){moduleDrag_.reset();arranging_=false;if(GetCapture()==hwnd_)ReleaseCapture();invalidate();return 0;}break;
 case WM_LBUTTONDOWN:if(top_&&arranging_){const float x=GET_X_LPARAM(l)/scale(),y=GET_Y_LPARAM(l)/scale();SetFocus(hwnd_);for(const auto& item:moduleBoxes_)if(x>=item.bounds.left&&x<item.bounds.right&&y>=item.bounds.top&&y<item.bounds.bottom){moduleDrag_=item.module;moduleDragStart_={GET_X_LPARAM(l),GET_Y_LPARAM(l)};moduleDragX_=x;SetCapture(hwnd_);invalidate();break;}return 0;}if(!top_){
  cancelPreviewTimers();app_.closeDockPreview();previewPin_.reset();
  GetCursorPos(&dragStart_);edgeDragging_=(GetKeyState(VK_CONTROL)&0x8000)!=0;
  int id=hit(GET_X_LPARAM(l)/scale(),GET_Y_LPARAM(l)/scale());dragPin_.reset();
  if(id>=0&&static_cast<std::size_t>(id)<groups_.size()&&groups_[static_cast<std::size_t>(id)].pinIndex>=0)dragPin_=groups_[static_cast<std::size_t>(id)].app;
  dragging_=edgeDragging_||dragPin_.has_value();if(dragging_)SetCapture(hwnd_);return 0;
 }break;
 case WM_CAPTURECHANGED:moduleDrag_.reset();invalidate();cancelAnimation();cancelPreviewTimers();dragging_=false;edgeDragging_=false;dragPin_.reset();return 0;
 case WM_CANCELMODE:moduleDrag_.reset();if(top_&&GetCapture()==hwnd_)ReleaseCapture();invalidate();cancelAnimation();cancelPreviewTimers();app_.closeDockPreview();previewPin_.reset();if(dragging_)ReleaseCapture();dragging_=false;edgeDragging_=false;dragPin_.reset();return 0;
 case WM_LBUTTONUP:if(top_&&moduleDrag_){const auto module=*moduleDrag_;moduleDrag_.reset();ReleaseCapture();if(std::abs(GET_X_LPARAM(l)-moduleDragStart_.x)+std::abs(GET_Y_LPARAM(l)-moduleDragStart_.y)<8){invalidate();return 0;}const float x=GET_X_LPARAM(l)/scale(),y=GET_Y_LPARAM(l)/scale();RECT bounds{};GetClientRect(hwnd_,&bounds);const float width=bounds.right/scale(),height=bounds.bottom/scale();if(x<0||x>=width||y<0||y>=height){invalidate();return 0;}int zone=x<width/3?0:x>width*2/3?2:1;std::optional<TopModule> before;for(const auto& target:moduleBoxes_)if(x>=target.bounds.left&&x<target.bounds.right){zone=target.zone;if(target.module!=module)before=target.module;break;}auto config=app_.config();auto key=win::utf8(monitor_.name);auto options=config.forDisplay(key);if(moveTopModule(options,module,zone,before)){config.displays[key]=options;app_.apply(std::move(config));}invalidate();return 0;}if(dragging_){
  POINT p{};GetCursorPos(&p);bool moved=std::abs(p.x-dragStart_.x)+std::abs(p.y-dragStart_.y)>8;
  bool edge=edgeDragging_;auto source=dragPin_;dragging_=false;ReleaseCapture();
  if(moved){
   if(edge)app_.setEdge(monitor_.name,nearestEdge(monitor_.bounds,p.x,p.y));
   else if(source){
    int dest=hit(GET_X_LPARAM(l)/scale(),GET_Y_LPARAM(l)/scale());
    if(dest>=0&&static_cast<std::size_t>(dest)<groups_.size()&&groups_[static_cast<std::size_t>(dest)].pinIndex>=0){
     auto from=findPinByTarget(app_.config(),*source),to=findPinByTarget(app_.config(),groups_[static_cast<std::size_t>(dest)].app);
     if(from&&to)app_.movePin(*from,*to);
    }
   }
   return 0;
  }
 }break;
 case WM_MBUTTONUP:if(!top_){cancelPreviewTimers();app_.closeDockPreview();previewPin_.reset();int id=hit(GET_X_LPARAM(l)/scale(),GET_Y_LPARAM(l)/scale());if(id>=0&&static_cast<std::size_t>(id)<groups_.size())app_.activateGroup(groups_[static_cast<std::size_t>(id)],true);return 0;}break;
 default:break;
 }
 handled=false;return 0;
}
}



