#include "extensions.hpp"
#include "extension_widget.hpp"
#include "app.hpp"
#include "platform/dialogs.hpp"
namespace aether::ui {
ExtensionRuntime::ExtensionRuntime(Graphics& graphics,App& app):graphics_(graphics),app_(app),data_(app.controller(),app.pluginProviders()){}
ExtensionRuntime::~ExtensionRuntime(){shutdown();}
bool ExtensionRuntime::start(std::string* error){
 if(started_){if(error)error->clear();return true;}if(!library_.load(error))return false;started_=true;
 try{synchronize();update();if(error)error->clear();return true;}catch(const std::exception& e){shutdown();if(error)*error=e.what();return false;}
}
void ExtensionRuntime::synchronize(){
 if(!started_)return;
 struct Wanted {const InstalledExtension* entry;const ExtensionWidget* widget;};std::vector<Wanted> wanted;
 for(const auto& entry:library_.entries())if(entry.enabled)for(const auto& widget:entry.manifest.widgets){if(wanted.size()>=ActiveWidgetLimit)break;wanted.push_back({&entry,&widget});}
 // Create all missing resources before moving existing ownership. A failed creation preserves old widgets.
 std::vector<std::unique_ptr<ExtensionWidgetSurface>> additions;
 for(const auto& item:wanted){bool present=std::any_of(widgets_.begin(),widgets_.end(),[&](const auto& w){return w&&w->matches(item.entry->storageId,item.widget->id);});if(!present)additions.push_back(std::make_unique<ExtensionWidgetSurface>(graphics_,app_,*this,*item.entry,*item.widget));}
 std::vector<std::unique_ptr<ExtensionWidgetSurface>> next;next.reserve(wanted.size());
 for(const auto& item:wanted){auto old=std::find_if(widgets_.begin(),widgets_.end(),[&](const auto& w){return w&&w->matches(item.entry->storageId,item.widget->id);});if(old!=widgets_.end())next.push_back(std::move(*old));else {auto added=std::find_if(additions.begin(),additions.end(),[&](const auto& w){return w&&w->matches(item.entry->storageId,item.widget->id);});next.push_back(std::move(*added));}}
 widgets_=std::move(next);
}
void ExtensionRuntime::update(){
 if(!started_)return;for(auto& widget:widgets_){for(const auto& entry:library_.entries())if(entry.enabled){auto descriptor=std::find_if(entry.manifest.widgets.begin(),entry.manifest.widgets.end(),[&](const auto& w){return widget->matches(entry.storageId,w.id);});if(descriptor!=entry.manifest.widgets.end()){widget->refresh(*descriptor,paused_);break;}}}
 bool media=false,weather=false;for(const auto& widget:widgets_)if(widget->visible()){media|=widget->kind()==WidgetKind::Media;weather|=widget->kind()==WidgetKind::Weather;}data_.configure(media,weather);data_.tick();
}
bool ExtensionRuntime::resourcesActive() const{if(!started_||paused_)return false;return std::any_of(widgets_.begin(),widgets_.end(),[](const auto& widget){return widget->visible()&&widget->kind()==WidgetKind::Resources;});}
void ExtensionRuntime::pause(bool value){paused_=value;if(!started_)return;if(value){data_.stop();for(auto& widget:widgets_){widget->cancelDrag();widget->hide();}}else update();}
void ExtensionRuntime::shutdown(){started_=false;data_.stop();widgets_.clear();}
bool ExtensionRuntime::setEnabled(std::string_view id,bool enabled,std::string* error){
 if(!started_){if(error)*error="The native extension runtime is stopped.";return false;}if(app_.interactionActive()){if(error)*error="Finish the current dialog before changing widgets.";return false;}if(!library_.setEnabled(id,enabled,error))return false;
 try{synchronize();update();return true;}catch(const std::exception& e){std::string rollback;if(enabled)library_.setEnabled(id,false,&rollback);synchronize();update();if(error)*error=std::string("Native widget creation failed: ")+e.what()+(rollback.empty()?"":". "+rollback);return false;}
}
bool ExtensionRuntime::remove(std::string_view id,std::string* error){if(app_.interactionActive()){if(error)*error="Finish the current dialog before removing widgets.";return false;}if(!library_.remove(id,error))return false;synchronize();update();return true;}
bool ExtensionRuntime::move(std::string_view id,int direction,std::string* error){bool ok=library_.move(id,direction,error);synchronize();update();return ok;}
bool ExtensionRuntime::setSize(std::string_view id,std::string_view widget,int width,int height,std::string* error){if(!library_.setSize(id,widget,width,height,error))return false;update();return true;}
bool ExtensionRuntime::setPlacement(std::string_view id,std::string_view widget,const WidgetPlacement& placement,std::string* error){if(!library_.setPlacement(id,widget,placement,error))return false;update();return true;}
bool ExtensionRuntime::addExamples(std::string* error){bool ok=library_.addExamples(error);synchronize();update();return ok;}
std::optional<std::string> ExtensionRuntime::importFile(const std::filesystem::path& path,std::string* error){auto id=library_.importFile(path,error);if(id){synchronize();update();}return id;}
bool ExtensionRuntime::exportFile(std::string_view id,const std::filesystem::path& path,std::string* error) const{return library_.exportFile(id,path,error);}
std::optional<std::string> ExtensionRuntime::install(const ExtensionManifest& m,std::string* error){for(const auto& entry:library_.entries())if(entry.manifest.id==m.id)return entry.storageId;auto id=library_.install(m,error);if(id){synchronize();update();}return id;}
bool ExtensionRuntime::setNote(std::string_view id,std::string_view widget,std::string_view text,std::string* error){if(!started_){if(error)*error="Extensions are stopped.";return false;}if(!library_.setNote(id,widget,text,error))return false;update();return true;}
Extensions::Extensions(Graphics& graphics,App& app):Surface(graphics),app_(app){
 if(!create(L"Aether Extensions",WS_POPUP|WS_THICKFRAME,WS_EX_TOOLWINDOW,{0,0,760,680}))throw std::runtime_error("Could not create the native Extensions window.");accessibility_=std::make_unique<Accessibility>(hwnd_);setTheme(app_.theme());
}
Extensions::~Extensions(){if(accessibility_)accessibility_->detach();destroy();}
void Extensions::open(const win::Monitor& monitor){setTheme(app_.theme());scroll_=0;focused_=-1;notice_=win::wide(app_.extensions().library().notice());move(popupBounds(monitor.work,760,680,monitor.dpi),false);win::roundedWindow(hwnd_,theme_.radius);show(true);UpdateWindow(hwnd_);focus(0);}
void Extensions::update(){if(theme_!=app_.theme())setTheme(app_.theme());invalidate();if(visible())UpdateWindow(hwnd_);if(focused_>=static_cast<int>(actions_.size()))focused_=static_cast<int>(actions_.size())-1;}
D2D1_SIZE_F Extensions::logicalSize() const{RECT rect{};GetClientRect(hwnd_,&rect);float factor=scale()*drawingScale_;return {rect.right/factor,rect.bottom/factor};}
float Extensions::contentHeight() const{float height=0;for(const auto& entry:app_.extensions().library().entries())height+=92.f+32.f*static_cast<float>(entry.manifest.widgets.size());return height;}
void Extensions::result(bool ok,const std::string& error){notice_=ok?L"Saved in your local profile.":win::wide(error.empty()?"Could not complete this local extension action.":error);update();}
void Extensions::button(Canvas& canvas,D2D1_RECT_F bounds,const std::wstring& name,std::function<void()> invokeAction,bool enabled,bool scrolling){
 canvas.fill(bounds,enabled?theme_.raised:theme_.surface,7);canvas.text(name,bounds,enabled?theme_.foreground:theme_.muted,11,true,1);
 int index=static_cast<int>(actions_.size());auto node=accessibleNode(hwnd_,index+1,name,ROLE_SYSTEM_PUSHBUTTON,STATE_SYSTEM_FOCUSABLE|(enabled?0:STATE_SYSTEM_UNAVAILABLE),bounds,drawingScale_);node.invoke=enabled?std::function<void()>([this,action=std::move(invokeAction)]{if(visible()&&hwnd_&&IsWindow(hwnd_))action();}):std::function<void()>{};node.focus=[this,index]{focus(index);};node.description=scrolling?L"Local extension library action.":L"Native extension library tool.";
 if(scrolling){auto size=canvas.size();if(bounds.top<116||bounds.bottom>size.height-42)node.state|=STATE_SYSTEM_OFFSCREEN;}actions_.push_back(std::move(node));rects_.push_back(bounds);scrolling_.push_back(scrolling);
 if(index==focused_&&GetFocus()==hwnd_&&enabled&&!(actions_.back().state&STATE_SYSTEM_OFFSCREEN))canvas.stroke(bounds,theme_.accent,7,2);
}
void Extensions::paint(Canvas& canvas){
 float actual=canvas.size().width;drawingScale_=std::min(1.f,actual/740.f);canvas.setScale(drawingScale_);auto size=canvas.size();float width=size.width,height=size.height;
 scroll_=std::clamp(scroll_,0.f,std::max(0.f,contentHeight()-(height-158.f)));actions_.clear();rects_.clear();scrolling_.clear();
 canvas.fill(box(0,0,width,height),theme_.background,static_cast<float>(theme_.radius));canvas.stroke(box(.5f,.5f,width-1,height-1),theme_.raised,static_cast<float>(theme_.radius));
 canvas.text(L"Extensions",box(24,15,220,36),theme_.foreground,24,true);canvas.text(L"Native local widgets · imports stay disabled",box(24,53,width-48,25),theme_.muted,12);
 button(canvas,box(width-333,20,97,31),L"Import",[this]{importManifest();},true,false);
 button(canvas,box(width-227,20,126,31),L"Add examples",[this]{std::string error;result(app_.extensions().addExamples(&error),error);},true,false);
 button(canvas,box(width-92,20,68,31),L"Close",[this]{hide();},true,false);
 const auto& entries=app_.extensions().library().entries();button(canvas,box(width-270,54,246,27),app_.config().widgetsLocked?L"Layout locked · Edit widgets":L"Editing · Lock widget layout",[this]{auto config=app_.config();config.widgetsLocked=!config.widgetsLocked;app_.apply(std::move(config));app_.extensions().update();update();},true,false);auto message=notice_.empty()?std::to_wstring(entries.size())+(app_.config().widgetsLocked?L" / 24 packages · Widget layout locked":L" / 24 packages · Drag headers to grid; arrows move"):notice_;
 canvas.text(message,box(24,82,width-48,26),theme_.muted,11);auto notice=accessibleNode(hwnd_,static_cast<long>(actions_.size()+1),L"Extension status: "+message,ROLE_SYSTEM_STATICTEXT,0,box(24,82,width-48,26),drawingScale_);notice.value=message;actions_.push_back(std::move(notice));rects_.push_back(box(24,82,width-48,26));scrolling_.push_back(false);
 canvas.pushClip(box(16,116,width-32,std::max(1.f,height-158)));float y=124-scroll_;
 if(entries.empty())canvas.text(L"Import a .aetherplugin file or add the native examples to begin.",box(28,y,width-56,46),theme_.foreground,14);
 for(std::size_t i=0;i<entries.size();++i){const auto& entry=entries[i];float rowHeight=92+32.f*static_cast<float>(entry.manifest.widgets.size());auto id=entry.storageId;auto packageName=win::wide(entry.manifest.name);bool enabled=entry.enabled;
  canvas.surface(box(20,y,width-40,rowHeight-10),theme_,12);
  canvas.text(packageName,box(34,y+8,width-450,27),theme_.foreground,15,true);
  auto summary=(enabled?L"Enabled":L"Disabled")+std::wstring(L" · ")+std::to_wstring(entry.manifest.widgets.size())+L" native widgets  ·  "+win::wide(entry.manifest.id);
  canvas.text(summary,box(34,y+37,width-68,22),theme_.muted,10);canvas.text(win::wide(entry.manifest.description),box(34,y+59,width-68,20),theme_.muted,10);
  auto nameNode=accessibleNode(hwnd_,static_cast<long>(actions_.size()+1),packageName+L": "+summary,ROLE_SYSTEM_STATICTEXT,0,box(34,y+7,width-450,53),drawingScale_);nameNode.description=win::wide(entry.manifest.description);if(y<116||y+60>height-42)nameNode.state|=STATE_SYSTEM_OFFSCREEN;actions_.push_back(std::move(nameNode));rects_.push_back(box(34,y+7,width-450,53));scrolling_.push_back(true);
  float x=width-400;
  button(canvas,box(x,y+10,74,29),enabled?L"Disable":L"Enable",[this,id,enabled]{std::string error;result(app_.extensions().setEnabled(id,!enabled,&error),error);});actions_.back().name=(enabled?L"Disable ":L"Enable ")+packageName;x+=82;
  button(canvas,box(x,y+10,42,29),L"Up",[this,id]{std::string error;result(app_.extensions().move(id,-1,&error),error);},i>0);actions_.back().name=L"Move up "+packageName;x+=50;
  button(canvas,box(x,y+10,51,29),L"Down",[this,id]{std::string error;result(app_.extensions().move(id,1,&error),error);},i+1<entries.size());actions_.back().name=L"Move down "+packageName;x+=59;
  button(canvas,box(x,y+10,62,29),L"Export",[this,id]{exportManifest(id);});actions_.back().name=L"Export "+packageName;x+=70;
  button(canvas,box(x,y+10,72,29),L"Remove",[this,id]{std::string error;result(app_.extensions().remove(id,&error),error);});actions_.back().name=L"Remove "+packageName;
  for(std::size_t n=0;n<entry.manifest.widgets.size();++n){const auto& widget=entry.manifest.widgets[n];float wy=y+83+32.f*static_cast<float>(n);canvas.text(win::wide(widget.label),box(38,wy,width-240,27),theme_.foreground,11);
   auto currentMonitor=widget.placement.monitor;auto widgetId=widget.id;button(canvas,box(width-196,wy,160,26),currentMonitor.empty()?L"Primary display":win::wide(currentMonitor),[this,id,widgetId,currentMonitor]{const auto& displays=app_.displays();if(displays.empty()){result(false,"No connected display is available.");return;}std::size_t next=0;for(std::size_t d=0;d<displays.size();++d)if(win::utf8(displays[d].name)==currentMonitor||(currentMonitor.empty()&&displays[d].primary)){next=(d+1)%displays.size();break;}const auto& all=app_.extensions().library().entries();auto package=std::find_if(all.begin(),all.end(),[&](const auto& item){return item.storageId==id;});if(package==all.end())return;auto selected=std::find_if(package->manifest.widgets.begin(),package->manifest.widgets.end(),[&](const auto& item){return item.id==widgetId;});if(selected==package->manifest.widgets.end())return;auto placement=selected->placement;placement.monitor=win::utf8(displays[next].name);std::string error;result(app_.extensions().setPlacement(id,widgetId,placement,&error),error);});actions_.back().name=L"Change display for "+win::wide(widget.label)+L" in "+packageName;
  }
  y+=rowHeight;
 }
 canvas.popClip();canvas.text(L"Tab: focus  ·  Enter: invoke  ·  Page Up/Down: scroll  ·  Esc: close",box(24,height-33,width-48,24),theme_.muted,10,false,1);snapshot();
}
void Extensions::snapshot(){if(!accessibility_)return;accessibility_->snapshot(L"Extensions",ROLE_SYSTEM_DIALOG,actions_,GetFocus()==hwnd_&&focused_>=0?focused_+1:0);}
void Extensions::focus(int index){
 if(!visible()||index<0||index>=static_cast<int>(actions_.size())||(actions_[static_cast<std::size_t>(index)].state&STATE_SYSTEM_UNAVAILABLE)||!(actions_[static_cast<std::size_t>(index)].state&STATE_SYSTEM_FOCUSABLE))return;
 focused_=index;auto bounds=rects_[static_cast<std::size_t>(index)];auto size=logicalSize();if(scrolling_[static_cast<std::size_t>(index)]){if(bounds.top<120)scroll_=std::max(0.f,scroll_-(120-bounds.top));else if(bounds.bottom>size.height-46)scroll_+=bounds.bottom-(size.height-46);scroll_=std::clamp(scroll_,0.f,std::max(0.f,contentHeight()-(size.height-158)));}SetFocus(hwnd_);invalidate();UpdateWindow(hwnd_);snapshot();
}
void Extensions::invoke(int index){if(!visible()||index<0||index>=static_cast<int>(actions_.size()))return;const auto& node=actions_[static_cast<std::size_t>(index)];if(node.state&(STATE_SYSTEM_UNAVAILABLE|STATE_SYSTEM_OFFSCREEN))return;auto action=node.invoke;if(action)action();}
void Extensions::click(float x,float y){x/=drawingScale_;y/=drawingScale_;for(std::size_t i=0;i<rects_.size();++i)if(contains(rects_[i],x,y)&&!(actions_[i].state&STATE_SYSTEM_OFFSCREEN)){focus(static_cast<int>(i));invoke(static_cast<int>(i));return;}}
void Extensions::importManifest(){
 const COMDLG_FILTERSPEC filter[]={{L"Aether native extension",L"*.aetherplugin"}};app_.beginInteraction();auto path=win::chooseFile(hwnd_,false,L"Import a local native extension",filter,1);if(path){std::string error;auto installed=app_.extensions().importFile(*path,&error);notice_=installed?L"Imported locally and kept disabled. Enable it when ready.":win::wide(error);update();}app_.endInteraction();
}
void Extensions::exportManifest(const std::string& id){
 const auto& entries=app_.extensions().library().entries();auto entry=std::find_if(entries.begin(),entries.end(),[&](const auto& e){return e.storageId==id;});if(entry==entries.end())return;auto name=win::wide(entry->manifest.id)+L".aetherplugin";const COMDLG_FILTERSPEC filter[]={{L"Aether native extension",L"*.aetherplugin"}};app_.beginInteraction();auto path=win::chooseFile(hwnd_,true,L"Export local extension manifest",filter,1,name.c_str(),L"aetherplugin");if(path){std::string error;result(app_.extensions().exportFile(id,*path,&error),error);}app_.endInteraction();
}
LRESULT Extensions::event(UINT message,WPARAM w,LPARAM l,bool& handled){
 handled=true;
 switch(message){
 case WM_CLOSE:hide();return 0;
 case WM_SETFOCUS:case WM_KILLFOCUS:invalidate();return 0;
 case WM_GETOBJECT:UpdateWindow(hwnd_);snapshot();if(accessibility_)return accessibility_->object(w,l,handled);handled=false;return 0;
 case WM_MOVE:if(accessibility_){invalidate();UpdateWindow(hwnd_);snapshot();}return 0;
 case WM_SIZE:handled=false;return 0;
 case WM_MOUSEWHEEL:scroll_=std::clamp(scroll_-GET_WHEEL_DELTA_WPARAM(w)/WHEEL_DELTA*64.f,0.f,std::max(0.f,contentHeight()-(logicalSize().height-158)));invalidate();UpdateWindow(hwnd_);return 0;
 case WM_KEYDOWN:
  if(w==VK_ESCAPE){hide();return 0;}
  if(w==VK_TAB){if(actions_.empty())return 0;int direction=(GetKeyState(VK_SHIFT)&0x8000)?-1:1,index=focused_;for(std::size_t n=0;n<actions_.size();++n){index=(index+direction+static_cast<int>(actions_.size()))%static_cast<int>(actions_.size());const auto& node=actions_[static_cast<std::size_t>(index)];if((node.state&STATE_SYSTEM_FOCUSABLE)&&!(node.state&STATE_SYSTEM_UNAVAILABLE)){focus(index);break;}}return 0;}
  if(w==VK_RETURN||w==VK_SPACE){invoke(focused_);return 0;}
  if(w==VK_NEXT||w==VK_PRIOR){scroll_=std::clamp(scroll_+(w==VK_NEXT?1.f:-1.f)*std::max(64.f,logicalSize().height-190),0.f,std::max(0.f,contentHeight()-(logicalSize().height-158)));invalidate();UpdateWindow(hwnd_);return 0;}break;
 default:break;
 }
 handled=false;return 0;
}
}
