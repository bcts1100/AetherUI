#include "editor.hpp"
#include "app.hpp"
#include "platform/dialogs.hpp"
#include <commdlg.h>
#include <cmath>
#include "core/tiling.hpp"
#include <sstream>
#include <iomanip>
namespace aether::ui {
namespace {
struct NameDialog {const wchar_t* title;std::string initial;std::optional<std::string> result;HFONT font=nullptr;};
INT_PTR CALLBACK nameDialogProc(HWND dialog,UINT message,WPARAM w,LPARAM l){
 auto* data=reinterpret_cast<NameDialog*>(GetWindowLongPtrW(dialog,DWLP_USER));
 auto validate=[&](){std::string error;auto name=win::normalizePresetName(win::utf8(win::windowText(GetDlgItem(dialog,101))),&error);
  EnableWindow(GetDlgItem(dialog,IDOK),name.has_value());SetDlgItemTextW(dialog,102,win::wide(error).c_str());return name;};
 switch(message){
 case WM_INITDIALOG:{
  data=reinterpret_cast<NameDialog*>(l);SetWindowLongPtrW(dialog,DWLP_USER,l);SetWindowTextW(dialog,data->title);
  int dpi=win::dpiFor(dialog);auto px=[&](int value){return MulDiv(value,dpi,96);};
  RECT owner{};GetWindowRect(GetWindow(dialog,GW_OWNER),&owner);
  MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(dialog,MONITOR_DEFAULTTONEAREST),&monitor);
  int width=px(480),height=px(210);int x=std::clamp((owner.left+owner.right-width)/2,monitor.rcWork.left,std::max(monitor.rcWork.left,monitor.rcWork.right-width));
  int y=std::clamp((owner.top+owner.bottom-height)/2,monitor.rcWork.top,std::max(monitor.rcWork.top,monitor.rcWork.bottom-height));
  SetWindowPos(dialog,nullptr,x,y,width,height,SWP_NOZORDER);
  data->font=CreateFontW(-px(14),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,L"Segoe UI");
  auto control=[&](const wchar_t* klass,const wchar_t* title,DWORD style,int id,int left,int top,int wide,int high){
   HWND item=CreateWindowExW(std::wcscmp(klass,L"EDIT")==0?WS_EX_CLIENTEDGE:0,klass,title,WS_CHILD|WS_VISIBLE|style,px(left),px(top),px(wide),px(high),dialog,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
   if(item)SendMessageW(item,WM_SETFONT,reinterpret_cast<WPARAM>(data->font?data->font:GetStockObject(DEFAULT_GUI_FONT)),TRUE);return item;};
  control(L"STATIC",std::wcsstr(data->title,L"pinned")?L"App label (up to 80 characters)":L"Preset name (up to 80 characters)",0,0,14,12,438,22);
  HWND edit=control(L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,101,14,38,438,28);
  control(L"STATIC",std::wcsstr(data->title,L"pinned")?L"Changes only its label. Launch target and app grouping stay attached.":std::wcsstr(data->title,L"Rename")?L"Changes only this preset's label.":L"Names are labels. Saving creates a new preset.",0,0,14,72,438,20);
  control(L"STATIC",L"",0,102,14,94,438,22);
  control(L"BUTTON",L"Save",WS_TABSTOP|BS_DEFPUSHBUTTON,IDOK,274,123,84,28);
  control(L"BUTTON",L"Cancel",WS_TABSTOP|BS_PUSHBUTTON,IDCANCEL,368,123,84,28);
  SendMessageW(dialog,DM_SETDEFID,IDOK,0);
  SendMessageW(edit,EM_SETLIMITTEXT,80,0);SetWindowTextW(edit,win::wide(data->initial).c_str());SendMessageW(edit,EM_SETSEL,0,-1);validate();SetFocus(edit);return FALSE;
 }
 case WM_COMMAND:
  if(LOWORD(w)==101&&HIWORD(w)==EN_CHANGE){validate();return TRUE;}
  if(LOWORD(w)==IDOK&&data){auto name=validate();if(name){data->result=std::move(*name);EndDialog(dialog,IDOK);}return TRUE;}
  if(LOWORD(w)==IDCANCEL){EndDialog(dialog,IDCANCEL);return TRUE;}break;
 case WM_CLOSE:EndDialog(dialog,IDCANCEL);return TRUE;
 case WM_DESTROY:if(data&&data->font){DeleteObject(data->font);data->font=nullptr;}return TRUE;
 default:break;
 }
 return FALSE;
}
std::optional<std::string> promptPresetName(HWND owner,const wchar_t* title,std::string_view initial){
 struct Template {DLGTEMPLATE dialog;WORD menu=0,windowClass=0,title=0;} definition{};
 definition.dialog.style=WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME;definition.dialog.cx=300;definition.dialog.cy=110;
 NameDialog data{title,std::string(initial),{},nullptr};
 if(DialogBoxIndirectParamW(GetModuleHandleW(nullptr),&definition.dialog,owner,nameDialogProc,reinterpret_cast<LPARAM>(&data))==-1)win::message(owner,L"Windows could not open the preset name editor.",MB_OK|MB_ICONERROR);
 return data.result;
}
}
Editor::Editor(Graphics& g,App& app):Surface(g,FontPart::Studio),app_(app){
 if(!create(L"Aether UI - Desktop Studio",WS_OVERLAPPEDWINDOW,WS_EX_APPWINDOW,{0,0,1000,760}))throw std::runtime_error("Could not create Desktop Studio.");
 accessibility_=std::make_unique<Accessibility>(hwnd_);
}
std::vector<win::Monitor> Editor::connectedDisplays() const{
 return app_.displays().empty()?win::monitors():app_.displays();
}
void Editor::open(const win::Monitor& monitor){
 move(popupBounds(monitor.work,1000,760,monitor.dpi),false);
 themeLibraryNotice_.clear();layoutLibraryNotice_.clear();reloadThemePresets();reloadLayoutPresets();update();BOOL dark=theme_.background<0xAAAAAA;DwmSetWindowAttribute(hwnd_,static_cast<DWMWINDOWATTRIBUTE>(20),&dark,sizeof(dark));show(true);invalidate();
}
void Editor::update(){
 updatePaletteImage();
 bool changed=theme_!=app_.theme();
 if(changed){theme_=app_.theme();BOOL dark=theme_.background<0xAAAAAA;
  DwmSetWindowAttribute(hwnd_,static_cast<DWMWINDOWATTRIBUTE>(20),&dark,sizeof(dark));}
 auto displays=connectedDisplays();bool topology=displays.size()!=displayMap_.size();
 if(!topology)for(std::size_t i=0;i<displays.size();++i){const auto& a=displays[i];const auto& b=displayMap_[i];
  if(a.name!=b.name||a.bounds!=b.bounds||a.dpi!=b.dpi||a.primary!=b.primary){topology=true;break;}}
 if(topology){displayMap_=std::move(displays);changed=true;
  if(!displayName_.empty()&&std::none_of(displayMap_.begin(),displayMap_.end(),[this](const auto& display){return win::utf8(display.name)==displayName_;}))displayName_.clear();
  focusedAction_=-1;
 }
 if(changed&&visible())invalidate();
}
void Editor::reloadThemePresets(){
 std::string error;savedThemes_=win::savedThemePresets(&error);if(!error.empty())themeLibraryNotice_=std::move(error);
 focusedAction_=-1;
}
void Editor::saveThemePreset(){
 app_.beginInteraction();auto name=promptPresetName(hwnd_,L"Save palette",app_.config().theme.name);app_.endInteraction();if(!name)return;
 std::string error;auto saved=win::saveThemePreset(app_.config().theme,*name,&error);
 themeLibraryNotice_=saved?"Saved palette: "+saved->name:std::move(error);reloadThemePresets();invalidate();
}
void Editor::applyThemePreset(const std::string& id){
 std::string error;auto theme=win::loadThemePreset(id,&error);
 if(!theme){themeLibraryNotice_=std::move(error);reloadThemePresets();invalidate();return;}
 auto config=app_.config();config.theme=*theme;app_.apply(std::move(config));
 auto saved=std::find_if(savedThemes_.begin(),savedThemes_.end(),[&](const auto& item){return item.id==id;});
 themeLibraryNotice_="Applied palette: "+(saved!=savedThemes_.end()?saved->name:id);invalidate();
}
void Editor::renameThemePreset(const std::string& id){
 auto saved=std::find_if(savedThemes_.begin(),savedThemes_.end(),[&](const auto& item){return item.id==id;});if(saved==savedThemes_.end())return;
 app_.beginInteraction();auto name=promptPresetName(hwnd_,L"Rename palette",saved->name);app_.endInteraction();if(!name)return;
 std::string error;bool changed=win::renameThemePreset(id,*name,&error);
 themeLibraryNotice_=changed?"Renamed palette: "+*name:std::move(error);reloadThemePresets();invalidate();
}
void Editor::deleteThemePreset(const std::string& id){
 std::string error;bool removed=win::removeThemePreset(id,&error);themeLibraryNotice_=removed?"Removed saved palette.":std::move(error);reloadThemePresets();invalidate();
}
void Editor::reloadLayoutPresets(){
 std::string error;savedLayouts_=win::savedLayoutPresets(&error);if(!error.empty())layoutLibraryNotice_=std::move(error);focusedAction_=-1;
}
void Editor::saveLayoutPreset(){
 app_.beginInteraction();auto name=promptPresetName(hwnd_,L"Save layout","My desktop");app_.endInteraction();if(!name)return;
 std::string error;auto saved=win::saveLayoutPreset(app_.config(),*name,&error);
 layoutLibraryNotice_=saved?"Saved layout: "+saved->name:std::move(error);reloadLayoutPresets();invalidate();
}
void Editor::applyLayoutPreset(const std::string& id){
 std::string error;auto saved=win::loadLayoutPreset(id,&error);
 if(!saved){layoutLibraryNotice_=std::move(error);reloadLayoutPresets();invalidate();return;}
 std::vector<std::string> connected;for(const auto& display:connectedDisplays())connected.push_back(win::utf8(display.name));
 auto config=win::applyLayoutPreset(app_.config(),*saved,connected,&error);
 if(error.empty()){app_.apply(std::move(config));layoutLibraryNotice_="Applied complete layout: "+saved->name;}else layoutLibraryNotice_=std::move(error);invalidate();
}
void Editor::renameLayoutPreset(const std::string& id){
 auto saved=std::find_if(savedLayouts_.begin(),savedLayouts_.end(),[&](const auto& item){return item.id==id;});if(saved==savedLayouts_.end())return;
 app_.beginInteraction();auto name=promptPresetName(hwnd_,L"Rename layout",saved->name);app_.endInteraction();if(!name)return;
 std::string error;bool changed=win::renameLayoutPreset(id,*name,&error);
 layoutLibraryNotice_=changed?"Renamed layout: "+*name:std::move(error);reloadLayoutPresets();invalidate();
}
void Editor::deleteLayoutPreset(const std::string& id){
 std::string error;bool removed=win::removeLayoutPreset(id,&error);layoutLibraryNotice_=removed?"Removed saved layout.":std::move(error);reloadLayoutPresets();invalidate();
}
void Editor::button(Canvas& c,D2D1_RECT_F r,const std::wstring& text,std::function<void()> callback,bool selected,std::wstring accessibleName,long role){
 bool hovered=contains(r,pointerX_,pointerY_);
 c.fill(r,selected?theme_.accent:hovered?theme_.raised:theme_.surface,9);
 c.text(text,r,selected?textOnAccent(theme_.accent):theme_.foreground,12,selected,1);
 action(r,std::move(callback),accessibleName.empty()?text:std::move(accessibleName),role,STATE_SYSTEM_FOCUSABLE|(selected?STATE_SYSTEM_SELECTED:0));
}
void Editor::toggle(Canvas& c,float y,const std::wstring& name,const std::wstring& detail,bool on,std::function<void()> action){
 float width=c.size().width-244;
 c.fill(box(220,y,width,68),theme_.surface,11);
 c.text(name,box(238,y+7,width-95,26),theme_.foreground,14,true);
 c.text(detail,box(238,y+34,width-95,22),theme_.muted,11);
 auto r=box(c.size().width-91,y+20,49,27);c.fill(r,on?theme_.accent:theme_.raised,13);
 c.circle(on?r.right-14:r.left+14,r.top+13.5f,9,0xFFFFFF);
 this->action(box(220,y,width,68),std::move(action),name,ROLE_SYSTEM_CHECKBUTTON,STATE_SYSTEM_FOCUSABLE|(on?STATE_SYSTEM_CHECKED:0),detail);
}
float Editor::layoutScale() const{
 RECT r{};GetClientRect(hwnd_,&r);return std::clamp(r.right/scale()/730.f,.1f,1.f);
}
float Editor::contentHeight() const{
 return page_==6?1020.f+static_cast<float>(app_.appearanceBridge().options().rules.size())*48:page_==5?1100.f+static_cast<float>(iconPacks_.size())*136:page_==4?660.f:page_==7?960.f:page_==8?1100.f:page_==9?1390.f:page_==10?470.f:page_==1?1870.f+static_cast<float>(savedLayouts_.size()+app_.config().snapLayouts.size())*62:page_==3?144.f+static_cast<float>(app_.config().pins.size())*108:page_==0?850.f+static_cast<float>(savedThemes_.size())*62:740.f;
}
void Editor::focusAction(int direction){
 UpdateWindow(hwnd_);if(actions_.empty())return;SetFocus(hwnd_);
 focusedAction_=focusedAction_<0?(direction>0?0:static_cast<int>(actions_.size())-1):(focusedAction_+direction+static_cast<int>(actions_.size()))%static_cast<int>(actions_.size());
 auto rect=actions_[static_cast<std::size_t>(focusedAction_)].first;RECT client{};GetClientRect(hwnd_,&client);
 // Keep focused rows inside the viewport instead of exactly on its float boundary.
 float bottom=client.bottom/scale()/layoutScale()-44;
 if(rect.left>=194){
  float adjustment=rect.top<112?rect.top-112:rect.bottom>bottom?rect.bottom-bottom:0;
  float maximum=std::max(0.f,contentHeight()-(bottom-108));scroll_=std::clamp(scroll_+adjustment,0.f,maximum);
 }
 invalidate();UpdateWindow(hwnd_);
}
void Editor::paint(Canvas& c){
 c.setScale(layoutScale());
 auto size=c.size();float width=size.width,height=size.height;actions_.clear();accessibleActions_.clear();
 c.fill(box(194,0,width-194,height),theme_.background);
 c.surface(box(0,0,194,height),theme_);
 c.grid(box(22,20,34,34),theme_.accent);
 c.text(L"AETHER",box(68,20,100,34),theme_.foreground,18,true);
 c.text(L"DESKTOP STUDIO",box(24,59,150,24),theme_.muted,10,true);
 const wchar_t* labels[]={L"Colors & themes",L"Displays & layout",L"Behavior & recovery",L"Pinned apps",L"Wallpaper",L"Icons & presets",L"Windows integration",L"Fonts",L"Bars & dock",L"Terminal",L"Widgets & plugins"};
 const float navPitch=std::clamp((height-122)/11,24.f,39.f);
 for(int i=0;i<11;++i)button(c,box(12,108+i*navPitch,170,navPitch-4),labels[i],[this,i]{page_=i;scroll_=0;focusedAction_=-1;if(i==5)iconPacks_=win::iconPacks();invalidate();},page_==i,labels[i],ROLE_SYSTEM_PAGETAB);
 if(height>540)c.text(L"Native desktop alpha",box(24,height-70,160,24),theme_.muted,11);
 if(height>540)c.text(L"v0.9.0  |  C++ desktop",box(24,height-46,160,22),theme_.muted,10);
 c.text(labels[page_],box(220,22,width-248,39),theme_.foreground,26,true);
 const wchar_t* descriptions[]={L"Desktop colors, theme files and saved palettes.",L"Monitor profiles, panel placement and window tiling.",L"Startup behavior, keyboard shortcuts and native recovery.",L"Manage the applications in your dock.",L"Backgrounds and automatic workspace colors.",L"Offline icon packs and coordinated desktop presets.",L"Optional Windows appearance and per-app frame rules.",L"Installed typography and fonts for individual UI components.",L"Top-bar and dock materials, dimensions and module placement.",L"Real shells, ten stock designs and your own background images.",L"Widgets, layout editing and bundled plugins."};
 c.text(descriptions[page_],box(221,68,width-248,27),theme_.muted,12);
 c.pushClip(box(201,108,width-201,std::max(1.f,height-151)));
 float top=114-scroll_;
 if(page_==0)appearance(c,top);else if(page_==1)layouts(c,top);else if(page_==2)behavior(c,top);else if(page_==4||page_==7||page_==8||page_==10)personalize(c,top);else if(page_==9)terminalSettings(c,top);else if(page_==5)packs(c,top);else if(page_==6)integration(c,top);else pins(c,top);
 c.popClip();
 c.fill(box(201,height-40,width-201,40),theme_.background);
 c.line(219,height-40,width-24,height-40,theme_.raised);
 c.text(page_==4?L"Palette previews wait for Apply | Fonts and bars save as you choose":L"Saved automatically  |  Tab / Shift+Tab to navigate, Enter to apply",box(221,height-36,width-245,30),theme_.muted,10);
 if(focusedAction_>=0 && static_cast<std::size_t>(focusedAction_)<actions_.size()){
  auto r=actions_[static_cast<std::size_t>(focusedAction_)].first;
  if(GetFocus()==hwnd_&&(r.left<194 || (r.top>=108 && r.bottom<=height-40)))c.stroke(r,theme_.foreground,9,2);
 }
 accessibleSnapshot();
}
void Editor::appearance(Canvas& c,float y){
 float available=c.size().width-244,card=(available-24)/3;
 for(int i=0;i<3;++i){
  Theme t=preset(i);float x=220+i*(card+12);
  c.fill(box(x,y,card,137),t.background,12);
  c.fill(box(x+10,y+10,card-20,14),t.surface,4);
  c.circle(x+20,y+17,2,t.accent);
  c.fill(box(x+card*.27f,y+82,card*.46f,24),t.surface,8);
  for(int j=0;j<4;++j)c.fill(box(x+card*.3f+j*(card*.095f),y+88,10,11),j==1?t.accent:t.muted,3);
  c.text(win::wide(t.name),box(x+12,y+110,card-24,24),t.foreground,13,true);
  if(app_.config().theme.name==t.name)c.stroke(box(x+.6f,y+.6f,card-1.2f,136),theme_.accent,12,2);
  action(box(x,y,card,137),[this,i]{auto config=app_.config();config.theme=preset(i);app_.apply(std::move(config));},win::wide(t.name),ROLE_SYSTEM_LISTITEM,STATE_SYSTEM_FOCUSABLE|STATE_SYSTEM_SELECTABLE|(app_.config().theme==t?STATE_SYSTEM_SELECTED:0));
 }
 c.text(L"COLOR TOKENS",box(222,y+159,200,24),theme_.muted,10,true);
 struct Token {const wchar_t* name;std::uint32_t Theme::*member;};
 const Token tokens[]={{L"Background",&Theme::background},{L"Surface",&Theme::surface},{L"Raised",&Theme::raised},{L"Text",&Theme::foreground},{L"Muted text",&Theme::muted},{L"Accent",&Theme::accent}};
 for(int i=0;i<6;++i){
  float x=220+(i%3)*(card+12),row=y+193+(i/3)*64.f;auto member=tokens[i].member;
  auto r=box(x,row,card,51);c.fill(r,theme_.surface,9);
  c.circle(x+22,row+25,10,theme_.*member);
  c.text(tokens[i].name,box(x+42,row,card-48,51),theme_.foreground,12);
  action(r,[this,member]{chooseColor(member);},std::wstring(L"Change ")+tokens[i].name);
 }
 c.text(L"Corner radius",box(224,y+330,200,33),theme_.foreground,14,true);
 button(c,box(440,y+330,42,33),L"-",[this]{auto config=app_.config();config.theme.radius=std::max(0,config.theme.radius-2);app_.apply(config);},false,L"Decrease corner radius");
 c.text(std::to_wstring(theme_.radius)+L" px",box(486,y+330,70,33),theme_.foreground,13,false,1);
 button(c,box(562,y+330,42,33),L"+",[this]{auto config=app_.config();config.theme.radius=std::min(28,config.theme.radius+2);app_.apply(config);},false,L"Increase corner radius");
 button(c,box(220,y+387,146,37),L"Import theme",[this]{importTheme();});
 button(c,box(378,y+387,146,37),L"Export theme",[this]{exportTheme();});
 c.text(L"Themes are data-only .aethertheme files. No scripts are executed.",box(221,y+440,available,24),theme_.muted,11);
 c.text(L"Frosted panels use desktop blur where available; other styles remain opaque.",box(221,y+468,available,24),theme_.muted,11);
 button(c,box(220,y+510,210,37),L"Save current palette",[this]{saveThemePreset();});
 c.text(L"Up to 32 saved palettes per profile",box(442,y+510,available-222,37),theme_.muted,10);
 c.text(themeLibraryNotice_.empty()?L"Saved here in your settings folder. Click a palette to apply it.":win::wide(themeLibraryNotice_),box(221,y+553,available,24),theme_.muted,11);
 c.text(L"SAVED PALETTES",box(222,y+577,available,17),theme_.muted,10,true);
 if(savedThemes_.empty())c.text(L"No saved palettes yet.",box(232,y+599,available-24,36),theme_.muted,12);
 for(std::size_t i=0;i<savedThemes_.size();++i){const auto& saved=savedThemes_[i];float row=y+594+static_cast<float>(i)*62;
  auto r=box(220,row,available-196,52);bool selected=app_.config().theme==saved.theme;
  c.fill(r,contains(r,pointerX_,pointerY_)?theme_.raised:theme_.surface,9);if(selected)c.stroke(r,theme_.accent,9,2);
  const auto& t=saved.theme;const std::uint32_t swatches[]={t.background,t.surface,t.raised,t.foreground,t.muted,t.accent};
  for(int n=0;n<6;++n)c.fill(box(232+n*15.f,row+12,12,28),swatches[n],3);
  c.text(win::wide(saved.name),box(334,row+4,available-320,25),theme_.foreground,12,true);
  c.text(L"Palette "+win::wide(saved.id.substr(7)),box(334,row+28,available-320,19),theme_.muted,10);
  action(r,[this,id=saved.id]{applyThemePreset(id);},win::wide(saved.name),ROLE_SYSTEM_LISTITEM,STATE_SYSTEM_FOCUSABLE|STATE_SYSTEM_SELECTABLE|(selected?STATE_SYSTEM_SELECTED:0));
  button(c,box(c.size().width-210,row+9,88,34),L"Rename",[this,id=saved.id]{renameThemePreset(id);},false,L"Rename palette "+win::wide(saved.name));
  button(c,box(c.size().width-112,row+9,88,34),L"Remove",[this,id=saved.id]{deleteThemePreset(id);},false,L"Remove palette "+win::wide(saved.name));
 }
 appearanceDetails(c,y+std::max(668.f,604.f+static_cast<float>(savedThemes_.size())*62.f));
}
void Editor::appearanceDetails(Canvas& c,float y){
 const float width=c.size().width-244;
 c.text(L"MORE LOOKS",box(222,y,width,24),theme_.muted,10,true);
 float half=(width-12.f)/2.f;
 button(c,box(220,y+34,half,37),L"Frosted glass",[this]{auto config=app_.config();config.theme=preset(3);app_.apply(config);},app_.config().theme==preset(3));
 button(c,box(232+half,y+34,half,37),L"Minimal",[this]{auto config=app_.config();config.theme=preset(4);app_.apply(config);},app_.config().theme==preset(4));
 c.text(L"Frosted uses desktop blur where available, with an opaque fallback.",box(221,y+80,width,30),theme_.muted,10);
 c.text(L"Fonts, wallpaper and bar designs each have their own section in the sidebar.",box(222,y+125,width,48),theme_.muted,12);

}
void Editor::layoutChange(const std::function<void(DisplayOptions&)>& change){
 auto config=app_.config();
 if(displayName_.empty())change(config.defaults);
 else {
  auto displays=connectedDisplays();if(std::none_of(displays.begin(),displays.end(),[this](const auto& display){return win::utf8(display.name)==displayName_;})){update();invalidate();return;}
  auto options=config.forDisplay(displayName_);change(options);config.displays[displayName_]=options;
 }
 app_.apply(std::move(config));invalidate();
}
void Editor::copyDisplayProfile(){
 auto displays=connectedDisplays();if(displays.empty())return;
 if(!displayName_.empty()&&std::none_of(displays.begin(),displays.end(),[this](const auto& display){return win::utf8(display.name)==displayName_;})){update();invalidate();return;}
 auto config=app_.config();auto options=displayName_.empty()?config.defaults:config.forDisplay(displayName_);
 for(const auto& display:displays)config.displays[win::utf8(display.name)]=options;
 app_.apply(std::move(config));invalidate();
}
void Editor::resetDisplayProfile(){
 if(displayName_.empty())return;
 auto config=app_.config();config.displays.erase(displayName_);app_.apply(std::move(config));invalidate();
}
void Editor::layouts(Canvas& c,float y){
 const auto& displays=displayMap_;
 c.fill(box(220,y,c.size().width-244,132),theme_.surface,11);
 c.text(L"YOUR DISPLAYS",box(234,y+4,180,23),theme_.muted,10,true);
 if(displays.empty())c.text(L"No connected displays are available.",box(234,y+36,c.size().width-274,70),theme_.muted,12);
 else{
  Rect bounds=displays.front().bounds;
  for(const auto& display:displays){bounds.left=std::min(bounds.left,display.bounds.left);bounds.top=std::min(bounds.top,display.bounds.top);bounds.right=std::max(bounds.right,display.bounds.right);bounds.bottom=std::max(bounds.bottom,display.bounds.bottom);}
  float available=c.size().width-278;
  float factor=std::min(available/std::max(1,bounds.width()),84.f/std::max(1,bounds.height()));
  float origin=234+(available-bounds.width()*factor)/2;
  for(std::size_t i=0;i<displays.size();++i){const auto& display=displays[i];
   auto r=box(origin+(display.bounds.left-bounds.left)*factor,y+33+(display.bounds.top-bounds.top)*factor,display.bounds.width()*factor,display.bounds.height()*factor);
   auto name=win::utf8(display.name);bool selected=displayName_==name;
   c.fill(r,selected?theme_.raised:theme_.background,5);c.stroke(r,selected?theme_.accent:theme_.muted,5,selected?2.f:1.f);
   c.text(std::to_wstring(i+1)+(display.primary?L" *":L""),box(r.left,r.top,r.right-r.left,(r.bottom-r.top)*.55f),theme_.foreground,13,true,1);
   c.text(std::to_wstring(MulDiv(display.dpi,100,96))+L"%",box(r.left,r.top+(r.bottom-r.top)*.55f,r.right-r.left,(r.bottom-r.top)*.45f),theme_.muted,10,false,1);
   action(r,[this,name]{displayName_=name;focusedAction_=-1;invalidate();},L"Display "+std::to_wstring(i+1)+(display.primary?L", primary":L"")+L", "+std::to_wstring(MulDiv(display.dpi,100,96))+L" percent scaling",ROLE_SYSTEM_LISTITEM,STATE_SYSTEM_FOCUSABLE|STATE_SYSTEM_SELECTABLE|(selected?STATE_SYSTEM_SELECTED:0));
  }
 }
 y+=144;
 std::wstring displayLabel=L"Default profile";
 for(std::size_t i=0;i<displays.size();++i)if(win::utf8(displays[i].name)==displayName_)displayLabel=L"Display "+std::to_wstring(i+1)+(app_.config().displays.contains(displayName_)?L" - custom":L" - default");
 button(c,box(220,y,258,36),displayLabel+L"  v",[this]{
  HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,1,L"Default profile");
  auto monitors=connectedDisplays();
  for(std::size_t i=0;i<monitors.size();++i){auto title=L"Display "+std::to_wstring(i+1)+L"  "+monitors[i].name;AppendMenuW(menu,MF_STRING,10+i,title.c_str());}
  POINT p{};GetCursorPos(&p);app_.beginInteraction();auto id=TrackPopupMenuEx(menu,TPM_RETURNCMD|TPM_NONOTIFY,p.x,p.y,hwnd_,nullptr);DestroyMenu(menu);app_.endInteraction();
  if(id==1)displayName_.clear();else if(id>=10&&id-10<monitors.size())displayName_=win::utf8(monitors[id-10].name);
  focusedAction_=-1;update();invalidate();
 });
 if(!displayName_.empty())button(c,box(490,y,180,36),L"Reset override",[this]{resetDisplayProfile();});
 button(c,box(220,y+47,280,36),L"Copy to connected displays",[this]{copyDisplayProfile();});
 c.text(L"Defaults stay unchanged",box(510,y+47,c.size().width-534,36),theme_.muted,10);
 auto options=displayName_.empty()?app_.config().defaults:app_.config().forDisplay(displayName_);
 c.text(L"Dock position",box(222,y+96,180,30),theme_.foreground,14,true);
 const Edge edges[]={Edge::Left,Edge::Bottom,Edge::Right};const wchar_t* titles[]={L"Left",L"Bottom",L"Right"};
 for(int i=0;i<3;++i)button(c,box(390+i*95.f,y+96,84,32),titles[i],[this,e=edges[i]]{layoutChange([e](DisplayOptions& o){o.edge=e;});},options.edge==edges[i]);
 c.text(L"Icon size",box(222,y+143,170,34),theme_.foreground,14,true);
 button(c,box(390,y+143,42,34),L"-",[this]{layoutChange([](DisplayOptions& o){o.iconSize=std::max(32,o.iconSize-4);});},false,L"Decrease icon size");
 c.text(std::to_wstring(options.iconSize)+L" px",box(440,y+143,74,34),theme_.foreground,13,false,1);
 button(c,box(526,y+143,42,34),L"+",[this]{layoutChange([](DisplayOptions& o){o.iconSize=std::min(72,o.iconSize+4);});},false,L"Increase icon size");
 c.text(L"Dock edge gap",box(222,y+190,170,34),theme_.foreground,14,true);
 button(c,box(390,y+190,42,34),L"-",[this]{layoutChange([](DisplayOptions& o){o.dockGap=std::max(0,o.dockGap-2);});},false,L"Decrease dock edge gap");
 c.text(std::to_wstring(options.dockGap)+L" px",box(440,y+190,74,34),theme_.foreground,13,false,1);
 button(c,box(526,y+190,42,34),L"+",[this]{layoutChange([](DisplayOptions& o){o.dockGap=std::min(32,o.dockGap+2);});},false,L"Increase dock edge gap");
 toggle(c,y+243,L"Top bar",L"Activities, focused window, clock, and system controls.",options.topBar,[this]{layoutChange([](DisplayOptions& o){o.topBar=!o.topBar;});});
 toggle(c,y+321,L"Dock",L"Pinned apps and running windows on this display.",options.dock,[this]{layoutChange([](DisplayOptions& o){o.dock=!o.dock;});});
 toggle(c,y+399,L"Auto-hide dock",L"Reveal from its edge; reserve-space is disabled while auto-hiding.",options.autoHide,[this]{layoutChange([](DisplayOptions& o){o.autoHide=!o.autoHide;});});
 toggle(c,y+477,L"Only this monitor's windows",L"Pins remain visible on every enabled dock.",options.monitorOnly,[this]{layoutChange([](DisplayOptions& o){o.monitorOnly=!o.monitorOnly;});});
 toggle(c,y+555,L"Reserve space for the dock",L"Keep maximized windows clear of the dock.",options.reserveDock,[this]{layoutChange([](DisplayOptions& o){o.reserveDock=!o.reserveDock;});});
 c.text(L"Ctrl-drag a dock to change its edge. Reset an override to inherit future default edits.",box(221,y+637,c.size().width-244,26),theme_.muted,11);
 float available=c.size().width-244;
 button(c,box(220,y+685,210,37),L"Save complete layout",[this]{saveLayoutPreset();});
 c.text(L"Up to 32 named layouts",box(442,y+685,available-222,37),theme_.muted,10);
 c.text(L"Includes defaults and display overrides. Theme, pins and behavior stay unchanged.",box(221,y+733,available,22),theme_.muted,10);
 c.text(L"Connected displays use saved overrides or inherit the saved defaults.",box(221,y+757,available,22),theme_.muted,10);
 c.text(L"Disconnected settings are kept. Missing saved profiles are added for reconnection.",box(221,y+781,available,22),theme_.muted,10);
 c.text(layoutLibraryNotice_.empty()?L"Data-only .aetherlayout files in your settings folder.":win::wide(layoutLibraryNotice_),box(221,y+812,available,24),theme_.muted,11);
 c.text(L"SAVED LAYOUTS",box(222,y+842,available,17),theme_.muted,10,true);
 if(savedLayouts_.empty())c.text(L"No saved layouts yet.",box(232,y+864,available-24,36),theme_.muted,12);
 for(std::size_t i=0;i<savedLayouts_.size();++i){const auto& saved=savedLayouts_[i];float row=y+860+static_cast<float>(i)*62;
  auto r=box(220,row,available-196,52);c.fill(r,contains(r,pointerX_,pointerY_)?theme_.raised:theme_.surface,9);
  c.text(win::wide(saved.name),box(234,row+4,available-224,25),theme_.foreground,12,true);
  c.text(std::to_wstring(saved.displays.size())+L" display overrides | "+win::wide(saved.id.substr(7)),box(234,row+28,available-224,19),theme_.muted,10);
  action(r,[this,id=saved.id]{applyLayoutPreset(id);},win::wide(saved.name),ROLE_SYSTEM_LISTITEM);
  button(c,box(c.size().width-210,row+9,88,34),L"Rename",[this,id=saved.id]{renameLayoutPreset(id);},false,L"Rename layout "+win::wide(saved.name));
  button(c,box(c.size().width-112,row+9,88,34),L"Remove",[this,id=saved.id]{deleteLayoutPreset(id);},false,L"Remove layout "+win::wide(saved.name));
 }
 snapLayouts(c,y+930.f+static_cast<float>(savedLayouts_.size())*62.f);
 float extras=y+1420.f+static_cast<float>(savedLayouts_.size()+app_.config().snapLayouts.size())*62.f;
 c.text(L"TOP BAR CONTENT",box(222,extras,available,24),theme_.muted,10,true);
 toggle(c,extras+32,L"Show clock",L"Clock visibility for the selected display profile.",options.clock,[this]{layoutChange([](DisplayOptions& o){o.clock=!o.clock;});});
 toggle(c,extras+111,L"Show focused title",L"Focused application title when the bar has room.",options.focusedTitle,[this]{layoutChange([](DisplayOptions& o){o.focusedTitle=!o.focusedTitle;});});
 toggle(c,extras+190,L"Show status",L"Network, volume and battery. Settings stays available.",options.status,[this]{layoutChange([](DisplayOptions& o){o.status=!o.status;});});
}
void Editor::behavior(Canvas& c,float y){
 const auto& config=app_.config();
 toggle(c,y,L"Hide Windows taskbar on load",L"Recovery helper required. Ctrl+Alt+F12 restores the taskbar.",app_.taskbarStartupRequested(),[this]{app_.setTaskbarHiding(!app_.taskbarStartupRequested());});
 toggle(c,y+79,L"Hide panels over fullscreen apps",L"Per-display detection. No game process injection or FPS claims.",config.hideFullscreen,[this]{auto next=app_.config();next.hideFullscreen=!next.hideFullscreen;app_.apply(next);});
 toggle(c,y+158,L"24-hour clock",L"Use a 24-hour clock in each top bar.",config.clock24,[this]{auto next=app_.config();next.clock24=!next.clock24;app_.apply(next);});
 toggle(c,y+237,L"Live window previews",L"Live previews in Activities and dock window lists.",config.windowPreviews,[this]{auto next=app_.config();next.windowPreviews=!next.windowPreviews;app_.apply(next);});
 button(c,box(220,y+332,208,39),app_.taskbarStartupRequested()&&!config.hideTaskbar?L"Resume taskbar hiding":L"Restore Windows taskbar",[this]{if(app_.taskbarStartupRequested()&&!app_.config().hideTaskbar)app_.setTaskbarHiding(true);else app_.restoreNative();},true);
 button(c,box(440,y+332,150,39),L"Rescan apps",[this]{app_.catalog().requestRescan();});
 button(c,box(220,y+387,208,39),L"Open settings folder",[this]{app_.launchTarget(hwnd_,win::configDirectory().wstring());});
 button(c,box(440,y+387,150,39),L"Quit Aether",[this]{app_.quit();});
 c.text(L"Ctrl+Alt+Space  Activities     Ctrl+Alt+E  Desktop Studio",box(222,y+454,c.size().width-248,27),theme_.muted,12);
 c.text(L"Ctrl+Alt+1 / 2  Snap left/right     Ctrl+Alt+3..6  Snap corners",box(222,y+487,c.size().width-248,27),theme_.muted,12);
 c.text(L"Ctrl+Alt+F12 restores the taskbar. Explorer stays running.",box(222,y+528,c.size().width-248,27),theme_.foreground,12,true);
 c.text(L"No startup entry is created. No browser engine is embedded.",box(222,y+562,c.size().width-248,27),theme_.muted,11);
 toggle(c,y+620,L"Reduce motion",L"Pause animated wallpaper and dock easing. Windows animation preferences are also respected.",config.reduceMotion,[this]{auto next=app_.config();next.reduceMotion=!next.reduceMotion;app_.apply(std::move(next));});
}
void Editor::pins(Canvas& c,float y){
 button(c,box(220,y,170,36),L"Add application",[this]{app_.choosePin(hwnd_);});
 button(c,box(402,y,170,36),L"Open Activities",[this]{app_.openActivities();});
 c.text(L"Rename labels or choose local PNG/ICO icons. Launch targets and grouping stay attached.",box(221,y+47,c.size().width-245,28),theme_.muted,11);
 const auto& pins=app_.config().pins;
 for(std::size_t i=0;i<pins.size();++i){
  float row=y+91+static_cast<float>(i)*108;auto pin=pins[i];auto name=win::wide(pin.name);
  auto r=box(220,row,c.size().width-244,99);c.fill(r,theme_.surface,10);
  c.icon(app_.icons().request(pin),box(231,row+10,32,32),theme_.muted);
  c.text(name,box(278,row+4,c.size().width-440,25),theme_.foreground,13,true);
  c.text(win::wide(pin.target),box(278,row+28,c.size().width-440,20),theme_.muted,10);
  button(c,box(c.size().width-162,row+10,35,32),L"<",[this,pin]{auto index=findPinByTarget(app_.config(),pin);if(index&&*index>0)app_.movePin(*index,*index-1);},false,L"Move "+name+L" earlier");
  button(c,box(c.size().width-122,row+10,35,32),L">",[this,pin]{auto index=findPinByTarget(app_.config(),pin);if(index)app_.movePin(*index,*index+1);},false,L"Move "+name+L" later");
  button(c,box(c.size().width-82,row+10,42,32),L"X",[this,pin]{auto index=findPinByTarget(app_.config(),pin);if(index)app_.removePin(*index);},false,L"Remove "+name);
  button(c,box(232,row+59,108,30),L"Rename",[this,pin]{renamePin(pin);},false,L"Rename "+name);
  button(c,box(350,row+59,130,30),L"Choose icon",[this,pin]{choosePinIcon(pin);},false,L"Choose icon for "+name);
  button(c,box(490,row+59,130,30),L"Reset icon",[this,pin]{resetPinIcon(pin);},false,L"Reset icon for "+name);
 }
}
void Editor::renamePin(const Pin& pin){
 app_.beginInteraction();auto name=promptPresetName(hwnd_,L"Rename pinned app",pin.name);app_.endInteraction();if(!name)return;
 auto config=app_.config();auto index=findPinByTarget(config,pin);if(!index)return;config.pins[*index].name=*name;app_.apply(std::move(config));invalidate();
}
void Editor::choosePinIcon(const Pin& pin){
 const COMDLG_FILTERSPEC filters[]={{L"Local icon image",L"*.png;*.ico"}};
 app_.beginInteraction();auto path=win::chooseFile(hwnd_,false,L"Choose a local PNG or ICO icon",filters,1);app_.endInteraction();if(!path)return;
 auto iconPath=win::utf8(path->wstring());if(!validLocalIconPath(iconPath)){win::message(hwnd_,L"Choose a local PNG or ICO image.",MB_OK|MB_ICONINFORMATION);return;}
 auto config=app_.config();auto index=findPinByTarget(config,pin);if(!index)return;auto& current=config.pins[*index];
 current.iconPath=std::move(iconPath);current.iconRevision=current.iconRevision==UINT64_MAX?1:current.iconRevision+1;app_.apply(std::move(config));invalidate();
}
void Editor::resetPinIcon(const Pin& pin){
 auto config=app_.config();auto index=findPinByTarget(config,pin);if(!index)return;auto& current=config.pins[*index];
 current.iconPath.clear();current.iconRevision=current.iconRevision==UINT64_MAX?1:current.iconRevision+1;app_.apply(std::move(config));invalidate();
}
void Editor::importTheme(){
 COMDLG_FILTERSPEC filter{L"Aether theme",L"*.aethertheme"};
 auto path=win::chooseFile(hwnd_,false,L"Import Aether theme",&filter,1);if(!path)return;
 std::string error;auto theme=parseTheme(win::readText(*path,65536),&error);
 if(!theme){win::message(hwnd_,win::wide(error),MB_OK|MB_ICONERROR);return;}
 auto config=app_.config();config.theme=*theme;app_.apply(std::move(config));
}
void Editor::exportTheme(){
 COMDLG_FILTERSPEC filter{L"Aether theme",L"*.aethertheme"};
 auto path=win::chooseFile(hwnd_,true,L"Export Aether theme",&filter,1,L"My Desktop.aethertheme",L"aethertheme");
 if(path)try{win::writeAtomic(*path,serializeTheme(app_.config().theme));}catch(const std::exception& e){win::message(hwnd_,win::wide(e.what()),MB_OK|MB_ICONERROR);}
}
void Editor::chooseColor(std::uint32_t Theme::*member){
 static COLORREF custom[16]{};auto current=app_.config().theme.*member;
 CHOOSECOLORW chooser{};chooser.lStructSize=sizeof(chooser);chooser.hwndOwner=hwnd_;
 chooser.rgbResult=RGB((current>>16)&255,(current>>8)&255,current&255);chooser.lpCustColors=custom;chooser.Flags=CC_FULLOPEN|CC_RGBINIT;
 if(ChooseColorW(&chooser)){auto config=app_.config();config.theme.*member=(static_cast<std::uint32_t>(GetRValue(chooser.rgbResult))<<16)|(static_cast<std::uint32_t>(GetGValue(chooser.rgbResult))<<8)|GetBValue(chooser.rgbResult);config.theme.name="Custom";app_.apply(config);}
}
void Editor::click(float x,float y){
 x/=layoutScale();y/=layoutScale();
 RECT client{};GetClientRect(hwnd_,&client);float bottom=client.bottom/scale()/layoutScale()-40;
 if(x>201&&(y<108||y>bottom))return;
 for(auto it=actions_.rbegin();it!=actions_.rend();++it)if(contains(it->first,x,y)){auto action=it->second;action();invalidate();return;}
}
void Editor::pointer(float x,float y){
 x/=layoutScale();y/=layoutScale();
 bool old=false,next=false;
 for(const auto& a:actions_){old=old||contains(a.first,pointerX_,pointerY_);next=next||contains(a.first,x,y);}
 if(x==pointerX_&&y==pointerY_)return;pointerX_=x;pointerY_=y;
 if(old||next)invalidate();
}
void Editor::leave(){pointerX_=-1;pointerY_=-1;invalidate();}
LRESULT Editor::event(UINT m,WPARAM w,LPARAM l,bool& handled){
 handled=true;
 switch(m){
 case WM_MOVE:if(accessibility_){accessibleSnapshot();NotifyWinEvent(EVENT_OBJECT_LOCATIONCHANGE,hwnd_,OBJID_CLIENT,CHILDID_SELF);}break;
 case WM_GETOBJECT:if(!accessibility_){handled=false;return 0;}UpdateWindow(hwnd_);accessibleSnapshot();return accessibility_->object(w,l,handled);
 case WM_SETFOCUS:case WM_KILLFOCUS:invalidate();break;
 case WM_CLOSE:hide();return 0;
 case WM_KEYDOWN:
  if(w==VK_ESCAPE){hide();return 0;}
  if(w==VK_TAB){focusAction((GetKeyState(VK_SHIFT)&0x8000)?-1:1);return 0;}
  if(w==VK_RETURN||w==VK_SPACE){UpdateWindow(hwnd_);if(focusedAction_>=0&&static_cast<std::size_t>(focusedAction_)<actions_.size()){auto action=actions_[static_cast<std::size_t>(focusedAction_)].second;RECT r{};GetClientRect(hwnd_,&r);auto rect=actions_[static_cast<std::size_t>(focusedAction_)].first;float bottom=r.bottom/scale()/layoutScale()-40;if(rect.left<194||(rect.top>=108&&rect.bottom<=bottom)){action();invalidate();}}return 0;}
  if(w==VK_NEXT||w==VK_PRIOR){SendMessageW(hwnd_,WM_MOUSEWHEEL,MAKEWPARAM(0,w==VK_NEXT?-360:360),0);return 0;}break;
 case WM_MOUSEWHEEL:{RECT r{};GetClientRect(hwnd_,&r);float content=contentHeight();float maxScroll=std::max(0.f,content-(r.bottom/scale()/layoutScale()-151));float next=std::clamp(scroll_-(GET_WHEEL_DELTA_WPARAM(w)/120.f)*55.f,0.f,maxScroll);if(next!=scroll_)focusedAction_=-1;scroll_=next;invalidate();return 0;}
 case WM_SIZE:focusedAction_=-1;break;
 case WM_GETMINMAXINFO:{auto* info=reinterpret_cast<MINMAXINFO*>(l);auto mtr=app_.display();info->ptMinTrackSize={std::min(MulDiv(730,win::dpiFor(hwnd_),96),mtr.work.width()),std::min(MulDiv(460,win::dpiFor(hwnd_),96),mtr.work.height())};return 0;}
 default:break;
 }
 handled=false;return 0;
}
}

