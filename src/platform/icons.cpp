#include "icons.hpp"
#include "provider_codec.hpp"
#include <chrono>
#include <atomic>
namespace aether::win {
void IconCache::setPack(const std::string& id){if(packId_==id)return;packId_=id;pack_.reset();if(!id.empty())for(auto& pack:iconPacks())if(pack.id==id){pack_=std::move(pack);break;}}
void IconCache::start(HWND receiver){
 stop();stopEvent_.reset(CreateEventW(nullptr,TRUE,FALSE,nullptr));if(!stopEvent_)throw std::runtime_error("Could not create icon cancellation event.");
 {std::lock_guard lock(mutex_);receiver_=receiver;stopping_=false;}thread_=std::thread([this]{run();});
}
void IconCache::stop(){
 if(stopEvent_)SetEvent(stopEvent_.value);{std::lock_guard lock(mutex_);stopping_=true;queue_.clear();}cv_.notify_one();
 if(thread_.joinable())thread_.join();{std::lock_guard lock(mutex_);entries_.clear();receiver_=nullptr;}stopEvent_.reset();
}
namespace {
std::atomic<std::uint64_t> imageSerial{0};
std::wstring imageKey(std::wstring_view target,std::wstring_view image,std::uint64_t revision){
 auto normalized=[](std::wstring_view value){auto out=lower(std::wstring(value));std::replace(out.begin(),out.end(),L'\\',L'/');return out;};
 return L"decor:"+std::to_wstring(target.size())+L":"+normalized(target)+L":"+normalized(image)+L":"+std::to_wstring(revision);
}
}
std::shared_ptr<const IconPixels> IconCache::request(const std::wstring& key,const std::wstring& target){return enqueue(key,target);}
std::shared_ptr<const IconPixels> IconCache::request(const Pin& pin){
 auto target=wide(pin.target);if(!validLocalIconPath(pin.iconPath)){if(pack_){const auto path=packIcon(*pack_,pin);if(!path.empty()){auto image=enqueue(L"pack:"+wide(packId_)+L":"+path.wstring(),target,path.wstring());return image?image:request(target,target);}}return request(target,target);}
 auto imagePath=wide(pin.iconPath);auto custom=enqueue(imageKey(target,imagePath,pin.iconRevision),target,imagePath);
 return custom?custom:request(target,target);
}
bool IconCache::workspaceImageFailed(const std::wstring& path){std::lock_guard lock(mutex_);auto it=entries_.find(L"wall:"+path);return it!=entries_.end()&&!it->second.pending&&!it->second.image;}
std::shared_ptr<const IconPixels> IconCache::requestWorkspaceImage(const std::wstring& path){if(path.empty()||path.size()>4096)return {};return enqueue(L"wall:"+path,{},path,true,true);}
std::shared_ptr<const IconPixels> IconCache::requestImage(const std::wstring& path,std::uint64_t revision){
 if(path.empty()||path.size()>4096)return {};return enqueue(imageKey({},path,revision),{},path,true);
}
std::shared_ptr<const IconPixels> IconCache::enqueue(std::wstring key,std::wstring target,std::wstring imagePath,bool imageOnly,bool workspace){
 if(key.empty()||key.size()>12320||(target.empty()&&imagePath.empty())||target.size()>4096||imagePath.size()>4096)return {};
 std::lock_guard lock(mutex_);auto it=entries_.find(key);if(it!=entries_.end()){it->second.touched=++tick_;return it->second.image;}if(stopping_||queue_.size()>=128)return {};
 if(workspace){std::size_t count=0;auto oldest=entries_.end();for(auto it2=entries_.begin();it2!=entries_.end();++it2)if(it2->first.starts_with(L"wall:")){++count;if(!it2->second.pending&&(oldest==entries_.end()||it2->second.touched<oldest->second.touched))oldest=it2;}if(count>=4){if(oldest==entries_.end())return {};entries_.erase(oldest);}}
 if(entries_.size()>=192){auto old=entries_.end();for(auto e=entries_.begin();e!=entries_.end();++e)if(!e->second.pending&&(old==entries_.end()||e->second.touched<old->second.touched))old=e;if(old!=entries_.end())entries_.erase(old);else return {};}
 entries_.emplace(key,Entry{nullptr,++tick_,true});queue_.push_back({std::move(key),std::move(target),std::move(imagePath),imageOnly,workspace});cv_.notify_one();return {};
}
void IconCache::run(){
 SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);ProviderClient provider(options_);
 for(;;){
  Job job;
  {
   std::unique_lock lock(mutex_);
   if(queue_.empty()){
    if(provider.processId()){
     if(!cv_.wait_for(lock,std::chrono::milliseconds(options_.idleMs),[&]{return stopping_||!queue_.empty();})){
      lock.unlock();provider.close();continue;
     }
    }else cv_.wait(lock,[&]{return stopping_||!queue_.empty();});
   }
   if(stopping_)break;job=std::move(queue_.front());queue_.pop_front();
  }
  std::shared_ptr<IconPixels> image;
  try{
   if(!job.imagePath.empty()){
    auto payload=encodeProviderTarget(job.imagePath);
    if(!payload.empty()){auto reply=provider.call(job.workspace?ProviderOperation::WorkspaceImage:ProviderOperation::Image,payload,stopEvent_.value,options_.iconTimeoutMs);if(reply.outcome==ProviderOutcome::Ok)image=job.workspace?decodeWorkspaceImage(reply.bytes):decodeIcon(reply.bytes);}
   }
   if(!image&&!job.imageOnly&&WaitForSingleObject(stopEvent_.value,0)!=WAIT_OBJECT_0){
    auto payload=encodeProviderTarget(job.target);
    if(!payload.empty()){auto reply=provider.call(ProviderOperation::Icon,payload,stopEvent_.value,options_.iconTimeoutMs);if(reply.outcome==ProviderOutcome::Ok)image=job.workspace?decodeWorkspaceImage(reply.bytes):decodeIcon(reply.bytes);}
   }
  }catch(...){}
  if(image)image->serial=++imageSerial;
  {
   std::lock_guard lock(mutex_);auto it=entries_.find(job.key);
   if(it!=entries_.end()){it->second.image=std::move(image);it->second.pending=false;}
   if(!stopping_)PostMessageW(receiver_,IconsMessage,0,0);
  }
 }
}
}
