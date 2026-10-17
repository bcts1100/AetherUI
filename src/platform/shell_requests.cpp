#include "shell_requests.hpp"
#include "provider_codec.hpp"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <chrono>
namespace aether::win {
struct ShellRequests::Impl {
 struct Job {ShellCompletion result;std::vector<WindowMetadataRequest> tokens;HWND foreground=nullptr;};
 ProviderProcessOptions options;Handle stopEvent;HWND receiver=nullptr;std::mutex mutex;std::condition_variable cv;
 std::deque<Job> launchQueue,pinQueue,metadataQueue;std::vector<ShellCompletion> completions;
 std::thread launchThread,metadataThread;bool stopping=true;std::uint64_t next=0;
 std::size_t launchOutstanding=0,pinOutstanding=0,metadataOutstanding=0;
 explicit Impl(ProviderProcessOptions value):options(std::move(value)){}
 bool available(ShellRequestKind kind) const {
  return !stopping&&completions.size()+launchQueue.size()+pinQueue.size()+metadataQueue.size()<126&&
   (kind==ShellRequestKind::Launch?launchOutstanding<32:kind==ShellRequestKind::Pin?pinOutstanding<64:metadataOutstanding<16);
 }
 std::uint64_t sequence(){if(++next==0)++next;return next;}
 void publish(ShellCompletion result){std::lock_guard lock(mutex);if(stopping)return;completions.push_back(std::move(result));if(receiver)PostMessageW(receiver,ShellMessage,0,0);}
 void run(bool launches){
  if(!launches)SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
  auto processOptions=options;processOptions.launchBreakaway=launches;ProviderClient provider(processOptions);
  for(;;){
   Job job;
   {
    std::unique_lock lock(mutex);auto pending=[&]{return stopping||(launches?!launchQueue.empty():!pinQueue.empty()||!metadataQueue.empty());};
    if(!pending()){
     if(provider.processId()){
      if(!cv.wait_for(lock,std::chrono::milliseconds(options.idleMs),pending)){lock.unlock();provider.close();continue;}
     }else cv.wait(lock,pending);
    }
    if(stopping)break;auto& queue=launches?launchQueue:!pinQueue.empty()?pinQueue:metadataQueue;job=std::move(queue.front());queue.pop_front();
   }
   try{
    ProviderBytes payload;ProviderOperation operation=ProviderOperation::Launch;DWORD deadline=options.launchTimeoutMs;
    if(job.result.kind==ShellRequestKind::Launch){
     bool activation=job.foreground&&GetForegroundWindow()==job.foreground;
     if(WaitForSingleObject(stopEvent.value,0)==WAIT_OBJECT_0)job.result.outcome=ProviderOutcome::Cancelled;
     else if(!provider.prepare())job.result.outcome=ProviderOutcome::Unavailable;
     else{
      if(activation)activation=AllowSetForegroundWindow(provider.processId())!=FALSE;
      LaunchRequest request{job.result.target,reinterpret_cast<std::uintptr_t>(job.result.owner),reinterpret_cast<std::uintptr_t>(job.foreground),job.result.ownerPid,activation};
      payload=encodeLaunch(request);
     }
    }else if(job.result.kind==ShellRequestKind::Pin){operation=ProviderOperation::PinIdentity;deadline=options.pinTimeoutMs;payload=encodeProviderTarget(job.result.target);}
    else {operation=ProviderOperation::WindowMetadata;deadline=options.metadataTimeoutMs;payload=encodeWindowMetadataRequests(job.tokens);}
    if(!payload.empty()){
     auto reply=provider.call(operation,payload,stopEvent.value,deadline);job.result.outcome=reply.outcome;
     if(reply.outcome==ProviderOutcome::Ok){
      if(job.result.kind==ShellRequestKind::Pin){job.result.pin=decodePin(reply.bytes);if(!job.result.pin||wide(job.result.pin->target)!=job.result.target){job.result.pin.reset();job.result.outcome=ProviderOutcome::Malformed;}}
      else if(job.result.kind==ShellRequestKind::Metadata){
       auto values=decodeWindowMetadata(reply.bytes);bool valid=values&&values->size()==job.tokens.size();
       if(valid)for(std::size_t i=0;i<values->size();++i)if((*values)[i].token!=job.tokens[i]){valid=false;break;}
       if(valid)job.result.metadata=std::move(*values);else job.result.outcome=ProviderOutcome::Malformed;
      }else if(!reply.bytes.empty())job.result.outcome=ProviderOutcome::Malformed;
     }
    }
   }catch(...){job.result.outcome=ProviderOutcome::Failed;}
   if(job.result.kind==ShellRequestKind::Metadata&&job.result.metadata.empty())for(const auto& token:job.tokens)job.result.metadata.push_back(WindowMetadata{token,{}});
   publish(std::move(job.result));
  }
 }
};
ShellRequests::ShellRequests(ProviderProcessOptions options):impl_(std::make_unique<Impl>(std::move(options))){}
ShellRequests::~ShellRequests(){stop();}
void ShellRequests::start(HWND receiver){
 stop();auto& state=*impl_;state.stopEvent.reset(CreateEventW(nullptr,TRUE,FALSE,nullptr));if(!state.stopEvent)throw std::runtime_error("Could not create Shell cancellation event.");
 {std::lock_guard lock(state.mutex);state.receiver=receiver;state.stopping=false;}
 try{state.launchThread=std::thread([this]{impl_->run(true);});state.metadataThread=std::thread([this]{impl_->run(false);});}catch(...){stop();throw;}
}
void ShellRequests::stop(){
 auto& state=*impl_;if(state.stopEvent)SetEvent(state.stopEvent.value);
 {std::lock_guard lock(state.mutex);state.stopping=true;state.launchQueue.clear();state.pinQueue.clear();state.metadataQueue.clear();}state.cv.notify_all();
 if(state.launchThread.joinable())state.launchThread.join();if(state.metadataThread.joinable())state.metadataThread.join();
 {std::lock_guard lock(state.mutex);state.completions.clear();state.receiver=nullptr;state.launchOutstanding=state.pinOutstanding=state.metadataOutstanding=0;}state.stopEvent.reset();
}
std::uint64_t ShellRequests::requestLaunch(HWND owner,const std::wstring& target){
 if(encodeProviderTarget(target).empty())return 0;auto& state=*impl_;std::lock_guard lock(state.mutex);if(!state.available(ShellRequestKind::Launch))return 0;
 DWORD ownerPid=0;if(owner)GetWindowThreadProcessId(owner,&ownerPid);if(ownerPid!=GetCurrentProcessId()){owner=nullptr;ownerPid=0;}
 Impl::Job job;job.result.kind=ShellRequestKind::Launch;job.result.id=state.sequence();job.result.target=target;job.result.owner=owner;job.result.ownerPid=ownerPid;job.foreground=GetForegroundWindow();
 auto id=job.result.id;state.launchQueue.push_back(std::move(job));++state.launchOutstanding;state.cv.notify_all();return id;
}
std::uint64_t ShellRequests::requestPin(const std::filesystem::path& path,std::uint64_t revision){
 auto target=path.wstring();if(encodeProviderTarget(target).empty())return 0;auto& state=*impl_;std::lock_guard lock(state.mutex);if(!state.available(ShellRequestKind::Pin))return 0;
 Impl::Job job;job.result.kind=ShellRequestKind::Pin;job.result.id=state.sequence();job.result.revision=revision;job.result.target=std::move(target);
 auto id=job.result.id;state.pinQueue.push_back(std::move(job));++state.pinOutstanding;state.cv.notify_all();return id;
}
bool ShellRequests::requestMetadata(std::vector<WindowMetadataRequest> tokens){
 if(encodeWindowMetadataRequests(tokens).empty())return false;auto& state=*impl_;std::lock_guard lock(state.mutex);if(!state.available(ShellRequestKind::Metadata))return false;
 Impl::Job job;job.result.kind=ShellRequestKind::Metadata;job.result.id=state.sequence();job.tokens=std::move(tokens);state.metadataQueue.push_back(std::move(job));++state.metadataOutstanding;state.cv.notify_all();return true;
}
std::vector<ShellCompletion> ShellRequests::takeCompletions(){
 auto& state=*impl_;std::lock_guard lock(state.mutex);auto result=std::move(state.completions);state.completions.clear();
 for(const auto& completion:result){auto& count=completion.kind==ShellRequestKind::Launch?state.launchOutstanding:completion.kind==ShellRequestKind::Pin?state.pinOutstanding:state.metadataOutstanding;if(count)--count;}return result;
}
}