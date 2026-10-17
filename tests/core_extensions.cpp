#include "core/extensions.hpp"
#include <iostream>
namespace {
const char* clockManifest="[AetherPlugin]\nversion=1\n[plugin]\nid=desk-clock\nname=Desk clock\ndescription=Native local clock\n[widget:clock]\ntype=clock\nlabel=Time\nwidth=260\nheight=120\nx=24\ny=48\nstyle=theme\n";
}
int main(){
 using namespace aether;int checks=0,failures=0;
 auto check=[&](bool ok,const char* why){++checks;if(!ok){++failures;std::cerr<<why<<'\n';}};std::string error;
 auto manifest=parseExtension(clockManifest,&error);
 check(manifest&&manifest->id=="desk-clock"&&manifest->widgets.size()==1&&manifest->widgets[0].placement.y==48,"A valid native manifest must preserve its label and placement.");
 if(manifest){
  auto encoded=serializeExtension(*manifest);auto decoded=parseExtension(encoded,&error);
  check(decoded&&*decoded==*manifest,"Manifest export must preserve the exact bounded native data.");
  InstalledExtension installed{"preset-000001",*manifest,true,3};
  auto stored=parseInstalledExtension(serializeInstalledExtension(installed),&error);
  check(stored&&stored->enabled&&stored->order==3&&stored->manifest==*manifest,"Enabled state and placement must survive profile reload.");
 }
 auto reject=[&](std::string text,const char* why){error.clear();check(!parseExtension(text,&error)&&!error.empty(),why);};
 reject(std::string(clockManifest)+"script=launch.exe\n","Unknown executable fields must be rejected.");
 reject(std::string(clockManifest)+"[widget:unsafe]\ntype=quick-action\nlabel=Unsafe\naction=shell\n","Unknown action cannot enter the native dispatcher.");
 reject(std::string(clockManifest)+"[widget:unsafe]\ntype=quick-action\nlabel=Unsafe\naction=pin\n","Pin actions require an exact configured pin key.");
 reject(std::string(clockManifest)+"[plugin]\nid=other\n","Duplicate sections must fail closed.");
 reject(std::string(clockManifest)+"height=100\n","Duplicate keys must fail closed.");
 auto changed=std::string(clockManifest);changed.replace(changed.find("version=1"),9,"version=2");reject(changed,"Future manifest versions must fail closed.");
 changed=clockManifest;changed.replace(changed.find("width=260"),9,"width=99999");reject(changed,"Widget dimensions must be bounded before window creation.");
 changed=clockManifest;changed.replace(changed.find("id=desk-clock"),13,"id=../escape");reject(changed,"Manifest IDs cannot become paths.");
 changed=clockManifest;changed.replace(changed.find("name=Desk clock"),15,"name=Bad%0AName");reject(changed,"Encoded line breaks must not become user-visible controls.");
 reject(std::string(clockManifest)+"color=GGGGGG\n","Malformed native colors must fail closed.");
 reject(std::string(65537,'x'),"Oversized manifests must fail before unbounded parsing.");
 changed=clockManifest;for(int i=0;i<8;++i)changed+="[widget:w"+std::to_string(i)+"]\ntype=clock\nlabel=Clock\n";
 reject(changed,"Widget count must be bounded across the entire package.");
 auto examples=exampleExtensions();check(examples.size()==3,"The local library must offer usable native clock, focused app and quick-action examples.");
 bool kinds[3]={};for(const auto& example:examples){auto parsed=parseExtension(serializeExtension(example));check(parsed.has_value(),"Every bundled example must use the same strict public manifest schema.");for(const auto& widget:example.widgets)kinds[static_cast<int>(widget.kind)]=true;}
 check(kinds[0]&&kinds[1]&&kinds[2],"Bundled examples must include all three native widget types.");
 ExtensionWidget widget;widget.width=260;widget.height=120;widget.placement={"",8192,8192};
 check(extensionBounds(widget,{-1200,-200,0,600},144)==Rect{-390,420,0,600},"Widget placement must clamp into negative-coordinate mixed-DPI work areas.");
 check(!extensionVisible(true,true,false,false)&&!extensionVisible(true,false,true,true)&&extensionVisible(true,false,false,true)&&!extensionVisible(false,false,false,false),"Session and fullscreen suppression must dominate enabled visibility.");
 std::cout<<"Extensions core: "<<(checks-failures)<<"/"<<checks<<" checks passed.\n";return failures?2:0;
}