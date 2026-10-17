#include "platform/catalog.hpp"
#include "platform/icons.hpp"
#include <iostream>
#include <set>

namespace {
std::wstring displayName(IShellItem* item,SIGDN kind){
 PWSTR value=nullptr;
 if(FAILED(item->GetDisplayName(kind,&value)))return {};
 std::wstring text=value?value:L"";CoTaskMemFree(value);return text;
}
struct Pidl {
 PIDLIST_ABSOLUTE value=nullptr;
 ~Pidl(){if(value)CoTaskMemFree(value);}
};
struct Fixture {
 std::wstring name;
 aether::win::ComPtr<IShellItem> item;
};
}
int main(){
 using namespace aether::win;
 ComApartment com;if(FAILED(com.result)){std::cerr<<"COM initialization failed.\n";return 1;}
 ComPtr<IShellItem> root;ComPtr<IEnumShellItems> enumerator;
 if(FAILED(SHCreateItemInKnownFolder(FOLDERID_AppsFolder,0,nullptr,IID_PPV_ARGS(&root)))||
    FAILED(root->BindToHandler(nullptr,BHID_EnumItems,IID_PPV_ARGS(&enumerator)))){
  std::cerr<<"Installed AppsFolder fixtures could not be enumerated.\n";return 2;
 }
 auto rootName=displayName(root.Get(),SIGDN_DESKTOPABSOLUTEPARSING);
 if(rootName.rfind(L"::{",0)!=0){std::cerr<<"AppsFolder root did not expose a namespace parsing name.\n";return 3;}
 std::vector<Fixture> fixtures;std::set<std::wstring> names;
 for(int examined=0;examined<2048&&fixtures.size()<8;++examined){
  ComPtr<IShellItem> item;ULONG got=0;
  if(enumerator->Next(1,&item,&got)!=S_OK||!got)break;
  auto name=displayName(item.Get(),SIGDN_NORMALDISPLAY);
  auto parsing=displayName(item.Get(),SIGDN_DESKTOPABSOLUTEPARSING);
  // A live packaged entry supplies a genuine AUMID fixture, never a made-up ID.
  if(name.empty()||parsing.find(L'!')==std::wstring::npos||!names.insert(lower(name)).second)continue;
  fixtures.push_back({std::move(name),std::move(item)});
 }
 if(fixtures.empty()){std::cerr<<"No installed packaged-app fixture was available for this acceptance check.\n";return 4;}
 Catalog catalog;catalog.start(nullptr);
 std::shared_ptr<const std::vector<Application>> apps;
 auto deadline=GetTickCount64()+10000;
 do{
  apps=catalog.snapshot();
  if(!apps->empty()&&!catalog.indexing())break;
  Sleep(25);
 }while(GetTickCount64()<deadline);
 if(apps->empty()||catalog.indexing()){std::cerr<<"Catalog did not publish its initial snapshot within 10 seconds.\n";return 5;}
 int failures=0;std::wstring iconTarget;const std::wstring namespacePrefix=L"shell:AppsFolder\\";
 for(const auto& fixture:fixtures){
  auto found=std::find_if(apps->begin(),apps->end(),[&](const Application& app){return lower(app.name)==lower(fixture.name);});
  if(found==apps->end()){std::cerr<<"FAIL installed app missing: "<<utf8(fixture.name)<<'\n';++failures;continue;}
  bool namespaceTarget=found->target.rfind(namespacePrefix,0)==0;
  Pidl parsed;HRESULT parse=SHParseDisplayName(found->target.c_str(),nullptr,&parsed.value,0,nullptr);
  ComPtr<IShellItem> resolved;HRESULT bind=SHCreateItemFromParsingName(found->target.c_str(),nullptr,IID_PPV_ARGS(&resolved));
  int order=1;HRESULT compare=SUCCEEDED(bind)?resolved->Compare(fixture.item.Get(),SICHINT_CANONICAL,&order):bind;
  if(!namespaceTarget||FAILED(parse)||!parsed.value||FAILED(bind)||FAILED(compare)||order!=0){
   std::cerr<<"FAIL catalog target does not resolve to its installed AppsFolder item: "<<utf8(fixture.name)
    <<", namespace="<<namespaceTarget<<", parse HRESULT="<<std::hex<<static_cast<unsigned long>(parse)
    <<", bind HRESULT="<<static_cast<unsigned long>(bind)<<std::dec<<", target="<<utf8(found->target)<<'\n';
   ++failures;
  }else{
   std::cout<<"PASS installed app namespace roundtrip: "<<utf8(fixture.name)<<'\n';
   if(iconTarget.empty())iconTarget=found->target;
  }
 }
 if(!iconTarget.empty()){
  IconCache icons;icons.start(nullptr);
  auto image=icons.request(iconTarget,iconTarget);deadline=GetTickCount64()+5000;
  while(!image&&GetTickCount64()<deadline){Sleep(25);image=icons.request(iconTarget,iconTarget);}
  bool visiblePixels=false;
  if(image)for(std::size_t offset=3;offset<image->bytes.size();offset+=4)if(image->bytes[offset]){visiblePixels=true;break;}
  if(!image||!image->width||!image->height||image->width>128||image->height>128||!visiblePixels){
   std::cerr<<"FAIL real installed app icon was not extracted within 5 seconds.\n";++failures;
  }else std::cout<<"PASS real installed app icon through IconCache: "<<image->width<<'x'<<image->height<<'\n';
 }
 std::cout<<fixtures.size()<<" installed packaged-app fixtures, "<<apps->size()<<" indexed entries; no apps launched.\n";
 return failures?6:0;
}
