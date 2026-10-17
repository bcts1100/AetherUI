#include "live_wallpaper.hpp"
#include <cmath>
#include "platform/display_policy.hpp"
namespace aether::ui {
namespace {
bool shellOwned(HWND h,DWORD pid){DWORD owner=0;GetWindowThreadProcessId(h,&owner);return h&&owner==pid;}
HWND desktopHost(){
 auto shell=GetShellWindow();DWORD pid=0;GetWindowThreadProcessId(shell,&pid);if(!pid)return nullptr;
 struct Search{DWORD pid;HWND host=nullptr;} search{pid};
 auto find=[](HWND h,LPARAM data)->BOOL{auto& s=*reinterpret_cast<Search*>(data);if(!shellOwned(h,s.pid))return TRUE;
  if(FindWindowExW(h,nullptr,L"SHELLDLL_DefView",nullptr)){auto behind=FindWindowExW(nullptr,h,L"WorkerW",nullptr);if(shellOwned(behind,s.pid)&&!FindWindowExW(behind,nullptr,L"SHELLDLL_DefView",nullptr))s.host=behind;}return TRUE;};
 EnumWindows(find,reinterpret_cast<LPARAM>(&search));
 if(!search.host){auto progman=FindWindowW(L"Progman",nullptr);if(!shellOwned(progman,pid))return nullptr;DWORD_PTR ignored=0;
  // Explorer's optional wallpaper host is a compatibility facility. Never
  // conceal or reparent Explorer windows; refuse unknown layouts.
  SendMessageTimeoutW(progman,0x052C,0,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,500,&ignored);EnumWindows(find,reinterpret_cast<LPARAM>(&search));
 }
 return search.host;
}
}
class LiveWallpaper::Scene final:public Surface {
 int preset_;std::shared_ptr<const win::IconPixels> image_;bool still_=false;bool ticking_=false;std::uint64_t started_=GetTickCount64(),frames_=0;
 void paint(Canvas& c)override{
  ++frames_;SetPropW(hwnd_,L"Aether.LiveWallpaper.Frames",reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(frames_)));auto s=c.size();const double t=still_?0.0:(GetTickCount64()-started_)/1000.0;const float w=s.width,h=s.height;
  c.fill(box(0,0,w,h),preset_==3?0x261B32:0x081321);
  if(image_){
   const float factor=std::max(w/image_->width,h/image_->height),iw=image_->width*factor,ih=image_->height*factor,ox=(w-iw)/2,oy=(h-ih)/2;
   c.pushClip(box(0,0,w,h));c.icon(image_,box(ox,oy,iw,ih),0);
   // Only the bundled, composition-matched pond moves. Ordinary local images
   // remain still; the cottage, trees and camera are never displaced.
   if(preset_==4&&!still_){
    for(int band=0;band<28;++band){
     const float v=band/28.f,y=.598f+v*.176f,half=.10f+v*.115f,x=.56f-half;
     const float shift=static_cast<float>(std::sin(t*.65+band*.53))*.0032f;
     c.imageRegion(image_,box(ox+x*iw,oy+y*ih,half*2*iw,.176f/28*ih+.2f),box((x+shift)*image_->width,y*image_->height,half*2*image_->width,.176f/28*image_->height),.85f);
    }
    for(int ring=0;ring<3;++ring){
     const float age=static_cast<float>(std::fmod(t*.10+ring*.333,1.0));
     const float x=.42f+ring*.105f,y=.735f-ring*.038f;
     c.ellipse(ox+x*iw,oy+y*ih,(.004f+age*.024f)*iw,(.001f+age*.004f)*ih,0xC8D7B1,.16f*std::sin(age*3.14159265f),true);
    }
    // Layered low-opacity ellipses give the distant mist feathered edges.
    for(int layer=0;layer<12;++layer){const float drift=static_cast<float>(std::sin(t*.035))*iw*.03f;c.ellipse(ox+iw*.55f+drift,oy+ih*.548f,iw*(.105f+layer*.005f),ih*(.004f+layer*.0013f),0xC6D7C4,.009f);}
   }
   c.popClip();return;
  }
  // Six broad Bezier ribbons avoid decoded media, network access and a busy
  // render loop. Geometry is released after each bounded frame.
  const std::uint32_t a=preset_==1?0x397F91:preset_==2?0x765DB8:0xC16D57;
  const std::uint32_t b=preset_==1?0x52BC97:preset_==2?0x3D86B0:0x896293;
  for(int band=0;band<6;++band){float phase=static_cast<float>(t*.075+band*.52);float y=h*(.22f+band*.09f);c.ribbon(w,y,h*.15f,h*.24f,phase,band<3?a:b,.09f);c.ribbon(w,y+h*.01f,h*.045f,h*.24f,phase,band<3?b:a,.06f);}
 }
 LRESULT event(UINT m,WPARAM w,LPARAM,bool& handled)override{handled=true;if(m==WM_TIMER&&w==31){invalidate();return 0;}if(m==WM_NCHITTEST)return HTTRANSPARENT;handled=false;return 0;}
public:
 Scene(Graphics& g,int preset,HWND parent,Rect screen,std::shared_ptr<const win::IconPixels> image,bool still):Surface(g),preset_(preset),image_(std::move(image)),still_(still){
  auto previous=SetThreadDpiAwarenessContext(GetWindowDpiAwarenessContext(parent));
  bool created=create(L"Aether animated wallpaper",WS_POPUP,WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,screen);
  if(created){SetLastError(0);auto old=SetParent(hwnd_,parent);if(!old&&GetLastError())created=false;}
  if(created){SetWindowLongPtrW(hwnd_,GWL_STYLE,WS_CHILD|WS_CLIPSIBLINGS);POINT p{screen.left,screen.top};MapWindowPoints(nullptr,parent,&p,1);SetWindowPos(hwnd_,HWND_BOTTOM,p.x,p.y,screen.width(),screen.height(),SWP_NOACTIVATE|SWP_SHOWWINDOW);}
  if(previous)SetThreadDpiAwarenessContext(previous);
  if(created){SetPropW(hwnd_,L"Aether.LiveWallpaper.Preset",reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(preset)));SetPropW(hwnd_,L"Aether.LiveWallpaper.Still",reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(still||(image_&&preset_!=4))));}
  if(!created){destroy();throw std::runtime_error("The desktop wallpaper host could not accept an owned surface.");}
 }
 ~Scene()override{KillTimer(hwnd_,31);}
 void pause(bool paused){paused=paused||still_||(image_&&preset_!=4);if(paused&&ticking_){KillTimer(hwnd_,31);ticking_=false;}else if(!paused&&!ticking_){ticking_=SetTimer(hwnd_,31,83,nullptr)!=0;}SetPropW(hwnd_,L"Aether.LiveWallpaper.Ticking",reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(ticking_)));if(!frames_)invalidate();}
 std::uint64_t frames()const{return frames_;}
};
LiveWallpaper::LiveWallpaper(Graphics& g):graphics_(g){}
LiveWallpaper::~LiveWallpaper(){stop();}
void LiveWallpaper::stop(){scenes_.clear();displays_.clear();host_=nullptr;preset_=0;image_.reset();still_=false;}
std::uint64_t LiveWallpaper::frames()const{std::uint64_t n=0;for(const auto& scene:scenes_)n+=scene->frames();return n;}
std::vector<Rect> LiveWallpaper::surfaceBounds()const{std::vector<Rect> result;for(const auto& scene:scenes_){RECT r{};if(GetWindowRect(scene->hwnd(),&r))result.push_back({r.left,r.top,r.right,r.bottom});}return result;}
bool LiveWallpaper::update(int preset,const std::vector<win::Monitor>& displays,const std::vector<win::Window>& windows,bool paused,HWND testHost,std::shared_ptr<const win::IconPixels> image,bool still){
 if(preset<1||preset>4){stop();notice_=L"Windows static wallpaper is visible.";return true;}
 if(preset==4&&!image){stop();notice_=L"Woodland Zen artwork unavailable.";return false;}
 if(preset_!=preset||!win::sameDisplayTopology(displays_,displays)||image_!=image||still_!=still||!IsWindow(host_)){
  if(attemptedPreset_==preset&&GetTickCount64()<retryAfter_)return false;
  stop();attemptedPreset_=preset;retryAfter_=GetTickCount64()+30000;host_=testHost?testHost:desktopHost();if(!host_){notice_=L"Animated wallpaper unavailable: Explorer has no supported background host.";return false;}
  if(testHost){DWORD pid=0;GetWindowThreadProcessId(testHost,&pid);if(pid!=GetCurrentProcessId()){stop();return false;}}
  try{for(const auto& monitor:displays)scenes_.push_back(std::make_unique<Scene>(graphics_,preset,host_,monitor.bounds,image,still));displays_=displays;preset_=preset;image_=std::move(image);still_=still;retryAfter_=0;}
  catch(const std::exception& e){stop();notice_=win::wide(e.what());return false;}
 }
 SYSTEM_POWER_STATUS power{};const bool battery=GetSystemPowerStatus(&power)&&power.ACLineStatus==0;
 for(std::size_t i=0;i<scenes_.size();++i){const auto r=displays_[i].bounds;bool covered=std::any_of(windows.begin(),windows.end(),[&](const auto& window){return !window.minimized&&window.bounds.left<=r.left&&window.bounds.top<=r.top&&window.bounds.right>=r.right&&window.bounds.bottom>=r.bottom;});scenes_[i]->pause(paused||battery||covered);}
 notice_=still_?L"Workspace wallpaper active; no animation timer.":paused?L"Animation paused by reduced-motion preference.":battery?L"Animation paused on battery.":L"Animated wallpaper active at up to 12 fps; covered displays pause.";return true;
}
}
