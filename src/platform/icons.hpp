#pragma once
#include "windows.hpp"
#include "icon_packs.hpp"
#include "provider_client.hpp"
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
namespace aether::win {
struct IconPixels {unsigned width{},height{};std::uint64_t serial{};std::vector<std::uint8_t> bytes;};
class IconCache {
 ProviderProcessOptions options_;Handle stopEvent_;std::string packId_;std::optional<IconPack> pack_;
 struct Entry {std::shared_ptr<const IconPixels> image;std::uint64_t touched{};bool pending=true;};
 struct Job {std::wstring key,target,imagePath;bool imageOnly=false,workspace=false;};
 std::mutex mutex_;std::condition_variable cv_;std::deque<Job> queue_;
 std::unordered_map<std::wstring,Entry> entries_;
 std::thread thread_;bool stopping_=false;HWND receiver_{};std::uint64_t tick_=0;
 std::shared_ptr<const IconPixels> enqueue(std::wstring key,std::wstring target,std::wstring imagePath={},bool imageOnly=false,bool workspace=false);
 void run();
public:
 explicit IconCache(ProviderProcessOptions options={}):options_(std::move(options)){}
 void setPack(const std::string& id);
 void start(HWND receiver);void stop();~IconCache(){stop();}
 std::shared_ptr<const IconPixels> request(const std::wstring& key,const std::wstring& target);
 std::shared_ptr<const IconPixels> request(const Pin& pin);
 bool workspaceImageFailed(const std::wstring& path);
 std::shared_ptr<const IconPixels> requestWorkspaceImage(const std::wstring& path);
 std::shared_ptr<const IconPixels> requestImage(const std::wstring& path,std::uint64_t revision=0);
};
}


