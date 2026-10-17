#include "status.hpp"
#include <netlistmgr.h>
#include <psapi.h>
namespace aether::win {
namespace {
ComPtr<IAudioEndpointVolume> endpoint(){ComPtr<IMMDeviceEnumerator> e;ComPtr<IMMDevice> d;ComPtr<IAudioEndpointVolume> v;if(SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&e)))&&SUCCEEDED(e->GetDefaultAudioEndpoint(eRender,eMultimedia,&d)))d->Activate(__uuidof(IAudioEndpointVolume),CLSCTX_INPROC_SERVER,nullptr,reinterpret_cast<void**>(v.GetAddressOf()));return v;}
}
Status readStatus(bool hour24,bool resources,bool queryExternal){Status s;SYSTEMTIME now{};GetLocalTime(&now);wchar_t date[80]{},time[80]{};GetDateFormatEx(LOCALE_NAME_USER_DEFAULT,0,&now,L"ddd, MMM d",date,80,nullptr);GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT,TIME_NOSECONDS,&now,hour24?L"HH:mm":nullptr,time,80);s.clock=std::wstring(date)+L"   "+time;
 if(queryExternal){
 auto v=endpoint();if(v){float level=0;BOOL muted=FALSE;if(SUCCEEDED(v->GetMasterVolumeLevelScalar(&level)))s.volume=static_cast<int>(level*100.f+0.5f);v->GetMute(&muted);s.muted=muted!=FALSE;}
 ComPtr<INetworkListManager> n;if(SUCCEEDED(CoCreateInstance(CLSID_NetworkListManager,nullptr,CLSCTX_ALL,IID_PPV_ARGS(&n)))){VARIANT_BOOL connected=VARIANT_FALSE;if(SUCCEEDED(n->get_IsConnectedToInternet(&connected)))s.network=connected?L"Online":L"Offline";}
 }
 SYSTEM_POWER_STATUS p{};if(GetSystemPowerStatus(&p)){if(p.BatteryFlag!=128&&p.BatteryLifePercent!=255)s.power=std::to_wstring(p.BatteryLifePercent)+L"%"+(p.ACLineStatus==1?L" +":L"");s.battery=p.BatteryFlag==128?L"AC power - no battery":s.power.empty()?L"Battery status unavailable":s.power+(p.ACLineStatus==1?L" - plugged in":L" - on battery");}else s.battery=L"Power status unavailable";
 static thread_local ULONGLONG previousIdle=0,previousTotal=0;
 if(resources){FILETIME idle{},kernel{},user{};auto ticks=[](FILETIME timeValue){return (static_cast<ULONGLONG>(timeValue.dwHighDateTime)<<32)|timeValue.dwLowDateTime;};
  if(GetSystemTimes(&idle,&kernel,&user)){const auto total=ticks(kernel)+ticks(user),idleNow=ticks(idle);s.resources=L"System CPU: measuring";if(previousTotal&&total>previousTotal&&idleNow>=previousIdle){const auto delta=total-previousTotal,busy=delta-std::min(delta,idleNow-previousIdle);s.resources=L"System CPU "+std::to_wstring(static_cast<unsigned>(100*busy/delta))+L"%";}previousIdle=idleNow;previousTotal=total;}
  PROCESS_MEMORY_COUNTERS_EX memory{};memory.cb=sizeof(memory);if(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory)))s.resources+=L"\nAether private "+std::to_wstring(memory.PrivateUsage/(1024*1024))+L" MiB";
 }else {previousIdle=0;previousTotal=0;}return s;}
bool setVolume(float level){auto v=endpoint();return v&&SUCCEEDED(v->SetMasterVolumeLevelScalar(std::clamp(level,0.f,1.f),nullptr));}
bool toggleMute(){auto v=endpoint();BOOL muted=FALSE;return v&&SUCCEEDED(v->GetMute(&muted))&&SUCCEEDED(v->SetMute(!muted,nullptr));}
}
