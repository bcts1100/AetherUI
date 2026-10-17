#include "status.hpp"
#include <condition_variable>
#include <mutex>
#include <thread>
#include <chrono>
namespace aether::win {
struct StatusService::Impl {
 ProviderProcessOptions options;Handle stopEvent;HWND receiver=nullptr;
 std::mutex mutex;std::condition_variable cv;std::thread thread;
 bool stopping=true,poll=false,toggle=false;std::optional<int> volume;
 std::optional<StatusCompletion> completion;
 explicit Impl(ProviderProcessOptions value):options(std::move(value)){}
 void run(){
  SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);ProviderClient provider(options);
  for(;;){
   ProviderBytes payload{0,0};
   {std::unique_lock lock(mutex);auto pending=[&]{return stopping||poll||toggle||volume.has_value();};
    if(!pending()){
     if(provider.processId()){if(!cv.wait_for(lock,std::chrono::milliseconds(options.idleMs),pending)){lock.unlock();provider.close();continue;}}
     else cv.wait(lock,pending);
    }
    if(stopping)break;
    if(volume){payload={1,static_cast<std::uint8_t>(*volume)};volume.reset();}
    else if(toggle){payload={2,0};toggle=false;}
    poll=false;
   }
   StatusCompletion result;
   try{auto reply=provider.call(ProviderOperation::Status,payload,stopEvent.value,options.metadataTimeoutMs);result.outcome=reply.outcome;
    if(reply.outcome==ProviderOutcome::Ok){
     if(reply.bytes.size()!=3||reply.bytes[0]>101||reply.bytes[1]>1||reply.bytes[2]>2)result.outcome=ProviderOutcome::Malformed;
     else{result.value.volume=static_cast<int>(reply.bytes[0])-1;result.value.muted=reply.bytes[1]!=0;result.value.network=reply.bytes[2]==2?L"Online":reply.bytes[2]==1?L"Offline":L"Network";}
    }
   }catch(...){result.outcome=ProviderOutcome::Failed;}
   {std::lock_guard lock(mutex);if(stopping)break;completion=std::move(result);if(receiver)PostMessageW(receiver,StatusMessage,0,0);}
  }
 }
};
StatusService::StatusService(ProviderProcessOptions options):impl_(std::make_unique<Impl>(std::move(options))){}
StatusService::~StatusService(){stop();}
void StatusService::start(HWND receiver){stop();auto& s=*impl_;s.stopEvent.reset(CreateEventW(nullptr,TRUE,FALSE,nullptr));if(!s.stopEvent)throw std::runtime_error("Status cancellation event unavailable.");{std::lock_guard lock(s.mutex);s.receiver=receiver;s.stopping=false;}try{s.thread=std::thread([this]{impl_->run();});}catch(...){stop();throw;}}
void StatusService::stop(){auto& s=*impl_;if(s.stopEvent)SetEvent(s.stopEvent.value);{std::lock_guard lock(s.mutex);s.stopping=true;s.poll=s.toggle=false;s.volume.reset();}s.cv.notify_one();if(s.thread.joinable())s.thread.join();{std::lock_guard lock(s.mutex);s.receiver=nullptr;s.completion.reset();}s.stopEvent.reset();}
void StatusService::request(){auto& s=*impl_;{std::lock_guard lock(s.mutex);if(s.stopping)return;s.poll=true;}s.cv.notify_one();}
void StatusService::volume(int value){auto& s=*impl_;{std::lock_guard lock(s.mutex);if(s.stopping)return;s.volume=std::clamp(value,0,100);}s.cv.notify_one();}
void StatusService::mute(){auto& s=*impl_;{std::lock_guard lock(s.mutex);if(s.stopping)return;s.toggle=!s.toggle;s.poll=true;}s.cv.notify_one();}
std::optional<StatusCompletion> StatusService::takeCompletion(){auto& s=*impl_;std::lock_guard lock(s.mutex);auto result=std::move(s.completion);s.completion.reset();return result;}
}
