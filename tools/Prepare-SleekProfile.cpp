#include "platform/config.hpp"
#include "platform/extensions.hpp"
#include <iostream>
int wmain(int argc,wchar_t** argv){
 using namespace aether;
 try {
  if(argc!=4||std::wstring_view(argv[1])!=L"--settings-dir"||(std::wstring_view(argv[3])!=L"--sleek-full"&&std::wstring_view(argv[3])!=L"--sleek-plain"))throw std::runtime_error("Use --settings-dir <staged absolute directory> --sleek-full or --sleek-plain");
  if(FindWindowW(L"Aether.Controller.v3",L"Aether UI Controller"))throw std::runtime_error("Close Aether normally before preparing a profile.");
  auto stage=win::executablePath().parent_path().parent_path().parent_path().lexically_normal().wstring()+L"\\";
  auto target=std::filesystem::path(argv[2]).lexically_normal();const auto text=target.wstring()+L"\\";
  if(!target.is_absolute()||text.size()<=stage.size()||_wcsnicmp(text.c_str(),stage.c_str(),stage.size())!=0)throw std::runtime_error("Profile must be inside this staged project. The original working copy is excluded.");
  win::setConfigDirectory(target);bool first=false;auto config=win::loadConfig(first);config.theme=preset(5);config.hideTaskbar=true;
  win::ExtensionLibrary library;std::string error;if(!library.load(&error))throw std::runtime_error(error);
  const bool full=std::wstring_view(argv[3])==L"--sleek-full";
  for(const auto& entry:library.entries()){bool managed=entry.manifest.id==productivityExtension().id;for(int i=0;i<4;++i)managed|=entry.manifest.id==sleekDeskExtension(i).id;if(managed&&entry.enabled&&!library.setEnabled(entry.storageId,false,&error))throw std::runtime_error(error);}
  if(full)for(const auto& manifest:{sleekDeskExtension(0),productivityExtension()}){
   std::optional<std::string> id;for(const auto& entry:library.entries())if(entry.manifest.id==manifest.id)id=entry.storageId;if(!id)id=library.install(manifest,&error);
   if(!id||!library.setEnabled(*id,true,&error))throw std::runtime_error(error);
  }
  win::saveConfig(config);size_t widgets=0;for(const auto& entry:library.entries())if(entry.enabled)widgets+=entry.manifest.widgets.size();
  std::cout<<"Prepared staged Graphite profile; hide-on-load=1; enabledWidgets="<<widgets<<"; mode="<<(full?"full":"plain")<<". Existing pins/layout/other preferences retained. No process launched.\n";return 0;
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