namespace aether::ui {
void Editor::action(D2D1_RECT_F bounds,std::function<void()> callback,std::wstring name,long role,long state,std::wstring detail){
 const int index=static_cast<int>(actions_.size());actions_.emplace_back(bounds,callback);
 auto node=accessibleNode(hwnd_,index+1,std::move(name),role,state,bounds,layoutScale());node.description=std::move(detail);node.defaultAction=role==ROLE_SYSTEM_CHECKBUTTON?L"Toggle":role==ROLE_SYSTEM_PAGETAB||role==ROLE_SYSTEM_LISTITEM?L"Select":L"Press";
 node.invoke=[this,index]{UpdateWindow(hwnd_);if(index<0||static_cast<std::size_t>(index)>=actions_.size())return;auto callback=actions_[static_cast<std::size_t>(index)].second;callback();invalidate();UpdateWindow(hwnd_);};node.focus=[this,index]{focusIndex(index);};accessibleActions_.push_back(std::move(node));
}
void Editor::focusIndex(int index){
 UpdateWindow(hwnd_);if(index<0||static_cast<std::size_t>(index)>=actions_.size())return;focusedAction_=index;SetFocus(hwnd_);
 auto r=actions_[static_cast<std::size_t>(index)].first;RECT client{};GetClientRect(hwnd_,&client);float bottom=client.bottom/scale()/layoutScale()-44;
 if(r.left>=194){float adjustment=r.top<112?r.top-112:r.bottom>bottom?r.bottom-bottom:0;scroll_=std::clamp(scroll_+adjustment,0.f,std::max(0.f,contentHeight()-(bottom-108)));}invalidate();UpdateWindow(hwnd_);accessibleSnapshot();
}
void Editor::accessibleSnapshot(){
 if(!accessibility_)return;RECT client{};GetClientRect(hwnd_,&client);const float bottom=client.bottom/scale()/layoutScale()-40;auto nodes=accessibleActions_;
 for(std::size_t i=0;i<nodes.size()&&i<actions_.size();++i){auto r=actions_[i].first;nodes[i].bounds=accessibleNode(hwnd_,nodes[i].id,L"",0,0,r,layoutScale()).bounds;if(r.left>=194&&(r.top<108||r.bottom>bottom))nodes[i].state|=STATE_SYSTEM_OFFSCREEN;}
 accessibility_->snapshot(L"Desktop Studio",ROLE_SYSTEM_PROPERTYPAGE,std::move(nodes),GetFocus()==hwnd_&&focusedAction_>=0?focusedAction_+1:0);
}
void Editor::tilingChange(const std::function<void(TilingOptions&)>& change){
 auto config=app_.config();if(displayName_.empty())change(config.tilingDefaults);else{auto displays=connectedDisplays();if(std::none_of(displays.begin(),displays.end(),[this](const auto& d){return win::utf8(d.name)==displayName_;})){update();return;}auto options=config.tilingForDisplay(displayName_);change(options);config.tilingDisplays[displayName_]=options;}app_.apply(std::move(config));invalidate();
}
void Editor::snapLayouts(Canvas& c,float y){
 const auto& config=app_.config();auto options=displayName_.empty()?config.tilingDefaults:config.tilingForDisplay(displayName_);const float width=c.size().width-244;
 c.text(L"WINDOW TILING",box(222,y,width,24),theme_.muted,10,true);
 toggle(c,y+30,L"Automatic tiling",L"Opt in for this profile. Minimized and manually moved windows stay put.",options.enabled,[this]{tilingChange([](TilingOptions& options){options.enabled=!options.enabled;});});
 std::wstring label=options.layoutId=="balanced"?L"Balanced grid":options.layoutId=="master"?L"Master and stack":L"Custom layout";for(const auto& layout:config.snapLayouts)if(layout.id==options.layoutId)label=win::wide(layout.name);
 button(c,box(220,y+110,width-152,36),label+L"  v",[this]{
  HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,1,L"Balanced grid");AppendMenuW(menu,MF_STRING,2,L"Master and stack");auto layouts=app_.config().snapLayouts;for(std::size_t i=0;i<layouts.size();++i)AppendMenuW(menu,MF_STRING,10+i,win::wide(layouts[i].name).c_str());
  POINT point{};if(focusedAction_>=0&&static_cast<std::size_t>(focusedAction_)<actions_.size()){auto r=actions_[static_cast<std::size_t>(focusedAction_)].first;auto n=accessibleNode(hwnd_,1,L"",0,0,r,layoutScale());point={n.bounds.left,n.bounds.bottom};}else GetCursorPos(&point);
  app_.beginInteraction();auto selected=TrackPopupMenuEx(menu,TPM_RETURNCMD|TPM_NONOTIFY,point.x,point.y,hwnd_,nullptr);DestroyMenu(menu);app_.endInteraction();std::string id=selected==1?"balanced":selected==2?"master":selected>=10&&selected-10<layouts.size()?layouts[selected-10].id:"";if(!id.empty())tilingChange([&](TilingOptions& o){o.layoutId=id;});
 },false,L"Tiling layout: "+label);
 button(c,box(c.size().width-164,y+110,140,36),L"Tile now",[this]{app_.tileNow(win::wide(displayName_));},false,L"Tile now on "+(displayName_.empty()?std::wstring(L"the current display"):win::wide(displayName_)));
 if(!displayName_.empty()&&config.tilingDisplays.contains(displayName_))button(c,box(220,y+158,230,34),L"Inherit default tiling",[this]{auto config=app_.config();config.tilingDisplays.erase(displayName_);app_.apply(std::move(config));invalidate();});
 c.text(L"Ctrl+Alt+Shift+arrows: move monitor  |  Ctrl+Alt+Shift+1-9: snap zone",box(221,y+204,width,24),theme_.muted,10);
 c.text(app_.tilingShortcutNotice(),box(221,y+230,width,24),theme_.muted,10);
 c.text(L"NAMED SNAP ZONES",box(222,y+269,width,24),theme_.muted,10,true);
 button(c,box(220,y+302,200,36),L"Add snap layout",[this]{editSnapLayout();});
 c.text(L"Up to 16 layouts, 12 zones each",box(433,y+302,width-213,36),theme_.muted,10);
 c.text(tilingNotice_.empty()?L"Define zones as percentages of the display work area.":win::wide(tilingNotice_),box(221,y+349,width,24),theme_.muted,10);
 for(std::size_t i=0;i<config.snapLayouts.size();++i){const auto& layout=config.snapLayouts[i];float row=y+385+static_cast<float>(i)*62;
  button(c,box(220,row,width-196,52),win::wide(layout.name)+L" ("+std::to_wstring(layout.zones.size())+L" zones)",[this,id=layout.id]{tilingChange([&](TilingOptions& o){o.layoutId=id;});},options.layoutId==layout.id,L"Select snap layout "+win::wide(layout.name),ROLE_SYSTEM_LISTITEM);
  button(c,box(c.size().width-210,row+9,88,34),L"Edit",[this,id=layout.id]{editSnapLayout(id);},false,L"Edit snap layout "+win::wide(layout.name));
  button(c,box(c.size().width-112,row+9,88,34),L"Remove",[this,id=layout.id]{removeSnapLayout(id);},false,L"Remove snap layout "+win::wide(layout.name));
 }
}
namespace {
LRESULT CALLBACK zoneTextProc(HWND h,UINT message,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR){
 const auto result=DefSubclassProc(h,message,w,l);
 // Standard multiline EDIT omits EN_CHANGE for WM_SETTEXT, including accessibility setters.
 if(message==WM_SETTEXT&&IsWindow(h))SendMessageW(GetParent(h),WM_COMMAND,MAKEWPARAM(GetDlgCtrlID(h),EN_CHANGE),reinterpret_cast<LPARAM>(h));
 if(message==WM_NCDESTROY)RemoveWindowSubclass(h,zoneTextProc,id);return result;
}
struct ZoneDialog {SnapLayout initial;std::optional<SnapLayout> result;HFONT font{};};
std::optional<SnapLayout> readZoneDialog(HWND dialog,const SnapLayout& initial){
 auto layout=initial;layout.name=win::utf8(win::windowText(GetDlgItem(dialog,201)));std::wstring gap=win::windowText(GetDlgItem(dialog,203));std::wistringstream gapInput(gap);int value=-1;wchar_t extra{};if(!(gapInput>>value)||(gapInput>>extra)||value<0||value>32)return {};layout.gapDips=value;layout.zones.clear();
 std::wistringstream lines(win::windowText(GetDlgItem(dialog,202)));std::wstring line;while(std::getline(lines,line)){if(line.find_first_not_of(L" \t\r")==std::wstring::npos)continue;std::replace(line.begin(),line.end(),L',',L' ');std::wistringstream input(line);double left{},top{},right{},bottom{};if(!(input>>left>>top>>right>>bottom)||(input>>extra))return {};if(!std::isfinite(left)||!std::isfinite(top)||!std::isfinite(right)||!std::isfinite(bottom)||left<0||top<0||right>100||bottom>100||left>=right||top>=bottom)return {};layout.zones.push_back({static_cast<int>(std::lround(left*100)),static_cast<int>(std::lround(top*100)),static_cast<int>(std::lround(right*100)),static_cast<int>(std::lround(bottom*100))});if(layout.zones.size()>12)return {};}
 if(!validSnapLayout(layout))return {};return layout;
}
INT_PTR CALLBACK zoneDialogProc(HWND dialog,UINT message,WPARAM w,LPARAM l){
 auto* data=reinterpret_cast<ZoneDialog*>(GetWindowLongPtrW(dialog,DWLP_USER));
 auto validate=[&]{auto result=readZoneDialog(dialog,data->initial);EnableWindow(GetDlgItem(dialog,IDOK),result.has_value());SetDlgItemTextW(dialog,204,result?L"":L"Enter a name, gap 0-32, and 1-12 non-overlapping zones within 0-100%.");return result;};
 switch(message){
 case WM_INITDIALOG:{data=reinterpret_cast<ZoneDialog*>(l);SetWindowLongPtrW(dialog,DWLP_USER,l);SetWindowTextW(dialog,L"Snap layout editor");int dpi=win::dpiFor(dialog);auto px=[&](int value){return MulDiv(value,dpi,96);};RECT owner{};GetWindowRect(GetWindow(dialog,GW_OWNER),&owner);MONITORINFO info{sizeof(info)};GetMonitorInfoW(MonitorFromWindow(dialog,MONITOR_DEFAULTTONEAREST),&info);int width=px(570),height=px(445);int x=std::clamp((owner.left+owner.right-width)/2,info.rcWork.left,std::max(info.rcWork.left,info.rcWork.right-width)),y=std::clamp((owner.top+owner.bottom-height)/2,info.rcWork.top,std::max(info.rcWork.top,info.rcWork.bottom-height));SetWindowPos(dialog,nullptr,x,y,width,height,SWP_NOZORDER);
 data->font=CreateFontW(-px(14),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,L"Segoe UI");
 auto control=[&](const wchar_t* klass,const wchar_t* title,DWORD style,int id,int left,int top,int width,int height){auto h=CreateWindowExW(std::wcscmp(klass,L"EDIT")==0?WS_EX_CLIENTEDGE:0,klass,title,WS_CHILD|WS_VISIBLE|style,px(left),px(top),px(width),px(height),dialog,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);if(h)SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(data->font?data->font:GetStockObject(DEFAULT_GUI_FONT)),TRUE);return h;};
 control(L"STATIC",L"Layout name",0,0,16,12,400,22);auto name=control(L"EDIT",win::wide(data->initial.name).c_str(),WS_TABSTOP|ES_AUTOHSCROLL,201,16,38,522,28);SendMessageW(name,EM_SETLIMITTEXT,80,0);accessibleNativeName(name,L"Snap layout name");
 control(L"STATIC",L"Zones: left, top, right, bottom percentages; one zone per line",0,0,16,78,522,22);std::wostringstream text;text<<std::fixed<<std::setprecision(2);for(const auto& r:data->initial.zones)text<<r.left/100.0<<L", "<<r.top/100.0<<L", "<<r.right/100.0<<L", "<<r.bottom/100.0<<L"\r\n";
 auto zones=control(L"EDIT",text.str().c_str(),WS_TABSTOP|WS_VSCROLL|ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN,202,16,105,522,155);SetWindowSubclass(zones,zoneTextProc,1,0);SendMessageW(zones,EM_SETLIMITTEXT,2048,0);accessibleNativeName(zones,L"Zone percentages, left top right bottom, one zone per line");
 control(L"STATIC",L"Gap in display-independent pixels (0-32)",0,0,16,272,400,24);auto gap=control(L"EDIT",std::to_wstring(data->initial.gapDips).c_str(),WS_TABSTOP|ES_NUMBER,203,430,270,108,28);SendMessageW(gap,EM_SETLIMITTEXT,2,0);accessibleNativeName(gap,L"Gap in display-independent pixels");
 control(L"STATIC",L"",0,204,16,305,522,44);control(L"BUTTON",L"Save",WS_TABSTOP|BS_DEFPUSHBUTTON,IDOK,354,360,88,30);control(L"BUTTON",L"Cancel",WS_TABSTOP|BS_PUSHBUTTON,IDCANCEL,450,360,88,30);SendMessageW(dialog,DM_SETDEFID,IDOK,0);validate();SetFocus(name);return FALSE;}
 case WM_COMMAND:if((LOWORD(w)==201||LOWORD(w)==202||LOWORD(w)==203)&&HIWORD(w)==EN_CHANGE){validate();return TRUE;}if(LOWORD(w)==IDOK&&data){auto result=validate();if(result){data->result=std::move(result);EndDialog(dialog,IDOK);}return TRUE;}if(LOWORD(w)==IDCANCEL){EndDialog(dialog,IDCANCEL);return TRUE;}break;
 case WM_CLOSE:EndDialog(dialog,IDCANCEL);return TRUE;case WM_DESTROY:if(data&&data->font){DeleteObject(data->font);data->font=nullptr;}return TRUE;default:break;
 }return FALSE;
}
}
void Editor::editSnapLayout(const std::string& id){
 auto config=app_.config();SnapLayout initial;auto existing=std::find_if(config.snapLayouts.begin(),config.snapLayouts.end(),[&](const auto& item){return item.id==id;});if(!id.empty()){if(existing==config.snapLayouts.end())return;initial=*existing;}else{
  if(config.snapLayouts.size()>=16){tilingNotice_="The 16-layout limit has been reached.";invalidate();return;}for(int i=1;i<=100;++i){auto candidate="custom-"+std::to_string(i);if(std::none_of(config.snapLayouts.begin(),config.snapLayouts.end(),[&](const auto& item){return item.id==candidate;})){initial.id=candidate;break;}}initial.name="Two columns";initial.zones={{0,0,5000,10000},{5000,0,10000,10000}};initial.gapDips=8;
 }
 struct Template{DLGTEMPLATE dialog;WORD menu=0,windowClass=0,title=0;} definition{};definition.dialog.style=WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME;definition.dialog.cx=356;definition.dialog.cy=278;ZoneDialog data{initial,{},nullptr};app_.beginInteraction();auto result=DialogBoxIndirectParamW(GetModuleHandleW(nullptr),&definition.dialog,hwnd_,zoneDialogProc,reinterpret_cast<LPARAM>(&data));app_.endInteraction();if(result==-1){tilingNotice_="Windows could not open the snap layout editor.";invalidate();return;}if(!data.result)return;
 config=app_.config();existing=std::find_if(config.snapLayouts.begin(),config.snapLayouts.end(),[&](const auto& item){return item.id==initial.id;});if(existing!=config.snapLayouts.end())*existing=*data.result;else if(config.snapLayouts.size()<16)config.snapLayouts.push_back(*data.result);else return;app_.apply(std::move(config));tilingNotice_="Saved snap layout: "+data.result->name;focusedAction_=-1;invalidate();
}
void Editor::removeSnapLayout(const std::string& id){
 auto config=app_.config();std::erase_if(config.snapLayouts,[&](const auto& item){return item.id==id;});if(config.tilingDefaults.layoutId==id)config.tilingDefaults.layoutId="balanced";for(auto& [display,options]:config.tilingDisplays)if(options.layoutId==id)options.layoutId="balanced";app_.apply(std::move(config));tilingNotice_="Removed snap layout. Profiles using it now use Balanced grid.";focusedAction_=-1;invalidate();
}
}
