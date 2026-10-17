#include "config.hpp"
#include <fstream>
#include <mutex>
namespace aether::win {
void setConfigDirectory(const std::filesystem::path& path){
 if(path.empty()){SetEnvironmentVariableW(L"AETHER_UI_SETTINGS_DIR",nullptr);return;}
 if(!path.is_absolute())throw std::runtime_error("Settings directory must be an absolute path.");
 if(!SetEnvironmentVariableW(L"AETHER_UI_SETTINGS_DIR",path.c_str()))throw std::runtime_error("Cannot set the settings directory.");
}
std::filesystem::path configDirectory(){
 std::vector<wchar_t> overridePath(32768);
 DWORD length=GetEnvironmentVariableW(L"AETHER_UI_SETTINGS_DIR",overridePath.data(),static_cast<DWORD>(overridePath.size()));
 if(length){
  if(length>=overridePath.size())throw std::runtime_error("Settings directory is too long.");
  std::filesystem::path result(std::wstring(overridePath.data(),length));
  if(!result.is_absolute())throw std::runtime_error("Settings directory must be an absolute path.");
  std::error_code ec;std::filesystem::create_directories(result,ec);
  if(ec)throw std::runtime_error("Cannot create the settings directory.");return result;
 }
 PWSTR path=nullptr;if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,KF_FLAG_DEFAULT,nullptr,&path)))throw std::runtime_error("Windows LocalAppData folder is unavailable.");std::filesystem::path result=std::filesystem::path(path)/L"AetherUI";CoTaskMemFree(path);std::error_code ec;std::filesystem::create_directories(result,ec);if(ec)throw std::runtime_error("Cannot create AetherUI settings directory.");return result;}
std::string readText(const std::filesystem::path& path,size_t limit){std::ifstream f(path,std::ios::binary|std::ios::ate);if(!f)return {};auto n=f.tellg();if(n<0||static_cast<std::uint64_t>(n)>limit)return {};std::string s(static_cast<size_t>(n),'\0');f.seekg(0);if(!s.empty()&&!f.read(s.data(),static_cast<std::streamsize>(s.size())))return {};if(s.rfind("\xEF\xBB\xBF",0)==0)s.erase(0,3);return s;}
void writeAtomic(const std::filesystem::path& path,const std::string& text){auto temp=path;temp+=L".tmp."+std::to_wstring(GetCurrentProcessId());Handle file(CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr));if(!file)throw std::runtime_error("Cannot write settings temporary file.");DWORD wrote=0;bool ok=WriteFile(file.value,text.data(),static_cast<DWORD>(text.size()),&wrote,nullptr)&&wrote==text.size()&&FlushFileBuffers(file.value);file.reset();if(!ok){DeleteFileW(temp.c_str());throw std::runtime_error("Failed to flush settings.");}if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){DeleteFileW(temp.c_str());throw std::runtime_error("Cannot replace settings file; check folder permissions.");}}
Config loadConfig(bool& first){auto dir=configDirectory();auto file=dir/L"settings-v3.ini";std::error_code ec;first=!std::filesystem::exists(file,ec);Config c;if(!first)c=parseSettings(readText(file));else{auto legacy=dir/L"settings.ini";c.defaults.iconSize=std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Desktop",L"IconSize",48,legacy.c_str())),32,72);wchar_t system[MAX_PATH]{};GetWindowsDirectoryW(system,MAX_PATH);std::wstring root(system);c.pins={{"Files",utf8(root+L"\\explorer.exe"),utf8(lower(root+L"\\explorer.exe"))},{"Terminal",utf8(root+L"\\System32\\WindowsPowerShell\\v1.0\\powershell.exe"),utf8(lower(root+L"\\System32\\WindowsPowerShell\\v1.0\\powershell.exe"))},{"Settings","ms-settings:",""}};}return c;}
void saveConfig(const Config& c){auto path=configDirectory()/L"settings-v3.ini";auto bak=path;bak+=L".bak";CopyFileW(path.c_str(),bak.c_str(),FALSE);writeAtomic(path,serializeSettings(c));}
void log(const std::string& s) noexcept{try{static std::mutex mutex;std::lock_guard lock(mutex);auto p=configDirectory()/L"aether.log";std::error_code ec;bool reset=std::filesystem::exists(p,ec)&&std::filesystem::file_size(p,ec)>1024*1024;std::ofstream f(p,std::ios::binary|(reset?std::ios::trunc:std::ios::app));SYSTEMTIME t{};GetLocalTime(&t);f<<t.wYear<<'-'<<t.wMonth<<'-'<<t.wDay<<' '<<t.wHour<<':'<<t.wMinute<<':'<<t.wSecond<<" "<<s<<'\n';}catch(...){}}
}
