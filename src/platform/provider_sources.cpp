#include <cmath>
#include "provider_sources.hpp"
#include "provider_codec.hpp"
#include "identity.hpp"
#include "plugin_provider.hpp"
#include "status.hpp"
#include <propkey.h>
#include <set>
#include <wincodec.h>
#include <propvarutil.h>
namespace aether::win {
namespace {
std::wstring shellString(IShellItem* item,SIGDN kind){PWSTR s=nullptr;if(FAILED(item->GetDisplayName(kind,&s)))return {};std::wstring out=s?s:L"";CoTaskMemFree(s);return out;}
std::wstring appsFolderTarget(IShellItem* item){

 auto child=shellString(item,SIGDN_PARENTRELATIVEPARSING);if(child.empty())return {};
 // AppsFolder can return a bare app ID even for desktop-absolute parsing.
 // The shell:AppsFolder form resolves packaged IDs; the CLSID form may fail.
 // Share this validated parsing target with Shell execution and image extraction.
 const std::wstring prefix=L"shell:AppsFolder\\";
 auto target=child.rfind(prefix,0)==0?child:prefix+child;
 PIDLIST_ABSOLUTE pidl=nullptr;
 HRESULT parsed=SHParseDisplayName(target.c_str(),nullptr,&pidl,0,nullptr);
 bool valid=SUCCEEDED(parsed)&&pidl;
 if(pidl)CoTaskMemFree(pidl);
 return valid?target:std::wstring{};
}
std::wstring appIdentity(IShellItem* item){ComPtr<IShellItem2> x;PWSTR text=nullptr;if(SUCCEEDED(item->QueryInterface(IID_PPV_ARGS(&x)))&&SUCCEEDED(x->GetString(PKEY_AppUserModel_ID,&text))){std::wstring s=text?text:L"";CoTaskMemFree(text);if(!s.empty())return L"aumid:"+lower(s);}return {};}

}
std::vector<Application> scanApplications(){std::vector<Application> apps;std::set<std::wstring> names;ComPtr<IShellItem> root;ComPtr<IEnumShellItems> items;
 if(SUCCEEDED(SHCreateItemInKnownFolder(FOLDERID_AppsFolder,0,nullptr,IID_PPV_ARGS(&root)))&&
    SUCCEEDED(root->BindToHandler(nullptr,BHID_EnumItems,IID_PPV_ARGS(&items)))){

  while(apps.size()<2048){
   ComPtr<IShellItem> item;ULONG got=0;if(items->Next(1,&item,&got)!=S_OK||!got)break;
   auto name=shellString(item.Get(),SIGDN_NORMALDISPLAY),target=appsFolderTarget(item.Get());
   if(name.empty()||target.empty()||!names.insert(lower(name)).second)continue;
   apps.push_back({name,target,appIdentity(item.Get())});
  }
 }
 for(const auto& id:{FOLDERID_Programs,FOLDERID_CommonPrograms}){PWSTR dir=nullptr;if(FAILED(SHGetKnownFolderPath(id,0,nullptr,&dir)))continue;std::filesystem::path path(dir);CoTaskMemFree(dir);std::error_code ec;std::filesystem::recursive_directory_iterator it(path,std::filesystem::directory_options::skip_permission_denied,ec),end;for(;it!=end&&apps.size()<2048;it.increment(ec)){if(ec){ec.clear();continue;}if(it.depth()>8){it.disable_recursion_pending();continue;}if(!it->is_regular_file(ec)||lower(it->path().extension().wstring())!=L".lnk")continue;auto pin=pinFromPath(it->path());if(!pin)continue;auto name=wide(pin->name);if(!names.insert(lower(name)).second)continue;apps.push_back({name,wide(pin->target),wide(pin->identity)});}}
 std::stable_sort(apps.begin(),apps.end(),[](const Application& a,const Application& b){return lower(a.name)<lower(b.name);});return apps;
}
namespace {
// Read one stable bounded local file snapshot. WIC and filesystem work stay in
// the disposable provider process; no image decode touches the controller.
std::shared_ptr<IconPixels> loadImage(IWICImagingFactory* wic,const std::wstring& path,bool workspace=false){
 if(!wic||path.size()<7||path.size()>4096||!((path[0]>=L'a'&&path[0]<=L'z')||(path[0]>=L'A'&&path[0]<=L'Z'))||path[1]!=L':'||(path[2]!=L'\\'&&path[2]!=L'/'))return {};
 for(std::size_t n=0;n<path.size();++n)if(path[n]<32||path[n]==127||(n!=1&&path[n]==L':'))return {};
 wchar_t root[]={path[0],L':',L'\\',0};auto drive=GetDriveTypeW(root);if(drive==DRIVE_REMOTE||drive==DRIVE_UNKNOWN||drive==DRIVE_NO_ROOT_DIR)return {};
 auto extension=lower(std::filesystem::path(path).extension().wstring());
 if(extension!=L".png"&&extension!=L".ico"&&extension!=L".jpg"&&extension!=L".jpeg"&&extension!=L".bmp")return {};
 Handle file(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_SEQUENTIAL_SCAN,nullptr));
 if(!file)return {};BY_HANDLE_FILE_INFORMATION information{};
 if(!GetFileInformationByHandle(file.value,&information)||(information.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)))return {};
 auto fileSize=(static_cast<std::uint64_t>(information.nFileSizeHigh)<<32)|information.nFileSizeLow;
 if(!fileSize||fileSize>8u*1024u*1024u)return {};
 std::vector<BYTE> encoded(static_cast<std::size_t>(fileSize));DWORD offset=0;
 while(offset<encoded.size()){DWORD read=0;if(!ReadFile(file.value,encoded.data()+offset,static_cast<DWORD>(encoded.size()-offset),&read,nullptr)||!read)return {};offset+=read;}
 ComPtr<IWICStream> stream;if(FAILED(wic->CreateStream(&stream))||FAILED(stream->InitializeFromMemory(encoded.data(),static_cast<DWORD>(encoded.size()))))return {};
 ComPtr<IWICBitmapDecoder> decoder;if(FAILED(wic->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnDemand,&decoder)))return {};
 GUID container{};if(FAILED(decoder->GetContainerFormat(&container))||
  (!IsEqualGUID(container,GUID_ContainerFormatPng)&&!IsEqualGUID(container,GUID_ContainerFormatIco)&&!IsEqualGUID(container,GUID_ContainerFormatJpeg)&&!IsEqualGUID(container,GUID_ContainerFormatBmp)))return {};
 UINT frames=0;if(FAILED(decoder->GetFrameCount(&frames))||!frames||frames>64)return {};
 ComPtr<IWICBitmapFrameDecode> frame;if(FAILED(decoder->GetFrame(0,&frame)))return {};
 UINT width=0,height=0;if(FAILED(frame->GetSize(&width,&height))||!width||!height||width>4096||height>4096||static_cast<std::uint64_t>(width)*height>16u*1024u*1024u)return {};
 auto bounded=boundedIconSize(static_cast<int>(width),static_cast<int>(height));if(workspace){const double factor=std::min({1.0,1024.0/std::max(width,height),std::sqrt(500000.0/(static_cast<double>(width)*height))});bounded.width=std::max(1,static_cast<int>(width*factor));bounded.height=std::max(1,static_cast<int>(height*factor));}ComPtr<IWICBitmapSource> source;if(FAILED(frame.As(&source)))return {};
 ComPtr<IWICBitmapScaler> scaler;if(width!=static_cast<UINT>(bounded.width)||height!=static_cast<UINT>(bounded.height)){
  if(FAILED(wic->CreateBitmapScaler(&scaler))||FAILED(scaler->Initialize(source.Get(),static_cast<UINT>(bounded.width),static_cast<UINT>(bounded.height),WICBitmapInterpolationModeFant))||FAILED(scaler.As(&source)))return {};
 }
 ComPtr<IWICFormatConverter> converter;if(FAILED(wic->CreateFormatConverter(&converter))||FAILED(converter->Initialize(source.Get(),GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom)))return {};
 auto image=std::make_shared<IconPixels>();image->width=static_cast<unsigned>(bounded.width);image->height=static_cast<unsigned>(bounded.height);
 image->bytes.resize(static_cast<std::size_t>(image->width)*image->height*4);
 if(FAILED(converter->CopyPixels(nullptr,image->width*4,static_cast<UINT>(image->bytes.size()),image->bytes.data())))return {};return image;
}
std::shared_ptr<IconPixels> loadIcon(IWICImagingFactory* wic,const std::wstring& path){
 if(!wic||path.empty())return {};
 ComPtr<IWICBitmap> bitmap;ComPtr<IShellItemImageFactory> imageFactory;HBITMAP handle=nullptr;
 if(SUCCEEDED(SHCreateItemFromParsingName(path.c_str(),nullptr,IID_PPV_ARGS(&imageFactory)))&&
    SUCCEEDED(imageFactory->GetImage({128,128},static_cast<SIIGBF>(SIIGBF_ICONONLY|SIIGBF_BIGGERSIZEOK),&handle))){
  wic->CreateBitmapFromHBITMAP(handle,nullptr,WICBitmapUsePremultipliedAlpha,&bitmap);DeleteObject(handle);
 }
 if(!bitmap){
  SHFILEINFOW info{};
  if(SHGetFileInfoW(path.c_str(),0,&info,sizeof(info),SHGFI_ICON|SHGFI_LARGEICON)&&info.hIcon){
   wic->CreateBitmapFromHICON(info.hIcon,&bitmap);DestroyIcon(info.hIcon);
  }
 }
 if(!bitmap)return {};
 UINT width=0,height=0;if(FAILED(bitmap->GetSize(&width,&height))||width>16384||height>16384)return {};
 auto bounded=boundedIconSize(static_cast<int>(width),static_cast<int>(height));
 if(!bounded.width||!bounded.height)return {};
 ComPtr<IWICBitmapSource> source;if(FAILED(bitmap.As(&source)))return {};
 ComPtr<IWICBitmapScaler> scaler;
 if(width!=static_cast<UINT>(bounded.width)||height!=static_cast<UINT>(bounded.height)){
  if(FAILED(wic->CreateBitmapScaler(&scaler))||FAILED(scaler->Initialize(source.Get(),
    static_cast<UINT>(bounded.width),static_cast<UINT>(bounded.height),WICBitmapInterpolationModeFant)))return {};
  if(FAILED(scaler.As(&source)))return {};
 }
 ComPtr<IWICFormatConverter> converter;
 if(FAILED(wic->CreateFormatConverter(&converter))||FAILED(converter->Initialize(source.Get(),
   GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom)))return {};
 auto image=std::make_shared<IconPixels>();image->width=static_cast<unsigned>(bounded.width);image->height=static_cast<unsigned>(bounded.height);
 image->bytes.resize(static_cast<std::size_t>(image->width)*image->height*4);
 if(FAILED(converter->CopyPixels(nullptr,image->width*4,static_cast<UINT>(image->bytes.size()),image->bytes.data())))return {};
 return image;
}

}
namespace {
std::uint64_t birthTime(HANDLE handle){FILETIME created{},exited{},kernel{},user{};if(!GetProcessTimes(handle,&created,&exited,&kernel,&user))return 0;return (static_cast<std::uint64_t>(created.dwHighDateTime)<<32)|created.dwLowDateTime;}
bool tokenAlive(const WindowMetadataRequest& token,HANDLE process){return process&&WaitForSingleObject(process,0)==WAIT_TIMEOUT&&windowAlive(reinterpret_cast<HWND>(static_cast<std::uintptr_t>(token.hwnd)),token.pid)&&birthTime(process)==token.birth;}
ProviderWireReply windowMetadata(const ProviderBytes& payload){
 auto requests=decodeWindowMetadataRequests(payload);if(!requests)return {1,{}};
 ComPtr<IVirtualDesktopManager> desktops;CoCreateInstance(CLSID_VirtualDesktopManager,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&desktops));
 std::vector<WindowMetadata> results;results.reserve(requests->size());
 for(const auto& token:*requests){
  WindowMetadata result;result.token=token;Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,token.pid));
  if(token.birth&&tokenAlive(token,process.value)){
   HWND window=reinterpret_cast<HWND>(static_cast<std::uintptr_t>(token.hwnd));ComPtr<IPropertyStore> properties;PROPVARIANT value{};
   if(SUCCEEDED(SHGetPropertyStoreForWindow(window,IID_PPV_ARGS(&properties)))){
    if(SUCCEEDED(properties->GetValue(PKEY_AppUserModel_ID,&value))&&value.vt==VT_LPWSTR&&value.pwszVal&&*value.pwszVal&&wcslen(value.pwszVal)<=1018)result.identity=L"aumid:"+lower(value.pwszVal);
    PropVariantClear(&value);
   }
   BOOL current=TRUE;if(desktops&&SUCCEEDED(desktops->IsWindowOnCurrentVirtualDesktop(window,&current))){result.desktopKnown=true;result.onCurrentDesktop=current!=FALSE;}
   if(!tokenAlive(token,process.value)){result.identity.clear();result.desktopKnown=false;}
  }
  results.push_back(std::move(result));
 }
 auto bytes=encodeWindowMetadata(results);return bytes.empty()?ProviderWireReply{1,{}}:ProviderWireReply{0,std::move(bytes)};
}
ProviderWireReply launchTarget(const ProviderBytes& payload){
 auto request=decodeLaunch(payload);if(!request)return {1,{}};
 // Strict providers may never launch into their one-process cleanup job.
 JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
 if(!QueryInformationJobObject(nullptr,JobObjectExtendedLimitInformation,&limits,sizeof(limits),nullptr)||!(limits.BasicLimitInformation.LimitFlags&JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK))return {1,{}};
 HWND owner=reinterpret_cast<HWND>(static_cast<std::uintptr_t>(request->owner));if(!windowAlive(owner,request->ownerPid))owner=nullptr;
 const bool activate=request->activate&&GetForegroundWindow()==reinterpret_cast<HWND>(static_cast<std::uintptr_t>(request->foreground));
 SHELLEXECUTEINFOW info{sizeof(info)};info.hwnd=owner;info.lpVerb=L"open";info.nShow=activate?SW_SHOWNORMAL:SW_SHOWNOACTIVATE;
 info.fMask=SEE_MASK_FLAG_NO_UI|SEE_MASK_NOASYNC|SEE_MASK_NOCLOSEPROCESS;PIDLIST_ABSOLUTE pidl=nullptr;
 if((request->target.rfind(L"::{",0)==0||request->target.rfind(L"shell:AppsFolder\\",0)==0)&&SUCCEEDED(SHParseDisplayName(request->target.c_str(),nullptr,&pidl,0,nullptr))&&pidl){info.fMask|=SEE_MASK_IDLIST;info.lpIDList=pidl;}else info.lpFile=request->target.c_str();
 const bool success=ShellExecuteExW(&info)!=FALSE;if(pidl)CoTaskMemFree(pidl);Handle launched(info.hProcess);
 return {success?0u:1u,{}};
}
}ProviderWireReply runProviderOperation(ProviderOperation operation,const ProviderBytes& payload){
 try{
  if(operation==ProviderOperation::MediaQuery||operation==ProviderOperation::MediaCommand||operation==ProviderOperation::SeattleForecast)return runPluginProviderOperation(operation,payload);
  ComApartment apartment;if(FAILED(apartment.result))return {1,{}};
  if(operation==ProviderOperation::Status){
   if(payload.size()!=2||payload[0]>2||(payload[0]==1?payload[1]>100:payload[1]!=0))return {1,{}};
   if(payload[0]==1&&!setVolume(payload[1]/100.f))return {1,{}};
   if(payload[0]==2&&!toggleMute())return {1,{}};
   const auto status=readStatus(true,false);
   return {0,{static_cast<std::uint8_t>(std::clamp(status.volume,-1,100)+1),static_cast<std::uint8_t>(status.muted?1:0),static_cast<std::uint8_t>(status.network==L"Online"?2:status.network==L"Offline"?1:0)}};
  }
  if(operation==ProviderOperation::PinIdentity){auto target=decodeProviderTarget(payload);if(!target)return {1,{}};auto pin=pinFromPath(*target);if(!pin)return {1,{}};auto bytes=encodePin(*pin);return bytes.empty()?ProviderWireReply{1,{}}:ProviderWireReply{0,std::move(bytes)};}
  if(operation==ProviderOperation::WindowMetadata)return windowMetadata(payload);
  if(operation==ProviderOperation::Launch)return launchTarget(payload);
  if(operation==ProviderOperation::Catalog){if(!payload.empty())return {1,{}};return {0,encodeApplications(scanApplications())};}
  if(operation==ProviderOperation::Icon||operation==ProviderOperation::Image||operation==ProviderOperation::WorkspaceImage){
   auto target=decodeProviderTarget(payload);if(!target)return {1,{}};
   ComPtr<IWICImagingFactory> wic;
   if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic))))return {1,{}};
   auto icon=operation!=ProviderOperation::Icon?loadImage(wic.Get(),*target,operation==ProviderOperation::WorkspaceImage):loadIcon(wic.Get(),*target);if(!icon)return {1,{}};
   auto bytes=operation==ProviderOperation::WorkspaceImage?encodeWorkspaceImage(icon.get()):encodeIcon(icon.get());if(bytes.empty())return {1,{}};return {0,std::move(bytes)};
  }
 }catch(...){return {1,{}};}
 return {1,{}};
}
}
