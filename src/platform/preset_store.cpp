#include "preset_store.hpp"
#include <atomic>
#include <cstring>
#include <iomanip>
#include <sstream>
namespace aether::win {
namespace {
void fail(std::string* error,const char* reason){if(error)*error=reason;}
bool safeId(std::string_view id){
 if(id.size()!=13||id.substr(0,7)!="preset-")return false;
 bool nonzero=false;for(std::size_t i=7;i<id.size();++i){if(id[i]<'0'||id[i]>'9')return false;nonzero=nonzero||id[i]!='0';}return nonzero;
}
int hex(char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;}
std::string trim(std::string_view text){auto a=text.find_first_not_of(" \t\r");if(a==text.npos)return {};return std::string(text.substr(a,text.find_last_not_of(" \t\r")-a+1));}
bool regular(HANDLE file){FILE_ATTRIBUTE_TAG_INFO info{};return GetFileInformationByHandleEx(file,FileAttributeTagInfo,&info,sizeof(info))&&!(info.FileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT));}
bool write(HANDLE file,std::string_view text){DWORD written=0;return !text.empty()&&text.size()<=65536&&WriteFile(file,text.data(),static_cast<DWORD>(text.size()),&written,nullptr)&&written==text.size()&&FlushFileBuffers(file);}
}
std::optional<std::string> normalizePresetName(std::string_view name,std::string* error){
 auto bad=[&]()->std::optional<std::string>{fail(error,"Use 1-80 characters without line breaks or control characters.");return {};};
 if(name.empty()||name.size()>320)return bad();
 int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,name.data(),static_cast<int>(name.size()),nullptr,0);
 if(count<=0||count>80)return bad();
 std::wstring decoded(static_cast<std::size_t>(count),L'\0');
 if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,name.data(),static_cast<int>(name.size()),decoded.data(),count))return bad();
 for(wchar_t c:decoded)if(c<32||c==127||(c>=0x80&&c<=0x9f)||c==0x2028||c==0x2029)return bad();
 auto first=decoded.find_first_not_of(L" \x00A0\x3000");if(first==decoded.npos)return bad();
 auto normalized=utf8(decoded.substr(first,decoded.find_last_not_of(L" \x00A0\x3000")-first+1));
 if(error)error->clear();return normalized;
}
namespace detail {
std::string presetEncode(std::string_view value){
 std::string result;const char* digits="0123456789ABCDEF";
 for(unsigned char c:value){if(c<=32||c=='%'||c=='='||c=='['||c==']'){result+='%';result+=digits[c>>4];result+=digits[c&15];}else result+=static_cast<char>(c);}return result;
}
std::optional<std::string> presetDecode(std::string_view value){
 std::string result;
 for(std::size_t i=0;i<value.size();++i){if(value[i]=='%'){if(i+2>=value.size()||hex(value[i+1])<0||hex(value[i+2])<0)return {};result+=static_cast<char>(hex(value[i+1])*16+hex(value[i+2]));i+=2;}else result+=value[i];}return result;
}
std::optional<PresetIni> parsePresetIni(std::string_view text,std::string* error){
 auto bad=[&]()->std::optional<PresetIni>{fail(error,"Preset contains missing, duplicate or malformed data.");return {};};
 if(text.empty()||text.size()>65536||text.find('\0')!=text.npos)return bad();
 PresetIni result;std::string section;std::size_t start=0;unsigned lines=0;
 while(start<text.size()){
  if(++lines>1024)return bad();auto end=text.find('\n',start);if(end==text.npos)end=text.size();
  auto line=trim(text.substr(start,end-start));start=end+1;if(line.empty()||line[0]=='#'||line[0]==';')continue;
  if(line.size()>8192)return bad();
  if(line.front()=='['){if(line.size()<3||line.back()!=']')return bad();section=line.substr(1,line.size()-2);if(result.size()>=40||result.contains(section))return bad();result.emplace(section,std::map<std::string,std::string>{});continue;}
  auto equal=line.find('=');if(section.empty()||equal==line.npos||equal==0)return bad();
  auto key=trim(std::string_view(line).substr(0,equal));auto value=trim(std::string_view(line).substr(equal+1));
  if(key.empty()||result[section].contains(key))return bad();result[section].emplace(std::move(key),std::move(value));
 }
 if(error)error->clear();return result;
}
PresetStore::PresetStore(const wchar_t* folder,const wchar_t* extension,bool createFolder,std::string* error):extension_(extension){
 if(error)error->clear();try{directory_=configDirectory()/folder;}catch(const std::exception& e){if(error)*error=e.what();return;}
 auto attributes=GetFileAttributesW(directory_.c_str());
 if(attributes==INVALID_FILE_ATTRIBUTES){auto reason=GetLastError();if(!createFolder&&(reason==ERROR_FILE_NOT_FOUND||reason==ERROR_PATH_NOT_FOUND))return;
  if(!createFolder||(reason!=ERROR_FILE_NOT_FOUND&&reason!=ERROR_PATH_NOT_FOUND)||!CreateDirectoryW(directory_.c_str(),nullptr)){fail(error,"Cannot open the saved preset folder.");return;}}
 // No delete sharing: the library cannot be redirected while child paths are used.
 lock_.reset(CreateFileW(directory_.c_str(),FILE_READ_ATTRIBUTES|FILE_LIST_DIRECTORY,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
 FILE_ATTRIBUTE_TAG_INFO info{};
 if(!lock_||!GetFileInformationByHandleEx(lock_.value,FileAttributeTagInfo,&info,sizeof(info))||!(info.FileAttributes&FILE_ATTRIBUTE_DIRECTORY)||(info.FileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)){
  lock_.reset();fail(error,"Saved presets require a regular folder inside your settings profile.");}
}
std::filesystem::path PresetStore::path(std::string_view id) const{return directory_/(wide(id)+extension_);}
std::vector<std::string> PresetStore::ids() const{
 std::vector<std::string> result;if(!lock_)return result;WIN32_FIND_DATAW data{};
 HANDLE find=FindFirstFileW((directory_/(L"preset-*"+extension_)).c_str(),&data);if(find==INVALID_HANDLE_VALUE)return result;
 unsigned visited=0;do{if(++visited>512)break;if(data.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))continue;
  std::filesystem::path leaf(data.cFileName);auto id=utf8(leaf.stem().wstring());if(leaf.extension()==extension_&&safeId(id))result.push_back(std::move(id));
 }while(FindNextFileW(find,&data));FindClose(find);std::sort(result.begin(),result.end());return result;
}
std::optional<std::string> PresetStore::read(std::string_view id,std::string* error) const{
 if(!safeId(id)){fail(error,"Invalid saved preset ID.");return {};}
 if(!lock_){fail(error,"The saved preset folder is unavailable.");return {};}
 Handle file(CreateFileW(path(id).c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));LARGE_INTEGER size{};
 if(!file||!regular(file.value)||!GetFileSizeEx(file.value,&size)||size.QuadPart<=0||size.QuadPart>65536){fail(error,"Saved preset is missing, linked or larger than 64 KiB.");return {};}
 std::string text(static_cast<std::size_t>(size.QuadPart),'\0');DWORD read=0;
 if(!ReadFile(file.value,text.data(),static_cast<DWORD>(text.size()),&read,nullptr)||read!=text.size()){fail(error,"Cannot read the saved preset.");return {};}
 if(text.rfind("\xEF\xBB\xBF",0)==0)text.erase(0,3);if(error)error->clear();return text;
}
std::optional<std::string> PresetStore::create(std::string_view text,std::string* error) const{
 if(!lock_||text.empty()||text.size()>65536){fail(error,"Cannot save this preset (64 KiB maximum).");return {};}
 for(unsigned n=1;n<=1024;++n){std::ostringstream name;name<<"preset-"<<std::setfill('0')<<std::setw(6)<<n;auto id=name.str();auto destination=path(id);
  // CREATE_NEW never overwrites an existing file or follows a linked leaf.
  Handle file(CreateFileW(destination.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
  if(!file){if(GetLastError()==ERROR_FILE_EXISTS||GetLastError()==ERROR_ALREADY_EXISTS)continue;fail(error,"Cannot save this preset in your profile.");return {};}
  bool ok=write(file.value,text);file.reset();if(!ok){DeleteFileW(destination.c_str());fail(error,"Cannot finish saving this preset.");return {};}
  if(error)error->clear();return id;
 }
 fail(error,"No safe preset slot is available. Remove an unused preset first.");return {};
}
bool PresetStore::replace(std::string_view id,std::string_view text,std::string* error) const{
 // Validate the existing regular leaf before preparing an atomic replacement.
 if(!read(id,error))return false;
 if(text.empty()||text.size()>65536){fail(error,"Cannot save this preset (64 KiB maximum).");return false;}
 static std::atomic<unsigned> sequence=0;auto temp=path(id);temp+=L".pending-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(++sequence);
 Handle file(CreateFileW(temp.c_str(),GENERIC_WRITE|DELETE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
 if(!file){fail(error,"Cannot prepare the renamed preset.");return false;}
 if(!write(file.value,text)){file.reset();DeleteFileW(temp.c_str());fail(error,"Cannot prepare the renamed preset.");return false;}
 // Rename through the owned handle; no temporary filename can be swapped in.
 auto destination=path(id).wstring();std::vector<BYTE> buffer(sizeof(FILE_RENAME_INFO)+destination.size()*sizeof(wchar_t));
 auto* rename=reinterpret_cast<FILE_RENAME_INFO*>(buffer.data());rename->ReplaceIfExists=TRUE;rename->RootDirectory=nullptr;
 rename->FileNameLength=static_cast<DWORD>(destination.size()*sizeof(wchar_t));std::memcpy(rename->FileName,destination.data(),rename->FileNameLength);
 bool ok=SetFileInformationByHandle(file.value,FileRenameInfo,rename,static_cast<DWORD>(buffer.size()))!=FALSE;
 file.reset();if(!ok){DeleteFileW(temp.c_str());fail(error,"Cannot replace the renamed preset.");return false;}
 if(error)error->clear();return true;
}
bool PresetStore::remove(std::string_view id,std::string* error) const{
 if(!safeId(id)){fail(error,"Invalid saved preset ID.");return false;}if(!lock_){fail(error,"The saved preset folder is unavailable.");return false;}
 Handle file(CreateFileW(path(id).c_str(),DELETE|FILE_READ_ATTRIBUTES,0,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
 FILE_DISPOSITION_INFO disposition{TRUE};
 if(!file||!regular(file.value)||!SetFileInformationByHandle(file.value,FileDispositionInfo,&disposition,sizeof(disposition))){fail(error,"Cannot remove this saved preset.");return false;}
 if(error)error->clear();return true;
}
}
}
