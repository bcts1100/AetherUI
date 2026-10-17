#include "wallpaper.hpp"
#include "wallpaper_codec.hpp"
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
namespace aether::win {
namespace {
bool displayMap(const std::vector<Monitor>& value){
 if(value.empty()||value.size()>WallpaperMonitorLimit)return false;
 for(std::size_t i=0;i<value.size();++i){
  const auto& m=value[i];if(m.name.empty()||m.name.size()>512||m.name.find(L'\0')!=std::wstring::npos||m.bounds.left>=m.bounds.right||m.bounds.top>=m.bounds.bottom)return false;
  for(std::size_t j=0;j<i;++j)if(value[j].name==m.name||value[j].bounds==m.bounds)return false;
 }return true;
}
bool sameDisplays(const std::vector<Monitor>& a,const std::vector<Monitor>& b){
 if(!displayMap(a)||a.size()!=b.size())return false;
 return std::all_of(a.begin(),a.end(),[&](const auto& monitor){auto found=std::find_if(b.begin(),b.end(),[&](const auto& other){return other.name==monitor.name;});return found!=b.end()&&found->bounds==monitor.bounds;});
}
std::optional<std::vector<WallpaperDisplay>> map(const WallpaperSnapshot& snapshot,const std::vector<Monitor>& displays){std::vector<Rect> rectangles;for(const auto& display:displays)rectangles.push_back(display.bounds);return mapWallpaperDisplays(snapshot,rectangles);}
bool successfulReply(const WallpaperRequest& request,const WallpaperResult& result){
 if(result.kind!=request.kind)return false;if(result.status!=WallpaperStatus::Ok)return true;if(!result.after)return false;
 if(request.kind==WallpaperKind::Query)return true;
 if(!result.before||!sameWallpaperTopology(request.displays,*result.before)||!sameWallpaperTopology(request.displays,*result.after))return false;
 if(request.kind==WallpaperKind::Undo)return request.before&&request.expected&&sameWallpaperState(*request.expected,*result.before)&&sameWallpaperState(*request.before,*result.after);
 auto desired=*result.before;desired.fit=request.fit;for(auto& monitor:desired.monitors)if(request.selectedDevice.empty()||request.selectedDevice==monitor.display.device)monitor.path=request.image;return sameWallpaperState(desired,*result.after);
}
}
struct WallpaperService::Impl {
 struct Undo {WallpaperSnapshot before,expected;};
 struct Job {WallpaperRequest request;WallpaperCompletion completion;bool inspectOnly=false;};
 ProviderProcessOptions options;Handle stopEvent;HWND receiver=nullptr;
 mutable std::mutex mutex;std::condition_variable cv;std::deque<Job> queue;std::vector<WallpaperCompletion> completions;std::thread thread;
 bool stopping=true;std::size_t outstanding=0,mutations=0;std::uint64_t next=0,activeScope=0,latestQuery=0,cachedScope=0;
 std::optional<WallpaperSnapshot> cached;std::vector<Monitor> cachedDisplays;std::optional<Undo> undo;
 explicit Impl(ProviderProcessOptions value):options(std::move(value)){if(options.executable.empty())options.executable=executablePath().parent_path()/L"AetherWallpaperProvider.exe";options.launchBreakaway=false;}
 std::uint64_t sequence(){if(++next==0)++next;return next;}
 bool available() const{return !stopping&&outstanding<8;}
 void publish(Job job){
  std::lock_guard lock(mutex);if(stopping)return;auto& completion=job.completion;auto& result=completion.result;
  if(completion.outcome==ProviderOutcome::Ok&&result.after){
   auto mapped=map(*result.after,completion.displays);
   if(!mapped){result.status=WallpaperStatus::Stale;result.message=L"Physical display mapping changed. Refresh to continue.";}
   else if(!job.inspectOnly&&completion.scope==activeScope&&(completion.kind!=WallpaperKind::Query||completion.id==latestQuery)){
    cached=*result.after;cachedDisplays=completion.displays;cachedScope=completion.scope;
   }
  }else if(!job.inspectOnly&&completion.scope==activeScope){cached.reset();cachedDisplays.clear();}
  if(result.status==WallpaperStatus::Ok&&completion.outcome==ProviderOutcome::Ok){
   if(completion.kind==WallpaperKind::Apply&&result.before&&result.after&&!sameWallpaperState(*result.before,*result.after))undo=Undo{*result.before,*result.after};
   else if(completion.kind==WallpaperKind::Undo)undo.reset();
  }
  completions.push_back(std::move(completion));if(receiver)PostMessageW(receiver,WallpaperMessage,0,0);
 }
 void run(){
  SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);ProviderClient provider(options);
  for(;;){
   Job job;
   {
    std::unique_lock lock(mutex);auto pending=[&]{return stopping||!queue.empty();};
    if(!pending()){
     if(provider.processId()){if(!cv.wait_for(lock,std::chrono::milliseconds(std::clamp<DWORD>(options.idleMs,200,10000)),pending)){lock.unlock();provider.close();continue;}}
     else cv.wait(lock,pending);
    }
    if(stopping)break;job=std::move(queue.front());queue.pop_front();
   }
   auto& completion=job.completion;completion.result.kind=completion.kind;
   try{
    auto payload=encodeWallpaperRequest(job.request);if(payload.empty())completion.outcome=ProviderOutcome::Malformed;
    else{
     auto reply=provider.call(static_cast<ProviderOperation>(8+static_cast<std::uint32_t>(completion.kind)),payload,stopEvent.value,completion.kind==WallpaperKind::Query?options.pinTimeoutMs:options.launchTimeoutMs);completion.outcome=reply.outcome;
     if(reply.outcome==ProviderOutcome::Ok){
      auto decoded=decodeWallpaperResult(reply.bytes);if(!decoded||!successfulReply(job.request,*decoded))completion.outcome=ProviderOutcome::Malformed;else completion.result=std::move(*decoded);
     }
    }
   }catch(...){completion.outcome=ProviderOutcome::Failed;}
   if(completion.outcome!=ProviderOutcome::Ok){
    completion.result.status=completion.kind==WallpaperKind::Query?WallpaperStatus::Unavailable:WallpaperStatus::Uncertain;
    completion.result.message=completion.kind==WallpaperKind::Query?L"Background query did not complete. Refresh to try again.":L"Background outcome is unknown. Refresh to inspect it; the change was not retried.";
    completion.result.before.reset();completion.result.after.reset();
   }
   publish(std::move(job));
  }
 }
};
WallpaperService::WallpaperService(ProviderProcessOptions options):impl_(std::make_unique<Impl>(std::move(options))){}
WallpaperService::~WallpaperService(){stop();}
void WallpaperService::start(HWND receiver){
 stop();auto& state=*impl_;state.stopEvent.reset(CreateEventW(nullptr,TRUE,FALSE,nullptr));if(!state.stopEvent)throw std::runtime_error("Could not create background cancellation event.");
 {std::lock_guard lock(state.mutex);state.receiver=receiver;state.stopping=false;}
 try{state.thread=std::thread([this]{impl_->run();});}catch(...){stop();throw;}
}
void WallpaperService::stop(){
 auto& state=*impl_;if(state.stopEvent)SetEvent(state.stopEvent.value);
 {std::lock_guard lock(state.mutex);state.stopping=true;state.queue.clear();}state.cv.notify_all();if(state.thread.joinable())state.thread.join();
 {std::lock_guard lock(state.mutex);state.completions.clear();state.receiver=nullptr;state.outstanding=state.mutations=0;state.cached.reset();state.cachedDisplays.clear();state.undo.reset();state.cachedScope=state.activeScope=state.latestQuery=0;}state.stopEvent.reset();
}
std::uint64_t WallpaperService::requestQuery(const std::vector<Monitor>& displays,std::uint64_t scope,bool inspectOnly){
 if(!scope||!displayMap(displays))return 0;auto& state=*impl_;std::lock_guard lock(state.mutex);if(!state.available()||state.mutations)return 0;
 Impl::Job job;job.inspectOnly=inspectOnly;job.completion.id=state.sequence();job.completion.scope=scope;job.completion.kind=WallpaperKind::Query;job.completion.displays=displays;
 auto id=job.completion.id;if(!inspectOnly){state.activeScope=scope;state.latestQuery=id;state.cached.reset();state.cachedDisplays.clear();}state.queue.push_back(std::move(job));++state.outstanding;state.cv.notify_one();return id;
}
std::uint64_t WallpaperService::requestApply(const std::vector<Monitor>& displays,std::uint64_t scope,const std::wstring& image,WallpaperFit fit,std::optional<std::wstring> displayName){
 if(!validWallpaperPath(image)||!scope||!displayMap(displays))return 0;auto& state=*impl_;std::lock_guard lock(state.mutex);
 if(!state.available()||state.mutations||!state.cached||scope!=state.cachedScope||scope!=state.activeScope||!sameDisplays(displays,state.cachedDisplays))return 0;
 auto targets=map(*state.cached,displays);if(!targets)return 0;WallpaperRequest request;request.kind=WallpaperKind::Apply;request.displays=*targets;request.image=image;request.fit=fit;
 if(displayName){auto found=std::find_if(displays.begin(),displays.end(),[&](const auto& display){return display.name==*displayName;});if(found==displays.end())return 0;request.selectedDevice=(*targets)[static_cast<std::size_t>(found-displays.begin())].device;}
 if(!validWallpaperRequest(request))return 0;Impl::Job job;job.request=std::move(request);job.completion.id=state.sequence();job.completion.scope=scope;job.completion.kind=WallpaperKind::Apply;job.completion.displays=displays;
 auto id=job.completion.id;state.queue.push_back(std::move(job));++state.outstanding;++state.mutations;state.cv.notify_one();return id;
}
std::uint64_t WallpaperService::requestUndo(const std::vector<Monitor>& displays,std::uint64_t scope){
 if(!scope||!displayMap(displays))return 0;auto& state=*impl_;std::lock_guard lock(state.mutex);if(!state.available()||state.mutations||!state.undo)return 0;
 auto targets=map(state.undo->expected,displays);if(!targets)return 0;WallpaperRequest request;request.kind=WallpaperKind::Undo;request.displays=*targets;request.before=state.undo->before;request.expected=state.undo->expected;
 if(!validWallpaperRequest(request))return 0;Impl::Job job;job.request=std::move(request);job.completion.id=state.sequence();job.completion.scope=scope;job.completion.kind=WallpaperKind::Undo;job.completion.displays=displays;
 auto id=job.completion.id;state.activeScope=scope;state.queue.push_back(std::move(job));++state.outstanding;++state.mutations;state.cv.notify_one();return id;
}
std::vector<WallpaperCompletion> WallpaperService::takeCompletions(){
 auto& state=*impl_;std::lock_guard lock(state.mutex);auto result=std::move(state.completions);state.completions.clear();
 for(const auto& completion:result){if(state.outstanding)--state.outstanding;if(completion.kind!=WallpaperKind::Query&&state.mutations)--state.mutations;}return result;
}
bool WallpaperService::canUndo() const{const auto& state=*impl_;std::lock_guard lock(state.mutex);return !state.stopping&&!state.mutations&&state.undo.has_value();}
}
