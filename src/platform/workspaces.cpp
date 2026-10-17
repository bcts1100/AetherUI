#include "workspaces.hpp"
#include <servprov.h>
#include <objectarray.h>
#include <winternl.h>
#include "config.hpp"
#include "core/wallpaper.hpp"
#include <sstream>
#include <iomanip>
#include <atomic>
namespace aether::win {
namespace {
// Windows 10 shell ABI declarations, not a supported cross-version contract.
// Reference: MScholtes/VirtualDesktop, VirtualDesktop.cs (interface declarations).
struct __declspec(uuid("FF72FFDD-BE7E-43FC-9C03-AD81681E88E4")) Desktop10:IUnknown {
 virtual HRESULT STDMETHODCALLTYPE IsViewVisible(IUnknown*,BOOL*)=0;
 virtual HRESULT STDMETHODCALLTYPE GetId(GUID*)=0;
};
struct __declspec(uuid("F31574D6-B682-4CDC-BD56-1827860ABEC6")) DesktopManager10:IUnknown {
 virtual HRESULT STDMETHODCALLTYPE GetCount(UINT*)=0;
 virtual HRESULT STDMETHODCALLTYPE MoveViewToDesktop(IUnknown*,Desktop10*)=0;
 virtual HRESULT STDMETHODCALLTYPE CanViewMoveDesktops(IUnknown*,BOOL*)=0;
 virtual HRESULT STDMETHODCALLTYPE GetCurrentDesktop(Desktop10**)=0;
 virtual HRESULT STDMETHODCALLTYPE GetDesktops(IObjectArray**)=0;
 virtual HRESULT STDMETHODCALLTYPE GetAdjacentDesktop(Desktop10*,int,Desktop10**)=0;
 virtual HRESULT STDMETHODCALLTYPE SwitchDesktop(Desktop10*)=0;
 virtual HRESULT STDMETHODCALLTYPE CreateDesktop(Desktop10**)=0;
 virtual HRESULT STDMETHODCALLTYPE RemoveDesktop(Desktop10*,Desktop10*)=0;
 virtual HRESULT STDMETHODCALLTYPE FindDesktop(const GUID*,Desktop10**)=0;
};
struct __declspec(uuid("1841C6D7-4F9D-42C0-AF41-8747538F10E5")) ViewCollection10:IUnknown {
 virtual HRESULT STDMETHODCALLTYPE GetViews(IObjectArray**)=0;
 virtual HRESULT STDMETHODCALLTYPE GetViewsByZOrder(IObjectArray**)=0;
 virtual HRESULT STDMETHODCALLTYPE GetViewsByAppUserModelId(PCWSTR,IObjectArray**)=0;
 virtual HRESULT STDMETHODCALLTYPE GetViewForHwnd(HWND,IUnknown**)=0;
};
struct __declspec(uuid("C179334C-4295-40D3-BEA1-C654D965605A")) Notification10:IUnknown {
 virtual HRESULT STDMETHODCALLTYPE Created(Desktop10*)=0;
 virtual HRESULT STDMETHODCALLTYPE DestroyBegin(Desktop10*,Desktop10*)=0;
 virtual HRESULT STDMETHODCALLTYPE DestroyFailed(Desktop10*,Desktop10*)=0;
 virtual HRESULT STDMETHODCALLTYPE Destroyed(Desktop10*,Desktop10*)=0;
 virtual HRESULT STDMETHODCALLTYPE ViewChanged(IUnknown*)=0;
 virtual HRESULT STDMETHODCALLTYPE CurrentChanged(Desktop10*,Desktop10*)=0;
};
struct __declspec(uuid("0CD45E71-D927-4F15-8B0A-8FEF525337BF")) NotificationService10:IUnknown {
 virtual HRESULT STDMETHODCALLTYPE Register(Notification10*,DWORD*)=0;
 virtual HRESULT STDMETHODCALLTYPE Unregister(DWORD)=0;
};
class WorkspaceSink final:public Notification10 {
 std::atomic<ULONG> references_{1};std::atomic<HWND> receiver_;
 HRESULT changed(){if(auto h=receiver_.load())PostMessageW(h,WorkspaceChangedMessage,0,0);return S_OK;}
public:
 explicit WorkspaceSink(HWND h):receiver_(h){}void detach(){receiver_=nullptr;}
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(id!=IID_IUnknown&&id!=__uuidof(Notification10))return E_NOINTERFACE;*out=static_cast<Notification10*>(this);AddRef();return S_OK;}
 ULONG STDMETHODCALLTYPE AddRef()override{return ++references_;}ULONG STDMETHODCALLTYPE Release()override{auto n=--references_;if(!n)delete this;return n;}
 HRESULT STDMETHODCALLTYPE Created(Desktop10*)override{return changed();}
 HRESULT STDMETHODCALLTYPE DestroyBegin(Desktop10*,Desktop10*)override{return S_OK;}
 HRESULT STDMETHODCALLTYPE DestroyFailed(Desktop10*,Desktop10*)override{return changed();}
 HRESULT STDMETHODCALLTYPE Destroyed(Desktop10*,Desktop10*)override{return changed();}
 HRESULT STDMETHODCALLTYPE ViewChanged(IUnknown*)override{return S_OK;}
 HRESULT STDMETHODCALLTYPE CurrentChanged(Desktop10*,Desktop10*)override{return changed();}
};
std::string desktopKey(const GUID& id){wchar_t key[40]{};StringFromGUID2(id,key,40);return utf8(key);}
struct Preference {GUID id{};std::wstring name;WorkspaceAppearance appearance;};
bool validName(std::wstring_view name){return name.size()<=80&&wide(utf8(name))==name&&std::none_of(name.begin(),name.end(),[](wchar_t c){return c<32||c==127;});}
bool validAppearance(const WorkspaceAppearance& a){return a.background>=0&&a.background<=9&&(a.background==7?(validWallpaperPath(a.image)&&a.image.size()>=3&&a.image[1]==L':'):a.image.empty());}
constexpr GUID ShellClass{0xC2F03A33,0x21F5,0x47FA,{0xB4,0xBB,0x15,0x63,0x62,0xA2,0xF2,0x39}};
constexpr GUID ManagerService{0xC5E0CDCA,0x7B6E,0x41B2,{0x9F,0xC4,0xD9,0x39,0x75,0xCC,0x46,0x7B}};
bool compatible(){auto module=GetModuleHandleW(L"ntdll.dll");auto version=reinterpret_cast<LONG(WINAPI*)(PRTL_OSVERSIONINFOW)>(module?GetProcAddress(module,"RtlGetVersion"):nullptr);RTL_OSVERSIONINFOW value{};value.dwOSVersionInfoSize=sizeof(value);return version&&version(&value)==0&&value.dwMajorVersion==10&&value.dwBuildNumber>=19041&&value.dwBuildNumber<=19045;}
struct PinnedWindow {
 Handle process;HWND hwnd{};DWORD pid=0,thread=0;FILETIME birth{};
 explicit PinnedWindow(const Window& window):hwnd(window.hwnd),pid(window.pid){
  if(!windowAlive(hwnd,pid)||GetAncestor(hwnd,GA_ROOT)!=hwnd)return;
  wchar_t name[128]{};GetClassNameW(hwnd,name,128);if(wcscmp(name,L"Shell_TrayWnd")==0||wcscmp(name,L"Shell_SecondaryTrayWnd")==0||wcscmp(name,L"Progman")==0||wcscmp(name,L"WorkerW")==0)return;
  DWORD owner=0;thread=GetWindowThreadProcessId(hwnd,&owner);if(owner!=pid)return;
  process.reset(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid));FILETIME exited{},kernel{},user{};
  if(!process||!GetProcessTimes(process.value,&birth,&exited,&kernel,&user)||(window.processBirth&&window.processBirth!=((static_cast<std::uint64_t>(birth.dwHighDateTime)<<32)|birth.dwLowDateTime)))process.reset();
 }
 bool valid()const{if(!process||WaitForSingleObject(process.value,0)!=WAIT_TIMEOUT||!windowAlive(hwnd,pid))return false;DWORD owner=0;if(GetWindowThreadProcessId(hwnd,&owner)!=thread||owner!=pid)return false;FILETIME now{},exited{},kernel{},user{};return GetProcessTimes(process.value,&now,&exited,&kernel,&user)&&CompareFileTime(&birth,&now)==0;}
};
}
std::wstring overviewAppKey(const Window& w){auto key=lower(w.identity.empty()?w.path:w.identity);std::replace(key.begin(),key.end(),L'\\',L'/');auto path=lower(w.path);std::replace(path.begin(),path.end(),L'\\',L'/');const auto file=path.substr(path.find_last_of(L'/')==std::wstring::npos?0:path.find_last_of(L'/')+1);const bool sharedHost=file==L"applicationframehost.exe"||file==L"wwahost.exe"||file==L"msedgewebview2.exe";return key.empty()||key.starts_with(L"pid:")||(sharedHost&&(w.identity.empty()||key==path))?L"window:"+std::to_wstring(reinterpret_cast<std::uintptr_t>(w.hwnd)):key;}
bool requestWindowClose(const Window& w) noexcept{PinnedWindow pinned(w);return pinned.valid()&&PostMessageW(w.hwnd,WM_SYSCOMMAND,SC_CLOSE,0)!=FALSE;}
struct Workspaces::Impl {
 ComPtr<DesktopManager10> manager;ComPtr<ViewCollection10> views;ComPtr<IVirtualDesktopManager> publicManager;
 std::vector<Workspace> items;std::vector<GUID> created;std::wstring notice;bool admitted=false;
 std::vector<Preference> preferences;bool loaded=false;ComPtr<NotificationService10> notifications;ComPtr<WorkspaceSink> sink;DWORD cookie=0;bool registered=false;
 void load(){if(loaded)return;loaded=true;try{auto text=readText(configDirectory()/L"workspaces-v1.txt",128*32768);std::istringstream input(text);std::string header;std::getline(input,header);if(!header.empty()&&header.back()==13)header.pop_back();if(header!="AetherWorkspaces1")return;std::vector<Preference> next;std::string line;
  while(std::getline(input,line)){if(line.empty())continue;if(next.size()>=128)return;std::istringstream row(line);std::string key,name,path,extra;int mode=0;GUID id{};if(!(row>>std::quoted(key)>>std::quoted(name)>>mode>>std::quoted(path))||(row>>extra)||key.size()!=38||name.size()>320||path.size()>16384||utf8(wide(name))!=name||utf8(wide(path))!=path||FAILED(CLSIDFromString(wide(key).c_str(),&id))||id==GUID_NULL)return;Preference value{id,wide(name),{mode,wide(path)}};if(!validName(value.name)||!validAppearance(value.appearance)||std::any_of(next.begin(),next.end(),[&](const auto& p){return p.id==id;}))return;next.push_back(std::move(value));}preferences=std::move(next);
 }catch(...){notice=L"Workspace preferences could not be read";}}
 bool save(std::vector<Preference> next){if(next.size()>128){notice=L"Workspace profile is full";return false;}try{std::ostringstream out;out<<"AetherWorkspaces1\n";for(const auto& p:next)out<<std::quoted(desktopKey(p.id))<<' '<<std::quoted(utf8(p.name))<<' '<<p.appearance.background<<' '<<std::quoted(utf8(p.appearance.image))<<'\n';writeAtomic(configDirectory()/L"workspaces-v1.txt",out.str());preferences=std::move(next);return true;}catch(...){notice=L"Workspace preferences could not be saved";return false;}}
 Preference& entry(std::vector<Preference>& list,const GUID& id){auto it=std::find_if(list.begin(),list.end(),[&](const auto& p){return p.id==id;});if(it!=list.end())return *it;list.push_back({id,{},{}});return list.back();}
 std::vector<Preference> ordered(){std::vector<Preference> result;for(const auto& w:items){auto p=std::find_if(preferences.begin(),preferences.end(),[&](const auto& value){return value.id==w.id;});result.push_back(p==preferences.end()?Preference{w.id,{},{}}:*p);}for(const auto& p:preferences)if(std::none_of(result.begin(),result.end(),[&](const auto& value){return value.id==p.id;})&&result.size()<128)result.push_back(p);return result;}
 bool connect(){
  if(manager)return true;if(!compatible()){notice=L"Workspace controls unavailable on this Windows version";return false;}
  ComPtr<IServiceProvider> shell;
  if(FAILED(CoCreateInstance(ShellClass,nullptr,CLSCTX_LOCAL_SERVER,IID_PPV_ARGS(&shell)))||FAILED(shell->QueryService(ManagerService,IID_PPV_ARGS(&manager)))||FAILED(shell->QueryService(__uuidof(ViewCollection10),IID_PPV_ARGS(&views)))||FAILED(CoCreateInstance(CLSID_VirtualDesktopManager,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&publicManager)))){manager.Reset();views.Reset();publicManager.Reset();notice=L"Windows workspace service unavailable";return false;}return true;
 }
 ComPtr<Desktop10> find(const GUID& id){ComPtr<Desktop10> result;if(manager&&std::any_of(items.begin(),items.end(),[&](const auto& w){return w.id==id;}))manager->FindDesktop(&id,&result);return result;}
 bool failed(){notice=L"Windows could not complete the workspace action";return false;}
};
Workspaces::Workspaces():impl_(std::make_unique<Impl>()){}Workspaces::~Workspaces(){unwatch();}
bool Workspaces::available()const{return impl_->admitted;}const std::vector<Workspace>& Workspaces::items()const{return impl_->items;}const std::wstring& Workspaces::notice()const{return impl_->notice;}
bool Workspaces::refresh(){
 auto& s=*impl_;s.load();s.admitted=false;if(!s.connect()){s.items.clear();return false;}
 ComPtr<IObjectArray> array;ComPtr<Desktop10> current;GUID currentId{};UINT count=0;
 if(FAILED(s.manager->GetCurrentDesktop(&current))||!current||FAILED(current->GetId(&currentId))||FAILED(s.manager->GetDesktops(&array))||!array||FAILED(array->GetCount(&count))||count==0||count>128){s.items.clear();s.manager.Reset();s.views.Reset();s.publicManager.Reset();return s.failed();}
 std::vector<Workspace> next;for(UINT i=0;i<count;++i){ComPtr<Desktop10> desktop;GUID id{};if(FAILED(array->GetAt(i,IID_PPV_ARGS(&desktop)))||!desktop||FAILED(desktop->GetId(&id))||id==GUID_NULL||std::any_of(next.begin(),next.end(),[&](const auto& w){return w.id==id;})){s.items.clear();return s.failed();}next.push_back({id,L"Desktop "+std::to_wstring(i+1),id==currentId});}
 if(std::count_if(next.begin(),next.end(),[](const auto& w){return w.current;})!=1){s.items.clear();return s.failed();}
 for(auto& w:next){auto p=std::find_if(s.preferences.begin(),s.preferences.end(),[&](const auto& value){return value.id==w.id;});if(p!=s.preferences.end()&&!p->name.empty())w.name=p->name;}
 auto rank=[&](const GUID& id){auto p=std::find_if(s.preferences.begin(),s.preferences.end(),[&](const auto& value){return value.id==id;});return std::distance(s.preferences.begin(),p);};
 std::stable_sort(next.begin(),next.end(),[&](const auto& a,const auto& b){return rank(a.id)<rank(b.id);});
 s.items=std::move(next);s.notice.clear();s.admitted=true;return true;
}
bool Workspaces::rename(const GUID& supplied,std::wstring_view requested){if(requested.size()>80)return false;const GUID id=supplied;const std::wstring name(requested);if(!validName(name)||!refresh()||!impl_->find(id))return false;auto next=impl_->ordered();impl_->entry(next,id).name=name;return impl_->save(std::move(next))&&refresh();}
bool Workspaces::reorder(const GUID& supplied,int position){const GUID id=supplied;if(!refresh()||position<0||position>=static_cast<int>(impl_->items.size()))return false;auto items=impl_->items;auto from=std::find_if(items.begin(),items.end(),[&](const auto& w){return w.id==id;});if(from==items.end())return false;auto item=*from;items.erase(from);items.insert(items.begin()+position,item);std::vector<Preference> next;for(const auto& w:items){auto old=impl_->preferences;next.push_back(impl_->entry(old,w.id));}for(const auto& p:impl_->preferences)if(std::none_of(next.begin(),next.end(),[&](const auto& n){return n.id==p.id;})&&next.size()<128)next.push_back(p);return impl_->save(std::move(next))&&refresh();}
WorkspaceAppearance Workspaces::appearance(const GUID& id)const{auto p=std::find_if(impl_->preferences.begin(),impl_->preferences.end(),[&](const auto& item){return item.id==id;});return p==impl_->preferences.end()?WorkspaceAppearance{}:p->appearance;}
bool Workspaces::setAppearance(const GUID& supplied,WorkspaceAppearance appearance){const GUID id=supplied;if(!validAppearance(appearance)||!refresh()||!impl_->find(id))return false;auto next=impl_->ordered();impl_->entry(next,id).appearance=std::move(appearance);return impl_->save(std::move(next));}
bool Workspaces::watch(HWND receiver){if(impl_->sink)return true;if(!IsWindow(receiver)||!refresh())return false;ComPtr<IServiceProvider> shell;constexpr GUID service{0xA501FDEC,0x4A09,0x464C,{0xAE,0x4E,0x1B,0x9C,0x21,0xB8,0x49,0x18}};if(FAILED(CoCreateInstance(ShellClass,nullptr,CLSCTX_LOCAL_SERVER,IID_PPV_ARGS(&shell)))||FAILED(shell->QueryService(service,IID_PPV_ARGS(&impl_->notifications))))return false;impl_->sink.Attach(new WorkspaceSink(receiver));if(FAILED(impl_->notifications->Register(impl_->sink.Get(),&impl_->cookie))){unwatch();return false;}impl_->registered=true;return true;}
void Workspaces::unwatch(){if(impl_->sink)impl_->sink->detach();if(impl_->notifications&&impl_->registered)impl_->notifications->Unregister(impl_->cookie);impl_->registered=false;impl_->cookie=0;impl_->sink.Reset();impl_->notifications.Reset();}
bool Workspaces::create(){if(!refresh()||impl_->items.size()>=16)return false;ComPtr<Desktop10> desktop;GUID id{};if(FAILED(impl_->manager->CreateDesktop(&desktop))||!desktop||FAILED(desktop->GetId(&id))||id==GUID_NULL)return impl_->failed();impl_->created.push_back(id);return refresh();}
bool Workspaces::switchTo(const GUID& supplied){const GUID id=supplied;if(!refresh())return false;auto target=impl_->find(id);if(!target||FAILED(impl_->manager->SwitchDesktop(target.Get())))return impl_->failed();return true;}
bool Workspaces::move(const Window& suppliedWindow,const GUID& supplied){const Window window=suppliedWindow;const GUID id=supplied;
 PinnedWindow pinned(window);if(!pinned.valid()){impl_->notice=L"Window no longer available";return false;}if(!refresh())return false;auto target=impl_->find(id);if(!target)return impl_->failed();
 ComPtr<IUnknown> view;BOOL movable=FALSE;
 if(FAILED(impl_->views->GetViewForHwnd(window.hwnd,&view))||!view||FAILED(impl_->manager->CanViewMoveDesktops(view.Get(),&movable))||!movable||!pinned.valid())return impl_->failed();
 if(FAILED(impl_->manager->MoveViewToDesktop(view.Get(),target.Get()))||!pinned.valid())return impl_->failed();
 GUID actual{};return SUCCEEDED(impl_->publicManager->GetWindowDesktopId(window.hwnd,&actual))&&actual==id?true:impl_->failed();
}
bool Workspaces::removeCreated(const GUID& supplied,const GUID& suppliedFallback){const GUID id=supplied,fallback=suppliedFallback;
 if(id==fallback||std::find(impl_->created.begin(),impl_->created.end(),id)==impl_->created.end()||!refresh())return false;
 auto target=impl_->find(id),replacement=impl_->find(fallback);if(!target||!replacement||FAILED(impl_->manager->RemoveDesktop(target.Get(),replacement.Get())))return impl_->failed();std::erase(impl_->created,id);return refresh();
}
}
