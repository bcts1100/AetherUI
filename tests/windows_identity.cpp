#include "platform/identity.hpp"
#include "platform/windows.hpp"
#include <propkey.h>
#include <propvarutil.h>
#include <fstream>
#include <functional>
#include <iostream>

using namespace aether;
namespace {
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}

struct Fixture {
 std::filesystem::path directory,executable;
 Fixture(){
  directory=std::filesystem::temp_directory_path()/
   (L"AetherIdentity-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
  std::filesystem::create_directories(directory);
  executable=directory/L"AetherIdentityOwnedTarget.exe";
  std::filesystem::copy_file(win::executablePath(),executable);
 }
 ~Fixture(){std::error_code error;std::filesystem::remove_all(directory,error);}
 std::filesystem::path shortcut(const wchar_t* name,const std::filesystem::path& target,
   const wchar_t* arguments=L"",const wchar_t* appId=L""){
  win::ComPtr<IShellLinkW> link;
  require(SUCCEEDED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&link))),"Cannot create owned Shell link fixture");
  require(SUCCEEDED(link->SetPath(target.c_str())),"Cannot set fixture shortcut path");
  require(SUCCEEDED(link->SetArguments(arguments)),"Cannot set fixture shortcut arguments");
  require(SUCCEEDED(link->SetWorkingDirectory(directory.c_str())),"Cannot set fixture working directory");
  if(*appId){
   win::ComPtr<IPropertyStore> properties;require(SUCCEEDED(link.As(&properties)),"Shortcut has no property store");
   PROPVARIANT value{};require(SUCCEEDED(InitPropVariantFromString(appId,&value)),"Cannot create fixture AUMID");
   HRESULT stored=properties->SetValue(PKEY_AppUserModel_ID,value);PropVariantClear(&value);
   require(SUCCEEDED(stored)&&SUCCEEDED(properties->Commit()),"Cannot store fixture AUMID");
  }
  win::ComPtr<IPersistFile> file;require(SUCCEEDED(link.As(&file)),"Shortcut has no persistence interface");
  auto path=directory/name;require(SUCCEEDED(file->Save(path.c_str(),TRUE)),"Cannot save owned shortcut fixture");return path;
 }
};
std::wstring pathIdentity(const std::filesystem::path& path){
 auto value=win::lower(path.wstring());std::replace(value.begin(),value.end(),L'\\',L'/');return value;
}
void executablePinsNormalizeIdentity(){
 Fixture fixture;auto mixed=fixture.executable.wstring();
 for(auto& character:mixed)if(character>=L'a'&&character<=L'z')character=static_cast<wchar_t>(character-L'a'+L'A');
 std::replace(mixed.begin(),mixed.end(),L'\\',L'/');
 auto pin=win::pinFromPath(std::filesystem::path(mixed));require(pin.has_value(),"Existing .exe was rejected");
 require(pin->name=="AETHERIDENTITYOWNEDTARGET","Executable display name did not use the selected filename");
 require(win::wide(pin->identity)==pathIdentity(fixture.executable),"Executable identity did not normalize case and path separators");
 require(!pin->target.empty(),"Executable pin has no launch target");
}
void shortcutsGroupByExecutable(){
 Fixture fixture;auto path=fixture.shortcut(L"Tea Shop.lnk",fixture.executable);
 auto shortcut=win::pinFromPath(path);auto executable=win::pinFromPath(fixture.executable);
 require(shortcut.has_value()&&executable.has_value(),"Existing owned shortcut or executable was rejected");
 require(shortcut->name=="Tea Shop","Shortcut lost its friendly filename");
 require(shortcut->target==win::utf8(path.wstring()),"Shortcut launch target was replaced with its executable");
 require(win::wide(shortcut->identity)==pathIdentity(fixture.executable),"Shortcut does not group by its resolved executable");
 require(shortcut->identity==executable->identity,"File-picker shortcut and executable identities disagree");
 auto groups=groupDock({*shortcut},{{"Owned window",executable->target,executable->identity,"fixture",true}},"fixture",true);
 require(groups.size()==1&&groups[0].tasks.size()==1,"Shortcut pin failed to join its running executable");
}
void shortcutsPreferApplicationId(){
 Fixture fixture;auto first=fixture.shortcut(L"Mail.lnk",fixture.executable,L"--mail",L"Contoso.Mail_123!Main");
 auto second=fixture.shortcut(L"Photos.lnk",fixture.executable,L"--photos",L"Contoso.Photos_123!Main");
 auto mail=win::pinFromPath(first);auto photos=win::pinFromPath(second);
 require(mail.has_value()&&photos.has_value(),"AUMID shortcuts were rejected");
 require(mail->identity=="aumid:contoso.mail_123!main","Shortcut AUMID did not take precedence over shared executable");
 require(photos->identity=="aumid:contoso.photos_123!main","Second shortcut AUMID was lost");
 require(mail->identity!=photos->identity,"Distinct app shortcuts were collapsed into their shared host");
}
void shortcutFallbackKeepsLaunchPath(){
 Fixture fixture;auto corrupt=fixture.directory/L"Corrupt.lnk";std::ofstream(corrupt)<<"not a shell link";
 auto pin=win::pinFromPath(corrupt);require(pin.has_value(),"Existing unresolved shortcut cannot be pinned");
 require(pin->target==win::utf8(corrupt.wstring()),"Unresolved shortcut lost its launch target");
 require(win::wide(pin->identity)==pathIdentity(corrupt),"Unresolved shortcut has no stable path identity");
}
void missingShortcutTargetRetainsResolvedIdentity(){
 Fixture fixture;auto missing=fixture.directory/L"Uninstalled.exe";auto shortcut=fixture.shortcut(L"Uninstalled.lnk",missing);
 auto pin=win::pinFromPath(shortcut);require(pin.has_value(),"Valid shortcut to a moved app was rejected");
 require(win::wide(pin->identity)==pathIdentity(missing),"Missing target fallback replaced the stored executable identity");
 require(pin->target==win::utf8(shortcut.wstring()),"Moved-app shortcut lost its stored launch target");
}
void unsupportedAndMissingFilesAreRejected(){
 Fixture fixture;auto text=fixture.directory/L"Notes.txt";std::ofstream(text)<<"fixture";
 require(!win::pinFromPath(text),"Non-application file was accepted as an app pin");
 require(!win::pinFromPath(fixture.directory/L"Missing.exe"),"Nonexistent executable was accepted as an app pin");
 require(!win::pinFromPath(fixture.directory),"Directory was accepted as an app pin");
}
void shortcutLaunchPreservesArguments(){
 Fixture fixture;auto shortcut=fixture.shortcut(L"Owned marker.lnk",fixture.executable,L"--owned-marker \"quoted value\"");
 auto pin=win::pinFromPath(shortcut);require(pin.has_value(),"Owned marker shortcut was rejected");
 auto target=win::wide(pin->target);SHELLEXECUTEINFOW launch{sizeof(launch)};
 launch.fMask=SEE_MASK_FLAG_NO_UI|SEE_MASK_NOCLOSEPROCESS;launch.lpFile=target.c_str();launch.nShow=SW_HIDE;
 require(ShellExecuteExW(&launch)!=FALSE,"Shell did not launch the owned marker shortcut");
 win::Handle process(launch.hProcess);
 require(process&&WaitForSingleObject(process.value,5000)==WAIT_OBJECT_0,"Owned marker child did not exit");
 DWORD result=1;require(GetExitCodeProcess(process.value,&result)&&result==0,"Owned marker child failed");
 auto marker=fixture.executable;marker+=L".launched";std::ifstream input(marker);std::string content((std::istreambuf_iterator<char>(input)),{});
 require(content=="--owned-marker\nquoted value\n","Shortcut arguments were not preserved by the pin launch target");
}
}
int main(){
 auto executable=win::executablePath();
 if(executable.filename()==L"AetherIdentityOwnedTarget.exe"){
  int count=0;wchar_t** arguments=CommandLineToArgvW(GetCommandLineW(),&count);
  if(!arguments)return 2;
  auto marker=executable;marker+=L".launched";std::ofstream output(marker);
  for(int index=1;index<count;++index)output<<win::utf8(arguments[index])<<'\n';LocalFree(arguments);return output?0:3;
 }
 win::ComApartment apartment;require(SUCCEEDED(apartment.result),"Cannot initialize test COM apartment");
 std::vector<std::pair<const char*,std::function<void()>>> tests={
  {"executable pins normalize case and slashes",executablePinsNormalizeIdentity},
  {"shortcuts group by executable while preserving launch path",shortcutsGroupByExecutable},
  {"shortcuts prefer application ID over shared executable",shortcutsPreferApplicationId},
  {"unresolved shortcut fallback retains launch path",shortcutFallbackKeepsLaunchPath},
  {"missing shortcut target retains stored identity",missingShortcutTargetRetainsResolvedIdentity},
  {"unsupported and missing files are rejected",unsupportedAndMissingFilesAreRejected},
  {"shortcut pin launches owned child with saved arguments",shortcutLaunchPreservesArguments}};
 int failures=0;for(const auto& [name,test]:tests)try{test();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& error){++failures;std::cerr<<"FAIL "<<name<<": "<<error.what()<<'\n';}
 return failures?1:0;
}