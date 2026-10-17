#include "catalog.hpp"
#include "provider_codec.hpp"
namespace aether::win {
void Catalog::start(HWND receiver){
 stop();stopEvent_.reset(CreateEventW(nullptr,TRUE,FALSE,nullptr));if(!stopEvent_)throw std::runtime_error("Could not create catalog cancellation event.");
 stopping_=false;receiver_=receiver;{std::lock_guard lock(mutex_);requested_=true;}
 thread_=std::thread([this]{run();});
}
void Catalog::requestRescan(){{std::lock_guard lock(mutex_);requested_=true;}cv_.notify_one();}
void Catalog::stop(){
 if(stopEvent_)SetEvent(stopEvent_.value);{std::lock_guard lock(mutex_);stopping_=true;}cv_.notify_one();if(thread_.joinable())thread_.join();
 {std::lock_guard lock(mutex_);requested_=false;}indexing_=false;receiver_=nullptr;stopEvent_.reset();
}
std::shared_ptr<const std::vector<Application>> Catalog::snapshot(){std::lock_guard lock(mutex_);return result_;}
void Catalog::run(){
 SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);ProviderClient provider(options_);
 for(;;){
  {std::unique_lock lock(mutex_);cv_.wait(lock,[&]{return stopping_||requested_;});if(stopping_)break;requested_=false;}
  indexing_=true;std::shared_ptr<const std::vector<Application>> next;
  try{
   auto reply=provider.call(ProviderOperation::Catalog,{},stopEvent_.value,options_.catalogTimeoutMs);
   if(reply.outcome==ProviderOutcome::Ok)if(auto apps=decodeApplications(reply.bytes))next=std::make_shared<const std::vector<Application>>(std::move(*apps));
  }catch(...){}
  provider.close();indexing_=false;if(stopping_)break;
  // A malformed, failed or timed-out provider cannot erase the last-good catalog.
  if(next){{std::lock_guard lock(mutex_);result_=std::move(next);}PostMessageW(receiver_,CatalogMessage,0,0);}
 }
 indexing_=false;
}
}

