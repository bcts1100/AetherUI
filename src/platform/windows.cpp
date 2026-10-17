#include "windows.hpp"
namespace aether::win {
std::wstring wide(std::string_view s){if(s.empty())return {};int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);if(!n)return {};std::wstring r(n,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),r.data(),n);return r;}
std::string utf8(std::wstring_view s){if(s.empty())return {};int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);if(!n)return {};std::string r(n,'\0');WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),r.data(),n,nullptr,nullptr);return r;}
std::wstring lower(std::wstring s){if(!s.empty())CharLowerBuffW(s.data(),static_cast<DWORD>(s.size()));return s;}
std::filesystem::path executablePath(){std::vector<wchar_t> b(32768);DWORD n=GetModuleFileNameW(nullptr,b.data(),static_cast<DWORD>(b.size()));return n?std::filesystem::path(std::wstring(b.data(),n)):std::filesystem::path{};}
std::wstring windowText(HWND h){int n=std::min(GetWindowTextLengthW(h),8192);if(n<=0)return {};std::wstring s(n+1,L'\0');n=GetWindowTextW(h,s.data(),n+1);s.resize(std::max(0,n));return s;}
std::wstring processPath(DWORD pid){Handle h(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid));if(!h)return {};std::wstring s(32768,L'\0');DWORD n=static_cast<DWORD>(s.size());if(!QueryFullProcessImageNameW(h.value,0,s.data(),&n))return {};s.resize(n);return s;}
bool windowAlive(HWND h,DWORD pid){DWORD actual=0;if(!IsWindow(h)||!pid)return false;GetWindowThreadProcessId(h,&actual);return actual==pid;}
bool openTarget(HWND owner,const std::wstring& target){if(target.empty())return false;SHELLEXECUTEINFOW s{sizeof(s)};s.hwnd=owner;s.lpVerb=L"open";s.nShow=SW_SHOWNORMAL;s.fMask=SEE_MASK_FLAG_NO_UI;
 PIDLIST_ABSOLUTE pidl=nullptr;
 if((target.rfind(L"::{",0)==0||target.rfind(L"shell:AppsFolder\\",0)==0)&&SUCCEEDED(SHParseDisplayName(target.c_str(),nullptr,&pidl,0,nullptr))){s.fMask|=SEE_MASK_IDLIST;s.lpIDList=pidl;}else s.lpFile=target.c_str();
 bool ok=ShellExecuteExW(&s)!=FALSE;if(pidl)CoTaskMemFree(pidl);if(!ok)message(owner,L"Windows could not open:\n"+target+L"\n\nThe app may have been moved or uninstalled.",MB_OK|MB_ICONWARNING);return ok;}
void roundedWindow(HWND h,int radius){RECT r{};GetClientRect(h,&r);int px=scaledRadius(radius,dpiFor(h));HRGN region=CreateRoundRectRgn(0,0,r.right+1,r.bottom+1,2*px,2*px);if(region&&!SetWindowRgn(h,region,TRUE))DeleteObject(region);}
int dpiFor(HWND h){auto dpi=GetDpiForWindow(h);return dpi?static_cast<int>(dpi):96;}
void message(HWND h,const std::wstring& t,UINT f){MessageBoxW(h,t.c_str(),L"Aether UI",f);}
}
