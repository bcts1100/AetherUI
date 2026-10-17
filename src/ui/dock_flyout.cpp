#include "dock_flyout.hpp"
#include <cmath>
namespace aether::ui {
namespace {
constexpr UINT_PTR LeaveTimer=1;constexpr int EscapeHotkey=7;
constexpr int PreviousId=1,NextId=2,CloseId=3,CardId=100;
std::wstring safeTitle(std::wstring text){
 if(text.size()>256)text.resize(256);
 for(auto& c:text)if(c<32)c=L' ';
 return text.empty()?L"Untitled window":text;
}
std::wstring fallback(win::PreviewState state){
 switch(state){
 case win::PreviewState::MinimizedSource:return L"Minimized - select to restore";
 case win::PreviewState::HiddenSource:return L"Window is hidden";
 case win::PreviewState::InvalidSource:return L"Window has closed";
 default:return L"Preview unavailable - select to switch";
 }
}
}
DockFlyout::DockFlyout(Graphics& graphics,win::IconCache& icons,std::function<void()> onClosed):Surface(graphics,FontPart::Dock),icons_(icons),onClosed_(std::move(onClosed)){
 if(!create(L"Aether Dock Windows",WS_POPUP,WS_EX_TOOLWINDOW|WS_EX_TOPMOST,{0,0,400,240}))
  throw std::runtime_error("Could not create dock window flyout.");
 auto button=[&](const wchar_t* text,int id){
  HWND control=CreateWindowExW(0,L"BUTTON",text,WS_CHILD|WS_TABSTOP|BS_OWNERDRAW,0,0,1,1,hwnd_,
   reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
  if(!control)throw std::runtime_error("Could not create dock window action.");
  SetWindowSubclass(control,buttonProcedure,1,reinterpret_cast<DWORD_PTR>(this));return control;
 };
 previous_=button(L"Previous windows",PreviousId);next_=button(L"Next windows",NextId);close_=button(L"Close window list",CloseId);
 for(int i=0;i<6;++i)cards_[static_cast<std::size_t>(i)]=button(L"Switch to window",CardId+i);
}
DockFlyout::~DockFlyout(){
 close();for(auto card:cards_)if(card)RemoveWindowSubclass(card,buttonProcedure,1);
 for(auto control:{previous_,next_,close_})if(control)RemoveWindowSubclass(control,buttonProcedure,1);
 destroy();if(font_)DeleteObject(font_);
}
std::vector<win::Window> DockFlyout::validWindows(const std::vector<win::Window>& source) const {
 std::vector<win::Window> result;result.reserve(std::min<std::size_t>(source.size(),2048));
 for(const auto& window:source){
  if(result.size()>=2048)break;
  if(window.hwnd==hwnd_||!win::windowAlive(window.hwnd,window.pid)||GetAncestor(window.hwnd,GA_ROOT)!=window.hwnd)continue;
  if(std::any_of(result.begin(),result.end(),[&](const win::Window& other){return other.hwnd==window.hwnd&&other.pid==window.pid;}))continue;
  result.push_back(window);
 }
 return result;
}
void DockFlyout::releasePreviews(){for(auto& preview:previews_)preview.hide();}
std::size_t DockFlyout::registeredPreviewCount() const {
 return static_cast<std::size_t>(std::count_if(previews_.begin(),previews_.end(),[](const auto& preview){return preview.nativeHandle()!=nullptr;}));
}
void DockFlyout::close(){
 KillTimer(hwnd_,LeaveTimer);if(escapeRegistered_){UnregisterHotKey(hwnd_,EscapeHotkey);escapeRegistered_=false;}
 releasePreviews();hide();windows_.clear();offset_=selected_=0;hover_=-1;
}
bool DockFlyout::containsPointer() const {
 if(!visible())return false;POINT point{};RECT rect{};
 return GetCursorPos(&point)&&GetWindowRect(hwnd_,&rect)&&PtInRect(&rect,point);
}
bool DockFlyout::pointerInAnchor() const {POINT point{};auto rect=win::native(anchor_);return GetCursorPos(&point)&&PtInRect(&rect,point);}
void DockFlyout::open(const win::Monitor& monitor,const Pin& app,Rect anchor,Edge edge,const std::vector<win::Window>& windows,const Theme& theme,bool previews,bool focus){
 close();monitor_=monitor;application_=app;anchor_=anchor;edge_=edge;theme_=theme;previewsEnabled_=previews;
 windows_=validWindows(windows);if(windows_.empty())return;
 auto active=std::find_if(windows_.begin(),windows_.end(),[](const win::Window& window){return window.active;});
 selected_=active==windows_.end()?0:static_cast<int>(std::distance(windows_.begin(),active));
 arrange();if(geometry_.capacity<=0){close();return;}
 show(focus);escapeRegistered_=RegisterHotKey(hwnd_,EscapeHotkey,MOD_NOREPEAT,VK_ESCAPE)!=FALSE;
 syncPreviews();invalidate();if(focus)focusSelected();
}
void DockFlyout::update(const std::vector<win::Window>& windows,const Theme& theme,bool previews){
 if(!visible())return;
 auto selected=selected_>=0&&static_cast<std::size_t>(selected_)<windows_.size()?std::optional<win::Window>(windows_[static_cast<std::size_t>(selected_)]):std::nullopt;
 auto nextWindows=validWindows(windows);if(nextWindows.empty()){close();return;}
 bool changed=nextWindows!=windows_||theme!=theme_||previews!=previewsEnabled_;
 if(changed){
  HWND focused=GetFocus();bool ownsFocus=focused==hwnd_||IsChild(hwnd_,focused);
  bool focusedCard=std::find(cards_.begin(),cards_.end(),focused)!=cards_.end();
  windows_=std::move(nextWindows);theme_=theme;previewsEnabled_=previews;
  auto previous=selected?std::find_if(windows_.begin(),windows_.end(),[&](const win::Window& window){return window.hwnd==selected->hwnd&&window.pid==selected->pid;}):windows_.end();
  selected_=previous==windows_.end()?std::clamp(selected_,0,static_cast<int>(windows_.size())-1):static_cast<int>(std::distance(windows_.begin(),previous));
  arrange();
  if(ownsFocus){
   if(focusedCard||focused==hwnd_||!IsWindowVisible(focused)||!IsWindowEnabled(focused))focusSelected();
   else if(GetFocus()!=focused)SetFocus(focused);
  }
  invalidate();
 }
 syncPreviews();
}
void DockFlyout::arrange(){
 if(arranging_)return;arranging_=true;
 geometry_=dockFlyoutGeometry(monitor_.work,anchor_,edge_,monitor_.dpi,static_cast<int>(windows_.size()));
 if(geometry_.capacity>0){
  offset_=std::clamp(offset_,0,std::max(0,(static_cast<int>(windows_.size())-1)/geometry_.capacity*geometry_.capacity));
  if(selected_<offset_||selected_>=offset_+geometry_.capacity)offset_=selected_/geometry_.capacity*geometry_.capacity;
  move(geometry_.bounds,false);win::roundedWindow(hwnd_,theme_.radius);positionControls();
 }
 arranging_=false;
}
D2D1_RECT_F DockFlyout::cardRect(int slot) const {
 RECT client{};GetClientRect(hwnd_,&client);float s=scale(),width=client.right/s,height=client.bottom/s;
 int columns=std::max(1,geometry_.columns),rows=std::max(1,(geometry_.capacity+columns-1)/columns);
 float gap=std::min(8.f,width/20.f),pad=std::min(12.f,width/15.f);
 float top=std::min(48.f,height*.18f),bottom=std::min(48.f,height*.18f);
 float cardWidth=std::max(1.f,(width-2*pad-gap*(columns-1))/columns);
 float cardHeight=std::max(1.f,(height-top-bottom-gap*(rows-1))/rows);
 return box(pad+(slot%columns)*(cardWidth+gap),top+(slot/columns)*(cardHeight+gap),cardWidth,cardHeight);
}
D2D1_RECT_F DockFlyout::previewRect(int slot) const {
 auto card=cardRect(slot);float height=card.bottom-card.top;
 if(height<110.f)return {};
 return {card.left+8.f,card.top+32.f,card.right-8.f,card.bottom-40.f};
}
void DockFlyout::positionControls(){
 RECT client{};GetClientRect(hwnd_,&client);float s=scale(),width=client.right/s,height=client.bottom/s;
 int dpi=win::dpiFor(hwnd_);
 if(!font_||fontDpi_!=dpi){
  auto font=CreateFontW(-MulDiv(12,dpi,96),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
  if(font){if(font_)DeleteObject(font_);font_=font;fontDpi_=dpi;}
 }
 auto position=[&](HWND control,D2D1_RECT_F rect,bool shown){
  SetWindowPos(control,nullptr,static_cast<int>(std::lround(rect.left*s)),static_cast<int>(std::lround(rect.top*s)),
   std::max(1,static_cast<int>(std::lround((rect.right-rect.left)*s))),std::max(1,static_cast<int>(std::lround((rect.bottom-rect.top)*s))),SWP_NOACTIVATE|SWP_NOZORDER|(shown?SWP_SHOWWINDOW:SWP_HIDEWINDOW));
  if(font_)SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(font_),FALSE);
 };
 int count=std::min(geometry_.capacity,static_cast<int>(windows_.size())-offset_);
 for(int i=0;i<6;++i){
  auto card=cardRect(i);float buttonHeight=std::min(32.f,std::max(12.f,card.bottom-card.top));
  position(cards_[static_cast<std::size_t>(i)],box(card.left+4.f,card.bottom-buttonHeight,card.right-card.left-8.f,buttonHeight-3.f),i<count);
 }
 float footerHeight=std::min(32.f,height*.14f),actionWidth=std::min(100.f,(width-24.f)/3.f);
 position(previous_,box(12.f,height-footerHeight-8.f,actionWidth,footerHeight),windows_.size()>static_cast<std::size_t>(geometry_.capacity));
 position(next_,box(width-actionWidth-12.f,height-footerHeight-8.f,actionWidth,footerHeight),windows_.size()>static_cast<std::size_t>(geometry_.capacity));
 EnableWindow(previous_,offset_>0);EnableWindow(next_,offset_+geometry_.capacity<static_cast<int>(windows_.size()));
 position(close_,box(std::max(0.f,width-38.f),6.f,30.f,std::min(30.f,height*.13f)),true);
}
void DockFlyout::syncPreviews(){
 if(!visible()){releasePreviews();return;}
 int count=std::min(geometry_.capacity,static_cast<int>(windows_.size())-offset_);bool changed=false;float s=scale();
 for(int i=0;i<6;++i){
  auto& preview=previews_[static_cast<std::size_t>(i)];auto oldState=preview.state();
  if(i>=count){preview.hide();continue;}
  const auto& window=windows_[static_cast<std::size_t>(offset_+i)];auto region=previewRect(i);
  if(!previewsEnabled_||region.right<=region.left||region.bottom<=region.top)preview.hide();
  else preview.show(hwnd_,window.hwnd,window.pid,{static_cast<LONG>(std::lround(region.left*s)),static_cast<LONG>(std::lround(region.top*s)),static_cast<LONG>(std::lround(region.right*s)),static_cast<LONG>(std::lround(region.bottom*s))});
  std::wstring title=safeTitle(window.title);
  if(IsIconic(window.hwnd))title+=L" (Minimized)";
  else if(preview.state()==win::PreviewState::InvalidSource)title+=L" (Closed)";
  auto control=cards_[static_cast<std::size_t>(i)];if(win::windowText(control)!=title){SetWindowTextW(control,title.c_str());changed=true;}
  changed|=oldState!=preview.state();
 }
 if(changed)invalidate();
}
int DockFlyout::hit(float x,float y) const {
 int count=std::min(geometry_.capacity,static_cast<int>(windows_.size())-offset_);
 for(int i=0;i<count;++i)if(contains(cardRect(i),x,y))return offset_+i;return -1;
}
void DockFlyout::focusSelected(){
 int slot=selected_-offset_;if(slot>=0&&slot<geometry_.capacity&&slot<6)SetFocus(cards_[static_cast<std::size_t>(slot)]);
}
void DockFlyout::select(int delta,bool focus){
 if(windows_.empty())return;selected_=std::clamp(selected_+delta,0,static_cast<int>(windows_.size())-1);
 if(selected_<offset_||selected_>=offset_+geometry_.capacity){offset_=selected_/geometry_.capacity*geometry_.capacity;releasePreviews();positionControls();syncPreviews();}
 if(focus)focusSelected();invalidate();for(auto card:cards_)InvalidateRect(card,nullptr,FALSE);
}
void DockFlyout::changePage(int delta,bool focus){
 if(geometry_.capacity<=0)return;
 int next=offset_+delta*geometry_.capacity;if(next<0||next>=static_cast<int>(windows_.size()))return;
 releasePreviews();offset_=next;selected_=offset_;hover_=-1;positionControls();syncPreviews();if(focus)focusSelected();invalidate();
}
void DockFlyout::activate(int index){
 if(index<0||static_cast<std::size_t>(index)>=windows_.size())return;auto window=windows_[static_cast<std::size_t>(index)];
 if(!win::windowAlive(window.hwnd,window.pid)){update(windows_,theme_,previewsEnabled_);return;}
 close();win::activate(window);
}
void DockFlyout::key(WPARAM key){
 switch(key){
 case VK_ESCAPE:close();break;case VK_RETURN:activate(selected_);break;
 case VK_LEFT:select(-1);break;case VK_RIGHT:select(1);break;
 case VK_UP:select(-std::max(1,geometry_.columns));break;case VK_DOWN:select(std::max(1,geometry_.columns));break;
 case VK_PRIOR:changePage(-1);break;case VK_NEXT:changePage(1);break;
 case VK_HOME:select(-selected_);break;case VK_END:select(static_cast<int>(windows_.size())-1-selected_);break;
 case VK_TAB:{
  std::vector<HWND> controls;for(auto card:cards_)if(IsWindowVisible(card)&&IsWindowEnabled(card))controls.push_back(card);
  for(auto control:{previous_,next_,close_})if(IsWindowVisible(control)&&IsWindowEnabled(control))controls.push_back(control);
  if(controls.empty())break;auto current=std::find(controls.begin(),controls.end(),GetFocus());const int direction=(GetKeyState(VK_SHIFT)&0x8000)?-1:1;
  int index=current==controls.end()?(direction>0?0:static_cast<int>(controls.size())-1):(static_cast<int>(std::distance(controls.begin(),current))+direction+static_cast<int>(controls.size()))%static_cast<int>(controls.size());SetFocus(controls[static_cast<std::size_t>(index)]);break;
 }
 }
}
void DockFlyout::pointer(float x,float y){KillTimer(hwnd_,LeaveTimer);int hover=hit(x,y);if(hover!=hover_){hover_=hover;invalidate();}}
void DockFlyout::leave(){if(visible())SetTimer(hwnd_,LeaveTimer,350,nullptr);}
void DockFlyout::click(float x,float y){activate(hit(x,y));}
void DockFlyout::paint(Canvas& canvas){
 auto size=canvas.size();canvas.fill(box(0,0,size.width,size.height),theme_.background,static_cast<float>(theme_.radius));
 canvas.stroke(box(.5f,.5f,size.width-1.f,size.height-1.f),theme_.raised,static_cast<float>(theme_.radius));
 canvas.text(win::wide(application_.name),box(14.f,6.f,std::max(0.f,size.width-58.f),30.f),theme_.foreground,14,true);
 int count=std::min(geometry_.capacity,static_cast<int>(windows_.size())-offset_);
 for(int i=0;i<count;++i){
  int index=offset_+i;const auto& window=windows_[static_cast<std::size_t>(index)];auto card=cardRect(i);
  canvas.fill(card,index==selected_||index==hover_?theme_.raised:theme_.surface,8.f);
  if(index==selected_)canvas.stroke(card,theme_.accent,8.f,1.2f);
  auto target=window.path.empty()?win::wide(application_.target):window.path;
  canvas.icon(icons_.request(target,target),box(card.left+8.f,card.top+4.f,24.f,24.f),theme_.muted);
  canvas.text(window.active?L"Active window":IsIconic(window.hwnd)?L"Minimized":L"Open window",box(card.left+40.f,card.top+4.f,std::max(0.f,card.right-card.left-48.f),24.f),theme_.muted,10);
  auto region=previewRect(i);if(region.right>region.left&&region.bottom>region.top){
   canvas.fill(region,theme_.background,4.f);
   if(previews_[static_cast<std::size_t>(i)].state()!=win::PreviewState::Ready)
    canvas.text(previewsEnabled_?fallback(previews_[static_cast<std::size_t>(i)].state()):L"Window previews are off",region,theme_.muted,11,false,1);
  }
 }
 if(windows_.size()>static_cast<std::size_t>(geometry_.capacity))
  canvas.text(std::to_wstring(offset_+1)+L"-"+std::to_wstring(offset_+count)+L" of "+std::to_wstring(windows_.size()),box(size.width*.35f,size.height-39.f,size.width*.3f,28.f),theme_.muted,10,false,1);
 else canvas.text(L"Select a title to switch - Esc closes",box(10.f,size.height-39.f,size.width-20.f,28.f),theme_.muted,10,false,1);
}
void DockFlyout::drawButton(const DRAWITEMSTRUCT& item){
 int id=static_cast<int>(item.CtlID);bool selected=id>=CardId&&offset_+id-CardId==selected_;
 auto value=selected?theme_.raised:theme_.surface;HBRUSH brush=CreateSolidBrush(RGB((value>>16)&255,(value>>8)&255,value&255));
 if(brush){FillRect(item.hDC,&item.rcItem,brush);DeleteObject(brush);}
 SetBkMode(item.hDC,TRANSPARENT);auto textColor=(item.itemState&ODS_DISABLED)?theme_.muted:theme_.foreground;
 SetTextColor(item.hDC,RGB((textColor>>16)&255,(textColor>>8)&255,textColor&255));
 auto previousFont=font_?SelectObject(item.hDC,font_):nullptr;auto rect=item.rcItem;rect.left+=6;rect.right-=6;
 auto title=id==CloseId?L"\u00D7":win::windowText(item.hwndItem);
 DrawTextW(item.hDC,title.c_str(),static_cast<int>(title.size()),&rect,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX|DT_CENTER);
 if(previousFont)SelectObject(item.hDC,previousFont);
 if(item.itemState&ODS_FOCUS){auto focusRect=item.rcItem;InflateRect(&focusRect,-2,-2);DrawFocusRect(item.hDC,&focusRect);}
}
LRESULT CALLBACK DockFlyout::buttonProcedure(HWND control,UINT message,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data){
 auto* self=reinterpret_cast<DockFlyout*>(data);if(!self)return DefSubclassProc(control,message,w,l);
 if(message==WM_KEYDOWN&&w==VK_RETURN){if(IsWindowEnabled(control))SendMessageW(control,BM_CLICK,0,0);return 0;}
 if(message==WM_KEYDOWN){self->key(w);if(w==VK_ESCAPE||w==VK_RETURN||w==VK_LEFT||w==VK_RIGHT||w==VK_UP||w==VK_DOWN||w==VK_PRIOR||w==VK_NEXT||w==VK_HOME||w==VK_END||w==VK_TAB)return 0;}
 if(message==WM_SETFOCUS){int id=GetDlgCtrlID(control);if(id>=CardId&&id<CardId+6){self->selected_=self->offset_+id-CardId;self->invalidate();}}
 if(message==WM_MOUSEMOVE){KillTimer(self->hwnd_,LeaveTimer);TRACKMOUSEEVENT tracking{sizeof(tracking),TME_LEAVE,control,0};TrackMouseEvent(&tracking);}
 if(message==WM_MOUSELEAVE)self->leave();
 return DefSubclassProc(control,message,w,l);
}
LRESULT DockFlyout::event(UINT message,WPARAM w,LPARAM l,bool& handled){
 handled=true;
 switch(message){
 case WM_CLOSE:close();return 0;
 case WM_HOTKEY:if(w==EscapeHotkey){close();return 0;}break;
 case WM_KEYDOWN:key(w);return 0;
 case WM_DRAWITEM:if(l){drawButton(*reinterpret_cast<DRAWITEMSTRUCT*>(l));return TRUE;}break;
 case WM_COMMAND:if(HIWORD(w)==BN_CLICKED){int id=LOWORD(w);if(id==PreviousId)changePage(-1);else if(id==NextId)changePage(1);else if(id==CloseId)close();else if(id>=CardId&&id<CardId+6)activate(offset_+id-CardId);return 0;}break;
 case WM_TIMER:if(w==LeaveTimer){KillTimer(hwnd_,LeaveTimer);if(!containsPointer()&&!pointerInAnchor())close();return 0;}break;
 case WM_ACTIVATE:if(LOWORD(w)==WA_INACTIVE&&visible()){close();return 0;}break;
  case WM_SHOWWINDOW:
  if(w)showing_=true;
  else {
   releasePreviews();KillTimer(hwnd_,LeaveTimer);if(escapeRegistered_){UnregisterHotKey(hwnd_,EscapeHotkey);escapeRegistered_=false;}
   bool notify=showing_;showing_=false;if(notify&&onClosed_)onClosed_();
  }
  break;
 case WM_SIZE:if(!arranging_){positionControls();syncPreviews();}break;
 case WM_DPICHANGED:canvas_.resize(hwnd_);if(!arranging_)close();return 0;
 case WM_NCDESTROY:releasePreviews();if(escapeRegistered_){UnregisterHotKey(hwnd_,EscapeHotkey);escapeRegistered_=false;}break;
 default:break;
 }
 handled=false;return 0;
}
}



