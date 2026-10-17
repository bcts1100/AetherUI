#include "platform/theme_library.hpp"
#include "platform/layout_library.hpp"
#include <iostream>
#include <winioctl.h>
#include <cstring>
namespace {
bool directoryJunction(const std::filesystem::path& link,const std::filesystem::path& target){
 struct MountPointHeader {DWORD tag;WORD length,reserved,substituteOffset,substituteLength,printOffset,printLength;};
 static_assert(sizeof(MountPointHeader)==16);
 std::wstring substitute=L"\\??\\"+target.wstring();
 auto print=target.wstring();
 if(substitute.size()+print.size()+2>16000)return false;
 MountPointHeader header{};header.tag=IO_REPARSE_TAG_MOUNT_POINT;
 header.substituteLength=static_cast<WORD>(substitute.size()*sizeof(wchar_t));
 header.printOffset=static_cast<WORD>(header.substituteLength+sizeof(wchar_t));
 header.printLength=static_cast<WORD>(print.size()*sizeof(wchar_t));
 std::wstring paths=substitute;paths.push_back(L'\0');paths+=print;paths.push_back(L'\0');
 header.length=static_cast<WORD>(8+paths.size()*sizeof(wchar_t));
 std::vector<BYTE> buffer(sizeof(header)+paths.size()*sizeof(wchar_t));
 std::memcpy(buffer.data(),&header,sizeof(header));std::memcpy(buffer.data()+sizeof(header),paths.data(),paths.size()*sizeof(wchar_t));
 if(!CreateDirectoryW(link.c_str(),nullptr))return false;
 aether::win::Handle directory(CreateFileW(link.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr));
 DWORD written=0;bool created=directory&&DeviceIoControl(directory.value,FSCTL_SET_REPARSE_POINT,buffer.data(),static_cast<DWORD>(buffer.size()),nullptr,0,&written,nullptr);
 directory.reset();if(!created)RemoveDirectoryW(link.c_str());return created;
}
}

