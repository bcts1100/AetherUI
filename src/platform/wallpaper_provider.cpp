#include "wallpaper_provider.hpp"
#include <cwctype>
namespace aether::win {
namespace {
struct TaskString {
 wchar_t* value=nullptr;~TaskString(){CoTaskMemFree(value);}
 bool copy(std::wstring& target,std::size_t limit){if(!value)return false;const auto count=wcsnlen_s(value,limit+1);if(count>limit)return false;target.assign(value,count);return true;}
};
bool readableImage(const std::wstring&);
class DesktopWallpaperBackend final:public WallpaperBackend {
 ComApartment apartment_;ComPtr<IDesktopWallpaper> wallpaper_;
public:
 DesktopWallpaperBackend(){if(SUCCEEDED(apartment_.result))CoCreateInstance(CLSID_DesktopWallpaper,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wallpaper_));}
 std::optional<WallpaperSnapshot> query() override{
  if(!wallpaper_)return {};WallpaperSnapshot snapshot;UINT count=0;DESKTOP_WALLPAPER_POSITION position=DWPOS_FILL;COLORREF background=0;DESKTOP_SLIDESHOW_STATE status{};
  if(wallpaper_->GetMonitorDevicePathCount(&count)!=S_OK||!count||count>WallpaperMonitorLimit||wallpaper_->GetPosition(&position)!=S_OK||wallpaper_->GetBackgroundColor(&background)!=S_OK||wallpaper_->GetStatus(&status)!=S_OK)return {};
  snapshot.fit=static_cast<WallpaperFit>(position);snapshot.color=background;snapshot.slideshow=(status&DSS_SLIDESHOW)!=0||(status&DSS_DISABLED_BY_REMOTE_SESSION)!=0;
  for(UINT i=0;i<count;++i){
   WallpaperMonitor monitor;TaskString device,image;RECT rectangle{};
   if(wallpaper_->GetMonitorDevicePathAt(i,&device.value)!=S_OK||!device.copy(monitor.display.device,WallpaperDeviceLimit))return {};
   const auto location=wallpaper_->GetMonitorRECT(device.value,&rectangle);if(location==S_FALSE)continue;if(location!=S_OK)return {};monitor.display.bounds=rect(rectangle);
   if(wallpaper_->GetWallpaper(device.value,&image.value)!=S_OK||!image.copy(monitor.path,WallpaperPathLimit))return {};snapshot.monitors.push_back(std::move(monitor));
  }
  if(!validWallpaperSnapshot(snapshot))return {};return snapshot;
 }
 bool restorable(const WallpaperSnapshot& snapshot) override{return std::all_of(snapshot.monitors.begin(),snapshot.monitors.end(),[](const auto& monitor){return readableImage(monitor.path);});}
 bool position(WallpaperFit value) override{return wallpaper_&&wallpaper_->SetPosition(static_cast<DESKTOP_WALLPAPER_POSITION>(value))==S_OK;}
 bool background(std::uint32_t value) override{return wallpaper_&&wallpaper_->SetBackgroundColor(value)==S_OK;}
 bool image(const std::wstring& device,const std::wstring& path) override{return wallpaper_&&wallpaper_->SetWallpaper(device.c_str(),path.c_str())==S_OK;}
};
bool readableImage(const std::wstring& path){
 if(!validWallpaperPath(path))return false;auto extension=std::filesystem::path(path).extension().wstring();std::transform(extension.begin(),extension.end(),extension.begin(),[](wchar_t c){return static_cast<wchar_t>(std::towlower(c));});
 if(extension!=L".png"&&extension!=L".jpg"&&extension!=L".jpeg"&&extension!=L".bmp")return false;
 Handle file(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));BY_HANDLE_FILE_INFORMATION information{};LARGE_INTEGER size{};
 return file&&GetFileInformationByHandle(file.value,&information)&&!(information.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))&&GetFileType(file.value)==FILE_TYPE_DISK&&GetFileSizeEx(file.value,&size)&&size.QuadPart>0&&size.QuadPart<=64LL*1024*1024;
}
}
ProviderWireReply runWallpaperOperation(ProviderOperation operation,const ProviderBytes& bytes){
 const auto code=static_cast<std::uint32_t>(operation);if(code<8||code>10)return {1,{}};auto request=decodeWallpaperRequest(bytes);
 if(!request||static_cast<std::uint32_t>(request->kind)+8!=code)return {1,{}};
 if(request->kind==WallpaperKind::Apply&&!readableImage(request->image)){WallpaperResult result;result.kind=request->kind;result.status=WallpaperStatus::Invalid;result.message=L"Choose a readable local PNG, JPEG or BMP image up to 64 MiB.";return {0,encodeWallpaperResult(result)};}
 DesktopWallpaperBackend backend;return {0,encodeWallpaperResult(executeWallpaper(backend,*request))};
}
}
