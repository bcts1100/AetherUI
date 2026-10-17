#include "plugin_provider.hpp"
#include "provider_codec.hpp"
#include <winhttp.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Data.Json.h>
#include <chrono>
#include <cmath>
#include <regex>
namespace aether::win {
namespace {
using namespace winrt::Windows::Media::Control;
std::wstring clean(std::wstring text,size_t limit){for(auto& c:text)if(c<32||c==127)c=L' ';if(text.size()>limit)text.resize(limit);if(!text.empty()&&text.back()>=0xD800&&text.back()<=0xDBFF)text.pop_back();return text;}
template<class Async> auto finish(Async async){if(async.wait_for(std::chrono::milliseconds(1100))!=winrt::Windows::Foundation::AsyncStatus::Completed){async.Cancel();throw std::runtime_error("Media request unavailable");}return async.GetResults();}
struct Apartment {Apartment(){winrt::init_apartment(winrt::apartment_type::multi_threaded);}~Apartment(){winrt::uninit_apartment();}};
struct Internet {HINTERNET handle=nullptr;explicit Internet(HINTERNET h):handle(h){}~Internet(){if(handle)WinHttpCloseHandle(handle);}Internet(const Internet&)=delete;};
std::optional<std::string> getNws(const std::wstring& path){
 Internet session(WinHttpOpen(L"AetherUI/0.8 Seattle desktop forecast",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));if(!session.handle)return {};
 if(!WinHttpSetTimeouts(session.handle,1500,1500,1500,2500))return {};
 Internet connection(WinHttpConnect(session.handle,L"api.weather.gov",INTERNET_DEFAULT_HTTPS_PORT,0));if(!connection.handle)return {};
 Internet request(WinHttpOpenRequest(connection.handle,L"GET",path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE));if(!request.handle)return {};
 DWORD redirects=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;if(!WinHttpSetOption(request.handle,WINHTTP_OPTION_REDIRECT_POLICY,&redirects,sizeof(redirects)))return {};
 const auto deadline=GetTickCount64()+5500;
 if(!WinHttpSendRequest(request.handle,L"Accept: application/geo+json\r\n",static_cast<DWORD>(-1),WINHTTP_NO_REQUEST_DATA,0,0,0)||!WinHttpReceiveResponse(request.handle,nullptr))return {};
 DWORD status=0,length=sizeof(status);if(!WinHttpQueryHeaders(request.handle,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&length,WINHTTP_NO_HEADER_INDEX)||status!=200)return {};
 std::string body;char buffer[8192];
 for(;;){if(GetTickCount64()>deadline)return {};DWORD read=0;if(!WinHttpReadData(request.handle,buffer,sizeof(buffer),&read))return {};if(!read)break;if(body.size()+read>128*1024)return {};body.append(buffer,read);}
 return body.empty()?std::nullopt:std::optional<std::string>(std::move(body));
}
ProviderWireReply media(ProviderOperation operation,const ProviderBytes& payload){
 if(operation==ProviderOperation::MediaQuery&&!payload.empty())return {1,{}};
 std::optional<std::wstring> command;std::wstring expectedSession;
 if(operation==ProviderOperation::MediaCommand){auto request=decodeProviderTarget(payload);if(!request||request->size()>512)return {1,{}};auto split=request->find(L'\n');if(split==request->npos||split+1==request->size())return {1,{}};command=request->substr(0,split);expectedSession=request->substr(split+1);if(*command!=L"toggle"&&*command!=L"previous"&&*command!=L"next")return {1,{}};}
 auto manager=finish(GlobalSystemMediaTransportControlsSessionManager::RequestAsync());auto session=manager.GetCurrentSession();
 if(!session)return operation==ProviderOperation::MediaQuery?ProviderWireReply{0,encodeProviderTarget(L"none\nNothing playing\nOpen a media app to get started")}:ProviderWireReply{1,{}};
 if(command){if(expectedSession!=std::wstring(session.SourceAppUserModelId()))return {1,{}};const bool success=*command==L"toggle"?finish(session.TryTogglePlayPauseAsync()):*command==L"previous"?finish(session.TrySkipPreviousAsync()):finish(session.TrySkipNextAsync());return {success?0u:1u,{}};}
 auto properties=finish(session.TryGetMediaPropertiesAsync());const auto info=session.GetPlaybackInfo();
 const bool playing=info&&info.PlaybackStatus()==GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
 const std::wstring title=clean(std::wstring(properties.Title()),180),artist=clean(std::wstring(properties.Artist()),120);
 const auto key=std::wstring(session.SourceAppUserModelId());if(key.empty()||key.size()>256||key.find(L'\n')!=key.npos||key.find(L'\r')!=key.npos)return {1,{}};
 return {0,encodeProviderTarget(std::wstring(playing?L"playing\n":L"paused\n")+(title.empty()?L"Media session":title)+L"\n"+artist+L"\n"+key)};
}
}
std::optional<std::wstring> weatherForecastPath(std::wstring_view url){
 const std::wstring prefix=L"https://api.weather.gov";if(url.size()>256||!url.starts_with(prefix))return {};
 auto path=std::wstring(url.substr(prefix.size()));static const std::wregex allowed(L"^/gridpoints/[A-Z]{3,4}/[0-9]{1,4},[0-9]{1,4}/forecast$");
 if(!std::regex_match(path,allowed))return {};return path;
}
std::optional<std::wstring> parseSeattleForecast(std::string_view body){
 try {if(body.empty()||body.size()>128*1024)return {};auto root=winrt::Windows::Data::Json::JsonObject::Parse(wide(std::string(body)));auto periods=root.GetNamedObject(L"properties").GetNamedArray(L"periods");if(!periods.Size()||periods.Size()>100)return {};
  auto period=periods.GetObjectAt(0);auto temperature=period.GetNamedNumber(L"temperature");auto unit=std::wstring(period.GetNamedString(L"temperatureUnit"));if(!std::isfinite(temperature)||temperature<-150||temperature>150||(unit!=L"F"&&unit!=L"C"))return {};
  auto summary=clean(std::wstring(period.GetNamedString(L"shortForecast")),120),name=clean(std::wstring(period.GetNamedString(L"name")),40);if(summary.empty()||name.empty())return {};
  return std::to_wstring(static_cast<int>(std::round(temperature)))+L"\u00b0"+unit+L"  "+summary+L"\n"+name+L" - NWS forecast";
 }catch(...){return {};}
}
ProviderWireReply runPluginProviderOperation(ProviderOperation operation,const ProviderBytes& payload){
 try {Apartment apartment;if(operation==ProviderOperation::MediaQuery||operation==ProviderOperation::MediaCommand)return media(operation,payload);
  if(operation!=ProviderOperation::SeattleForecast)return {1,{}};auto location=decodeProviderTarget(payload);if(!location||*location!=L"seattle")return {1,{}};
  auto points=getNws(L"/points/47.6062,-122.3321");if(!points)return {1,{}};auto root=winrt::Windows::Data::Json::JsonObject::Parse(wide(*points));auto path=weatherForecastPath(std::wstring(root.GetNamedObject(L"properties").GetNamedString(L"forecast")));if(!path)return {1,{}};
  auto body=getNws(*path);auto result=body?parseSeattleForecast(*body):std::nullopt;return result?ProviderWireReply{0,encodeProviderTarget(*result)}:ProviderWireReply{1,{}};
 }catch(...){return {1,{}};}
}
}
