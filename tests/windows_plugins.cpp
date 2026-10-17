#include "platform/plugin_data.hpp"
#include "platform/plugin_provider.hpp"
#include "platform/provider_codec.hpp"
#include <iostream>
#include <stdexcept>
using namespace aether;
namespace {void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}}
int wmain(int argc,wchar_t** argv){
 if(argc==7)return win::runProviderHost(argc,argv,[](win::ProviderOperation operation,const win::ProviderBytes& bytes,std::wstring_view mode){
  if(mode==L"plugin-hang")Sleep(INFINITE);
  if(operation==win::ProviderOperation::MediaQuery)return win::ProviderWireReply{0,win::encodeProviderTarget(L"playing\nOwned track\nOwned artist\nowned-session")};
  if(operation==win::ProviderOperation::SeattleForecast)return win::ProviderWireReply{0,win::encodeProviderTarget(L"55F - owned weather fixture\nNo network request")};
  auto command=win::decodeProviderTarget(bytes);return win::ProviderWireReply{command&&*command==L"toggle\nowned-session"?0u:1u,{}};
 });
 try{
  win::ComApartment apartment;check(SUCCEEDED(apartment.result),"COM init failed");
  check(win::weatherForecastPath(L"https://api.weather.gov/gridpoints/SEW/125,67/forecast").has_value(),"Expected NWS endpoint rejected");
  for(const auto* bad:{L"https://api.weather.gov.evil/gridpoints/SEW/125,67/forecast",L"http://api.weather.gov/gridpoints/SEW/125,67/forecast",L"https://api.weather.gov/gridpoints/SEW/125,67/forecast?redirect=x",L"https://api.weather.gov/../../evil"})check(!win::weatherForecastPath(bad),"Untrusted weather target accepted");
  auto sample=win::parseSeattleForecast(R"({"properties":{"periods":[{"temperature":55,"temperatureUnit":"F","shortForecast":"Rain","name":"Tonight"}]}})");check(sample&&sample->find(L"Rain")!=sample->npos&&sample->find(L"NWS forecast")!=sample->npos,"Forecast fixture not decoded");
  check(!win::parseSeattleForecast(R"({"properties":{"periods":[]}})"),"Empty forecast accepted");
  check(!win::parseSeattleForecast(R"({"properties":{"periods":[{"temperature":999,"temperatureUnit":"F","shortForecast":"Rain","name":"Tonight"}]}})"),"Unbounded forecast temperature accepted");
  win::ProviderProcessOptions options;options.executable=win::executablePath();options.mode=L"plugin-fixture";
  win::PluginData data(nullptr,options);check(!data.command(L"toggle"),"Disabled data service accepted a media action");data.configure(true,true);
  const auto until=GetTickCount64()+5000;while(data.snapshot().completed<2&&GetTickCount64()<until)Sleep(10);
  auto snapshot=data.snapshot();check(snapshot.mediaAvailable&&snapshot.playing&&snapshot.weatherCached&&snapshot.media.find(L"Owned track")!=snapshot.media.npos,"Owned data worker did not publish media/weather");
  check(data.command(L"toggle")&&!data.command(L"execute"),"Typed media actions not enforced");
  data.configure(false,false);check(!data.command(L"next"),"Stopped worker accepted a late action");
  options.mode=L"plugin-hang";win::PluginData blocked(nullptr,options);blocked.configure(true,false);Sleep(100);const auto began=GetTickCount64();blocked.stop();check(GetTickCount64()-began<1500,"Cancellation of a blocked provider exceeded its bound");
  win::ProviderClient real;const auto invalid=real.call(win::ProviderOperation::MediaCommand,win::encodeProviderTarget(L"execute"),nullptr,3000);check(invalid.outcome==win::ProviderOutcome::Failed,"Actual provider accepted an unsupported media command");real.close();
  if(argc==2&&wcscmp(argv[1],L"--live-weather")==0){win::ProviderClient weather;const auto reply=weather.call(win::ProviderOperation::SeattleForecast,win::encodeProviderTarget(L"seattle"),nullptr,8000);check(reply.outcome==win::ProviderOutcome::Ok&&win::decodeProviderTarget(reply.bytes).has_value(),"Live Seattle NWS forecast unavailable");std::cout<<"Live Seattle NWS provider PASS\n";weather.close();}
  if(argc==2&&wcscmp(argv[1],L"--live-media")==0){win::ProviderClient media;auto query=media.call(win::ProviderOperation::MediaQuery,{},nullptr,3000);check(query.outcome==win::ProviderOutcome::Ok&&win::decodeProviderTarget(query.bytes).has_value(),"Actual Windows media session query unavailable");auto mismatch=media.call(win::ProviderOperation::MediaCommand,win::encodeProviderTarget(L"toggle\nAether-owned-intentionally-unmatched-session-key"),nullptr,3000);check(mismatch.outcome==win::ProviderOutcome::Failed,"Actual provider accepted a stale/different media source");std::cout<<"Live Windows media query and mismatched-source rejection PASS; no real playback control invoked\n";media.close();}
  std::cout<<"Windows plugins PASS: NWS parser/targets, owned provider round trip, disabled action guard and blocked-provider cancellation. No real media controls invoked.\n";return 0;
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
