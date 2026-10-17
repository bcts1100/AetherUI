#include "plugin_data.hpp"
#include "provider_codec.hpp"
#include <thread>
#include <mutex>
#include <deque>
namespace aether::win {
struct PluginData::Impl {
 struct Job {ProviderOperation operation;std::wstring argument;std::uint64_t generation;};
 mutable std::mutex mutex;HWND notify;ProviderProcessOptions options;PluginSnapshot value;std::wstring mediaKey;
 bool media=false,weather=false,quit=false;std::uint64_t generation=0;ULONGLONG lastMedia=0,lastWeather=0;
 std::deque<Job> jobs;std::shared_ptr<Handle> activeCancel;Handle stopEvent,wakeEvent;std::thread worker;
 Impl(HWND window,ProviderProcessOptions config):notify(window),options(std::move(config)){}
 ~Impl(){stop();}
 void queue(ProviderOperation operation,std::wstring argument){
  if(operation!=ProviderOperation::MediaCommand&&std::any_of(jobs.begin(),jobs.end(),[&](const auto& job){return job.operation==operation&&job.generation==generation;}))return;
  if(jobs.size()>=16)return;jobs.push_back({operation,std::move(argument),generation});SetEvent(wakeEvent.value);
 }
 void due(){const auto now=GetTickCount64();if(media&&(!lastMedia||now-lastMedia>=15000)){lastMedia=now;queue(ProviderOperation::MediaQuery,L"");}if(weather&&(!lastWeather||now-lastWeather>=900000)){lastWeather=now;queue(ProviderOperation::SeattleForecast,L"seattle");}}
 void run(){
  try {
  ProviderClient client(options);HANDLE waits[]{stopEvent.value,wakeEvent.value};DWORD idle=INFINITE;
  for(;;){const auto result=WaitForMultipleObjects(2,waits,FALSE,idle);if(result==WAIT_OBJECT_0||result==WAIT_FAILED)break;if(result==WAIT_TIMEOUT){client.close(0);idle=INFINITE;continue;}
   Job job{};std::shared_ptr<Handle> cancel;
   {std::lock_guard lock(mutex);if(quit)break;if(jobs.empty())continue;job=std::move(jobs.front());jobs.pop_front();if(!jobs.empty())SetEvent(wakeEvent.value);if(job.generation!=generation)continue;
    cancel=std::make_shared<Handle>(CreateEventW(nullptr,TRUE,FALSE,nullptr));if(!*cancel)continue;activeCancel=cancel;}
   const auto bytes=job.argument.empty()?ProviderBytes{}:encodeProviderTarget(job.argument);
   const auto reply=client.call(job.operation,bytes,cancel->value,job.operation==ProviderOperation::SeattleForecast?7000:2500);
   auto text=reply.outcome==ProviderOutcome::Ok&&!reply.bytes.empty()?decodeProviderTarget(reply.bytes):std::nullopt;
   HWND target=nullptr;
   {std::lock_guard lock(mutex);activeCancel.reset();if(quit||job.generation!=generation)continue;
    if(reply.outcome==ProviderOutcome::Ok)++value.completed;else ++value.failed;
    if(job.operation==ProviderOperation::MediaQuery){
     value.mediaAvailable=false;value.playing=false;mediaKey.clear();
     if(text&&text->size()<=700){const auto first=text->find(L'\n'),last=text->rfind(L'\n');if(first!=text->npos){auto state=text->substr(0,first);if(state==L"none"){value.media=text->substr(first+1);}else if((state==L"playing"||state==L"paused")&&last>first&&last+1<text->size()){mediaKey=text->substr(last+1);value.media=text->substr(first+1,last-first-1);value.mediaAvailable=true;value.playing=state==L"playing";}else value.media=L"Media unavailable";}else value.media=L"Media unavailable";}
     else value.media=L"Media unavailable";
    }else if(job.operation==ProviderOperation::SeattleForecast){if(text&&text->size()<=512){value.weather=*text;value.weatherCached=true;}else {if(!value.weatherCached)value.weather=L"Seattle forecast unavailable\nNWS will be retried in 15 minutes";else if(value.weather.find(L"Cached forecast")!=0)value.weather=L"Cached forecast - refresh unavailable\n"+value.weather;}}
    else if(media)queue(ProviderOperation::MediaQuery,L"");
    target=notify;
   }
   // Runtime stop joins this worker before the controller is destroyed.
   if(target&&IsWindow(target))PostMessageW(target,PluginDataMessage,0,0);idle=3000;
  }
  client.close(0);
  }catch(...){HWND target=nullptr;{std::lock_guard lock(mutex);quit=true;jobs.clear();activeCancel.reset();++value.failed;value.mediaAvailable=false;value.media=L"Media unavailable";value.weather=L"Widget data unavailable - disable and re-enable to retry";target=notify;}if(target&&IsWindow(target))PostMessageW(target,PluginDataMessage,0,0);}
 }
 void start(){stopEvent.reset(CreateEventW(nullptr,TRUE,FALSE,nullptr));wakeEvent.reset(CreateEventW(nullptr,FALSE,FALSE,nullptr));if(!stopEvent||!wakeEvent)throw std::runtime_error("Could not start widget data events");quit=false;worker=std::thread([this]{run();});}
 void stop(){
  {std::lock_guard lock(mutex);media=false;weather=false;++generation;jobs.clear();quit=true;if(activeCancel)SetEvent(activeCancel->value);if(stopEvent)SetEvent(stopEvent.value);if(wakeEvent)SetEvent(wakeEvent.value);}
  if(worker.joinable())worker.join();stopEvent.reset();wakeEvent.reset();
 }
};
PluginData::PluginData(HWND window,ProviderProcessOptions options):impl_(std::make_unique<Impl>(window,std::move(options))){}
PluginData::~PluginData()=default;
void PluginData::configure(bool media,bool weather){
 if(!media&&!weather){bool active;{std::lock_guard lock(impl_->mutex);active=impl_->worker.joinable();}if(active)impl_->stop();return;}
 std::lock_guard lock(impl_->mutex);if(impl_->media==media&&impl_->weather==weather)return;
 if(!impl_->worker.joinable())impl_->start();++impl_->generation;impl_->jobs.clear();if(impl_->activeCancel)SetEvent(impl_->activeCancel->value);
 impl_->media=media;impl_->weather=weather;impl_->lastMedia=0;impl_->due();
}
void PluginData::tick(){std::lock_guard lock(impl_->mutex);if(impl_->worker.joinable()&&!impl_->quit)impl_->due();}
void PluginData::stop(){impl_->stop();}
bool PluginData::command(std::wstring_view command){if(command!=L"toggle"&&command!=L"previous"&&command!=L"next")return false;std::lock_guard lock(impl_->mutex);if(!impl_->media||impl_->quit||!impl_->value.mediaAvailable||impl_->mediaKey.empty()||impl_->jobs.size()>=16)return false;impl_->queue(ProviderOperation::MediaCommand,std::wstring(command)+L"\n"+impl_->mediaKey);return true;}
PluginSnapshot PluginData::snapshot() const{std::lock_guard lock(impl_->mutex);return impl_->value;}
}
