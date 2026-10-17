#include "backgrounds.hpp"
#include "app.hpp"
#include "platform/dialogs.hpp"
#include <commctrl.h>
namespace aether::ui {
namespace {
constexpr int ControlCount=11;
constexpr float DesignWidth=690,DesignHeight=600;
const wchar_t* fitNames[]={L"Center",L"Tile",L"Stretch",L"Fit",L"Fill",L"Span"};
const wchar_t* controlNames[]={L"Background image path",L"Choose background image",L"Select background display",L"Background position (global)",L"Apply to selected display",L"Apply to all displays",L"Undo last background change",L"Refresh current backgrounds",L"Close Backgrounds",L"Browse animated wallpapers",L"Stop animated wallpaper"};
std::vector<win::Monitor> connected(const App& app){return app.displays().empty()?win::monitors():app.displays();}
bool currentAnimation(App& app){for(const auto& w:app.workspaces().items())if(w.current){const int mode=app.workspaces().appearance(w.id).background;if(mode)return (mode>=4&&mode<=6)||mode==9;}return app.config().animatedWallpaper!=0;}
bool sameMap(const std::vector<win::Monitor>& a,const std::vector<win::Monitor>& b){
 if(a.size()!=b.size())return false;return std::all_of(a.begin(),a.end(),[&](const auto& monitor){auto found=std::find_if(b.begin(),b.end(),[&](const auto& other){return other.name==monitor.name;});return found!=b.end()&&found->bounds==monitor.bounds;});
}
}
Backgrounds::Backgrounds(Graphics& graphics,App& app):Surface(graphics),app_(app){
 if(!create(L"Aether Backgrounds",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,WS_EX_TOOLWINDOW,{0,0,720,650}))throw std::runtime_error("Could not create Backgrounds.");
 pathEdit_=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,0,0,0,0,hwnd_,reinterpret_cast<HMENU>(static_cast<INT_PTR>(301)),GetModuleHandleW(nullptr),nullptr);
 if(!pathEdit_){destroy();throw std::runtime_error("Could not create background path field.");}
 SendMessageW(pathEdit_,EM_SETLIMITTEXT,WallpaperPathLimit,0);SetWindowSubclass(pathEdit_,editProcedure,1,reinterpret_cast<DWORD_PTR>(this));accessibleNativeName(pathEdit_,L"Background image path");accessibility_=std::make_unique<Accessibility>(hwnd_);layoutEdit();
}
Backgrounds::~Backgrounds(){
 closing_=true;if(accessibility_)accessibility_->detach();destroy();if(editFont_)DeleteObject(editFont_);
}
float Backgrounds::layoutScale() const{
 RECT client{};GetClientRect(hwnd_,&client);const float dpi=scale();return std::clamp(std::min(client.right/dpi/DesignWidth,client.bottom/dpi/DesignHeight),.2f,1.f);
}
D2D1_RECT_F Backgrounds::controlRect(int index) const{
 switch(index){
 case 0:return box(24,81,525,32);case 1:return box(562,81,104,32);
 case 2:return box(24,150,313,38);case 3:return box(353,150,313,38);
 case 4:return box(24,363,313,40);case 5:return box(353,363,313,40);
 case 6:return box(24,415,232,36);case 7:return box(270,415,236,36);case 8:return box(520,415,146,36);case 9:return box(24,467,313,38);default:return box(353,467,313,38);
 }
}
void Backgrounds::layoutEdit(){
 if(!pathEdit_||closing_)return;const float factor=layoutScale()*scale();auto r=controlRect(0);SetWindowPos(pathEdit_,nullptr,static_cast<int>(r.left*factor),static_cast<int>(r.top*factor),static_cast<int>((r.right-r.left)*factor),static_cast<int>((r.bottom-r.top)*factor),SWP_NOZORDER|SWP_NOACTIVATE);
 auto family=win::wide(theme_.fontFamily);auto font=CreateFontW(-std::max(8,static_cast<int>(14*factor*theme_.fontScale/100.f)),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,family.c_str());
 if(font){SendMessageW(pathEdit_,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);if(editFont_)DeleteObject(editFont_);editFont_=font;}
}
bool Backgrounds::enabled(int index) const{
 if(index>=4&&index<=6&&!visible())return false;
 if(index==8)return true;if(index==10)return currentAnimation(app_);
 if(pending_)return pendingKind_==WallpaperKind::Query&&(index==0||index==1);
 if(index==4||index==5){
  return validWallpaperPath(selected_)&&snapshot_&&!snapshot_->slideshow&&!snapshot_->monitors.empty()
   &&std::all_of(snapshot_->monitors.begin(),snapshot_->monitors.end(),[](const auto& monitor){return !monitor.path.empty();})
   &&(index==5||std::any_of(displays_.begin(),displays_.end(),[&](const auto& monitor){return monitor.name==displayName_;}));
 }
 if(index==6)return app_.wallpaper().canUndo();if(index==2)return !displays_.empty();return true;
}
void Backgrounds::focusControl(int index){
 if(index<0||index>=ControlCount||!enabled(index))return;focused_=index;SetFocus(index==0?pathEdit_:hwnd_);invalidate();accessibleSnapshot();
}
void Backgrounds::selectPath(const std::wstring& value){
 if(value.size()>WallpaperPathLimit||value.find(L'\0')!=std::wstring::npos)return;selected_=value;if(++previewRevision_==0)++previewRevision_;
 if(pathEdit_&&win::windowText(pathEdit_)!=value)SetWindowTextW(pathEdit_,value.c_str());invalidate();accessibleSnapshot();
}
void Backgrounds::query(){
 if(pending_)return;snapshot_.reset();pendingKind_=WallpaperKind::Query;pending_=app_.wallpaper().requestQuery(displays_,scope_);
 notice_=pending_?L"Reading current Windows backgrounds...":L"Background query is unavailable. Refresh after any current action finishes.";invalidate();accessibleSnapshot();
}
void Backgrounds::open(const win::Monitor& monitor){
 setTheme(app_.theme());displays_=connected(app_);if(++scope_==0)++scope_;pending_=0;snapshot_.reset();
 if(std::none_of(displays_.begin(),displays_.end(),[&](const auto& display){return display.name==displayName_;}))displayName_=monitor.name;
 auto rectangle=popupBounds(monitor.work,720,650,monitor.dpi);move(rectangle,false);layoutEdit();show(true);query();focusControl(0);
}
void Backgrounds::refreshTopology(){
 setTheme(app_.theme());auto current=connected(app_);if(sameMap(current,displays_)){invalidate();return;}
 displays_=std::move(current);snapshot_.reset();pending_=0;if(++scope_==0)++scope_;
 if(std::none_of(displays_.begin(),displays_.end(),[&](const auto& monitor){return monitor.name==displayName_;}))displayName_=displays_.empty()?L"":displays_.front().name;
 notice_=L"Displays changed. Refresh to continue.";if(visible())query();invalidate();accessibleSnapshot();
}
void Backgrounds::update(const win::WallpaperCompletion& completion){
 if(completion.scope!=scope_||completion.id!=pending_){invalidate();accessibleSnapshot();return;}pending_=0;
 if(!sameMap(completion.displays,displays_)||!sameMap(displays_,connected(app_))){snapshot_.reset();notice_=L"Displays changed. Refresh before another background action.";invalidate();accessibleSnapshot();return;}
 notice_=completion.result.message.empty()?(completion.result.status==WallpaperStatus::Ok?L"Current backgrounds loaded.":L"Background information is unavailable."):completion.result.message;
 snapshot_.reset();
 if(completion.outcome==win::ProviderOutcome::Ok&&completion.result.after){
  std::vector<Rect> bounds;for(const auto& display:displays_)bounds.push_back(display.bounds);
  if(mapWallpaperDisplays(*completion.result.after,bounds)){snapshot_=completion.result.after;fit_=snapshot_->fit;
   if(selected_.empty()){auto display=std::find_if(displays_.begin(),displays_.end(),[&](const auto& item){return item.name==displayName_;});if(display!=displays_.end()){auto mapped=mapWallpaperDisplays(*snapshot_,bounds);const auto index=static_cast<std::size_t>(display-displays_.begin());for(const auto& monitor:snapshot_->monitors)if(monitor.display.device==(*mapped)[index].device)selectPath(monitor.path);}}
  }
 }
 if(snapshot_&&snapshot_->slideshow)notice_=L"Windows slideshow is active. Use Windows Settings to change it.";
 EnableWindow(pathEdit_,enabled(0));invalidate();accessibleSnapshot();
}
void Backgrounds::choose(){
 const COMDLG_FILTERSPEC filters[]={{L"Background images",L"*.png;*.jpg;*.jpeg;*.bmp"},{L"All files",L"*.*"}};
 app_.beginInteraction();auto file=win::chooseFile(hwnd_,false,L"Choose a desktop background",filters,2);app_.endInteraction();
 if(file){selectPath(file->wstring());notice_=L"Image selected. Preview and selection do not change your background.";invalidate();}focusControl(1);
}
void Backgrounds::invoke(int index){
 if(!visible()||!enabled(index))return;
 if(index==9){app_.openMarketplace();return;}if(index==10){bool local=false;for(const auto& workspace:app_.workspaces().items())if(workspace.current){auto appearance=app_.workspaces().appearance(workspace.id);if((appearance.background>=4&&appearance.background<=6)||appearance.background==9){appearance.background=appearance.background==9?8:appearance.background-3;local=app_.workspaces().setAppearance(workspace.id,std::move(appearance));if(!local){notice_=app_.workspaces().notice();invalidate();return;}}break;}if(local)app_.workspaceAppearanceChanged();else{auto config=app_.config();config.animatedWallpaper=0;app_.apply(config);}notice_=local?L"Workspace animation stopped. Its still wallpaper is visible.":L"Animation stopped. Windows static wallpaper is visible.";invalidate();return;}
 if(index==0){focusControl(0);return;}if(index==1){choose();return;}
 if(index==2||index==3){
  auto menu=CreatePopupMenu();if(!menu)return;const auto monitorMenu=index==2;
  if(monitorMenu){for(std::size_t i=0;i<displays_.size();++i){auto label=displays_[i].name+L"  ("+std::to_wstring(displays_[i].bounds.width())+L" x "+std::to_wstring(displays_[i].bounds.height())+L")";AppendMenuW(menu,MF_STRING|(displayName_==displays_[i].name?MF_CHECKED:0),static_cast<UINT_PTR>(i+1),label.c_str());}}
  else for(int i=0;i<6;++i)AppendMenuW(menu,MF_STRING|(static_cast<int>(fit_)==i?MF_CHECKED:0),static_cast<UINT_PTR>(i+1),fitNames[i]);
  auto node=accessibleNode(hwnd_,1,L"",0,0,controlRect(index),layoutScale());POINT position{node.bounds.left,node.bounds.bottom};app_.beginInteraction();auto selected=TrackPopupMenuEx(menu,TPM_RETURNCMD|TPM_NONOTIFY,position.x,position.y,hwnd_,nullptr);DestroyMenu(menu);app_.endInteraction();
  if(monitorMenu&&selected>0&&selected<=displays_.size())displayName_=displays_[selected-1].name;else if(!monitorMenu&&selected>=1&&selected<=6)fit_=static_cast<WallpaperFit>(selected-1);focusControl(index);invalidate();return;
 }
 if(index==7){query();return;}if(index==8){hide();return;}
 if(index==6){pendingKind_=WallpaperKind::Undo;pending_=app_.wallpaper().requestUndo(displays_,scope_);}
 else {pendingKind_=WallpaperKind::Apply;pending_=app_.wallpaper().requestApply(displays_,scope_,selected_,fit_,index==4?std::optional<std::wstring>(displayName_):std::nullopt);}
 notice_=pending_?(pendingKind_==WallpaperKind::Undo?L"Restoring the previous background...":L"Applying the selected background..."):L"Background action was not accepted. Refresh to continue.";
 EnableWindow(pathEdit_,enabled(0));if(pending_)focusControl(8);invalidate();accessibleSnapshot();
}
void Backgrounds::paint(Canvas& c){
 if(theme_!=app_.theme()){theme_=app_.theme();layoutEdit();}c.setTheme(theme_);const auto factor=layoutScale();c.setScale(factor);c.fill(box(0,0,c.size().width,c.size().height),theme_.background);
 c.text(L"Backgrounds",box(24,15,640,38),theme_.foreground,25,true);c.text(L"Choose, preview, then Apply. Position affects every display.",box(25,53,640,22),theme_.muted,11);
 c.text(L"DISPLAY",box(25,123,313,23),theme_.muted,10,true);c.text(L"POSITION (GLOBAL)",box(353,123,313,23),theme_.muted,10,true);
 EnableWindow(pathEdit_,enabled(0));
 auto button=[&](int index,std::wstring label,bool accent=false){auto r=controlRect(index);c.fill(r,accent&&enabled(index)?theme_.accent:theme_.surface,8);c.text(label,r,enabled(index)?theme_.foreground:theme_.muted,12,index==4||index==5,1);if(focused_==index&&GetFocus()==hwnd_)c.stroke(r,theme_.foreground,8,2);};
 button(1,L"Choose...");button(2,displayName_.empty()?L"No display":displayName_+L"  v");button(3,std::wstring(fitNames[static_cast<unsigned>(fit_)])+L"  v");
 c.fill(box(24,207,219,137),theme_.surface,10);
 if(!selected_.empty()){
  auto image=app_.icons().requestImage(selected_,previewRevision_);
  if(image){const auto width=static_cast<float>(image->width),height=static_cast<float>(image->height);const auto ratio=std::min(195.f/width,113.f/height);const auto w=width*ratio,h=height*ratio;c.icon(image,box(133.5f-w/2,275.5f-h/2,w,h),theme_.muted);}
  else {c.glyph(L'\uE91B',box(79,235,110,51),theme_.muted,31);c.text(L"Preview unavailable",box(31,297,205,25),theme_.muted,10,false,1);}
 }else {c.glyph(L'\uE91B',box(79,235,110,51),theme_.muted,31);c.text(L"Choose an image",box(31,297,205,25),theme_.muted,10,false,1);}
 c.text(L"SELECTED IMAGE",box(264,207,398,23),theme_.muted,10,true);c.text(selected_.empty()?L"No image selected":std::filesystem::path(selected_).filename().wstring(),box(264,233,398,28),theme_.foreground,14,true);
 std::wstring current=L"Refresh to read the current background.";
 if(snapshot_){std::vector<Rect> bounds;for(const auto& display:displays_)bounds.push_back(display.bounds);auto mapped=mapWallpaperDisplays(*snapshot_,bounds);auto display=std::find_if(displays_.begin(),displays_.end(),[&](const auto& item){return item.name==displayName_;});if(mapped&&display!=displays_.end()){const auto index=static_cast<std::size_t>(display-displays_.begin());for(const auto& monitor:snapshot_->monitors)if(monitor.display.device==(*mapped)[index].device)current=monitor.path.empty()?L"Solid color":std::filesystem::path(monitor.path).filename().wstring();}}
 c.text(L"CURRENT ON SELECTED DISPLAY",box(264,279,398,23),theme_.muted,10,true);c.text(current,box(264,303,398,27),theme_.foreground,12);
 button(4,L"Apply to selected",true);button(5,L"Apply to all",true);button(6,L"Undo last change");button(7,L"Refresh current");button(8,L"Close");
 button(9,L"Animated wallpapers / Marketplace");button(10,L"Stop animation");
 c.text(notice_,box(25,529,640,27),theme_.muted,11);c.text(L"Tab: focus   Enter: action   Escape: close",box(25,569,640,22),theme_.muted,10);
 accessibleSnapshot();
}
void Backgrounds::click(float x,float y){x/=layoutScale();y/=layoutScale();for(int i=1;i<ControlCount;++i)if(contains(controlRect(i),x,y)){if(enabled(i)){focusControl(i);invoke(i);}return;}}
LRESULT Backgrounds::event(UINT message,WPARAM w,LPARAM l,bool& handled){
 handled=true;
 switch(message){
 case WM_COMMAND:if(LOWORD(w)==301&&HIWORD(w)==EN_CHANGE&&!closing_){auto text=win::windowText(pathEdit_);if(text.size()<=WallpaperPathLimit){selected_=std::move(text);if(++previewRevision_==0)++previewRevision_;invalidate();accessibleSnapshot();}return 0;}break;
 case WM_SIZE:case WM_DPICHANGED:layoutEdit();break;
 case WM_MOVE:if(accessibility_){accessibleSnapshot();NotifyWinEvent(EVENT_OBJECT_LOCATIONCHANGE,hwnd_,OBJID_CLIENT,CHILDID_SELF);}break;
 case WM_SETFOCUS:case WM_KILLFOCUS:invalidate();accessibleSnapshot();break;
 case WM_GETOBJECT:if(accessibility_){UpdateWindow(hwnd_);accessibleSnapshot();return accessibility_->object(w,l,handled);}break;
 case WM_CLOSE:hide();return 0;
 case WM_KEYDOWN:
  if(w==VK_ESCAPE){hide();return 0;}
  if(w==VK_TAB){const int direction=(GetKeyState(VK_SHIFT)&0x8000)?-1:1;for(int n=1;n<=ControlCount;++n){const int next=(focused_+direction*n+ControlCount*2)%ControlCount;if(enabled(next)){focusControl(next);break;}}return 0;}
  if(w==VK_RETURN||w==VK_SPACE){invoke(focused_);return 0;}break;
 default:break;
 }
 handled=false;return 0;
}
LRESULT CALLBACK Backgrounds::editProcedure(HWND input,UINT message,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR data){
 auto* self=reinterpret_cast<Backgrounds*>(data);
 if(message==WM_SETTEXT&&l&&wcsnlen_s(reinterpret_cast<const wchar_t*>(l),WallpaperPathLimit+1)>WallpaperPathLimit)return FALSE;
 if(self&&!self->closing_){
  if(message==WM_KEYDOWN&&w==VK_TAB){SendMessageW(self->hwnd_,WM_KEYDOWN,w,l);return 0;}
  if(message==WM_KEYDOWN&&w==VK_ESCAPE){self->hide();return 0;}
  if(message==WM_KEYDOWN&&w==VK_RETURN){self->focusControl(1);return 0;}
  if(message==WM_CHAR&&(w==L'\t'||w==L'\r'||w==27))return 0;
  if(message==WM_SETFOCUS){self->focused_=0;self->accessibleSnapshot();self->invalidate();}
 }
 auto result=DefSubclassProc(input,message,w,l);if(message==WM_NCDESTROY)RemoveWindowSubclass(input,editProcedure,id);return result;
}
void Backgrounds::accessibleSnapshot(){
 if(!accessibility_||closing_)return;std::vector<AccessibleNode> nodes;
 for(int i=0;i<ControlCount;++i){
  long state=STATE_SYSTEM_FOCUSABLE;if(!enabled(i))state|=STATE_SYSTEM_UNAVAILABLE;
  auto node=accessibleNode(hwnd_,i+1,controlNames[i],i==0?ROLE_SYSTEM_TEXT:i==2||i==3?ROLE_SYSTEM_COMBOBOX:ROLE_SYSTEM_PUSHBUTTON,state,controlRect(i),layoutScale());
  if(i==0){node.native=pathEdit_;node.value=selected_;node.defaultAction=L"Focus";node.description=L"Editing the image path only selects a preview. Use Apply to change your desktop.";}
  if(i==2){node.value=displayName_;node.defaultAction=L"Open";}
  if(i==3){node.value=fitNames[static_cast<unsigned>(fit_)];node.defaultAction=L"Open";node.description=L"Windows uses this wallpaper position globally across all displays.";}
  node.focus=[this,i]{focusControl(i);};node.invoke=[this,i]{invoke(i);};nodes.push_back(std::move(node));
 }
 auto status=accessibleNode(hwnd_,12,L"Background status",ROLE_SYSTEM_STATICTEXT,0,box(25,529,640,27),layoutScale());status.value=notice_;status.description=notice_;status.defaultAction.clear();nodes.push_back(std::move(status));
 accessibility_->snapshot(L"Backgrounds",ROLE_SYSTEM_DIALOG,std::move(nodes),GetFocus()==pathEdit_?1:GetFocus()==hwnd_?focused_+1:0);
}
}
