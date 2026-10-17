#include "extension_widget.hpp"
#include "app.hpp"
#include "platform/dialogs.hpp"
namespace aether::ui {
namespace {
const Pin* resolvedPin(const App& app,std::string_view key){for(const auto& pin:app.config().pins)if((!pin.identity.empty()&&pin.identity==key)||pin.target==key)return &pin;return nullptr;}
}
ExtensionWidgetSurface::ExtensionWidgetSurface(Graphics& graphics,App& app,ExtensionRuntime& runtime,const InstalledExtension& entry,const ExtensionWidget& widget):Surface(graphics,FontPart::Widgets),app_(app),runtime_(runtime),storageId_(entry.storageId),packageName_(entry.manifest.name),widget_(widget){
 auto title=L"Aether extension: "+win::wide(packageName_)+L" / "+win::wide(widget.label);
 if(!create(title.c_str(),WS_POPUP,WS_EX_TOOLWINDOW,{0,0,widget.width,widget.height}))throw std::runtime_error("Could not create a native extension widget.");
 accessibility_=std::make_unique<Accessibility>(hwnd_);snapshot();
}
ExtensionWidgetSurface::~ExtensionWidgetSurface(){cancelDrag();if(accessibility_)accessibility_->detach();destroy();}
void ExtensionWidgetSurface::cancelDrag(){
 if(!dragging_)return;dragging_=false;if(GetCapture()==hwnd_)ReleaseCapture();move(dragBounds_,false);app_.endInteraction();
}
void ExtensionWidgetSurface::refresh(const ExtensionWidget& widget,bool paused){
 if(app_.config().widgetsLocked||widget.placement.monitor!=widget_.placement.monitor)cancelDrag();bool changed=widget!=widget_;widget_=widget;
 auto theme=app_.theme();if(widget_.accent)theme.accent=*widget_.accent;
 if(widget_.style==WidgetStyle::Minimal){theme.style=ThemeStyle::Minimal;theme.gradientStrength=0;theme.radius=8;}
 if(theme!=theme_){setTheme(theme);changed=true;}
 std::wstring value;actionAvailable_=true;
 switch(widget_.kind){
 case WidgetKind::Clock:value=app_.status().clock.empty()?L"Waiting for status":app_.status().clock;break;
 case WidgetKind::FocusedApp:for(const auto& window:app_.windows())if(window.active){value=window.title;break;}if(value.empty())value=L"No active app";break;
 case WidgetKind::QuickAction:actionAvailable_=widget_.action!=WidgetAction::Pin||resolvedPin(app_,widget_.pin);value=actionAvailable_?win::wide(widget_.label):L"Configured pin unavailable";break;
 case WidgetKind::Media:{auto snapshot=runtime_.data().snapshot();value=snapshot.media;actionAvailable_=snapshot.mediaAvailable;break;}
 case WidgetKind::Battery:value=app_.status().battery;break;
 case WidgetKind::Resources:value=app_.status().resources.empty()?L"Measuring system CPU\nAether memory on next refresh":app_.status().resources;break;
 case WidgetKind::Note:value=widget_.text.empty()?L"Click to add a note":win::wide(widget_.text);break;
 case WidgetKind::Weather:value=runtime_.data().snapshot().weather;break;
 }
 if(value!=value_){value_=std::move(value);changed=true;}
 monitorValid_=false;const auto& displays=app_.displays();
 if(!widget_.placement.monitor.empty()){auto requested=win::wide(widget_.placement.monitor);for(const auto& display:displays)if(display.name==requested){monitor_=display;monitorValid_=true;break;}}
 else if(!displays.empty()){auto primary=std::find_if(displays.begin(),displays.end(),[](const auto& d){return d.primary;});monitor_=primary==displays.end()?displays.front():*primary;monitorValid_=true;}
 if(monitorValid_&&(monitor_.work.width()<=0||monitor_.work.height()<=0))monitorValid_=false;
 bool fullscreen=false;if(monitorValid_)for(const auto& window:app_.windows())if(window.active&&window.fullscreen&&window.display==monitor_.name){fullscreen=true;break;}
 bool showWidget=monitorValid_&&extensionVisible(true,paused,app_.config().hideFullscreen,fullscreen);
 if(!showWidget){cancelDrag();hide();snapshot();return;}
 if(!dragging_){auto bounds=extensionBounds(widget_,monitor_.work,monitor_.dpi);RECT current{};GetWindowRect(hwnd_,&current);if(win::rect(current)!=bounds)move(bounds,false);}
 roundedCorners(theme_.radius);SetPropW(hwnd_,L"Aether.Widget.Locked",reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(app_.config().widgetsLocked)));
 if(!visible()){show(false);changed=true;}
 if(changed)invalidate();snapshot();
}
void ExtensionWidgetSurface::paint(Canvas& canvas){
 auto size=canvas.size();canvas.surface(box(0,0,size.width,size.height),theme_,static_cast<float>(theme_.radius));
 if(widget_.style==WidgetStyle::Accent)canvas.fill(box(0,0,size.width,3),theme_.accent);
 const bool controls=hovered_||GetFocus()==hwnd_;canvas.text(win::wide(widget_.label),box(14,4,size.width-58,24),theme_.muted,11,true);if(controls)canvas.glyph(L'\uE712',box(size.width-36,4,24,24),theme_.muted,13);
 auto body=box(14,34,size.width-28,std::max(24.f,size.height-58));
 if(widget_.kind==WidgetKind::Clock){auto split=value_.find(L"   ");if(split!=std::wstring::npos){canvas.text(value_.substr(split+3),box(14,33,size.width-28,38),theme_.foreground,25,true);canvas.text(value_.substr(0,split),box(14,72,size.width-28,24),theme_.muted,12);}else canvas.text(value_,body,theme_.foreground,16,true);}
 else if(widget_.kind==WidgetKind::FocusedApp)canvas.text(value_,body,theme_.foreground,17,true);
 else if(widget_.kind==WidgetKind::Media){canvas.text(value_,box(14,33,size.width-28,std::max(24.f,size.height-85)),actionAvailable_?theme_.foreground:theme_.muted,12,true);
  const float buttonWidth=(size.width-40)/3,top=size.height-43;const wchar_t* labels[]={L"Previous",runtime_.data().snapshot().playing?L"Pause":L"Play",L"Next"};for(int i=0;i<3;++i){auto rect=box(14+i*(buttonWidth+6),top,buttonWidth,30);canvas.fill(rect,theme_.raised,8);canvas.text(labels[i],rect,actionAvailable_?theme_.foreground:theme_.muted,11,true,1);}}
 else if(widget_.kind!=WidgetKind::QuickAction)canvas.text(value_,box(14,34,size.width-28,size.height-44),theme_.foreground,widget_.kind==WidgetKind::Battery?15:12,widget_.kind==WidgetKind::Battery);
 else {canvas.fill(body,theme_.raised,8);canvas.text(value_,body,actionAvailable_?theme_.foreground:theme_.muted,13,true,1);}
 if(!app_.config().widgetsLocked){canvas.stroke(box(1,1,size.width-2,size.height-2),theme_.accent,static_cast<float>(theme_.radius),1);canvas.line(size.width-13,size.height-3,size.width-3,size.height-13,theme_.accent);canvas.line(size.width-8,size.height-3,size.width-3,size.height-8,theme_.accent);}
 if(GetFocus()==hwnd_)canvas.stroke(box(2,2,size.width-4,size.height-4),theme_.accent,static_cast<float>(std::max(0,theme_.radius-2)),2);snapshot();
}
void ExtensionWidgetSurface::invoke(){
 if(!visible())return;
 if(widget_.kind==WidgetKind::Media){mediaCommand(L"toggle");return;}
 if(widget_.kind==WidgetKind::Note){auto* app=&app_;auto* runtime=&runtime_;const auto id=storageId_,widgetId=widget_.id,text=widget_.text;const auto owner=hwnd_;app->beginInteraction();auto edited=win::editNote(owner,text);app->endInteraction();if(edited&&IsWindow(owner)&&IsWindowVisible(owner)){std::string error;if(!runtime->setNote(id,widgetId,*edited,&error))win::message(owner,win::wide(error),MB_OK|MB_ICONERROR);}return;}
 if(widget_.kind==WidgetKind::Battery||widget_.kind==WidgetKind::Resources){app_.openQuick(monitor_.name);return;}
 // Data can select a known action or a current configured pin; it never supplies a launch target.
 if(widget_.kind!=WidgetKind::QuickAction){app_.openExtensions();return;}if(!actionAvailable_)return;
 switch(widget_.action){case WidgetAction::Activities:app_.openActivities(monitor_.name);break;case WidgetAction::Studio:app_.openEditor();break;case WidgetAction::QuickSettings:app_.openQuick(monitor_.name);break;case WidgetAction::Pin:if(const auto* pin=resolvedPin(app_,widget_.pin))app_.launchTarget(hwnd_,win::wide(pin->target));break;default:break;}
}
void ExtensionWidgetSurface::mediaCommand(std::wstring_view command){if(visible()&&widget_.kind==WidgetKind::Media&&runtime_.data().snapshot().mediaAvailable)runtime_.data().command(command);}
void ExtensionWidgetSurface::pointer(float,float){if(!hovered_){hovered_=true;invalidate();}}
void ExtensionWidgetSurface::leave(){if(hovered_){hovered_=false;invalidate();}}
void ExtensionWidgetSurface::click(float x,float y){if(!visible())return;RECT rect{};GetClientRect(hwnd_,&rect);float width=rect.right/scale(),height=rect.bottom/scale();if(y<30&&x>=width-42){app_.openExtensions();return;}if(widget_.kind==WidgetKind::Media&&y>=height-43){const float buttonWidth=(width-40)/3;for(int i=0;i<3;++i)if(contains(box(14+i*(buttonWidth+6),height-43,buttonWidth,30),x,y)){mediaCommand(i==0?L"previous":i==1?L"toggle":L"next");break;}return;}if(y>=30)invoke();}
void ExtensionWidgetSurface::saveSize(){RECT r{};GetClientRect(hwnd_,&r);std::string error;if(!runtime_.setSize(storageId_,widget_.id,MulDiv(r.right,96,win::dpiFor(hwnd_)),MulDiv(r.bottom,96,win::dpiFor(hwnd_)),&error)){win::log("Widget size was not saved: "+error);runtime_.update();}}
void ExtensionWidgetSurface::savePosition(){
 if(!monitorValid_)return;RECT current{};GetWindowRect(hwnd_,&current);WidgetPlacement placement{win::utf8(monitor_.name),std::clamp(MulDiv(current.left-monitor_.work.left,96,std::max(48,monitor_.dpi)),0,8192),std::clamp(MulDiv(current.top-monitor_.work.top,96,std::max(48,monitor_.dpi)),0,8192)};
 std::string error;if(!runtime_.setPlacement(storageId_,widget_.id,placement,&error)){win::log("Widget move was not saved: "+error);runtime_.update();}
}
void ExtensionWidgetSurface::keyboardMove(int dx,int dy){
 if(app_.config().widgetsLocked||!monitorValid_||!visible())return;RECT current{};GetWindowRect(hwnd_,&current);auto proposed=widget_;
 proposed.placement.x=std::clamp(MulDiv(current.left-monitor_.work.left,96,std::max(48,monitor_.dpi))+dx,0,8192);proposed.placement.y=std::clamp(MulDiv(current.top-monitor_.work.top,96,std::max(48,monitor_.dpi))+dy,0,8192);
 auto bounds=extensionBounds(proposed,monitor_.work,monitor_.dpi);move(bounds,false);savePosition();
}
void ExtensionWidgetSurface::snapshot(){
 if(!accessibility_||!hwnd_)return;RECT client{};GetClientRect(hwnd_,&client);float width=client.right/scale(),height=client.bottom/scale();std::vector<AccessibleNode> nodes;
 auto status=accessibleNode(hwnd_,1,win::wide(widget_.label)+L": "+value_,ROLE_SYSTEM_STATICTEXT,0,box(12,30,width-24,std::max(24.f,height-40)));status.value=value_;nodes.push_back(std::move(status));
 auto moveNode=accessibleNode(hwnd_,2,(app_.config().widgetsLocked?L"Locked layout: ":L"Move ")+win::wide(widget_.label),ROLE_SYSTEM_PUSHBUTTON,STATE_SYSTEM_FOCUSABLE,box(0,0,width-42,30));moveNode.description=app_.config().widgetsLocked?L"Layout locked. Unlock widget editing in Desktop Studio > Personalize.":L"Drag the header to snap to the grid, or use arrow keys. Placement saves per monitor.";moveNode.defaultAction=L"Focus";moveNode.invoke=[this]{if(visible()){SetFocus(hwnd_);invalidate();}};moveNode.focus=moveNode.invoke;nodes.push_back(std::move(moveNode));
 auto manage=accessibleNode(hwnd_,3,L"Manage "+win::wide(packageName_),ROLE_SYSTEM_PUSHBUTTON,STATE_SYSTEM_FOCUSABLE,box(width-40,0,40,30));manage.invoke=[this]{if(visible())app_.openExtensions();};manage.focus=[this]{if(visible())SetFocus(hwnd_);};nodes.push_back(std::move(manage));
 if(widget_.kind==WidgetKind::QuickAction){auto action=accessibleNode(hwnd_,4,win::wide(widget_.label),ROLE_SYSTEM_PUSHBUTTON,STATE_SYSTEM_FOCUSABLE|(actionAvailable_?0:STATE_SYSTEM_UNAVAILABLE),box(12,32,width-24,std::max(24.f,height-40)));action.description=widget_.action==WidgetAction::Pin?L"Open the currently configured matching pin.":L"Open the selected native Aether tool.";action.invoke=[this]{invoke();};action.focus=[this]{if(visible())SetFocus(hwnd_);};nodes.push_back(std::move(action));}
 if(widget_.kind==WidgetKind::Note){auto note=accessibleNode(hwnd_,4,L"Edit quick note",ROLE_SYSTEM_PUSHBUTTON,STATE_SYSTEM_FOCUSABLE,box(12,32,width-24,height-40));note.invoke=[this]{invoke();};note.focus=[this]{if(visible())SetFocus(hwnd_);};nodes.push_back(std::move(note));}
 if(widget_.kind==WidgetKind::Media){const wchar_t* labels[]={L"Previous track",L"Play or pause",L"Next track"};const wchar_t* commands[]={L"previous",L"toggle",L"next"};const float wide=(width-40)/3;for(int i=0;i<3;++i){auto button=accessibleNode(hwnd_,4+i,labels[i],ROLE_SYSTEM_PUSHBUTTON,STATE_SYSTEM_FOCUSABLE|(actionAvailable_?0:STATE_SYSTEM_UNAVAILABLE),box(14+i*(wide+6),height-43,wide,30));button.invoke=[this,command=commands[i]]{mediaCommand(command);};button.focus=[this]{if(visible())SetFocus(hwnd_);};nodes.push_back(std::move(button));}}
 accessibility_->snapshot(L"Native extension "+win::wide(packageName_),ROLE_SYSTEM_PANE,std::move(nodes),GetFocus()==hwnd_?2:0);
}
LRESULT ExtensionWidgetSurface::event(UINT message,WPARAM w,LPARAM l,bool& handled){
 handled=true;
 switch(message){
 case WM_GETOBJECT:snapshot();if(accessibility_)return accessibility_->object(w,l,handled);handled=false;return 0;
 case WM_SETFOCUS:case WM_KILLFOCUS:invalidate();snapshot();return 0;
 case WM_CLOSE:app_.openExtensions();return 0;
 case WM_KEYDOWN:
  if(widget_.kind==WidgetKind::Media&&GetKeyState(VK_CONTROL)<0&&(w==VK_LEFT||w==VK_RIGHT)){mediaCommand(w==VK_LEFT?L"previous":L"next");return 0;}
  if(w==VK_LEFT){keyboardMove(-12,0);return 0;}if(w==VK_RIGHT){keyboardMove(12,0);return 0;}if(w==VK_UP){keyboardMove(0,-12);return 0;}if(w==VK_DOWN){keyboardMove(0,12);return 0;}if(w==VK_RETURN||w==VK_SPACE){invoke();return 0;}if(w==VK_ESCAPE){cancelDrag();return 0;}break;
 case WM_LBUTTONDOWN:{RECT client{};GetClientRect(hwnd_,&client);float x=GET_X_LPARAM(l)/scale(),y=GET_Y_LPARAM(l)/scale();if(!app_.config().widgetsLocked&&visible()&&monitorValid_&&x>=client.right/scale()-16&&y>=client.bottom/scale()-14){POINT point{GET_X_LPARAM(l),GET_Y_LPARAM(l)};ClientToScreen(hwnd_,&point);dragOrigin_=point;RECT r{};GetWindowRect(hwnd_,&r);dragBounds_=win::rect(r);dragging_=true;dragMoved_=false;resizing_=true;app_.beginInteraction();SetCapture(hwnd_);SetFocus(hwnd_);return 0;}if(!app_.config().widgetsLocked&&visible()&&y<30&&x<client.right/scale()-42&&monitorValid_){POINT point{GET_X_LPARAM(l),GET_Y_LPARAM(l)};ClientToScreen(hwnd_,&point);dragOrigin_=point;RECT r{};GetWindowRect(hwnd_,&r);dragBounds_=win::rect(r);dragging_=true;dragMoved_=false;resizing_=false;app_.beginInteraction();SetCapture(hwnd_);SetFocus(hwnd_);return 0;}break;}
 case WM_MOUSEMOVE:if(dragging_){if(!(w&MK_LBUTTON)||GetCapture()!=hwnd_||app_.config().widgetsLocked){cancelDrag();return 0;}POINT point{GET_X_LPARAM(l),GET_Y_LPARAM(l)};ClientToScreen(hwnd_,&point);if(!dragMoved_&&std::abs(point.x-dragOrigin_.x)<GetSystemMetrics(SM_CXDRAG)&&std::abs(point.y-dragOrigin_.y)<GetSystemMetrics(SM_CYDRAG))return 0;dragMoved_=true;if(resizing_){const int grid=std::max(1,MulDiv(app_.config().widgetGrid,monitor_.dpi,96));int width=dragBounds_.width()+point.x-dragOrigin_.x,height=dragBounds_.height()+point.y-dragOrigin_.y;int minHeight=widget_.kind==WidgetKind::Media?128:widget_.kind==WidgetKind::Weather?120:widget_.kind==WidgetKind::Resources||widget_.kind==WidgetKind::Note?88:64;width=std::clamp(((width+grid/2)/grid)*grid,MulDiv(120,monitor_.dpi,96),std::max(MulDiv(120,monitor_.dpi,96),std::min(MulDiv(480,monitor_.dpi,96),monitor_.work.right-dragBounds_.left)));height=std::clamp(((height+grid/2)/grid)*grid,MulDiv(minHeight,monitor_.dpi,96),std::max(MulDiv(minHeight,monitor_.dpi,96),std::min(MulDiv(240,monitor_.dpi,96),monitor_.work.bottom-dragBounds_.top)));move({dragBounds_.left,dragBounds_.top,dragBounds_.left+width,dragBounds_.top+height},false);snapshot();return 0;}int left=dragBounds_.left+point.x-dragOrigin_.x,top=dragBounds_.top+point.y-dragOrigin_.y;const int grid=std::max(1,MulDiv(app_.config().widgetGrid,monitor_.dpi,96));left=monitor_.work.left+((left-monitor_.work.left+grid/2)/grid)*grid;top=monitor_.work.top+((top-monitor_.work.top+grid/2)/grid)*grid;left=std::clamp(left,monitor_.work.left,std::max(monitor_.work.left,monitor_.work.right-dragBounds_.width()));top=std::clamp(top,monitor_.work.top,std::max(monitor_.work.top,monitor_.work.bottom-dragBounds_.height()));move({left,top,left+dragBounds_.width(),top+dragBounds_.height()},false);snapshot();return 0;}break;
 case WM_LBUTTONUP:if(dragging_){dragging_=false;if(GetCapture()==hwnd_)ReleaseCapture();if(dragMoved_){if(resizing_)saveSize();else savePosition();}app_.endInteraction();return 0;}break;
 case WM_CANCELMODE:case WM_CAPTURECHANGED:cancelDrag();return 0;
 case WM_MOVE:snapshot();return 0;
 default:break;
 }
 handled=false;return 0;
}
}
