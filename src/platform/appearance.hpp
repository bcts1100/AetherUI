#pragma once
#include "config.hpp"
#include "desktop.hpp"
#include "core/integration.hpp"
#include <thread>
namespace aether::win {
struct RegistryThemeValue {std::wstring path,name;DWORD value{};};
class RegistryThemeJournal {
 struct Entry {RegistryThemeValue applied;bool existed=false;DWORD before{},fallback{};};
 std::filesystem::path file_;std::vector<std::pair<std::wstring,std::wstring>> allowed_;std::vector<Entry> entries_;bool valid_=true;
 bool permitted(const RegistryThemeValue&)const;void persist();
public:
 RegistryThemeJournal(std::filesystem::path,std::vector<std::pair<std::wstring,std::wstring>>);
 bool empty()const{return entries_.empty();}
 bool apply(const std::vector<RegistryThemeValue>&,std::string* error=nullptr);bool restore(std::string* error=nullptr,bool retain=false);
};
class AppearanceBridge {
 IntegrationOptions options_;std::filesystem::path profile_,fragment_;RegistryThemeJournal registry_;
 std::optional<Theme> applied_;std::string notice_,fragmentLast_;std::optional<std::string> fragmentBefore_;bool fragmentOwned_=false,active_=false;
 struct Attribute {DWORD id{},before{},last{};};struct Frame {HWND hwnd{};DWORD pid{};std::uint64_t birth{};std::vector<Attribute> attributes;};std::vector<Frame> frames_;std::jthread notifier_;
 bool terminalTheme(const Theme&);bool removeTerminal();void restoreFrames();void notifyWindows();bool restoreWindows(std::string*);
public:
 explicit AppearanceBridge(std::filesystem::path profile=configDirectory(),std::filesystem::path fragment={});~AppearanceBridge();
 const IntegrationOptions& options()const{return options_;}const std::string& notice()const{return notice_;}
 bool configure(IntegrationOptions,const Theme&);void apply(const Theme&);void reconcile(const std::vector<Window>&,const Theme&);
 void restore(const Theme&);void stop();void launchTools(HWND owner,bool terminal,const std::wstring& directory=L"")const;
};
UINT toolsThemeMessage();UINT appearanceTransitionMessage();void applyOwnFrame(HWND,const Theme&);
}
