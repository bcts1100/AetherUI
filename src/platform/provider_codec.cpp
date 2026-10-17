#include "provider_codec.hpp"
#include <cstring>
namespace aether::win {
namespace {
struct Writer {
 ProviderBytes bytes;
 void number(std::uint32_t value){const auto* raw=reinterpret_cast<const std::uint8_t*>(&value);bytes.insert(bytes.end(),raw,raw+sizeof(value));}
 void large(std::uint64_t value){number(static_cast<std::uint32_t>(value));number(static_cast<std::uint32_t>(value>>32));}
 bool string(std::wstring_view value,std::size_t limit,bool allowEmpty=false){
  if((!allowEmpty&&value.empty())||value.size()>limit||value.find(L'\0')!=std::wstring_view::npos
    ||bytes.size()+4+value.size()*sizeof(wchar_t)>ProviderPayloadLimit)return false;
  number(static_cast<std::uint32_t>(value.size()));const auto* raw=reinterpret_cast<const std::uint8_t*>(value.data());
  if(!value.empty())bytes.insert(bytes.end(),raw,raw+value.size()*sizeof(wchar_t));return true;
 }
};
struct Reader {
 const ProviderBytes& bytes;std::size_t offset=0;
 bool number(std::uint32_t& result){if(bytes.size()>ProviderPayloadLimit||offset>bytes.size()||bytes.size()-offset<4)return false;
  memcpy(&result,bytes.data()+offset,4);offset+=4;return true;}
 bool large(std::uint64_t& result){std::uint32_t lo=0,hi=0;if(!number(lo)||!number(hi))return false;result=static_cast<std::uint64_t>(lo)|(static_cast<std::uint64_t>(hi)<<32);return true;}
 bool string(std::wstring& result,std::size_t limit,bool allowEmpty=false){
  std::uint32_t count=0;if(!number(count)||count>limit||(!allowEmpty&&!count)||count>(bytes.size()-offset)/sizeof(wchar_t))return false;
  result.resize(count);if(count)memcpy(result.data(),bytes.data()+offset,count*sizeof(wchar_t));offset+=count*sizeof(wchar_t);
  return result.find(L'\0')==std::wstring::npos;
 }
};
}
ProviderBytes encodeApplications(const std::vector<Application>& apps){
 Writer writer;writer.number(0);std::uint32_t count=0;
 for(const auto& app:apps){if(count==2048)break;const auto before=writer.bytes.size();
  if(!writer.string(app.name,512)||!writer.string(app.target,4096)||!writer.string(app.identity,1024,true)){writer.bytes.resize(before);continue;}++count;}
 memcpy(writer.bytes.data(),&count,4);return std::move(writer.bytes);
}
std::optional<std::vector<Application>> decodeApplications(const ProviderBytes& bytes){
 Reader reader{bytes};std::uint32_t count=0;if(!reader.number(count)||count>2048)return std::nullopt;
 std::vector<Application> apps;apps.reserve(count);
 for(std::uint32_t i=0;i<count;++i){Application app;
  if(!reader.string(app.name,512)||!reader.string(app.target,4096)||!reader.string(app.identity,1024,true))return std::nullopt;
  apps.push_back(std::move(app));}
 if(reader.offset!=bytes.size())return std::nullopt;return apps;
}
ProviderBytes encodeIcon(const IconPixels* icon){
 if(!icon||!icon->width||!icon->height||icon->width>128||icon->height>128||icon->bytes.size()!=static_cast<std::size_t>(icon->width)*icon->height*4)return {};
 Writer writer;writer.number(icon->width);writer.number(icon->height);writer.bytes.insert(writer.bytes.end(),icon->bytes.begin(),icon->bytes.end());return std::move(writer.bytes);
}
std::shared_ptr<IconPixels> decodeIcon(const ProviderBytes& bytes){
 Reader reader{bytes};std::uint32_t width=0,height=0;
 if(!reader.number(width)||!reader.number(height)||!width||!height||width>128||height>128||bytes.size()-reader.offset!=static_cast<std::size_t>(width)*height*4)return {};
 auto result=std::make_shared<IconPixels>();result->width=width;result->height=height;
 result->bytes.assign(bytes.begin()+reader.offset,bytes.end());return result;
}
ProviderBytes encodeWorkspaceImage(const IconPixels* image){if(!image||!image->width||!image->height||image->width>1024||image->height>1024||static_cast<std::uint64_t>(image->width)*image->height>500000||image->bytes.size()!=static_cast<std::size_t>(image->width)*image->height*4)return {};Writer writer;writer.number(image->width);writer.number(image->height);writer.bytes.insert(writer.bytes.end(),image->bytes.begin(),image->bytes.end());return std::move(writer.bytes);}
std::shared_ptr<IconPixels> decodeWorkspaceImage(const ProviderBytes& bytes){Reader reader{bytes};std::uint32_t width=0,height=0;if(!reader.number(width)||!reader.number(height)||!width||!height||width>1024||height>1024||static_cast<std::uint64_t>(width)*height>500000||bytes.size()-reader.offset!=static_cast<std::size_t>(width)*height*4)return {};auto result=std::make_shared<IconPixels>();result->width=width;result->height=height;result->bytes.assign(bytes.begin()+reader.offset,bytes.end());return result;}
ProviderBytes encodeProviderTarget(std::wstring_view target){Writer writer;if(!writer.string(target,4096))return {};return std::move(writer.bytes);}
std::optional<std::wstring> decodeProviderTarget(const ProviderBytes& bytes){Reader reader{bytes};std::wstring target;
 if(!reader.string(target,4096)||reader.offset!=bytes.size())return std::nullopt;return target;}
namespace {
bool validToken(const WindowMetadataRequest& token){return token.hwnd&&token.pid&&token.epoch&&token.hwnd<=UINTPTR_MAX;}
void writeToken(Writer& writer,const WindowMetadataRequest& token){writer.large(token.hwnd);writer.large(token.birth);writer.large(token.epoch);writer.number(token.pid);}
bool readToken(Reader& reader,WindowMetadataRequest& token){std::uint32_t pid=0;if(!reader.large(token.hwnd)||!reader.large(token.birth)||!reader.large(token.epoch)||!reader.number(pid))return false;token.pid=pid;return validToken(token);}
}
ProviderBytes encodePin(const Pin& pin){
 Writer writer;if(!writer.string(wide(pin.name),512)||!writer.string(wide(pin.target),4096)||!writer.string(wide(pin.identity),1024,true)||
  (!pin.iconPath.empty()&&!validLocalIconPath(pin.iconPath))||!writer.string(wide(pin.iconPath),4096,true))return {};writer.large(pin.iconRevision);return std::move(writer.bytes);
}
std::optional<Pin> decodePin(const ProviderBytes& bytes){
 Reader reader{bytes};std::wstring name,target,identity,image;std::uint64_t revision=0;
 if(!reader.string(name,512)||!reader.string(target,4096)||!reader.string(identity,1024,true))return {};
 // Original three-string replies remain readable during profile migration.
 if(reader.offset!=bytes.size()&&(!reader.string(image,4096,true)||!reader.large(revision)))return {};
 if(reader.offset!=bytes.size())return {};auto iconPath=utf8(image);if(!iconPath.empty()&&!validLocalIconPath(iconPath))return {};
 return Pin{utf8(name),utf8(target),utf8(identity),std::move(iconPath),revision};
}
ProviderBytes encodeWindowMetadataRequests(const std::vector<WindowMetadataRequest>& values){Writer writer;if(values.empty()||values.size()>128)return {};writer.number(static_cast<std::uint32_t>(values.size()));for(const auto& token:values){if(!validToken(token))return {};writeToken(writer,token);}return std::move(writer.bytes);}
std::optional<std::vector<WindowMetadataRequest>> decodeWindowMetadataRequests(const ProviderBytes& bytes){Reader reader{bytes};std::uint32_t count=0;if(!reader.number(count)||!count||count>128)return {};std::vector<WindowMetadataRequest> values(count);for(auto& token:values)if(!readToken(reader,token))return {};if(reader.offset!=bytes.size())return {};return values;}
ProviderBytes encodeWindowMetadata(const std::vector<WindowMetadata>& values){Writer writer;if(values.empty()||values.size()>128)return {};writer.number(static_cast<std::uint32_t>(values.size()));for(const auto& value:values){if(!validToken(value.token))return {};writeToken(writer,value.token);if(!writer.string(value.identity,1024,true))return {};writer.number((value.desktopKnown?1u:0u)|(value.onCurrentDesktop?2u:0u));}return std::move(writer.bytes);}
std::optional<std::vector<WindowMetadata>> decodeWindowMetadata(const ProviderBytes& bytes){Reader reader{bytes};std::uint32_t count=0;if(!reader.number(count)||!count||count>128)return {};std::vector<WindowMetadata> values(count);for(auto& value:values){std::uint32_t flags=0;if(!readToken(reader,value.token)||!reader.string(value.identity,1024,true)||!reader.number(flags)||flags>3)return {};value.desktopKnown=(flags&1)!=0;value.onCurrentDesktop=(flags&2)!=0;}if(reader.offset!=bytes.size())return {};return values;}
ProviderBytes encodeLaunch(const LaunchRequest& value){Writer writer;if(value.owner>UINTPTR_MAX||value.foreground>UINTPTR_MAX||(value.owner&&!value.ownerPid)||(!value.owner&&value.ownerPid)||!writer.string(value.target,4096))return {};writer.large(value.owner);writer.large(value.foreground);writer.number(value.ownerPid);writer.number(value.activate?1u:0u);return std::move(writer.bytes);}
std::optional<LaunchRequest> decodeLaunch(const ProviderBytes& bytes){Reader reader{bytes};LaunchRequest value;std::uint32_t flags=0,pid=0;if(!reader.string(value.target,4096)||!reader.large(value.owner)||!reader.large(value.foreground)||!reader.number(pid)||!reader.number(flags)||flags>1||value.owner>UINTPTR_MAX||value.foreground>UINTPTR_MAX||(value.owner&&!pid)||(!value.owner&&pid)||reader.offset!=bytes.size())return {};value.ownerPid=pid;value.activate=flags!=0;return value;}
}
