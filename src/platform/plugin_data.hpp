#pragma once
#include "provider_client.hpp"
namespace aether::win {
struct PluginSnapshot {
 std::wstring media=L"Nothing playing",weather=L"Seattle forecast\nWaiting for NWS";
 bool mediaAvailable=false,playing=false,weatherCached=false;
 std::uint64_t completed=0,failed=0;
};
class PluginData {
 struct Impl;std::unique_ptr<Impl> impl_;
public:
 explicit PluginData(HWND notify,ProviderProcessOptions options={});~PluginData();
 void configure(bool media,bool weather);void tick();void stop();
 bool command(std::wstring_view);PluginSnapshot snapshot() const;
};
}