int main(){
 using namespace aether;
 auto root=win::executablePath().parent_path()/L"test-profiles"/(L"presets-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
 win::setConfigDirectory(root);int checks=0,failures=0;
 auto check=[&](bool ok,const char* message){++checks;if(!ok){++failures;std::cerr<<message<<'\n';}};
 std::string error;auto theme=preset(1);theme.name="Legacy Theme";
 auto saved=win::saveThemePreset(theme,"  Evening desk / ../ [palette]  ",&error);
 check(saved&&saved->name=="Evening desk / ../ [palette]"&&saved->theme==theme,"Named palette does not normalize its independent display name.");
 if(saved){
  check(win::renameThemePreset(saved->id,"Night desk",&error),"Palette rename failed.");
  auto list=win::savedThemePresets(&error);
  check(list.size()==1&&list[0].name=="Night desk"&&list[0].theme==theme,"Palette rename changed theme tokens or was not persisted.");
  check(!win::renameThemePreset(saved->id,"bad\nname",&error)&&!error.empty(),"Palette accepts newline names.");
 }
 check(!win::saveThemePreset(theme,"   ",&error),"Palette accepts blank names.");
 check(!win::saveThemePreset(theme,std::string(81,'x'),&error),"Palette accepts oversized names.");
 auto themes=root/L"theme-presets";std::filesystem::create_directories(themes);
 win::writeAtomic(themes/L"preset-000090.aethertheme",serializeTheme(theme));
 auto legacy=win::savedThemePresets(&error);
 check(legacy.size()==2&&legacy[1].name=="Legacy Theme","Legacy theme.name was not used as its palette display name.");
 win::writeAtomic(themes/L"preset-000091.aethertheme",serializeTheme(theme)+"\n[preset]\nname=bad%0Aname\n");
 check(win::savedThemePresets(&error).size()==2,"Malformed name metadata appears in the palette library.");
 check(!win::loadThemePreset("../preset-000001",&error)&&!win::removeThemePreset("preset-000000",&error),"Palette accepts unsafe IDs.");
 Config source;source.defaults.edge=Edge::Left;source.defaults.iconSize=64;source.defaults.dockGap=24;source.defaults.autoHide=true;
 auto override=source.defaults;override.edge=Edge::Right;override.topBar=false;override.reserveDock=false;
 source.displays["connected-one"]=override;source.displays["absent-saved"]=override;
 auto layout=win::saveLayoutPreset(source,"  Work desk  ",&error);
 check(layout&&layout->name=="Work desk"&&layout->defaults==source.defaults&&layout->displays==source.displays,"Named layout failed to persist defaults and overrides.");
 if(layout){
  Config current;current.theme=preset(2);current.pins={{"Editor","C:/editor.exe","editor"}};
  current.hideFullscreen=false;current.clock24=true;current.windowPreviews=false;current.hideTaskbar=true;
  DisplayOptions retained;retained.dockGap=6;retained.dock=false;
  current.displays["connected-one"]=retained;current.displays["connected-two"]=retained;
  current.displays["absent-saved"]=retained;current.displays["absent-current"]=retained;
  auto applied=win::applyLayoutPreset(current,*layout,{"connected-one","connected-two"});
  check(applied.defaults==source.defaults&&applied.forDisplay("connected-one")==override,"Layout did not replace connected profiles and defaults.");
  check(!applied.displays.contains("connected-two")&&applied.forDisplay("connected-two")==source.defaults,"Complete layout did not restore connected default inheritance.");
  check(applied.forDisplay("absent-saved")==retained&&applied.forDisplay("absent-current")==retained,"Layout overwrote disconnected preferences.");
  check(applied.theme==current.theme&&applied.pins==current.pins&&!applied.hideFullscreen&&applied.clock24&&!applied.windowPreviews&&applied.hideTaskbar==current.hideTaskbar,"Applying a layout changed non-layout settings or the native taskbar preference.");
  auto optedOut=current;optedOut.hideTaskbar=false;auto visible=win::applyLayoutPreset(optedOut,*layout,{"connected-one","connected-two"});
  check(!visible.hideTaskbar,"Applying a layout enabled a taskbar preference the user opted out of.");
  for(bool hidden:{false,true}){
   auto invalid=*layout;invalid.defaults.iconSize=999;auto original=current;original.hideTaskbar=hidden;
   error.clear();auto unchanged=win::applyLayoutPreset(original,invalid,{"connected-one"},&error);
   check(!error.empty()&&unchanged.hideTaskbar==hidden&&unchanged.defaults==original.defaults&&unchanged.displays==original.displays,"Refused invalid layout changed the native taskbar preference or existing display settings.");
   auto missing=original;missing.tilingDisplays["absent-custom"]={false,"missing-custom"};
   error.clear();unchanged=win::applyLayoutPreset(missing,*layout,{"connected-one"},&error);
   check(!error.empty()&&unchanged.hideTaskbar==hidden&&unchanged.tilingDisplays==missing.tilingDisplays,"Refused missing disconnected zones changed the native taskbar preference or retained tiling policy.");
   auto full=*layout;full.snapLayouts.clear();
   for(int n=0;n<16;++n)full.snapLayouts.push_back({"full-"+std::to_string(n),"Full "+std::to_string(n),{{0,0,10000,10000}},8});
   auto custom=original;custom.snapLayouts={{"retained-custom","Retained custom",{{0,0,10000,10000}},8}};custom.tilingDisplays["absent-custom"]={false,"retained-custom"};
   error.clear();unchanged=win::applyLayoutPreset(custom,full,{"connected-one"},&error);
   check(!error.empty()&&unchanged.hideTaskbar==hidden&&unchanged.snapLayouts==custom.snapLayouts&&unchanged.tilingDisplays==custom.tilingDisplays,"Refused retained-zone capacity changed the native taskbar preference or retained custom geometry.");
  }
  Config crowded;crowded.hideTaskbar=true;for(int n=0;n<32;++n)crowded.displays["absent-"+std::to_string(n)]=retained;
  error.clear();auto refused=win::applyLayoutPreset(crowded,*layout,{"connected-one"},&error);
  check(!error.empty()&&refused.defaults==crowded.defaults&&refused.displays==crowded.displays&&refused.hideTaskbar==crowded.hideTaskbar,"Refused display-profile capacity changed current settings or the native taskbar preference.");
  check(win::renameLayoutPreset(layout->id,"Gaming",&error),"Layout rename failed.");
  auto loaded=win::loadLayoutPreset(layout->id,&error);
  check(loaded&&loaded->name=="Gaming"&&loaded->defaults==source.defaults&&loaded->displays==source.displays,"Renamed layout data changed or was not persisted.");
  auto file=root/L"layout-presets"/(win::wide(layout->id)+L".aetherlayout");
  auto validText=win::readText(file);
  win::writeAtomic(file,validText+"\n[behavior]\nhideTaskbar=1\n");
  check(!win::loadLayoutPreset(layout->id,&error),"Layout accepts behavior payload outside its data-only schema.");
  win::writeAtomic(file,validText+"\n[layout]\nedge=left\n");
  check(!win::loadLayoutPreset(layout->id,&error),"Layout accepts duplicate profile sections.");
  win::writeAtomic(file,validText);
  auto text=win::readText(file);auto pos=text.find("iconSize=64");if(pos!=std::string::npos)text.replace(pos,11,"iconSize=999");win::writeAtomic(file,text);
  check(!win::loadLayoutPreset(layout->id,&error)&&!error.empty(),"Invalid layout range was accepted.");
  check(win::savedLayoutPresets(&error).empty(),"Malformed layout appears as applicable.");
  check(win::removeLayoutPreset(layout->id,&error),"Cannot remove malformed regular layout file.");
 }
 check(!win::saveLayoutPreset(source,"bad\tname",&error),"Layout accepts control characters in names.");
 check(!win::loadLayoutPreset("preset-000001/other",&error),"Layout accepts path IDs.");
 for(int i=0;i<32;++i)check(win::saveLayoutPreset(source,"Layout "+std::to_string(i),&error).has_value(),"Valid layout could not use a bounded library slot.");
 check(!win::saveLayoutPreset(source,"Overflow",&error)&&win::savedLayoutPresets().size()==32,"Layout library exceeds 32 presets.");
 // Reject oversized regular files and linked library folders without touching targets.
 auto layoutFolder=root/L"layout-presets";std::filesystem::create_directories(layoutFolder);
 win::writeAtomic(layoutFolder/L"preset-000900.aetherlayout",std::string(65537,'x'));
 check(!win::loadLayoutPreset("preset-000900",&error),"Oversized layout file was accepted.");
 auto linked=root/L"linked-profile",outside=root/L"outside";
 std::filesystem::create_directories(linked);std::filesystem::create_directories(outside);
 win::writeAtomic(outside/L"sentinel.txt","Keep this target intact.");
 bool junction=directoryJunction(linked/L"layout-presets",outside);
 check(junction,"Cannot create owned layout junction fixture.");
 if(junction){
  win::setConfigDirectory(linked);
  check(!win::saveLayoutPreset(source,"Linked",&error)&&!error.empty(),"Layout save followed a linked directory.");
  check(!win::loadLayoutPreset("preset-000001",&error)&&!win::renameLayoutPreset("preset-000001","Linked",&error)&&!win::removeLayoutPreset("preset-000001",&error),"Layout mutation accepted a linked directory.");
  check(std::filesystem::directory_iterator(outside)!=std::filesystem::directory_iterator()&&win::readText(outside/L"sentinel.txt")=="Keep this target intact."&&!std::filesystem::exists(outside/L"preset-000001.aetherlayout"),"Layout operation changed the junction target.");
  check(RemoveDirectoryW((linked/L"layout-presets").c_str())!=FALSE,"Cannot remove owned junction itself.");
  win::setConfigDirectory(root);
 }
 std::cout<<"Named presets: "<<(checks-failures)<<"/"<<checks<<" checks passed.\n";
 return failures?2:0;
}
