#pragma once
#include "windows.hpp"
#include <atomic>
#include <mutex>
#include <thread>
namespace aether::win {
inline constexpr UINT PtyOutputMessage=WM_APP+151,PtyExitMessage=WM_APP+152;
std::wstring quoteArgument(std::wstring_view);
std::wstring shellCommand(int profile);std::wstring homeDirectory();
class PtySession {
 HPCON console_{};Handle input_,output_,process_,job_,inputReady_,stopEvent_;std::thread reader_,writer_;
 std::mutex mutex_;std::string pending_,inputQueue_;bool notified_=false;HWND owner_{};std::atomic_bool stopping_=false;
 void readLoop();void writeLoop();
public:
 PtySession()=default;~PtySession(){stop();}PtySession(const PtySession&)=delete;PtySession& operator=(const PtySession&)=delete;
 bool start(HWND owner,const std::wstring& command,const std::wstring& directory,int columns,int rows,std::wstring* error=nullptr);
 bool write(std::string_view);std::string takeOutput();bool resize(int columns,int rows);bool alive()const;DWORD pid()const;
 void stop()noexcept;
};
}
