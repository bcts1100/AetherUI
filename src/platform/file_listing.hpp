#pragma once
#include "windows.hpp"
#include <mutex>
#include <thread>
namespace aether::win {
inline constexpr UINT FilesReadyMessage=WM_APP+153;
struct FileItem {std::wstring name;bool directory=false;std::uint64_t size=0,modified=0;};
struct FileListing {std::vector<FileItem> items;std::wstring error;bool limited=false;};
bool localDirectory(const std::wstring&);std::string listDirectoryWire(const std::wstring&);FileListing parseDirectoryWire(std::string_view);
class DirectoryQuery {
 Handle process_,job_,read_;std::thread thread_;std::mutex mutex_;FileListing result_;std::uint64_t generation_=0;
public:
 ~DirectoryQuery(){stop();}bool start(HWND,const std::wstring&,std::wstring* error=nullptr);void stop();FileListing take();std::uint64_t generation()const{return generation_;}
};
}
