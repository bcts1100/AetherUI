#include "extensions.hpp"
#include <atomic>
#include <cstring>
#include <iomanip>
#include <set>
#include <memory>
#include <sstream>
namespace aether::win {
namespace {
void fail(std::string* error,const char* reason){if(error)*error=reason;}
bool regular(HANDLE handle,bool directory=false){FILE_ATTRIBUTE_TAG_INFO info{};return GetFileInformationByHandleEx(handle,FileAttributeTagInfo,&info,sizeof(info))&&!(info.FileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)&&((info.FileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0)==directory;}
bool safeId(std::string_view id){if(id.size()!=13||!id.starts_with("preset-"))return false;bool nonzero=false;for(std::size_t i=7;i<id.size();++i){if(id[i]<'0'||id[i]>'9')return false;nonzero=nonzero||id[i]!='0';}return nonzero;}
struct DirectoryLease {
 std::vector<Handle> handles;bool ok=false;
 DirectoryLease()=default;
 explicit DirectoryLease(const std::filesystem::path& path){
  if(!path.is_absolute()||path.empty())return;auto normalized=path.lexically_normal();auto current=normalized.root_path();if(current.empty())return;
  auto hold=[&](const std::filesystem::path& p){Handle h(CreateFileW(p.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));if(!h||!regular(h.value,true))return false;handles.push_back(std::move(h));return true;};
  if(!hold(current))return;for(const auto& part:normalized.relative_path()){if(part==L".")continue;if(part==L"..")return;current/=part;if(!hold(current))return;}ok=true;
 }
};
struct Store {
 std::filesystem::path directory;std::unique_ptr<DirectoryLease> lease;bool newFolder=false,ok=false;
 explicit Store(std::string* error){
  try{auto profile=configDirectory().lexically_normal();DirectoryLease parent(profile);if(!parent.ok){fail(error,"Extensions require a regular profile without linked directories.");return;}directory=profile/L"extensions";
   auto attr=GetFileAttributesW(directory.c_str());if(attr==INVALID_FILE_ATTRIBUTES){auto reason=GetLastError();if((reason!=ERROR_FILE_NOT_FOUND&&reason!=ERROR_PATH_NOT_FOUND)||!CreateDirectoryW(directory.c_str(),nullptr)){fail(error,"Cannot create the local extension library.");return;}newFolder=true;}
   lease=std::make_unique<DirectoryLease>(directory);if(!lease->ok){fail(error,"The local extension library cannot be a linked folder.");return;}ok=true;
  }catch(const std::exception& e){if(error)*error=e.what();}
 }
 std::filesystem::path path(std::string_view id) const{return directory/(wide(id)+L".aetherplugin");}
};
std::optional<std::string> read(const std::filesystem::path& path,std::string* error){
 Handle file(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));LARGE_INTEGER size{};
 if(!file||!regular(file.value)||!GetFileSizeEx(file.value,&size)||size.QuadPart<=0||size.QuadPart>static_cast<LONGLONG>(ExtensionBytesLimit)){fail(error,"Extension must be a regular file of at most 64 KiB.");return {};}
 std::string result(static_cast<std::size_t>(size.QuadPart),'\0');DWORD got=0;if(!ReadFile(file.value,result.data(),static_cast<DWORD>(result.size()),&got,nullptr)||got!=result.size()){fail(error,"Cannot read this local extension.");return {};}if(error)error->clear();return result;
}
void discard(HANDLE file){FILE_DISPOSITION_INFO info{TRUE};SetFileInformationByHandle(file,FileDispositionInfo,&info,sizeof(info));}
bool atomic(const std::filesystem::path& destination,std::string_view text,bool replace,std::string* error){
 if(text.empty()||text.size()>ExtensionBytesLimit){fail(error,"Extension profile data is larger than 64 KiB.");return false;}
 static std::atomic<unsigned> sequence=0;auto temporary=destination.parent_path()/(L".aether-extension-pending-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(++sequence));
 Handle file(CreateFileW(temporary.c_str(),GENERIC_WRITE|DELETE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));if(!file){fail(error,"Cannot prepare an atomic extension write.");return false;}
 DWORD wrote=0;if(!WriteFile(file.value,text.data(),static_cast<DWORD>(text.size()),&wrote,nullptr)||wrote!=text.size()||!FlushFileBuffers(file.value)){discard(file.value);fail(error,"Cannot finish writing extension data.");return false;}
 auto target=destination.wstring();std::vector<BYTE> buffer(sizeof(FILE_RENAME_INFO)+target.size()*sizeof(wchar_t));auto* rename=reinterpret_cast<FILE_RENAME_INFO*>(buffer.data());rename->ReplaceIfExists=replace?TRUE:FALSE;rename->RootDirectory=nullptr;rename->FileNameLength=static_cast<DWORD>(target.size()*sizeof(wchar_t));std::memcpy(rename->FileName,target.data(),rename->FileNameLength);
 if(!SetFileInformationByHandle(file.value,FileRenameInfo,rename,static_cast<DWORD>(buffer.size()))){discard(file.value);fail(error,"Cannot commit the extension file; its destination is unavailable or already exists.");return false;}if(error)error->clear();return true;
}
bool selectedPath(const std::filesystem::path& path,std::string* error){
 if(!path.is_absolute()||path.filename().empty()||path.filename().wstring().find(L':')!=std::wstring::npos||lower(path.extension().wstring())!=L".aetherplugin"){fail(error,"Choose an absolute .aetherplugin file path.");return false;}return true;
}
}
bool ExtensionLibrary::load(std::string* error){
 entries_.clear();notice_.clear();if(error)error->clear();Store store(error);if(!store.ok){if(error)notice_=*error;return false;}
 WIN32_FIND_DATAW data{};HANDLE find=FindFirstFileW((store.directory/L"preset-*.aetherplugin").c_str(),&data);std::set<std::string> manifestIds;std::size_t active=0;
 if(find!=INVALID_HANDLE_VALUE){unsigned visited=0;do{
  if(++visited>512){notice_="The library scan stopped at its bounded file limit.";break;}if(data.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))continue;
  std::filesystem::path leaf(data.cFileName);auto id=utf8(leaf.stem().wstring());if(!safeId(id)||leaf.extension()!=L".aetherplugin")continue;
  auto text=read(store.path(id),nullptr);auto item=text?parseInstalledExtension(*text):std::nullopt;
  if(!item||!manifestIds.insert(item->manifest.id).second){notice_="Malformed or duplicate extension files were skipped and preserved.";continue;}
  if(entries_.size()>=ExtensionLimit){notice_="Only the first 24 valid local packages were loaded; other files were preserved.";break;}
  item->storageId=id;if(item->enabled){if(active+item->manifest.widgets.size()>ActiveWidgetLimit){item->enabled=false;notice_="An enabled package exceeded 48 active widgets and stayed inactive.";}else active+=item->manifest.widgets.size();}entries_.push_back(std::move(*item));
 }while(FindNextFileW(find,&data));FindClose(find);}
 std::sort(entries_.begin(),entries_.end(),[](const auto& a,const auto& b){return a.order==b.order?a.storageId<b.storageId:a.order<b.order;});
 if(store.newFolder&&!addExamples(error))return false;if(error)error->clear();return true;
}
std::optional<std::string> ExtensionLibrary::install(const ExtensionManifest& manifest,std::string* error){
 if(entries_.size()>=ExtensionLimit){fail(error,"The local library has 24 packages. Remove one before importing.");return {};}
 auto valid=parseExtension(serializeExtension(manifest),error);if(!valid||*valid!=manifest){fail(error,"Package data is not a valid bounded native manifest.");return {};}
 if(std::any_of(entries_.begin(),entries_.end(),[&](const auto& e){return e.manifest.id==manifest.id;})){fail(error,"A package with this ID is already in your local library.");return {};}
 Store store(error);if(!store.ok)return {};InstalledExtension entry;entry.manifest=manifest;entry.order=entries_.empty()?0:std::min(1023,entries_.back().order+1);auto text=serializeInstalledExtension(entry);
 for(unsigned n=1;n<=1024;++n){std::ostringstream name;name<<"preset-"<<std::setfill('0')<<std::setw(6)<<n;entry.storageId=name.str();auto path=store.path(entry.storageId);
  if(GetFileAttributesW(path.c_str())!=INVALID_FILE_ATTRIBUTES)continue;
  if(atomic(path,text,false,error)){entries_.push_back(entry);return entry.storageId;}if(GetFileAttributesW(path.c_str())!=INVALID_FILE_ATTRIBUTES)continue;return {};
 }fail(error,"No safe local package filename is available.");return {};
}
bool ExtensionLibrary::addExamples(std::string* error){for(const auto& sample:exampleExtensions()){if(std::any_of(entries_.begin(),entries_.end(),[&](const auto& e){return e.manifest.id==sample.id;}))continue;if(!install(sample,error))return false;}if(error)error->clear();return true;}
std::optional<std::string> ExtensionLibrary::importFile(const std::filesystem::path& path,std::string* error){
 if(!selectedPath(path,error))return {};auto selected=path.lexically_normal();DirectoryLease parent(selected.parent_path());if(!parent.ok){fail(error,"Import cannot follow a linked directory.");return {};}auto text=read(selected,error);auto manifest=text?parseExtension(*text,error):std::nullopt;return manifest?install(*manifest,error):std::nullopt;
}
bool ExtensionLibrary::exportFile(std::string_view id,const std::filesystem::path& path,std::string* error) const{
 auto found=std::find_if(entries_.begin(),entries_.end(),[&](const auto& e){return e.storageId==id;});if(found==entries_.end()||!safeId(id)){fail(error,"Select a loaded local extension to export.");return false;}if(!selectedPath(path,error))return false;auto selected=path.lexically_normal();
 try{if(lower(selected.parent_path().wstring())==lower((configDirectory().lexically_normal()/L"extensions").wstring())){fail(error,"Export to a file outside the managed extension library.");return false;}}catch(const std::exception& e){if(error)*error=e.what();return false;}DirectoryLease parent(selected.parent_path());if(!parent.ok){fail(error,"Export cannot follow a linked directory.");return false;}
 auto attributes=GetFileAttributesW(selected.c_str());bool replace=attributes!=INVALID_FILE_ATTRIBUTES;if(replace){Handle existing(CreateFileW(selected.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));if(!existing||!regular(existing.value)){fail(error,"Export cannot replace a linked or nonregular file.");return false;}}
 // An explicitly selected regular export destination is the only non-library file that may be replaced.
 return atomic(selected,serializeExtension(found->manifest),replace,error);
}
bool ExtensionLibrary::persist(const InstalledExtension& entry,std::string* error){
 if(!safeId(entry.storageId)){fail(error,"Invalid local library ID.");return false;}Store store(error);if(!store.ok)return false;auto before=read(store.path(entry.storageId),error);auto known=before?parseInstalledExtension(*before,error):std::nullopt;
 if(!known||known->manifest.id!=entry.manifest.id){fail(error,"The selected package changed on disk; its file was preserved.");return false;}auto text=serializeInstalledExtension(entry);if(!parseInstalledExtension(text,error))return false;return atomic(store.path(entry.storageId),text,true,error);
}
bool ExtensionLibrary::setEnabled(std::string_view id,bool enabled,std::string* error){
 auto found=std::find_if(entries_.begin(),entries_.end(),[&](const auto& e){return e.storageId==id;});if(found==entries_.end()||!safeId(id)){fail(error,"Select a loaded local extension.");return false;}
 if(enabled){std::size_t count=found->manifest.widgets.size();for(const auto& e:entries_)if(e.enabled&&e.storageId!=id)count+=e.manifest.widgets.size();if(count>ActiveWidgetLimit){fail(error,"This package would exceed the 48 active native widget limit.");return false;}}
 if(found->enabled==enabled){if(error)error->clear();return true;}auto changed=*found;changed.enabled=enabled;if(!persist(changed,error))return false;*found=std::move(changed);return true;
}
bool ExtensionLibrary::remove(std::string_view id,std::string* error){
 auto found=std::find_if(entries_.begin(),entries_.end(),[&](const auto& e){return e.storageId==id;});if(found==entries_.end()||!safeId(id)){fail(error,"Select a loaded local extension to remove.");return false;}Store store(error);if(!store.ok)return false;auto text=read(store.path(id),error);auto stored=text?parseInstalledExtension(*text,error):std::nullopt;if(!stored||stored->manifest.id!=found->manifest.id){fail(error,"The selected package changed on disk; its file was preserved.");return false;}
 Handle file(CreateFileW(store.path(id).c_str(),DELETE|FILE_READ_ATTRIBUTES,0,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));FILE_DISPOSITION_INFO deletion{TRUE};if(!file||!regular(file.value)||!SetFileInformationByHandle(file.value,FileDispositionInfo,&deletion,sizeof(deletion))){fail(error,"Cannot remove this local extension.");return false;}entries_.erase(found);if(error)error->clear();return true;
}
bool ExtensionLibrary::move(std::string_view id,int direction,std::string* error){
 auto found=std::find_if(entries_.begin(),entries_.end(),[&](const auto& e){return e.storageId==id;});if(found==entries_.end()||(direction!=-1&&direction!=1)){fail(error,"Select a package and an adjacent order direction.");return false;}auto index=static_cast<int>(found-entries_.begin()),next=index+direction;if(next<0||next>=static_cast<int>(entries_.size())){if(error)error->clear();return true;}
 auto updated=entries_;std::swap(updated[static_cast<std::size_t>(index)],updated[static_cast<std::size_t>(next)]);for(std::size_t i=0;i<updated.size();++i)updated[i].order=static_cast<int>(i);
 for(const auto& entry:updated)if(!persist(entry,error)){auto reason=error?*error:std::string{};load(nullptr);if(error)*error="Package ordering was only partly saved; the library was reloaded. "+reason;return false;}entries_=std::move(updated);if(error)error->clear();return true;
}
bool ExtensionLibrary::setNote(std::string_view id,std::string_view widgetId,std::string_view text,std::string* error){
 auto found=std::find_if(entries_.begin(),entries_.end(),[&](const auto& e){return e.storageId==id;});if(found==entries_.end()){fail(error,"Select a loaded note.");return false;}auto changed=*found;
 auto widget=std::find_if(changed.manifest.widgets.begin(),changed.manifest.widgets.end(),[&](const auto& w){return w.id==widgetId&&w.kind==WidgetKind::Note;});if(widget==changed.manifest.widgets.end()){fail(error,"This widget is not a note.");return false;}widget->text=std::string(text);
 auto validated=parseExtension(serializeExtension(changed.manifest),error);if(!validated||*validated!=changed.manifest)return false;if(!persist(changed,error))return false;*found=std::move(changed);return true;
}
bool ExtensionLibrary::setSize(std::string_view id,std::string_view widgetId,int width,int height,std::string* error){
 auto found=std::find_if(entries_.begin(),entries_.end(),[&](const auto& e){return e.storageId==id;});if(found==entries_.end()){fail(error,"Select a loaded native widget.");return false;}auto changed=*found;auto widget=std::find_if(changed.manifest.widgets.begin(),changed.manifest.widgets.end(),[&](const auto& w){return w.id==widgetId;});if(widget==changed.manifest.widgets.end()){fail(error,"The selected widget no longer exists.");return false;}widget->width=width;widget->height=height;auto validated=parseExtension(serializeExtension(changed.manifest),error);if(!validated||*validated!=changed.manifest)return false;if(!persist(changed,error))return false;*found=std::move(changed);return true;
}
bool ExtensionLibrary::setPlacement(std::string_view id,std::string_view widgetId,const WidgetPlacement& placement,std::string* error){
 auto found=std::find_if(entries_.begin(),entries_.end(),[&](const auto& e){return e.storageId==id;});if(found==entries_.end()){fail(error,"Select a loaded native widget.");return false;}auto changed=*found;auto widget=std::find_if(changed.manifest.widgets.begin(),changed.manifest.widgets.end(),[&](const auto& w){return w.id==widgetId;});if(widget==changed.manifest.widgets.end()){fail(error,"The selected widget no longer exists.");return false;}widget->placement=placement;
 auto validated=parseExtension(serializeExtension(changed.manifest),error);if(!validated||*validated!=changed.manifest)return false;if(!persist(changed,error))return false;*found=std::move(changed);return true;
}
}
