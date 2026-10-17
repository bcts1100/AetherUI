#include "quick_settings.hpp"
#include "app.hpp"
namespace aether::ui {
QuickSettings::QuickSettings(Graphics& g,App& a):Surface(g),app_(a){
 if(!create(L"Aether Quick Settings",WS_POPUP,WS_EX_TOOLWINDOW|WS_EX_TOPMOST,{0,0,370,370}))throw std::runtime_error("Could not create Quick Settings.");
 accessibility_=std::make_unique<Accessibility>(hwnd_);
}
void QuickSettings::open(const win::Monitor& m){
 app_.refreshStatus();setTheme(app_.theme());previewVolume_=-1;
 auto r=popupBounds(m.work,370,370,m.dpi);int margin=MulDiv(12,m.dpi,96);
 int width=r.width(),height=r.height();r={m.work.right-width-margin,m.work.top+margin,m.work.right-margin,m.work.top+margin+height};
 move(r,false);win::roundedWindow(hwnd_,theme_.radius);show(true);focusControl(app_.status().volume>=0?0:2);
}
void QuickSettings::paint(Canvas& c){
 auto width=c.size().width;auto height=c.size().height;const auto& status=app_.status();
 c.fill(box(0,0,width,height),theme_.background,static_cast<float>(theme_.radius));
 c.stroke(box(.5f,.5f,width-1,height-1),theme_.raised,static_cast<float>(theme_.radius));
 c.text(L"Quick settings",box(22,15,width-44,35),theme_.foreground,20,true);
 c.text(status.clock,box(23,53,width-46,27),theme_.muted,11);
 c.fill(box(20,98,width-40,92),theme_.surface,12);
 c.glyph(status.muted?L'\uE74F':L'\uE767',box(28,106,31,31),theme_.foreground,20);
 int volume=previewVolume_>=0?previewVolume_:status.volume;
 c.text(status.volume<0?L"No output device":L"Output volume",box(71,108,180,30),theme_.foreground,13,true);
 c.text(volume<0?L"--":std::to_wstring(volume)+L"%",box(width-90,108,48,30),theme_.muted,12,false,2);
 float end=width-42;float x=42+(end-42)*std::clamp(volume,0,100)/100.f;
 c.fill(box(42,156,end-42,5),theme_.raised,2);c.fill(box(42,156,x-42,5),theme_.accent,2);c.circle(x,158.5f,7,theme_.foreground);
 c.fill(box(20,209,(width-52)/2,67),theme_.surface,10);
 c.fill(box((width+12)/2,209,(width-52)/2,67),theme_.surface,10);
 c.glyph(L'\uE701',box(28,217,34,36),theme_.accent,21);
 c.text(status.network,box(66,217,100,27),theme_.foreground,12,true);
 c.text(L"Network settings",box(66,245,106,20),theme_.muted,9);
 c.glyph(L'\uE7F4',box((width+12)/2+9,217,32,36),theme_.accent,21);
 c.text(L"Display",box((width+12)/2+48,217,100,27),theme_.foreground,12,true);
 c.text(L"Open settings",box((width+12)/2+48,245,100,20),theme_.muted,9);
 c.text(L"Sound settings",box(22,297,147,34),theme_.foreground,12);
 c.text(L"Desktop Studio",box(width-170,297,148,34),theme_.accent,12,true,2);
 c.text(L"Tab: focus  |  Enter: open  |  Arrows: volume",box(22,height-32,width-44,24),theme_.muted,10,false,1);
 if(focused_>=0&&GetFocus()==hwnd_)c.stroke(controlRect(focused_),theme_.foreground,8,2);accessibleSnapshot();
}
void QuickSettings::volumeAt(float x){
 RECT r{};GetClientRect(hwnd_,&r);float width=r.right/scale();
 previewVolume_=static_cast<int>(std::clamp((x-42)/std::max(1.f,width-84),0.f,1.f)*100+0.5f);invalidate();
}
void QuickSettings::click(float x,float y){
 RECT r{};GetClientRect(hwnd_,&r);float width=r.right/scale();
 if(y>=100&&y<143&&x<65){win::toggleMute();app_.refreshStatus();invalidate();return;}
 if(y>=140&&y<184&&app_.status().volume>=0){volumeAt(x);win::setVolume(previewVolume_/100.f);previewVolume_=-1;app_.refreshStatus();invalidate();return;}
 if(y>=209&&y<277){hide();app_.launchTarget(hwnd_,x<width/2?L"ms-settings:network-status":L"ms-settings:display");}
 if(y>=290&&y<337){hide();if(x<width/2)app_.launchTarget(hwnd_,L"ms-settings:sound");else app_.openEditor();}
}
LRESULT QuickSettings::event(UINT m,WPARAM w,LPARAM l,bool& handled){
 handled=true;
 switch(m){
 case WM_MOVE:if(accessibility_){accessibleSnapshot();NotifyWinEvent(EVENT_OBJECT_LOCATIONCHANGE,hwnd_,OBJID_CLIENT,CHILDID_SELF);}break;
 case WM_GETOBJECT:if(!accessibility_){handled=false;return 0;}UpdateWindow(hwnd_);accessibleSnapshot();return accessibility_->object(w,l,handled);
 case WM_SETFOCUS:case WM_KILLFOCUS:invalidate();break;
 case WM_ACTIVATE:if(LOWORD(w)==WA_INACTIVE&&!draggingVolume_)hide();return 0;
 case WM_CLOSE:hide();return 0;
 case WM_KEYDOWN:
  if(w==VK_ESCAPE){hide();return 0;}
  if(w==VK_TAB){int direction=(GetKeyState(VK_SHIFT)&0x8000)?-1:1;int next=focused_<0?(direction>0?0:5):(focused_+direction+6)%6;if(app_.status().volume<0&&next<2)next=direction>0?2:5;focusControl(next);return 0;}
  if(w==VK_RETURN||w==VK_SPACE){if(focused_>=0)invoke(focused_);return 0;}
  if(focused_==1&&app_.status().volume>=0){if(w==VK_LEFT||w==VK_DOWN){adjustVolume(std::max(0,app_.status().volume-5));return 0;}if(w==VK_RIGHT||w==VK_UP){adjustVolume(std::min(100,app_.status().volume+5));return 0;}if(w==VK_HOME||w==VK_END){adjustVolume(w==VK_HOME?0:100);return 0;}}break;
 case WM_LBUTTONDOWN:if(GET_Y_LPARAM(l)/scale()>=140&&GET_Y_LPARAM(l)/scale()<184&&app_.status().volume>=0){draggingVolume_=true;SetCapture(hwnd_);volumeAt(GET_X_LPARAM(l)/scale());return 0;}break;
 case WM_MOUSEMOVE:if(draggingVolume_){volumeAt(GET_X_LPARAM(l)/scale());return 0;}break;
 case WM_LBUTTONUP:if(draggingVolume_){volumeAt(GET_X_LPARAM(l)/scale());int volume=previewVolume_;draggingVolume_=false;ReleaseCapture();win::setVolume(volume/100.f);previewVolume_=-1;app_.refreshStatus();invalidate();return 0;}break;
 case WM_CAPTURECHANGED:draggingVolume_=false;previewVolume_=-1;invalidate();return 0;
 default:break;
 }
 handled=false;return 0;
}
}
namespace aether::ui {
D2D1_RECT_F QuickSettings::controlRect(int index) const{
 RECT r{};GetClientRect(hwnd_,&r);const float width=r.right/scale();switch(index){case 0:return box(25,103,40,39);case 1:return box(35,142,width-70,36);case 2:return box(20,209,(width-52)/2,67);case 3:return box((width+12)/2,209,(width-52)/2,67);case 4:return box(20,294,(width-40)/2,40);default:return box(width/2,294,(width-40)/2,40);}
}
void QuickSettings::focusControl(int index){focused_=index;SetFocus(hwnd_);invalidate();accessibleSnapshot();}
void QuickSettings::adjustVolume(int value){if(app_.status().volume<0)return;win::setVolume(std::clamp(value,0,100)/100.f);previewVolume_=-1;app_.refreshStatus();invalidate();accessibleSnapshot();}
void QuickSettings::invoke(int index){switch(index){case 0:if(app_.status().volume>=0){win::toggleMute();app_.refreshStatus();invalidate();accessibleSnapshot();}break;case 1:focusControl(1);break;case 2:hide();app_.launchTarget(hwnd_,L"ms-settings:network-status");break;case 3:hide();app_.launchTarget(hwnd_,L"ms-settings:display");break;case 4:hide();app_.launchTarget(hwnd_,L"ms-settings:sound");break;case 5:hide();app_.openEditor();break;default:break;}}
void QuickSettings::accessibleSnapshot(){
 if(!accessibility_)return;std::vector<AccessibleNode> nodes;const wchar_t* names[]={L"Mute output",L"Output volume",L"Network settings",L"Display settings",L"Sound settings",L"Desktop Studio"};
 for(int i=0;i<6;++i){long state=STATE_SYSTEM_FOCUSABLE;if(i<2&&app_.status().volume<0)state|=STATE_SYSTEM_UNAVAILABLE;if(i==0&&app_.status().muted)state|=STATE_SYSTEM_CHECKED;auto n=accessibleNode(hwnd_,i+1,names[i],i==0?ROLE_SYSTEM_CHECKBUTTON:i==1?ROLE_SYSTEM_SLIDER:ROLE_SYSTEM_PUSHBUTTON,state,controlRect(i));n.focus=[this,i]{focusControl(i);};n.invoke=[this,i]{invoke(i);};if(i==0)n.defaultAction=L"Toggle";if(i==1){n.value=app_.status().volume<0?L"Unavailable":std::to_wstring(app_.status().volume)+L"%";n.defaultAction=L"Focus";n.description=L"Use arrow keys to change by five percent; Home sets zero and End sets one hundred.";n.setValue=[this](const std::wstring& text){try{std::size_t used=0;int value=std::stoi(text,&used);if(used<text.size()&&text[used]==L'%')++used;if(used!=text.size()||value<0||value>100)return false;adjustVolume(value);return true;}catch(...){return false;}};}nodes.push_back(std::move(n));}
 accessibility_->snapshot(L"Quick settings",ROLE_SYSTEM_DIALOG,std::move(nodes),GetFocus()==hwnd_&&focused_>=0?focused_+1:0);
}
}