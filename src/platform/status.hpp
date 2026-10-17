#pragma once
#include "windows.hpp"
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include "provider_client.hpp"
#include <optional>
namespace aether::win {
struct Status {std::wstring clock,network=L"Network",power,battery,resources;int volume=-1;bool muted=false;};
Status readStatus(bool clock24,bool resources=false,bool queryExternal=true);
bool setVolume(float level);bool toggleMute();
struct StatusCompletion {ProviderOutcome outcome=ProviderOutcome::Unavailable;Status value;};
// Untrusted audio/network COM calls run in a cancellable provider process.
class StatusService {
 struct Impl;std::unique_ptr<Impl> impl_;
public:
 explicit StatusService(ProviderProcessOptions options={});~StatusService();
 void start(HWND);void stop();void request();void volume(int);void mute();
 std::optional<StatusCompletion> takeCompletion();
};
}
