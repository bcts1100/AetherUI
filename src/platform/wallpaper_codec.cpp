#include "wallpaper_codec.hpp"
#include <cstring>
namespace aether::win {
namespace {
constexpr std::uint32_t WireVersion=1;
struct Writer {
 ProviderBytes bytes;
 void number(std::uint32_t value){for(unsigned shift=0;shift<32;shift+=8)bytes.push_back(static_cast<std::uint8_t>(value>>shift));}
 bool string(std::wstring_view value,std::size_t limit){if(value.size()>limit||value.find(L'\0')!=std::wstring_view::npos)return false;number(static_cast<std::uint32_t>(value.size()));for(wchar_t c:value){auto unit=static_cast<std::uint16_t>(c);bytes.push_back(static_cast<std::uint8_t>(unit));bytes.push_back(static_cast<std::uint8_t>(unit>>8));}return bytes.size()<=ProviderPayloadLimit;}
 bool display(const WallpaperDisplay& value){if(!string(value.device,WallpaperDeviceLimit))return false;number(static_cast<std::uint32_t>(value.bounds.left));number(static_cast<std::uint32_t>(value.bounds.top));number(static_cast<std::uint32_t>(value.bounds.right));number(static_cast<std::uint32_t>(value.bounds.bottom));return true;}
 bool snapshot(const std::optional<WallpaperSnapshot>& value){
  number(value?1:0);if(!value)return true;if(!validWallpaperSnapshot(*value))return false;number(static_cast<std::uint32_t>(value->fit));number(value->color);number(value->slideshow?1:0);number(static_cast<std::uint32_t>(value->monitors.size()));
  for(const auto& monitor:value->monitors)if(!display(monitor.display)||!string(monitor.path,WallpaperPathLimit))return false;return true;
 }
};
struct Reader {
 const ProviderBytes& bytes;std::size_t offset=0;
 bool number(std::uint32_t& result){if(bytes.size()>ProviderPayloadLimit||offset>bytes.size()||bytes.size()-offset<4)return false;result=0;for(unsigned shift=0;shift<32;shift+=8)result|=static_cast<std::uint32_t>(bytes[offset++])<<shift;return true;}
 bool string(std::wstring& value,std::size_t limit){std::uint32_t count=0;if(!number(count)||count>limit||count>(bytes.size()-offset)/2)return false;value.clear();value.reserve(count);for(std::uint32_t i=0;i<count;++i){const auto unit=static_cast<std::uint16_t>(bytes[offset])|static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset+1])<<8);offset+=2;if(!unit)return false;value.push_back(static_cast<wchar_t>(unit));}return true;}
 bool display(WallpaperDisplay& value){std::uint32_t left=0,top=0,right=0,bottom=0;if(!string(value.device,WallpaperDeviceLimit)||!number(left)||!number(top)||!number(right)||!number(bottom))return false;value.bounds={static_cast<std::int32_t>(left),static_cast<std::int32_t>(top),static_cast<std::int32_t>(right),static_cast<std::int32_t>(bottom)};return true;}
 bool snapshot(std::optional<WallpaperSnapshot>& value){
  std::uint32_t present=0;if(!number(present)||present>1)return false;if(!present){value.reset();return true;}WallpaperSnapshot snapshot;std::uint32_t position=0,flags=0,count=0;
  if(!number(position)||position>5||!number(snapshot.color)||!number(flags)||flags>1||!number(count)||!count||count>WallpaperMonitorLimit)return false;snapshot.fit=static_cast<WallpaperFit>(position);snapshot.slideshow=flags!=0;snapshot.monitors.resize(count);
  for(auto& monitor:snapshot.monitors)if(!display(monitor.display)||!string(monitor.path,WallpaperPathLimit))return false;if(!validWallpaperSnapshot(snapshot))return false;value=std::move(snapshot);return true;
 }
};
}
ProviderBytes encodeWallpaperRequest(const WallpaperRequest& request){
 if(!validWallpaperRequest(request))return {};Writer writer;writer.number(WireVersion);writer.number(static_cast<std::uint32_t>(request.kind));writer.number(static_cast<std::uint32_t>(request.fit));writer.number(static_cast<std::uint32_t>(request.displays.size()));
 for(const auto& display:request.displays)if(!writer.display(display))return {};
 if(!writer.string(request.image,WallpaperPathLimit)||!writer.string(request.selectedDevice,WallpaperDeviceLimit)||!writer.snapshot(request.before)||!writer.snapshot(request.expected))return {};return std::move(writer.bytes);
}
std::optional<WallpaperRequest> decodeWallpaperRequest(const ProviderBytes& bytes){
 Reader reader{bytes};WallpaperRequest request;std::uint32_t version=0,kind=0,position=0,count=0;
 if(!reader.number(version)||version!=WireVersion||!reader.number(kind)||kind>2||!reader.number(position)||position>5||!reader.number(count)||count>WallpaperMonitorLimit)return {};
 request.kind=static_cast<WallpaperKind>(kind);request.fit=static_cast<WallpaperFit>(position);request.displays.resize(count);for(auto& display:request.displays)if(!reader.display(display))return {};
 if(!reader.string(request.image,WallpaperPathLimit)||!reader.string(request.selectedDevice,WallpaperDeviceLimit)||!reader.snapshot(request.before)||!reader.snapshot(request.expected)||reader.offset!=bytes.size()||!validWallpaperRequest(request))return {};return request;
}
ProviderBytes encodeWallpaperResult(const WallpaperResult& result){
 if(static_cast<std::uint32_t>(result.kind)>2||static_cast<std::uint32_t>(result.status)>6||result.message.size()>512)return {};Writer writer;writer.number(WireVersion);writer.number(static_cast<std::uint32_t>(result.kind));writer.number(static_cast<std::uint32_t>(result.status));
 if(!writer.snapshot(result.before)||!writer.snapshot(result.after)||!writer.string(result.message,512))return {};return std::move(writer.bytes);
}
std::optional<WallpaperResult> decodeWallpaperResult(const ProviderBytes& bytes){
 Reader reader{bytes};WallpaperResult result;std::uint32_t version=0,kind=0,status=0;if(!reader.number(version)||version!=WireVersion||!reader.number(kind)||kind>2||!reader.number(status)||status>6)return {};
 result.kind=static_cast<WallpaperKind>(kind);result.status=static_cast<WallpaperStatus>(status);
 if(!reader.snapshot(result.before)||!reader.snapshot(result.after)||!reader.string(result.message,512)||reader.offset!=bytes.size()||(result.status==WallpaperStatus::Ok&&!result.after))return {};return result;
}
}
