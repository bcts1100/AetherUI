#include "platform/dialogs.hpp"
#include "activities.hpp"
#include "app.hpp"
#include <cmath>
#include <unordered_map>
namespace aether::ui {
namespace {constexpr UINT OverviewAnimationTimer=71;}
void OverviewPreviews::hide() noexcept{for(auto& tile:tiles_)tile.hide();}
void OverviewPreviews::sync(HWND owner,std::span<const win::Window> sources,const OverviewGrid& grid,int offset,float scale,bool enabled,BYTE opacity) noexcept{
 if(!enabled||grid.capacity()<=0||!std::isfinite(scale)||scale<=0.f||!IsWindowVisible(owner)){hide();return;}
 const int capacity=std::min(static_cast<int>(tiles_.size()),grid.capacity());
 for(int slot=0;slot<static_cast<int>(tiles_.size());++slot){
  auto& preview=tiles_[static_cast<std::size_t>(slot)];const auto index=static_cast<std::int64_t>(offset)+slot;
  if(slot>=capacity||index<0||static_cast<std::uint64_t>(index)>=sources.size()){preview.hide();continue;}
  const auto& source=sources[static_cast<std::size_t>(index)];const auto bounds=grid.cell(slot);
  RECT region{static_cast<LONG>(std::lround((bounds.left+10.f)*scale)),static_cast<LONG>(std::lround((bounds.top+10.f)*scale)),
   static_cast<LONG>(std::lround((bounds.right-10.f)*scale)),static_cast<LONG>(std::lround((bounds.bottom-56.f)*scale))};
  preview.show(owner,source.hwnd,source.pid,region,opacity);
 }
}
Activities::Activities(Graphics& g,App& a):Surface(g,FontPart::Overview),app_(a),workspaces_(a.workspaces()){
 if(!create(L"Aether Activities",WS_POPUP,WS_EX_TOOLWINDOW|WS_EX_TOPMOST,{0,0,720,630}))throw std::runtime_error("Could not create Activities.");
 edit_=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL|WS_TABSTOP,0,0,100,30,hwnd_,reinterpret_cast<HMENU>(static_cast<INT_PTR>(10)),GetModuleHandleW(nullptr),nullptr);
 if(!edit_)throw std::runtime_error("Could not create Activities search box.");
 accessibility_=std::make_unique<Accessibility>(hwnd_);accessibleNativeName(edit_,L"Search applications");
 SendMessageW(edit_,EM_SETLIMITTEXT,256,0);
 SendMessageW(edit_,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"Search applications..."));
 SetWindowSubclass(edit_,editProcedure,1,reinterpret_cast<DWORD_PTR>(this));
}
Activities::~Activities(){cancelDrag();KillTimer(hwnd_,OverviewAnimationTimer);if(accessibility_)accessibility_->detach();previews_.hide();if(edit_)RemoveWindowSubclass(edit_,editProcedure,1);destroy();if(font_)DeleteObject(font_);if(editBrush_)DeleteObject(editBrush_);}
bool Activities::motionAllowed()const{BOOL enabled=TRUE;HIGHCONTRASTW contrast{sizeof(contrast)};return fullscreen_&&!app_.config().reduceMotion&&SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&enabled,0)&&enabled&&!(SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(contrast),&contrast,0)&&(contrast.dwFlags&HCF_HIGHCONTRASTON));}
void Activities::hide(){cancelDrag();KillTimer(hwnd_,OverviewAnimationTimer);RemovePropW(hwnd_,L"Aether.Overview.Animating");closing_=false;presentation_=1.f;afterClose_={};previews_.hide();Surface::hide();if(fullscreen_)canvas_.release();}
void Activities::transition(float target){
 KillTimer(hwnd_,OverviewAnimationTimer);transitionFrom_=presentation_;transitionTo_=target;closing_=target==0;transitionStarted_=GetTickCount64();transitionDuration_=closing_?160:220;
 if(!motionAllowed()||!SetTimer(hwnd_,OverviewAnimationTimer,16,nullptr)){presentation_=target;if(closing_){auto action=std::move(afterClose_);hide();if(action)action();}else{RemovePropW(hwnd_,L"Aether.Overview.Animating");syncPreview();invalidate();}return;}
 SetPropW(hwnd_,L"Aether.Overview.Animating",reinterpret_cast<HANDLE>(1));syncPreview();invalidate();
}
void Activities::animate(){
 if(!visible()){hide();return;}const float elapsed=static_cast<float>(GetTickCount64()-transitionStarted_)/static_cast<float>(transitionDuration_);
 SetPropW(hwnd_,L"Aether.Overview.Frames",reinterpret_cast<HANDLE>(++animationFrames_));
 presentation_=transitionFrom_+(transitionTo_-transitionFrom_)*overviewEase(elapsed);syncPreview();invalidate();
 if(elapsed>=1.f||!motionAllowed()){KillTimer(hwnd_,OverviewAnimationTimer);RemovePropW(hwnd_,L"Aether.Overview.Animating");presentation_=transitionTo_;if(closing_){auto action=std::move(afterClose_);hide();if(action)action();}else{syncPreview();invalidate();}}
}
void Activities::dismiss(){if(closing_)return;if(visible()&&fullscreen_)transition(0.f);else hide();}
void Activities::positionEdit(){
 if(!edit_)return;RECT r{};GetClientRect(hwnd_,&r);float s=scale();
 const float width=static_cast<float>(r.right)/s;const float searchWidth=std::min(720.f,width-64.f);
 SetWindowPos(edit_,nullptr,static_cast<int>((fullscreen_?(width-searchWidth)/2.f+46.f:58.f)*s),static_cast<int>((fullscreen_?78.f:56.f)*s),fullscreen_?static_cast<int>((searchWidth-68.f)*s):std::max(20,static_cast<int>(r.right-86*s)),static_cast<int>((fullscreen_?34.f:29.f)*s),SWP_NOZORDER);
 int dpi=win::dpiFor(hwnd_);
 int textScale=std::clamp(theme_.fontScale,85,125);auto family=validFontFamily(theme_.fontFamily)?theme_.fontFamily:std::string("Segoe UI");
 if(!font_||fontDpi_!=dpi||fontScale_!=textScale||fontFamily_!=family){
  auto nativeFamily=win::wide(family);auto next=CreateFontW(-MulDiv(17*textScale,dpi,9600),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,nativeFamily.c_str());
  if(next){SendMessageW(edit_,WM_SETFONT,reinterpret_cast<WPARAM>(next),TRUE);if(font_)DeleteObject(font_);font_=next;fontDpi_=dpi;fontScale_=textScale;fontFamily_=family;}
 }
 auto c=theme_.surface;
 if(!editBrush_||brushColor_!=c){
  auto next=CreateSolidBrush(RGB((c>>16)&255,(c>>8)&255,c&255));
  if(next){if(editBrush_)DeleteObject(editBrush_);editBrush_=next;brushColor_=c;}
 }
 InvalidateRect(edit_,nullptr,TRUE);
}
void Activities::open(const win::Monitor& m,bool openWindows){
 const float resume=visible()&&closing_?presentation_:0.f;KillTimer(hwnd_,OverviewAnimationTimer);afterClose_={};closing_=false;fullscreen_=openWindows;presentation_=fullscreen_?resume:1.f;
 monitorName_=m.name;notice_.clear();groupKey_.clear();cancelDrag();if(openWindows){workspaces_.refresh();workspacePage_=0;const auto& items=workspaces_.items();auto current=std::find_if(items.begin(),items.end(),[](const auto& item){return item.current;});const int perPage=std::clamp(static_cast<int>((static_cast<float>(m.bounds.width())*96.f/m.dpi-180.f)/150.f),1,6);if(current!=items.end())workspacePage_=static_cast<int>(std::distance(items.begin(),current))/perPage;}setTheme(app_.theme());
 move(fullscreen_?m.bounds:popupBounds(m.work,720,630,m.dpi),false);win::roundedWindow(hwnd_,fullscreen_?0:theme_.radius);
 SetPropW(hwnd_,L"Aether.Overview.Fullscreen",reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(fullscreen_?1:0)));
 positionEdit();SetWindowTextW(edit_,L"");switchTab(openWindows);show(true);SetWindowPos(hwnd_,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);focusPart(Focus::Search);transition(1.f);
}
Activities::Layout Activities::layout() const{
 RECT r{};GetClientRect(hwnd_,&r);const float s=scale();
 const float width=static_cast<float>(r.right)/s,height=static_cast<float>(r.bottom)/s;
 auto grid=fullscreen_?(windowsTab_?workspaceOverviewLayout(width,height,static_cast<int>(results_.size())):fullscreenOverviewLayout(width,height,static_cast<int>(results_.size()))):windowsTab_?overviewLayout(width,height):OverviewGrid{};
 if(fullscreen_){const float zoom=.94f+.06f*presentation_;grid.left=width/2.f+(grid.left-width/2.f)*zoom;grid.top=height/2.f+(grid.top-height/2.f)*zoom+18.f*(1.f-presentation_);grid.gap*=zoom;grid.tileWidth*=zoom;grid.tileHeight*=zoom;}
 return {width,height-40.f,grid};
}
int Activities::visibleRows() const{return std::max(0,static_cast<int>((layout().listBottom-146.f)/54.f));}
void Activities::syncPreview(){
 std::array<win::PreviewState,12> previous{};for(int slot=0;slot<12;++slot)previous[static_cast<std::size_t>(slot)]=previews_.at(slot).state();
 if(!visible()||!windowsTab_||!app_.config().windowPreviews){previews_.hide();}
 else{
  const auto grid=layout().grid;std::vector<win::Window> sources;const int count=static_cast<int>(results_.size());
  for(int index=offset_;index>=0&&index<count&&index<offset_+grid.capacity();++index)if(results_[static_cast<std::size_t>(index)].window)sources.push_back(*results_[static_cast<std::size_t>(index)].window);
  previews_.sync(hwnd_,sources,grid,0,scale(),true,static_cast<BYTE>(std::lround(255.f*presentation_)));
 }
 for(int slot=0;slot<12;++slot)if(previous[static_cast<std::size_t>(slot)]!=previews_.at(slot).state()){invalidate();break;}
}
void Activities::rebuild(bool resetSelection){
 std::wstring query=win::windowText(edit_);std::vector<Result> next;
 auto matchPin=[&](const win::Application& application)->std::optional<Pin>{
  auto normalize=[](std::wstring value){value=win::lower(value);std::replace(value.begin(),value.end(),L'\\',L'/');return value;};
  auto target=normalize(application.target),identity=normalize(application.identity);
  for(const auto& pin:app_.config().pins)if(normalize(win::wide(pin.target))==target||(!identity.empty()&&normalize(win::wide(pin.identity))==identity))return pin;return {};
 };
 if(windowsTab_){
  for(const auto& w:app_.windows()){
   if(!filterMonitor_.empty()&&w.display!=filterMonitor_)continue;
   if(!groupKey_.empty()&&win::overviewAppKey(w)!=groupKey_)continue;
   int score=std::max(searchScore(query,w.title),searchScore(query,std::filesystem::path(w.path).stem().wstring()));if(score)next.push_back({{w.title,w.path,w.identity},w,score+(w.active?20:0)});
  }
  if(grouped_&&groupKey_.empty()){
   std::vector<Result> groups;std::unordered_map<std::wstring,std::size_t> indices;
   for(auto& result:next){const auto key=win::overviewAppKey(*result.window);auto [entry,inserted]=indices.try_emplace(key,groups.size());
    if(inserted){result.members.push_back(*result.window);groups.push_back(std::move(result));}
    else{auto& group=groups[entry->second];group.members.push_back(*result.window);if(result.score>group.score){group.window=result.window;group.score=result.score;}}
   }
   for(auto& group:groups)if(group.members.size()>1){auto stem=std::filesystem::path(group.window->path).stem().wstring();group.app.name=(stem.empty()?group.app.name:stem)+L" ("+std::to_wstring(group.members.size())+L" windows)";}
   next=std::move(groups);
  }
 }else{
  auto apps=app_.catalog().snapshot();
  for(const auto& a:*apps){auto presented=a;auto pin=matchPin(a);if(pin)presented.name=win::wide(pin->name);int score=std::max(searchScore(query,presented.name),searchScore(query,a.name));if(score)next.push_back({std::move(presented),{},score,false,pin});}
  if(apps->empty())for(const auto& p:app_.config().pins){win::Application a{win::wide(p.name),win::wide(p.target),win::wide(p.identity)};int score=searchScore(query,a.name);if(score)next.push_back({std::move(a),{},score});}
 }
 for(auto& result:next){
  if(!result.presentation)result.presentation=matchPin(result.app);result.pinned=result.presentation.has_value();
  if(result.members.size()>1&&result.presentation)result.app.name=win::wide(result.presentation->name)+L" ("+std::to_wstring(result.members.size())+L" windows)";
 }
 std::stable_sort(next.begin(),next.end(),[](const Result& a,const Result& b){if(a.score!=b.score)return a.score>b.score;return win::lower(a.app.name)<win::lower(b.app.name);});
 auto sameIdentity=[](const Result& a,const Result& b){
  if(a.window||b.window)return a.window&&b.window&&a.window->hwnd==b.window->hwnd&&a.window->pid==b.window->pid;
  return win::lower(a.app.target)==win::lower(b.app.target);
 };
 auto sameResult=[&](const Result& a,const Result& b){return sameIdentity(a,b)&&a.app.name==b.app.name&&a.app.target==b.app.target&&a.app.identity==b.app.identity&&a.score==b.score&&a.pinned==b.pinned&&a.presentation==b.presentation&&a.members==b.members&&a.window==b.window;};
 if(!resetSelection&&next.size()==results_.size()&&std::equal(next.begin(),next.end(),results_.begin(),sameResult))return;
 std::optional<Result> previous;
 if(!resetSelection&&selected_>=0&&static_cast<std::size_t>(selected_)<results_.size())previous=results_[static_cast<std::size_t>(selected_)];
 results_=std::move(next);hover_=-1;SetPropW(hwnd_,L"Aether.Overview.Results",reinterpret_cast<HANDLE>(results_.size()));
 if(resetSelection){selected_=results_.empty()?-1:0;offset_=0;syncPreview();invalidate();return;}
 if(results_.empty()){selected_=-1;offset_=0;syncPreview();invalidate();return;}
 auto match=previous?std::find_if(results_.begin(),results_.end(),[&](const Result& r){return sameIdentity(*previous,r);}):results_.end();
 selected_=match==results_.end()?std::clamp(selected_,0,static_cast<int>(results_.size())-1):static_cast<int>(std::distance(results_.begin(),match));
 if(!windowsTab_)offset_=std::clamp(offset_,0,std::max(0,static_cast<int>(results_.size())-visibleRows()));
 select(0);
}
void Activities::update(){
 if(!app_.workspaceWallpaperNotice().empty())notice_=app_.workspaceWallpaperNotice();
 bool changed=theme_!=app_.theme();
 if(!filterMonitor_.empty()&&std::none_of(app_.displays().begin(),app_.displays().end(),[&](const auto& m){return m.name==filterMonitor_;})){filterMonitor_.clear();changed=true;}
 if(changed)setTheme(app_.theme());
 if(visible()){
  if(fullscreen_){const auto monitor=app_.display(monitorName_);RECT current{};GetWindowRect(hwnd_,&current);if(win::rect(current)!=monitor.bounds){monitorName_=monitor.name;move(monitor.bounds,false);}}
  if(changed){positionEdit();win::roundedWindow(hwnd_,fullscreen_?0:theme_.radius);}
  rebuild(false);syncPreview();
 }
}
void Activities::select(int delta){
 selected_=advanceSelection(selected_,delta,static_cast<int>(results_.size()));
 if(windowsTab_||fullscreen_){
  const auto grid=layout().grid;if(grid.capacity()>0)offset_=overviewPageOffset(selected_,static_cast<int>(results_.size()),grid);
 }else{
  int rows=visibleRows();
  offset_=std::clamp(offset_,0,std::max(0,static_cast<int>(results_.size())-std::max(1,rows)));
  // Keep the viewport anchor while a constrained popup has no complete result rows.
  if(rows>0){if(selected_<offset_)offset_=std::max(0,selected_);if(selected_>=offset_+rows)offset_=selected_-rows+1;}
 }
 syncPreview();invalidate();
}
void Activities::navigate(OverviewDirection direction){
 selected_=overviewNavigate(selected_,static_cast<int>(results_.size()),layout().grid,direction);select(0);
}
void Activities::activateSelected(){
 if(selected_<0||static_cast<std::size_t>(selected_)>=results_.size())return;
 auto result=results_[static_cast<std::size_t>(selected_)];
 if(result.members.size()>1){groupKey_=win::overviewAppKey(*result.window);changedView();focusPart(Focus::Results);return;}
 if(result.window&&!win::windowAlive(result.window->hwnd,result.window->pid)){rebuild(false);syncPreview();return;}
 auto action=[this,result]{if(result.window){if(win::windowAlive(result.window->hwnd,result.window->pid))win::activate(*result.window);}else app_.launchTarget(hwnd_,result.app.target);};
 if(fullscreen_){afterClose_=std::move(action);dismiss();}else{hide();action();}
}
void Activities::pinSelected(){
 if(selected_<0||static_cast<std::size_t>(selected_)>=results_.size())return;
 const auto result=results_[static_cast<std::size_t>(selected_)];
 const auto& app=result.app;
 app_.togglePin(result.presentation.value_or(Pin{win::utf8(app.name),win::utf8(app.target),win::utf8(app.identity)}));
 invalidate();
}
void Activities::changedView(){cancelDrag();if(focus_==Focus::Close)focus_=Focus::Results;rebuild();if(visible()&&fullscreen_){presentation_=0.f;transition(1.f);}SetPropW(hwnd_,L"Aether.Overview.Grouped",reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(grouped_)));invalidate();}
D2D1_RECT_F Activities::closeBounds(int index)const{
 const auto grid=layout().grid;const int slot=index-offset_;if(!fullscreen_||!windowsTab_||slot<0||slot>=grid.capacity()||index<0||index>=static_cast<int>(results_.size())||results_[static_cast<std::size_t>(index)].members.size()>1)return {};
 auto cell=grid.cell(slot);return box(cell.right-38.f,cell.bottom-43.f,28.f,28.f);
}
std::vector<Activities::Control> Activities::controls()const{
 std::vector<Control> result;if(!fullscreen_||!windowsTab_)return result;RECT r{};GetClientRect(hwnd_,&r);const float width=static_cast<float>(r.right)/scale(),height=static_cast<float>(r.bottom)/scale();
 if(height>=440.f){
  const float y=height>=600.f?188.f:174.f,monitorWidth=std::min(260.f,(width-76.f)*.52f),groupWidth=std::min(180.f,width-monitorWidth-76.f);
  std::wstring label=L"All monitors  ›";const auto& monitors=app_.displays();for(std::size_t i=0;i<monitors.size();++i)if(monitors[i].name==filterMonitor_)label=L"Monitor "+std::to_wstring(i+1)+(monitors[i].primary?L" · Primary  ›":L"  ›");
  result.push_back({20,std::move(label),box(32,y,monitorWidth,30),!filterMonitor_.empty()});
  result.push_back({21,grouped_?L"Group: Applications":L"Group: Off",box(44+monitorWidth,y,groupWidth,30),grouped_});
  if(!groupKey_.empty()&&width>700)result.push_back({22,L"Back to groups",box(56+monitorWidth+groupWidth,y,152,30)});
 }
 const float y=height-(height>=440.f?108.f:74.f),chipHeight=height>=440.f?52.f:38.f;
 if(!workspaces_.available()){result.push_back({23,L"Refresh workspaces",box(width/2.f-100,y,200,chipHeight)});return result;}
 const int perPage=std::clamp(static_cast<int>((width-180.f)/150.f),1,6),count=static_cast<int>(workspaces_.items().size());
 const int page=std::clamp(workspacePage_,0,std::max(0,(count-1)/perPage)),first=page*perPage,shown=std::min(perPage,count-first);
 const float chipWidth=std::min(150.f,(width-180.f)/static_cast<float>(std::max(1,shown))),left=(width-(chipWidth+8.f)*shown-44.f)/2.f;
 if(first>0)result.push_back({30,L"‹",box(24,y,34,chipHeight)});
 for(int i=0;i<shown;++i){const auto& item=workspaces_.items()[static_cast<std::size_t>(first+i)];result.push_back({4000+first+i,item.name,box(left+i*(chipWidth+8),y,chipWidth,chipHeight),item.current});}
 result.push_back({32,L"+",box(left+shown*(chipWidth+8),y,36,chipHeight),false,count<16});
 if(first+shown<count)result.push_back({31,L"›",box(width-58,y,34,chipHeight)});
 return result;
}
void Activities::monitorMenu(){
 HMENU menu=CreatePopupMenu();if(!menu)return;AppendMenuW(menu,MF_STRING|(filterMonitor_.empty()?MF_CHECKED:0),1,L"All monitors");
 const auto monitors=app_.displays();for(std::size_t i=0;i<monitors.size();++i){auto name=L"Monitor "+std::to_wstring(i+1)+(monitors[i].primary?L" · Primary":L"");AppendMenuW(menu,MF_STRING|(filterMonitor_==monitors[i].name?MF_CHECKED:0),2+i,name.c_str());}
 POINT point{32,220};for(const auto& item:controls())if(item.id==20)point={static_cast<LONG>(item.bounds.left*scale()),static_cast<LONG>(item.bounds.bottom*scale())};ClientToScreen(hwnd_,&point);
 app_.beginInteraction();const UINT choice=TrackPopupMenuEx(menu,TPM_RETURNCMD|TPM_NONOTIFY,point.x,point.y,hwnd_,nullptr);DestroyMenu(menu);app_.endInteraction();
 if(choice==1)filterMonitor_.clear();else if(choice>=2&&choice<2+monitors.size())filterMonitor_=monitors[choice-2].name;else return;
 notice_.clear();changedView();
}
void Activities::closeSelected(){
 if(!windowsTab_||selected_<0||selected_>=static_cast<int>(results_.size()))return;const auto result=results_[static_cast<std::size_t>(selected_)];
 if(result.members.size()>1){notice_=L"Open the application group to close an individual window";invalidate();return;}
 if(result.window){notice_=win::requestWindowClose(*result.window)?L"Close requested · the application may ask to save":L"Window closed or unavailable";PostMessageW(app_.controller(),win::RefreshMessage,0,0);invalidate();}
}
void Activities::workspaceMenu(int index,bool renameOnly){
 if(index<0||index>=static_cast<int>(workspaces_.items().size()))return;
 const auto item=workspaces_.items()[static_cast<std::size_t>(index)];const auto appearance=workspaces_.appearance(item.id);
 workspaceEditing_=true;focusedControl_=4000+index;cancelDrag();app_.beginInteraction();
 struct End{Activities& ui;~End(){ui.workspaceEditing_=false;ui.app_.endInteraction();if(ui.visible()){SetForegroundWindow(ui.hwnd_);ui.focusPart(Focus::Workspaces);ui.invalidate();}}}end{*this};
 UINT selected=renameOnly?1:0;
 if(!renameOnly){auto menu=CreatePopupMenu();if(!menu)return;AppendMenuW(menu,MF_STRING,1,L"Rename workspace…");AppendMenuW(menu,MF_STRING|(index==0?MF_GRAYED:0),2,L"Move left");AppendMenuW(menu,MF_STRING|(index+1==static_cast<int>(workspaces_.items().size())?MF_GRAYED:0),3,L"Move right");AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,4,L"Choose wallpaper image…");
  const wchar_t* labels[]={L"Use default background",L"Aurora — still",L"Nebula — still",L"Ember dunes — still",L"Aurora — animated",L"Nebula — animated",L"Ember dunes — animated"};for(int mode=0;mode<=6;++mode)AppendMenuW(menu,MF_STRING|(appearance.background==mode?MF_CHECKED:0),10+mode,labels[mode]);AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING|(appearance.background==8?MF_CHECKED:0),18,L"Woodland Zen — still");AppendMenuW(menu,MF_STRING|(appearance.background==9?MF_CHECKED:0),19,L"Woodland Zen — animated");
  POINT point{};GetCursorPos(&point);selected=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,hwnd_,nullptr);DestroyMenu(menu);
 }
 bool changed=false;
 if(selected==1){auto name=win::editNote(hwnd_,win::utf8(item.name),L"Rename workspace",L"Aether workspace name — up to 80 characters. Empty resets it.",80);if(name)changed=workspaces_.rename(item.id,win::wide(*name));}
 else if(selected==2||selected==3)changed=workspaces_.reorder(item.id,index+(selected==2?-1:1));
 else if(selected==4){const COMDLG_FILTERSPEC filters[]={{L"Wallpaper images",L"*.png;*.jpg;*.jpeg;*.bmp"}};auto path=win::chooseFile(hwnd_,false,L"Choose this workspace's wallpaper",filters,1);if(path)changed=workspaces_.setAppearance(item.id,{7,path->wstring()});}
 else if((selected>=10&&selected<=16)||selected==18||selected==19)changed=workspaces_.setAppearance(item.id,{static_cast<int>(selected-10),{}});
 if(changed){notice_=L"Workspace customization saved";app_.workspaceAppearanceChanged();}else if(selected)notice_=workspaces_.notice();
}
void Activities::workspaceAction(int index,bool move){
 if(!workspaces_.available()||index<0||index>=static_cast<int>(workspaces_.items().size()))return;const auto id=workspaces_.items()[static_cast<std::size_t>(index)].id;
 if(move){
  if(selected_<0||selected_>=static_cast<int>(results_.size()))return;const auto result=results_[static_cast<std::size_t>(selected_)];
  if(!result.window||result.members.size()>1){notice_=L"Open the group to move an individual window";invalidate();return;}
  notice_=workspaces_.move(*result.window,id)?L"Window moved to "+workspaces_.items()[static_cast<std::size_t>(index)].name:workspaces_.notice();
  PostMessageW(app_.controller(),win::RefreshMessage,0,0);syncPreview();invalidate();
 }else if(workspaces_.switchTo(id)){hide();PostMessageW(app_.controller(),win::WorkspaceChangedMessage,0,0);PostMessageW(app_.controller(),win::RefreshMessage,0,0);}else{notice_=workspaces_.notice();invalidate();}
}
void Activities::control(int id){
 if(id==20){const auto& monitors=app_.displays();auto current=std::find_if(monitors.begin(),monitors.end(),[&](const auto& m){return m.name==filterMonitor_;});if(current==monitors.end()){if(!monitors.empty())filterMonitor_=monitors.front().name;}else if(++current==monitors.end())filterMonitor_.clear();else filterMonitor_=current->name;notice_.clear();changedView();return;}if(id==21){grouped_=!grouped_;groupKey_.clear();notice_.clear();changedView();return;}
 if(id==22){const auto previous=groupKey_;groupKey_.clear();changedView();auto found=std::find_if(results_.begin(),results_.end(),[&](const auto& result){return result.window&&win::overviewAppKey(*result.window)==previous;});if(found!=results_.end()){selected_=static_cast<int>(std::distance(results_.begin(),found));select(0);}return;}
 if(id==23){workspaces_.refresh();notice_=workspaces_.notice();invalidate();return;}
 if(id==30||id==31){workspacePage_=std::max(0,workspacePage_+(id==30?-1:1));invalidate();return;}
 if(id==32){if(workspaces_.create()){const auto width=layout().listWidth;const int perPage=std::clamp(static_cast<int>((width-180.f)/150.f),1,6);workspacePage_=static_cast<int>(workspaces_.items().size()-1)/perPage;notice_=L"Workspace created · drop a window onto it";}else notice_=workspaces_.notice().empty()?L"Workspace limit reached":workspaces_.notice();invalidate();return;}
 if(id>=4000&&id<4128)workspaceAction(id-4000,false);
}
void Activities::cancelDrag(){drag_={};if(GetCapture()==hwnd_)ReleaseCapture();SetCursor(LoadCursorW(nullptr,IDC_ARROW));}
int Activities::workspaceHit(float x,float y)const{for(const auto& item:controls())if(item.id>=4000&&item.id<4128&&x>=item.bounds.left&&x<item.bounds.right&&y>=item.bounds.top&&y<item.bounds.bottom)return item.id-4000;return -1;}
void Activities::dragPointer(float x,float y){
 if(!drag_.window&&!drag_.workspace)return;const bool before=drag_.active;
 if(!drag_.active&&(std::abs(x-static_cast<float>(drag_.origin.x))>=6.f||std::abs(y-static_cast<float>(drag_.origin.y))>=6.f))drag_.active=true;
 if(drag_.active){const int target=workspaceHit(x,y);if(!before||target!=drag_.target){drag_.target=target;invalidate();}SetCursor(LoadCursorW(nullptr,target>=0?IDC_HAND:IDC_SIZEALL));}
}
void Activities::paint(Canvas& c){
 if(fullscreen_){paintOverview(c);return;}
 auto size=c.size();float width=size.width,height=size.height;const auto content=layout();float listWidth=content.listWidth;
 Theme frameTheme=theme_;frameTheme.surface=theme_.background;c.surface(box(0,0,width,height),frameTheme,static_cast<float>(theme_.radius));
 c.text(L"Activities",box(24,7,200,32),theme_.foreground,15,true);
 c.text(L"ESC to close",box(width-130,8,104,30),theme_.muted,11,false,2);
 c.fill(box(20,46,width-40,49),theme_.surface,10);c.stroke(box(20,46,width-40,49),focus_==Focus::Search?theme_.accent:theme_.raised,10,1.2f);
 c.glyph(L'\uE721',box(27,49,27,41),theme_.muted,17);
 c.fill(box(windowsTab_?183.f:20.f,106,154,29),theme_.raised,8);
 c.text(L"Applications",box(24,106,145,29),windowsTab_?theme_.muted:theme_.foreground,12,true,1);
 c.text(L"Open windows",box(187,106,145,29),windowsTab_?theme_.foreground:theme_.muted,12,true,1);
 c.text(std::to_wstring(results_.size())+L" results",box(width-130,106,108,29),theme_.muted,11,false,2);
 if(windowsTab_){
  const auto grid=content.grid;const int count=std::max(0,std::min(grid.capacity(),static_cast<int>(results_.size())-offset_));
  for(int slot=0;slot<count;++slot){
   const int index=offset_+slot;const auto& result=results_[static_cast<std::size_t>(index)];const auto bounds=grid.cell(slot);
   const D2D1_RECT_F frame{bounds.left,bounds.top,bounds.right,bounds.bottom};const float tileWidth=bounds.right-bounds.left;
   c.fill(frame,index==selected_||index==hover_?theme_.raised:theme_.surface,11);
   c.stroke(frame,index==selected_?theme_.accent:theme_.raised,11,index==selected_?2.f:1.f);
   const auto image=box(bounds.left+10.f,bounds.top+10.f,tileWidth-20.f,bounds.bottom-bounds.top-66.f);c.fill(image,theme_.background,5);
   const auto& preview=previews_.at(slot);const auto icon=app_.icons().request(result.presentation.value_or(Pin{win::utf8(result.app.name),win::utf8(result.app.target),win::utf8(result.app.identity)}));
   if(preview.state()!=win::PreviewState::Ready){
    std::wstring_view fallback=app_.config().windowPreviews?L"Preview unavailable":L"Live previews disabled";
    switch(preview.state()){
    case win::PreviewState::InvalidSource:fallback=L"Window closed";break;
    case win::PreviewState::HiddenSource:fallback=L"Window hidden";break;
    case win::PreviewState::MinimizedSource:fallback=L"Minimized - Enter to restore";break;
    default:break;
    }
    const float imageHeight=image.bottom-image.top;const float iconSize=std::min(52.f,imageHeight*.5f);
    c.icon(icon,box(image.left+(image.right-image.left-iconSize)/2.f,image.top+std::max(0.f,(imageHeight-iconSize-28.f)/2.f),iconSize,iconSize),theme_.muted);
    c.text(fallback,box(image.left+4.f,image.bottom-25.f,image.right-image.left-8.f,23.f),theme_.muted,10,false,1);
   }
   c.icon(icon,box(bounds.left+12.f,bounds.bottom-42.f,26.f,26.f),theme_.muted);
   c.text(result.app.name,box(bounds.left+47.f,bounds.bottom-45.f,tileWidth-59.f,26.f),theme_.foreground,12,index==selected_);
   if(result.pinned)c.glyph(L'\uE718',box(bounds.right-26.f,bounds.bottom-18.f,14.f,14.f),theme_.accent,10);
  }
  if(grid.capacity()==0)c.text(L"More space needed for window tiles",box(20,146,std::max(0.f,width-40.f),std::max(0.f,height-186.f)),theme_.muted,12,false,1);
 }else{
  int count=std::min(visibleRows(),static_cast<int>(results_.size())-offset_);
  for(int i=0;i<count;++i){
   int index=offset_+i;const auto& result=results_[static_cast<std::size_t>(index)];float y=146.f+static_cast<float>(i)*54.f;
   if(index==selected_||index==hover_)c.fill(box(16,y,listWidth-32,51),theme_.surface,10);
   if(index==selected_)c.fill(box(16,y+10,3,31),theme_.accent,1);
   c.icon(app_.icons().request(result.presentation.value_or(Pin{win::utf8(result.app.name),win::utf8(result.app.target),win::utf8(result.app.identity)})),box(29,y+8,34,34),theme_.muted);
   c.text(result.app.name,box(80,y+4,listWidth-140,24),theme_.foreground,14,index==selected_);
   c.text(result.pinned?L"Pinned application":L"Installed application",box(80,y+26,listWidth-140,20),theme_.muted,10);
   if(result.pinned)c.glyph(L'\uE718',box(listWidth-54,y+10,26,28),theme_.accent,14);
  }
 }
 if(focus_==Focus::Applications||focus_==Focus::Windows)c.stroke(box(focus_==Focus::Applications?20.f:183.f,106,154,29),theme_.foreground,8,2);
 if(focus_==Focus::Results&&!windowsTab_&&selected_>=offset_&&selected_<offset_+visibleRows())c.stroke(box(16,146.f+(selected_-offset_)*54.f,listWidth-32,51),theme_.foreground,10,2);
 if(results_.empty())c.text(!windowsTab_&&app_.catalog().indexing()?L"Indexing installed applications...":L"No matches. Try a shorter name.",box(24,174,std::max(0.f,width-48.f),std::max(0.f,std::min(90.f,height-214.f))),theme_.muted,15,false,1);
 c.line(20,height-36,width-20,height-36,theme_.raised);
 if(windowsTab_){
  const int capacity=content.grid.capacity();const int pages=capacity>0?std::max(1,(static_cast<int>(results_.size())+capacity-1)/capacity):1;
  const int page=capacity>0?offset_/capacity+1:1;
  c.text(L"Arrows Select  |  Enter Switch  |  PgUp/PgDn  |  Ctrl+Tab Apps",box(24,height-33,std::max(0.f,width-165.f),27),theme_.muted,10);
  c.text(L"Page "+std::to_wstring(page)+L" / "+std::to_wstring(pages),box(width-128,height-33,104,27),theme_.muted,10,false,2);
 }else c.text(L"Tab  Focus    |    Enter  Open    |    Ctrl+P  Pin/unpin    |    Ctrl+Tab  Switch list",box(24,height-33,width-48,27),theme_.muted,10);
 accessibleSnapshot();
}
void Activities::paintOverview(Canvas& c){
 const auto size=c.size();const float width=size.width,height=size.height;const auto grid=layout().grid;
 Theme background=theme_;background.surface=theme_.background;background.gradientStrength=35;
 c.surface(box(0,0,width,height),background,0);c.fill(box(0,0,width,height),theme_.background,0,.28f);
 c.text(L"Activities",box(44,24,280,42),theme_.foreground,25,true);
 if(width>=900)c.text(L"Windows or Esc to close",box(width-320,26,276,36),theme_.muted,12,false,2);
 const float searchWidth=std::min(720.f,width-64.f),searchLeft=(width-searchWidth)/2.f;
 c.fill(box(searchLeft,66,searchWidth,58),theme_.surface,20);
 c.stroke(box(searchLeft,66,searchWidth,58),focus_==Focus::Search?theme_.accent:theme_.raised,20,1.5f);
 c.glyph(L'\uE721',box(searchLeft+14,76,26,36),theme_.muted,19);
 const float tabsLeft=width/2.f-170.f;
 c.fill(box(windowsTab_?tabsLeft+174:tabsLeft,138,166,34),theme_.raised,16);
 c.text(L"Applications",box(tabsLeft,138,166,34),windowsTab_?theme_.muted:theme_.foreground,13,true,1);
 c.text(L"Open windows",box(tabsLeft+174,138,166,34),windowsTab_?theme_.foreground:theme_.muted,13,true,1);
 if(focus_==Focus::Applications||focus_==Focus::Windows)c.stroke(box(focus_==Focus::Windows?tabsLeft+174:tabsLeft,138,166,34),theme_.foreground,16,2);
 const int count=std::max(0,std::min(grid.capacity(),static_cast<int>(results_.size())-offset_));
 for(int slot=0;slot<count;++slot){
  const int index=offset_+slot;const auto& result=results_[static_cast<std::size_t>(index)];const auto bounds=grid.cell(slot);const float tileWidth=bounds.right-bounds.left;
   const bool selected=index==selected_;const bool hover=index==hover_;const auto frame=D2D1::RectF(bounds.left,bounds.top,bounds.right,bounds.bottom);
  c.fill(box(bounds.left+3,bounds.top+8,tileWidth,bounds.bottom-bounds.top),0,18,.18f*presentation_);
  c.fill(frame,selected||hover?theme_.raised:theme_.surface,16,.65f+.35f*presentation_);
  c.stroke(frame,selected?theme_.accent:theme_.raised,16,selected?2.5f:1.f);
  if(drag_.active&&result.window&&drag_.window&&result.window->hwnd==drag_.window->hwnd)c.stroke(frame,theme_.accent,16,3.f);
  const auto icon=app_.icons().request(result.presentation.value_or(Pin{win::utf8(result.app.name),win::utf8(result.app.target),win::utf8(result.app.identity)}));
  const auto image=box(bounds.left+10,bounds.top+10,tileWidth-20,bounds.bottom-bounds.top-66);
  c.fill(image,theme_.background,9);
  if(!windowsTab_||previews_.at(slot).state()!=win::PreviewState::Ready){
   const float iconSize=std::min(88.f,std::max(24.f,(image.bottom-image.top)*.42f));
   c.icon(icon,box(image.left+(image.right-image.left-iconSize)/2.f,image.top+(image.bottom-image.top-iconSize)/2.f-12,iconSize,iconSize),theme_.muted);
   const auto text=!windowsTab_?L"Open application":!app_.config().windowPreviews?L"Live previews disabled":result.window&&IsIconic(result.window->hwnd)?L"Minimized · Enter to restore":L"Preview unavailable";
   c.text(text,box(image.left+8,image.bottom-30,image.right-image.left-16,26),theme_.muted,12,false,1);
  }
  c.icon(icon,box(bounds.left+14,bounds.bottom-43,30,30),theme_.muted);
  const bool group=result.members.size()>1,minimized=windowsTab_&&result.window&&IsIconic(result.window->hwnd);
  c.text(result.app.name,box(bounds.left+55,bounds.bottom-48,tileWidth-(windowsTab_&&!group?104.f:72.f),group||minimized?25.f:35.f),theme_.foreground,16,selected);
  if(group)c.text(L"Enter to choose a window",box(bounds.left+55,bounds.bottom-23,tileWidth-72,18),theme_.muted,11);
  else if(minimized)c.text(L"Minimized · Enter to restore",box(bounds.left+55,bounds.bottom-23,tileWidth-104,18),theme_.muted,11);
  if(windowsTab_&&!group){const auto close=closeBounds(index);c.fill(close,theme_.background,8,.75f);c.glyph(L'\uE8BB',close,theme_.foreground,12);if(focus_==Focus::Close&&selected)c.stroke(close,theme_.accent,8,2);}
 }
 if(windowsTab_){
  for(const auto& item:controls()){
   const bool drop=drag_.active&&item.id==4000+drag_.target;const bool dragged=drag_.workspace&&item.id>=4000&&workspaces_.items()[static_cast<std::size_t>(item.id-4000)].id==*drag_.workspace;const bool focused=(focus_==Focus::Tools||focus_==Focus::Workspaces)&&focusedControl_==item.id;
   c.fill(item.bounds,item.selected||drop?theme_.raised:theme_.surface,12,item.enabled?.95f:.4f);
   c.stroke(item.bounds,drop||dragged||item.selected?theme_.accent:focused?theme_.foreground:theme_.raised,12,drop?2.5f:focused?2.f:1.f);
   auto label=item.bounds;if(item.id>=4000)label.right-=26.f;c.text(item.label,label,item.enabled?theme_.foreground:theme_.muted,item.id>=4000?14:13,item.selected,1);if(item.id>=4000)c.text(L"⋯",box(item.bounds.right-26,item.bounds.top,26,item.bounds.bottom-item.bounds.top),theme_.muted,15,false,1);
  }
  if(!workspaces_.available())c.text(workspaces_.notice(),box(24,height-148,width-48,28),theme_.muted,12,false,1);
  else c.text(drag_.active?(drag_.workspace?L"Drop to reorder workspaces · Escape to cancel":L"Drop on a desktop · Escape to cancel"):!notice_.empty()&&width<1100?notice_:L"Workspaces span all monitors · drag a window onto a desktop",box(24,height-136,width-48,24),theme_.muted,11,false,1);
  if(!notice_.empty()&&height>=600&&width>=1100)c.text(notice_,box(width*.52f,188,width*.48f-32,30),theme_.muted,12,false,2);
 }
 if(results_.empty())c.text(windowsTab_?L"No matching open windows":L"No matching applications",box(32,height/2.f-25,width-64,50),theme_.muted,22,false,1);
 const int capacity=grid.capacity(),pages=capacity?std::max(1,(static_cast<int>(results_.size())+capacity-1)/capacity):1,page=capacity?offset_/capacity+1:1;
 if(width>=1100){
  c.text(std::to_wstring(results_.size())+(windowsTab_?(grouped_&&groupKey_.empty()?L" application groups":L" open windows"):L" applications"),box(44,height-48,280,32),theme_.muted,12);
  c.text(windowsTab_?L"Enter to switch · Ctrl+W close · Ctrl+M monitors · Ctrl+G groups":L"Arrows to select   ·   Enter to open   ·   Type to search",box(width/2.f-360,height-48,720,32),theme_.muted,12,false,1);
  c.text(L"Page "+std::to_wstring(page)+L" / "+std::to_wstring(pages)+L"   PgUp / PgDn",box(width-325,height-48,281,32),theme_.muted,12,false,2);
 }else c.text(L"Page "+std::to_wstring(page)+L" / "+std::to_wstring(pages)+L"   ·   Arrows / Enter   ·   Esc",box(24,height-48,width-48,32),theme_.muted,11,false,1);
 accessibleSnapshot();
}
int Activities::hitRow(float x,float y) const{
 const auto content=layout();
 if(windowsTab_||fullscreen_){const int slot=content.grid.hit(x,y);const int index=offset_+slot;return slot>=0&&index>=0&&index<static_cast<int>(results_.size())?index:-1;}
 if(x<16.f||x>=content.listWidth-16.f||y<146.f||y>=content.listBottom)return -1;
 int row=static_cast<int>((y-146.f)/54.f),index=offset_+row;
 if(row>=visibleRows()||y>=146.f+static_cast<float>(row)*54.f+51.f||index>=static_cast<int>(results_.size()))return -1;
 return index;
}
void Activities::pointer(float x,float y){if(drag_.window||drag_.workspace){dragPointer(x,y);return;}int next=hitRow(x,y);if(next!=hover_){hover_=next;invalidate();}}
void Activities::leave(){hover_=-1;invalidate();}
void Activities::click(float x,float y){
 if(closing_)return;
 if(fullscreen_&&windowsTab_)for(const auto& item:controls())if(x>=item.bounds.left&&x<item.bounds.right&&y>=item.bounds.top&&y<item.bounds.bottom){if(item.id>=4000&&x>=item.bounds.right-26.f)workspaceMenu(item.id-4000);else if(item.enabled)control(item.id);return;}
 if(fullscreen_){RECT r{};GetClientRect(hwnd_,&r);const float center=static_cast<float>(r.right)/scale()/2.f;if(y>=138&&y<172&&x>=center-170&&x<center+170){switchTab(x>=center);focusPart(x>=center?Focus::Windows:Focus::Applications);return;}}
 else if(y>=106&&y<136&&x<345){switchTab(x>=180);focusPart(x>=180?Focus::Windows:Focus::Applications);return;}
 int index=hitRow(x,y);if(index>=0){selected_=index;const auto close=closeBounds(index);if(x>=close.left&&x<close.right&&y>=close.top&&y<close.bottom)closeSelected();else activateSelected();}
}
void Activities::context(POINT p){POINT local=p;ScreenToClient(hwnd_,&local);const float x=local.x/scale(),y=local.y/scale();const int desktop=workspaceHit(x,y);if(desktop>=0){workspaceMenu(desktop);return;}for(const auto& item:controls())if(item.id==20&&x>=item.bounds.left&&x<item.bounds.right&&y>=item.bounds.top&&y<item.bounds.bottom){monitorMenu();return;}pointer(x,y);if(hover_<0)return;selected_=hover_;syncPreview();pinSelected();}
LRESULT Activities::event(UINT m,WPARAM w,LPARAM l,bool& handled){
 handled=true;
 switch(m){
 case WM_LBUTTONDOWN:if(fullscreen_&&windowsTab_&&!closing_){const float x=GET_X_LPARAM(l)/scale(),y=GET_Y_LPARAM(l)/scale();const int workspace=workspaceHit(x,y);if(workspace>=0){for(const auto& item:controls())if(item.id==4000+workspace&&x>=item.bounds.right-26.f)return 0;drag_.workspace=workspaces_.items()[static_cast<std::size_t>(workspace)].id;drag_.origin={static_cast<LONG>(x),static_cast<LONG>(y)};focusedControl_=4000+workspace;focusPart(Focus::Workspaces);SetCapture(hwnd_);return 0;}const int index=hitRow(x,y);if(index>=0){selected_=index;const auto close=closeBounds(index);const auto& result=results_[static_cast<std::size_t>(index)];if(result.window&&result.members.size()<=1&&!(x>=close.left&&x<close.right&&y>=close.top&&y<close.bottom)){drag_.window=result.window;drag_.origin={static_cast<LONG>(x),static_cast<LONG>(y)};SetCapture(hwnd_);}invalidate();}return 0;}break;
 case WM_LBUTTONUP:if(drag_.workspace){const auto id=*drag_.workspace;const bool active=drag_.active;const int target=workspaceHit(GET_X_LPARAM(l)/scale(),GET_Y_LPARAM(l)/scale());cancelDrag();if(active){if(target>=0){notice_=workspaces_.reorder(id,target)?L"Workspace order saved":workspaces_.notice();app_.workspaceAppearanceChanged();}invalidate();return 0;}auto item=std::find_if(workspaces_.items().begin(),workspaces_.items().end(),[&](const auto& value){return value.id==id;});if(item!=workspaces_.items().end())workspaceAction(static_cast<int>(std::distance(workspaces_.items().begin(),item)),false);return 0;}if(drag_.window){const auto source=*drag_.window;const int target=workspaceHit(GET_X_LPARAM(l)/scale(),GET_Y_LPARAM(l)/scale());const bool active=drag_.active;cancelDrag();if(active){if(target>=0&&target<static_cast<int>(workspaces_.items().size())){const auto id=workspaces_.items()[static_cast<std::size_t>(target)].id;notice_=workspaces_.move(source,id)?L"Window moved to workspace":workspaces_.notice();PostMessageW(app_.controller(),win::RefreshMessage,0,0);syncPreview();}else notice_=L"Move cancelled · drop onto a workspace";invalidate();return 0;}auto pressed=std::find_if(results_.begin(),results_.end(),[&](const auto& result){return result.window&&result.window->hwnd==source.hwnd&&result.window->pid==source.pid&&result.window->processBirth==source.processBirth;});if(pressed!=results_.end()){selected_=static_cast<int>(std::distance(results_.begin(),pressed));activateSelected();}return 0;}break;
 case WM_MBUTTONUP:if(fullscreen_&&windowsTab_){const auto index=hitRow(GET_X_LPARAM(l)/scale(),GET_Y_LPARAM(l)/scale());if(index>=0){selected_=index;closeSelected();}return 0;}break;
 case WM_CAPTURECHANGED:case WM_CANCELMODE:if(drag_.window||drag_.workspace){drag_={};invalidate();}break;
 case WM_TIMER:if(w==OverviewAnimationTimer){animate();return 0;}break;
 case WM_MOVE:if(accessibility_){accessibleSnapshot();NotifyWinEvent(EVENT_OBJECT_LOCATIONCHANGE,hwnd_,OBJID_CLIENT,CHILDID_SELF);}break;
 case WM_GETOBJECT:if(!accessibility_){handled=false;return 0;}UpdateWindow(hwnd_);accessibleSnapshot();return accessibility_->object(w,l,handled);
 case WM_KEYDOWN:case WM_SYSKEYDOWN:if(keyboard(w,false))return 0;break;
 case WM_SETFOCUS:invalidate();break;
 case WM_KILLFOCUS:invalidate();break;
 case WM_COMMAND:if(reinterpret_cast<HWND>(l)==edit_&&HIWORD(w)==EN_CHANGE){rebuild();return 0;}break;
 case WM_SIZE:positionEdit();select(0);break;
 case WM_DPICHANGED:PostMessageW(hwnd_,WM_APP+80,0,0);break;
 case WM_APP+80:if(fullscreen_){auto monitor=app_.display(monitorName_);move(monitor.bounds,false);}positionEdit();win::roundedWindow(hwnd_,fullscreen_?0:theme_.radius);select(0);return 0;
 case WM_APP+81:syncPreview();return 0;
 case WM_SHOWWINDOW:if(!w){KillTimer(hwnd_,OverviewAnimationTimer);RemovePropW(hwnd_,L"Aether.Overview.Animating");previews_.hide();}else PostMessageW(hwnd_,WM_APP+81,0,0);break;
 case WM_WINDOWPOSCHANGED:if(reinterpret_cast<WINDOWPOS*>(l)->flags&SWP_HIDEWINDOW)previews_.hide();break;
 case WM_DWMCOMPOSITIONCHANGED:syncPreview();invalidate();return 0;
 case WM_DESTROY:previews_.hide();break;
 case WM_CTLCOLOREDIT:{HDC dc=reinterpret_cast<HDC>(w);auto f=theme_.foreground,b=theme_.surface;SetTextColor(dc,RGB((f>>16)&255,(f>>8)&255,f&255));SetBkColor(dc,RGB((b>>16)&255,(b>>8)&255,b&255));return reinterpret_cast<LRESULT>(editBrush_);}
 case WM_ACTIVATE:if(LOWORD(w)==WA_INACTIVE&&!workspaceEditing_)hide();return 0;
 case WM_MOUSEWHEEL:if(windowsTab_||fullscreen_)navigate(GET_WHEEL_DELTA_WPARAM(w)>0?OverviewDirection::Up:OverviewDirection::Down);else select(GET_WHEEL_DELTA_WPARAM(w)>0?-3:3);return 0;
 case WM_CLOSE:dismiss();return 0;
 default:break;
 }
 handled=false;return 0;
}
void Activities::focusPart(Focus part){
 focus_=part;SetFocus(part==Focus::Search?edit_:hwnd_);select(0);accessibleSnapshot();
}
void Activities::tabFocus(bool reverse){std::vector<Focus> parts{Focus::Search,Focus::Applications,Focus::Windows};if(!results_.empty()){parts.push_back(Focus::Results);if(fullscreen_&&windowsTab_&&selected_>=0&&results_[static_cast<std::size_t>(selected_)].members.size()<=1)parts.push_back(Focus::Close);}if(fullscreen_&&windowsTab_){const auto items=controls();if(std::any_of(items.begin(),items.end(),[](const auto& item){return item.id<30;}))parts.push_back(Focus::Tools);if(workspaces_.available())parts.push_back(Focus::Workspaces);}auto current=std::find(parts.begin(),parts.end(),focus_);const int size=static_cast<int>(parts.size()),index=current==parts.end()?0:static_cast<int>(std::distance(parts.begin(),current));auto next=parts[static_cast<std::size_t>((index+(reverse?size-1:1))%size)];if(next==Focus::Tools){for(const auto& item:controls())if(item.id<30){focusedControl_=item.id;break;}}if(next==Focus::Workspaces){for(const auto& item:controls())if(item.id>=4000){focusedControl_=item.id;break;}}focusPart(next);}
void Activities::switchTab(bool windows){const bool expand=windows&&!fullscreen_;if(expand){workspaces_.refresh();fullscreen_=true;presentation_=0.f;const auto monitor=app_.display(monitorName_);move(monitor.bounds,false);win::roundedWindow(hwnd_,0);SetPropW(hwnd_,L"Aether.Overview.Fullscreen",reinterpret_cast<HANDLE>(1));positionEdit();}windowsTab_=windows;const auto name=windows?L"Search open windows":L"Search applications";accessibleNativeName(edit_,name);SendMessageW(edit_,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(windows?L"Search open windows...":L"Search applications..."));rebuild();accessibleSnapshot();if(expand&&visible())transition(1.f);}
bool Activities::keyboard(WPARAM key,bool search){
 if(key==VK_ESCAPE){if(drag_.window||drag_.workspace){cancelDrag();invalidate();}else if(!groupKey_.empty()){control(22);}else dismiss();return true;}
 const bool ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0,shift=(GetKeyState(VK_SHIFT)&0x8000)!=0,alt=(GetKeyState(VK_MENU)&0x8000)!=0;
 if(fullscreen_&&windowsTab_){
  if(focus_==Focus::Workspaces&&focusedControl_>=4000){const int index=focusedControl_-4000;if(key==VK_F2||key==VK_APPS){workspaceMenu(index,key==VK_F2);return true;}if(alt&&shift&&(key==VK_LEFT||key==VK_RIGHT)&&index<static_cast<int>(workspaces_.items().size())){const int to=index+(key==VK_LEFT?-1:1);if(to>=0&&to<static_cast<int>(workspaces_.items().size())){const auto id=workspaces_.items()[static_cast<std::size_t>(index)].id;if(workspaces_.reorder(id,to)){focusedControl_=4000+to;app_.workspaceAppearanceChanged();}}invalidate();return true;}}
  if(ctrl&&key=='M'){control(20);return true;}if(ctrl&&key=='G'){control(21);return true;}if(ctrl&&key=='W'){closeSelected();return true;}
  if(ctrl&&key>='1'&&key<='9'&&(shift||alt)){workspaceAction(static_cast<int>(key-'1'),shift);return true;}
  if(!search&&(focus_==Focus::Tools||focus_==Focus::Workspaces)){
   if(key==VK_RETURN||key==VK_SPACE){control(focusedControl_);return true;}
   if(key==VK_LEFT||key==VK_RIGHT){std::vector<int> ids;for(const auto& item:controls())if(item.enabled&&((focus_==Focus::Tools&&item.id<30)||(focus_==Focus::Workspaces&&item.id>=30)))ids.push_back(item.id);auto current=std::find(ids.begin(),ids.end(),focusedControl_);if(!ids.empty()){int index=current==ids.end()?0:static_cast<int>(std::distance(ids.begin(),current));index=(index+(key==VK_LEFT?static_cast<int>(ids.size())-1:1))%static_cast<int>(ids.size());focusedControl_=ids[static_cast<std::size_t>(index)];invalidate();accessibleSnapshot();}return true;}
   if(key==VK_UP||key==VK_DOWN){focusPart(Focus::Results);return true;}
  }
  if(!search&&(focus_==Focus::Close)&&(key==VK_RETURN||key==VK_SPACE)){closeSelected();return true;}
  if(!search&&key==VK_DELETE){closeSelected();return true;}
  if(!search&&key==VK_BACK&&!groupKey_.empty()){control(22);return true;}
 }
 if(key==VK_TAB){if(GetKeyState(VK_CONTROL)&0x8000)switchTab(!windowsTab_);else tabFocus((GetKeyState(VK_SHIFT)&0x8000)!=0);return true;}
 if(key=='P'&&(GetKeyState(VK_CONTROL)&0x8000)){pinSelected();return true;}
 if(!search&&(focus_==Focus::Applications||focus_==Focus::Windows)){
  if(key==VK_LEFT||key==VK_RIGHT){focusPart(focus_==Focus::Applications?Focus::Windows:Focus::Applications);return true;}
  if(key==VK_RETURN||key==VK_SPACE){switchTab(focus_==Focus::Windows);return true;}
  if(key==VK_DOWN){focusPart(Focus::Results);return true;}return false;
 }
 // An empty overview search lets arrows navigate tiles immediately. Once
 // text is entered, retain native caret/selection and word movement.
 if(search&&(windowsTab_||fullscreen_)&&(key==VK_LEFT||key==VK_RIGHT)&&win::windowText(edit_).empty()){
  navigate(key==VK_LEFT?OverviewDirection::Left:OverviewDirection::Right);return true;
 }
 // Preserve native caret, selection and word movement in the search EDIT.
 if(search&&(key==VK_LEFT||key==VK_RIGHT||key==VK_HOME||key==VK_END))return false;
 if(key==VK_RETURN||(!search&&key==VK_SPACE)){activateSelected();return true;}
 if(key==VK_HOME||key==VK_END){selected_=key==VK_HOME?0:static_cast<int>(results_.size())-1;select(0);accessibleSnapshot();return true;}
 if(key==VK_LEFT||key==VK_RIGHT||key==VK_UP||key==VK_DOWN||key==VK_PRIOR||key==VK_NEXT){
  if(windowsTab_||fullscreen_){const auto direction=key==VK_LEFT?OverviewDirection::Left:key==VK_RIGHT?OverviewDirection::Right:key==VK_UP?OverviewDirection::Up:key==VK_DOWN?OverviewDirection::Down:key==VK_PRIOR?OverviewDirection::PreviousPage:OverviewDirection::NextPage;navigate(direction);}
  else if(key==VK_UP||key==VK_DOWN||key==VK_PRIOR||key==VK_NEXT)select(key==VK_UP?-1:key==VK_DOWN?1:key==VK_PRIOR?-visibleRows():visibleRows());
  focusPart(Focus::Results);return true;
 }
 return false;
}
void Activities::accessibleSnapshot(){
 if(!accessibility_)return;const auto content=layout();std::vector<AccessibleNode> nodes;
 RECT editBounds{};GetWindowRect(edit_,&editBounds);AccessibleNode search;search.id=1;search.name=windowsTab_?L"Search open windows":L"Search applications";search.role=ROLE_SYSTEM_TEXT;search.bounds=editBounds;search.native=edit_;search.defaultAction=L"Focus";search.focus=[this]{focusPart(Focus::Search);};search.invoke=search.focus;nodes.push_back(std::move(search));
 for(int i=0;i<2;++i){auto n=accessibleNode(hwnd_,2+i,i?L"Open windows":L"Applications",ROLE_SYSTEM_PAGETAB,STATE_SYSTEM_FOCUSABLE|STATE_SYSTEM_SELECTABLE|((windowsTab_==(i==1))?STATE_SYSTEM_SELECTED:0),fullscreen_?box(layout().listWidth/2.f-170.f+(i?174.f:0.f),138,166,34):box(i?183.f:20.f,106,154,29));n.defaultAction=L"Select";n.invoke=[this,i]{switchTab(i==1);focusPart(i?Focus::Windows:Focus::Applications);};n.focus=[this,i]{focusPart(i?Focus::Windows:Focus::Applications);};nodes.push_back(std::move(n));}
 for(std::size_t i=0;i<results_.size();++i){const int index=static_cast<int>(i),slot=index-offset_;bool visible=(windowsTab_||fullscreen_)?slot>=0&&slot<content.grid.capacity():slot>=0&&slot<visibleRows();D2D1_RECT_F bounds{};if(visible){if(windowsTab_||fullscreen_){auto cell=content.grid.cell(slot);bounds={cell.left,cell.top,cell.right,cell.bottom};}else bounds=box(16,146.f+slot*54.f,content.listWidth-32,51);}
  auto n=accessibleNode(hwnd_,100+index,results_[i].app.name,ROLE_SYSTEM_LISTITEM,STATE_SYSTEM_FOCUSABLE|STATE_SYSTEM_SELECTABLE|(visible?0:STATE_SYSTEM_OFFSCREEN),bounds);n.description=windowsTab_?(results_[i].window&&IsIconic(results_[i].window->hwnd)?L"Minimized window":L"Open window"):(results_[i].pinned?L"Pinned application":L"Installed application");if(results_[i].members.size()>1)n.description=L"Application group, "+std::to_wstring(results_[i].members.size())+L" windows";n.defaultAction=results_[i].members.size()>1?L"Open group":windowsTab_?L"Switch":L"Open";n.focus=[this,index]{selected_=index;focusPart(Focus::Results);};n.invoke=[this,index]{selected_=index;activateSelected();};nodes.push_back(std::move(n));
  if(visible&&fullscreen_&&windowsTab_&&results_[i].window&&results_[i].members.size()<=1){
   const auto window=*results_[i].window;auto close=accessibleNode(hwnd_,6000+index,L"Close "+results_[i].app.name,ROLE_SYSTEM_PUSHBUTTON,STATE_SYSTEM_FOCUSABLE,closeBounds(index));close.description=L"Request normal close; the application can ask to save changes";close.defaultAction=L"Close";close.focus=[this,index]{selected_=index;focusPart(Focus::Close);};close.invoke=[this,window]{notice_=win::requestWindowClose(window)?L"Close requested · the application may ask to save":L"Window unavailable";PostMessageW(app_.controller(),win::RefreshMessage,0,0);invalidate();};nodes.insert(nodes.end()-1,std::move(close));
  }
 }
 for(const auto& item:controls()){auto n=accessibleNode(hwnd_,item.id,item.label,item.id>=4000?ROLE_SYSTEM_PAGETAB:ROLE_SYSTEM_PUSHBUTTON,STATE_SYSTEM_FOCUSABLE|(item.selected?STATE_SYSTEM_SELECTED:0)|(item.enabled?0:STATE_SYSTEM_UNAVAILABLE),item.bounds);n.defaultAction=item.id>=4000?L"Switch workspace":L"Activate";n.description=item.id>=4000?L"Drop a window here to move it; drag this chip to reorder; F2 renames; Alt+Shift+arrow reorders; right-click customizes":item.id==20?L"Click or Ctrl+M cycles monitors; right-click to choose a monitor":L"Overview control";n.focus=[this,id=item.id]{focusedControl_=id;focusPart(id>=30?Focus::Workspaces:Focus::Tools);};if(item.enabled)n.invoke=[this,id=item.id]{control(id);};if(item.id>=4000){const auto id=workspaces_.items()[static_cast<std::size_t>(item.id-4000)].id;n.value=item.label;n.setValue=[this,id](std::wstring_view value){const bool saved=workspaces_.rename(id,value);if(saved)app_.workspaceAppearanceChanged();return saved;};auto manage=accessibleNode(hwnd_,5000+item.id-4000,L"Customize "+item.label,ROLE_SYSTEM_PUSHBUTTON,STATE_SYSTEM_FOCUSABLE,box(item.bounds.right-26,item.bounds.top,26,item.bounds.bottom-item.bounds.top));manage.defaultAction=L"Customize workspace";manage.invoke=[this,index=item.id-4000]{workspaceMenu(index);};manage.focus=[this,id=item.id]{focusedControl_=id;focusPart(Focus::Workspaces);};nodes.push_back(std::move(manage));}nodes.push_back(std::move(n));}
 const auto count=reinterpret_cast<HANDLE>(workspaces_.items().size());if(GetPropW(hwnd_,L"Aether.Overview.Workspaces")!=count)SetPropW(hwnd_,L"Aether.Overview.Workspaces",count);
 long focused=0;auto nativeFocus=GetFocus();if(nativeFocus==edit_)focused=1;else if(nativeFocus==hwnd_)focused=focus_==Focus::Applications?2:focus_==Focus::Windows?3:focus_==Focus::Tools||focus_==Focus::Workspaces?focusedControl_:focus_==Focus::Close?6000+selected_:selected_>=0?100+selected_:0;
 accessibility_->snapshot(L"Activities",ROLE_SYSTEM_LIST,std::move(nodes),focused,selected_>=0?100+selected_:0);
}
LRESULT CALLBACK Activities::editProcedure(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data){
 auto* self=reinterpret_cast<Activities*>(data);
 if((m==WM_KEYDOWN||m==WM_SYSKEYDOWN)&&self->keyboard(w,true))return 0;
 if(m==WM_SETFOCUS){self->focus_=Focus::Search;self->invalidate();self->accessibleSnapshot();}
 if(m==WM_CHAR&&(w==VK_RETURN||w==VK_ESCAPE||w==VK_TAB||w==16||w==7||w==23))return 0;
 if(m==WM_MOUSEWHEEL)return SendMessageW(self->hwnd_,m,w,l);
 return DefSubclassProc(h,m,w,l);
}
}


