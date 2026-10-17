#pragma once
#include "windows.hpp"
#include <mmdeviceapi.h>
#include <endpointvolume.h>
namespace aether::win {
struct Status {std::wstring clock,network=L"Network",power,battery,resources;int volume=-1;bool muted=false;};
Status readStatus(bool clock24,bool resources=false);
bool setVolume(float level);bool toggleMute();
}
