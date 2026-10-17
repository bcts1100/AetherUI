#include "platform/appearance.hpp"
#include <iostream>
#include <sstream>
using namespace aether;
using namespace aether::win;
namespace {
struct Value {std::wstring key,name;bool exists=false;DWORD value{};bool operator==(const Value&)const=default;};
std::vector<Value> snapshot(){
 const std::wstring personalize=L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",dwm=L"Software\\Microsoft\\Windows\\DWM";
 std::vector<Value> values={{personalize,L"AppsUseLightTheme"},{personalize,L"SystemUsesLightTheme"},{dwm,L"AccentColor"},{dwm,L"ColorizationColor"},{dwm,L"ColorPrevalence"},{L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent",L"AccentColorMenu"}};
 for(auto& v:values){HKEY key{};LONG status=RegOpenKeyExW(HKEY_CURRENT_USER,v.key.c_str(),0,KEY_QUERY_VALUE,&key);if(status==ERROR_FILE_NOT_FOUND)continue;if(status!=ERROR_SUCCESS)throw std::runtime_error("Cannot read original Windows preference key");DWORD type=0,size=sizeof(v.value);status=RegQueryValueExW(key,v.name.c_str(),nullptr,&type,reinterpret_cast<BYTE*>(&v.value),&size);RegCloseKey(key);if(status==ERROR_FILE_NOT_FOUND)continue;if(status!=ERROR_SUCCESS||type!=REG_DWORD||size!=sizeof(v.value))throw std::runtime_error("Unexpected Windows preference type");v.exists=true;}return values;
}
}
int main(int argc,char** argv){
 if(argc!=2||std::string_view(argv[1])!="--run-opt-in"){std::cerr<<"Explicit --run-opt-in is required for this live per-user appearance test.\n";return 2;}
 std::cout<<std::unitbuf;ComApartment com;auto path=executablePath().parent_path()/L"test-profiles"/(L"live-theme-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));std::filesystem::create_directories(path);
 try{
  const auto original=snapshot();auto restored=[&]{auto now=snapshot();if(!original[2].exists&&now[2].exists&&now[2].value==(0xFF000000u|static_cast<DWORD>(RGB((original[3].value>>16)&255,(original[3].value>>8)&255,original[3].value&255)))){now[2]=original[2];std::cout<<"NOTE Windows regenerated its original-color AccentColor cache; absence is not certified\n";}return now==original;};std::ostringstream evidence;for(const auto& v:original)evidence<<utf8(v.key)<<'|'<<utf8(v.name)<<'|'<<v.exists<<'|'<<v.value<<'\n';writeAtomic(path/L"original-values.txt",evidence.str());
  {AppearanceBridge bridge(path,path/L"unused-terminal.json");auto options=bridge.options();options.windowsMode=true;options.windowsAccent=true;options.restoreOnExit=true;if(!bridge.configure(options,preset(1)))throw std::runtime_error("Cannot configure opt-in appearance");auto dark=snapshot();if(!dark[0].exists||dark[0].value!=0||dark[1].value!=0||dark[2].value==original[2].value)throw std::runtime_error("Dark mode/accent write did not match requested theme");std::cout<<"PASS live dark mode and accent applied\n";Sleep(600);auto settled=snapshot();if(settled[2].value!=(0xFF000000u|static_cast<DWORD>(RGB(0x89,0xB4,0xFA)))||settled[3].value!=(0xC4000000u|preset(1).accent))throw std::runtime_error("Dark accent did not remain synchronized after Windows notifications");bridge.apply(preset(2));auto light=snapshot();if(light[0].value!=1||light[1].value!=1)throw std::runtime_error("Light mode write did not match requested theme");std::cout<<"PASS live light mode applied\n";Sleep(400);writeAtomic(path/L"before-restore-journal.txt",readText(path/L"windows-appearance-journal.txt"));for(const auto& v:snapshot())std::cout<<"BEFORE "<<utf8(v.name)<<" exists="<<v.exists<<" value="<<v.value<<'\n';bridge.restore(preset(2));std::cout<<"RESTORE "<<bridge.notice()<<'\n';for(const auto& v:snapshot())std::cout<<"AFTER "<<utf8(v.name)<<" exists="<<v.exists<<" value="<<v.value<<'\n';if(!restored())throw std::runtime_error("Live appearance restore differs from captured original");std::cout<<"PASS original Windows preferences restored; generated-cache exception reported\n";}
  for(int n=0;n<5;++n){Sleep(300);if(!restored())throw std::runtime_error("Delayed Windows update changed restored preferences");}std::cout<<"PASS restored preferences remain stable in five delayed samples\nEvidence: "<<utf8(path.wstring())<<'\n';return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\nRecovery journal: "<<utf8(path.wstring())<<'\n';return 1;}
}
