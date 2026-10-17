#include "platform/extensions.hpp"
#include <iostream>
#include <fstream>
#include <winioctl.h>
#include <cstring>
namespace {
bool junction(const std::filesystem::path& link,const std::filesystem::path& target){
 struct Header {DWORD tag;WORD length,reserved,substituteOffset,substituteLength,printOffset,printLength;};
 static_assert(sizeof(Header)==16);auto substitute=L"\\??\\"+target.wstring();auto print=target.wstring();
 Header h{};h.tag=IO_REPARSE_TAG_MOUNT_POINT;h.substituteLength=static_cast<WORD>(substitute.size()*2);h.printOffset=static_cast<WORD>(h.substituteLength+2);h.printLength=static_cast<WORD>(print.size()*2);
 auto paths=substitute;paths.push_back(L'\0');paths+=print;paths.push_back(L'\0');h.length=static_cast<WORD>(8+paths.size()*2);
 std::vector<BYTE> data(sizeof(h)+paths.size()*2);std::memcpy(data.data(),&h,sizeof(h));std::memcpy(data.data()+sizeof(h),paths.data(),paths.size()*2);
 if(!CreateDirectoryW(link.c_str(),nullptr))return false;
 aether::win::Handle directory(CreateFileW(link.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr));
 DWORD written=0;bool ok=directory&&DeviceIoControl(directory.value,FSCTL_SET_REPARSE_POINT,data.data(),static_cast<DWORD>(data.size()),nullptr,0,&written,nullptr);directory.reset();if(!ok)RemoveDirectoryW(link.c_str());return ok;
}
}
int main(){
 using namespace aether;auto root=win::executablePath().parent_path()/L"test-profiles"/(L"extensions-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
 std::filesystem::create_directories(root);win::setConfigDirectory(root);int checks=0,failures=0;
 auto check=[&](bool ok,const char* why){++checks;if(!ok){++failures;std::cerr<<why<<'\n';}};std::string error;
 win::ExtensionLibrary library;check(library.load(&error),"Owned regular profile must initialize its native extension library.");
 check(library.entries().size()==3&&std::none_of(library.entries().begin(),library.entries().end(),[](const auto& x){return x.enabled;}),"First-profile examples must be present and disabled until explicitly enabled.");
 auto package=root/L"import.aetherplugin";const std::string external="[AetherPlugin]\nversion=1\n[plugin]\nid=custom-clock\nname=Imported clock\ndescription=Local fixture\n[widget:clock]\ntype=clock\nlabel=Clock\n";
 win::writeAtomic(package,external);auto imported=library.importFile(package,&error);check(imported.has_value(),"Import must accept a bounded native manifest.");
 if(imported){
  auto entry=std::find_if(library.entries().begin(),library.entries().end(),[&](const auto& x){return x.storageId==*imported;});
  check(entry!=library.entries().end()&&!entry->enabled,"Importing cannot enable or run a widget.");
  check(!library.importFile(package,&error),"Duplicate package IDs must not shadow a loaded extension.");
  check(library.setEnabled(*imported,true,&error),"An explicit toggle must persist enabled state.");
  check(library.setPlacement(*imported,"clock",{"fixture-display",90,140},&error),"A widget move must persist independently of its manifest defaults.");
  win::ExtensionLibrary reload;check(reload.load(&error),"Saved local library must reload.");
  auto saved=std::find_if(reload.entries().begin(),reload.entries().end(),[&](const auto& x){return x.storageId==*imported;});
  check(saved!=reload.entries().end()&&saved->enabled&&saved->manifest.widgets[0].placement==WidgetPlacement{"fixture-display",90,140},"Explicit enable and widget move must survive a real disk reload.");
  auto exported=root/L"export.aetherplugin";check(library.exportFile(*imported,exported,&error),"Local extension must export to a selected regular file.");
  auto manifest=parseExtension(win::readText(exported));check(manifest&&manifest->id=="custom-clock"&&manifest->widgets[0].placement.y==140,"Export must be an importable manifest without enabled state.");
  auto unrelated=root/L"extensions"/L"notes.txt";win::writeAtomic(unrelated,"Preserve this unrelated file.");
  check(library.remove(*imported,&error),"Remove must remove the selected generated library entry.");
  check(win::readText(unrelated)=="Preserve this unrelated file.","Extension removal must preserve unrelated files.");
 }
 check(!library.remove("../notes.txt",&error)&&!library.setEnabled("preset-000000",true,&error),"Library mutations cannot accept arbitrary file paths.");
 win::writeAtomic(package,external+"[code]\nscript=run.exe\n");check(!library.importFile(package,&error),"Import cannot admit executable or unknown data.");
 std::filesystem::create_directories(root/L"extensions");auto malformed=root/L"extensions"/L"preset-000099.aetherplugin";win::writeAtomic(malformed,external+"[AetherInstallation]\nversion=1\nenabled=1\norder=2\n[unexpected]\nrun=yes\n");
 win::ExtensionLibrary clean;clean.load(&error);check(std::none_of(clean.entries().begin(),clean.entries().end(),[](const auto& x){return x.manifest.id=="custom-clock";}),"Malformed enabled profile entry must not load or run.");
 ExtensionManifest fixture;fixture.id="fixture";fixture.name="Fixture clock";ExtensionWidget fixtureClock;fixtureClock.id="clock";fixtureClock.label="Clock";fixture.widgets.push_back(fixtureClock);
 auto saturated=fixture;saturated.widgets.clear();
 for(int i=0;i<8;++i){ExtensionWidget widget;widget.id="clock"+std::to_string(i);widget.label="Clock";saturated.widgets.push_back(widget);}
 for(int n=0;n<7;++n){saturated.id="crowded"+std::to_string(n);saturated.name="Crowded clocks";auto id=clean.install(saturated,&error);check(id.has_value(),"Bounded native package must install.");if(id)check(clean.setEnabled(*id,true,&error)==(n<6),"Active widget cap must reject the package that exceeds 48 native surfaces.");}
 while(clean.entries().size()<24){auto next=fixture;next.id="extra"+std::to_string(clean.entries().size());auto id=clean.install(next,&error);if(!id){check(false,"Local library rejected an available bounded slot.");break;}}
 auto overflow=fixture;overflow.id="overflow";check(!clean.install(overflow,&error),"Local package cap must refuse a 25th entry.");
 auto linkedProfile=root/L"linked-profile",outside=root/L"outside";std::filesystem::create_directories(linkedProfile);std::filesystem::create_directories(outside);
 win::writeAtomic(outside/L"sentinel.txt","Keep the owned junction target.");auto link=linkedProfile/L"extensions";bool created=junction(link,outside);check(created,"Owned reparse-point fixture must be constructible.");
 if(created){win::setConfigDirectory(linkedProfile);win::ExtensionLibrary guarded;check(!guarded.load(&error)&&!guarded.install(fixture,&error),"Library cannot follow a reparse directory for load or writes.");check(win::readText(outside/L"sentinel.txt")=="Keep the owned junction target."&&!std::filesystem::exists(outside/L"preset-000001.aetherplugin"),"Rejected reparse library must preserve all target files.");check(RemoveDirectoryW(link.c_str())!=FALSE,"Owned junction itself must be removable.");win::setConfigDirectory(root);}
 std::cout<<"Extensions storage: "<<(checks-failures)<<"/"<<checks<<" checks passed.\n";return failures?2:0;
}