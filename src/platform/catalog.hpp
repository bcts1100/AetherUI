#pragma once
#include "windows.hpp"
#include "provider_client.hpp"
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
namespace aether::win {
struct Application {std::wstring name,target,identity;};
class Catalog {
 ProviderProcessOptions options_;Handle stopEvent_;
 std::mutex mutex_;std::condition_variable cv_;std::thread thread_;
 std::shared_ptr<const std::vector<Application>> result_=std::make_shared<const std::vector<Application>>();
 std::atomic<bool> stopping_{false};bool requested_=false;std::atomic<bool> indexing_{false};HWND receiver_{};
 void run();
public:
 explicit Catalog(ProviderProcessOptions options={}):options_(std::move(options)){}
 void start(HWND receiver);void requestRescan();void stop();~Catalog(){stop();}
 std::shared_ptr<const std::vector<Application>> snapshot();bool indexing() const{return indexing_.load();}
};
}


