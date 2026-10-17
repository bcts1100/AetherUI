#pragma once
#include "windows.hpp"
#include <functional>
#include <memory>
namespace aether::win {
using ProviderBytes=std::vector<std::uint8_t>;
constexpr std::uint32_t ProviderMagic=0x41455448,ProviderVersion=1;
constexpr std::size_t ProviderPayloadLimit=2*1024*1024;
enum class ProviderOperation:std::uint32_t {Catalog=1,Icon=2,Quit=3,PinIdentity=4,WindowMetadata=5,Launch=6,Image=7,WallpaperQuery=8,WallpaperApply=9,WallpaperUndo=10,MediaQuery=11,MediaCommand=12,SeattleForecast=13,WorkspaceImage=14,Status=15};
enum class ProviderOutcome {Ok,Failed,Unavailable,Cancelled,Timeout,Exited,Malformed};
struct ProviderResult {ProviderOutcome outcome=ProviderOutcome::Unavailable;ProviderBytes bytes;};
struct ProviderWireReply {std::uint32_t status=0;ProviderBytes bytes;};
struct ProviderProcessOptions {std::filesystem::path executable;std::wstring mode;DWORD catalogTimeoutMs=15000,iconTimeoutMs=2000,idleMs=3000;DWORD pinTimeoutMs=3000,metadataTimeoutMs=1200,launchTimeoutMs=8000;bool launchBreakaway=false;};
struct ProviderStatistics {std::uint64_t started=0,completed=0,timeouts=0,cancelled=0,failed=0;};
using ProviderHandler=std::function<ProviderWireReply(ProviderOperation,const ProviderBytes&,std::wstring_view)>;
class ProviderClient {
 struct Impl;std::unique_ptr<Impl> impl_;ProviderStatistics statistics_;
public:
 explicit ProviderClient(ProviderProcessOptions options={});~ProviderClient();
 ProviderClient(const ProviderClient&)=delete;ProviderClient& operator=(const ProviderClient&)=delete;
 ProviderResult call(ProviderOperation,const ProviderBytes&,HANDLE stop,DWORD timeout);
 bool prepare();
 void close(DWORD grace=250);DWORD processId() const;
 const ProviderStatistics& statistics() const{return statistics_;}
};
int runProviderHost(int argc,wchar_t** argv,const ProviderHandler& handler);
}
