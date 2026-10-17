#include "identity.hpp"
#include "windows.hpp"
#include <propkey.h>
#include <propvarutil.h>

namespace aether::win {
namespace {
std::wstring expandedPath(const std::wstring& value){
 DWORD size=ExpandEnvironmentStringsW(value.c_str(),nullptr,0);
 if(!size||size>32768)return value;
 std::wstring expanded(size,L'\0');
 DWORD written=ExpandEnvironmentStringsW(value.c_str(),expanded.data(),size);
 if(!written||written>size)return value;
 expanded.resize(written-1);return expanded;
}
std::wstring pathIdentity(const std::filesystem::path& path){
 std::error_code error;auto absolute=std::filesystem::absolute(path,error);
 auto value=(error?path:absolute).lexically_normal().wstring();
 // Process image queries normally omit the extended-length path prefix.
 if(value.rfind(L"\\\\?\\UNC\\",0)==0)value=L"\\\\"+value.substr(8);
 else if(value.rfind(L"\\\\?\\",0)==0)value.erase(0,4);
 value=lower(std::move(value));std::replace(value.begin(),value.end(),L'\\',L'/');return value;
}
std::wstring shortcutIdentity(IShellLinkW* link,const std::filesystem::path& shortcut){
 ComPtr<IPropertyStore> properties;
 if(SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&properties)))){
  PROPVARIANT value{};
  if(SUCCEEDED(properties->GetValue(PKEY_AppUserModel_ID,&value))){
   PWSTR text=nullptr;
   if(SUCCEEDED(PropVariantToStringAlloc(value,&text))&&text){
    std::wstring appId(text);CoTaskMemFree(text);PropVariantClear(&value);
    if(!appId.empty())return L"aumid:"+lower(std::move(appId));
   }else PropVariantClear(&value);
  }else PropVariantClear(&value);
 }
 std::vector<wchar_t> target(32768,L'\0');
 if(SUCCEEDED(link->GetPath(target.data(),static_cast<int>(target.size()),nullptr,SLGP_RAWPATH))&&target.front()){
  std::filesystem::path executable(expandedPath(target.data()));
  if(lower(executable.extension().wstring())==L".exe"){
   if(executable.is_relative())executable=shortcut.parent_path()/executable;
   return pathIdentity(executable);
  }
 }
 return pathIdentity(shortcut);
}
}
std::optional<Pin> pinFromPath(const std::filesystem::path& path){
 std::error_code error;
 if(path.empty()||!std::filesystem::is_regular_file(path,error)||error)return {};
 auto extension=lower(path.extension().wstring());
 if(extension!=L".exe"&&extension!=L".lnk")return {};
 auto identity=pathIdentity(path);
 if(extension==L".lnk"){
  // File-picker callers and catalog workers may use different COM apartments.
  // A changed apartment mode means the caller already initialized COM.
  ComApartment apartment;ComPtr<IShellLinkW> link;ComPtr<IPersistFile> file;
  if((SUCCEEDED(apartment.result)||apartment.result==RPC_E_CHANGED_MODE)&&
     SUCCEEDED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&link)))&&
     SUCCEEDED(link.As(&file))&&SUCCEEDED(file->Load(path.c_str(),STGM_READ)))
   identity=shortcutIdentity(link.Get(),path);
 }
 auto name=utf8(path.stem().wstring());
 if(name.empty())name="Application";
 return Pin{std::move(name),utf8(path.wstring()),utf8(identity)};
}
}