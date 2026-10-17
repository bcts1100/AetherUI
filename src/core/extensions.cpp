#include "extensions.hpp"
#include <algorithm>
#include <charconv>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
namespace aether {
namespace {
using Fields=std::map<std::string,std::string>;using Ini=std::map<std::string,Fields>;
void fail(std::string* error,const char* reason){if(error)*error=reason;}
std::string trim(std::string_view value){auto first=value.find_first_not_of(" \t\r");return first==value.npos?std::string{}:std::string(value.substr(first,value.find_last_not_of(" \t\r")-first+1));}
bool identifier(std::string_view id){if(id.empty()||id.size()>64)return false;for(char c:id)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'))return false;return true;}
int hex(char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;}
std::string encode(std::string_view s){std::string result;const char* digits="0123456789ABCDEF";for(unsigned char c:s){if(c<=32||c=='%'||c=='='||c=='['||c==']'){result+='%';result+=digits[c>>4];result+=digits[c&15];}else result+=static_cast<char>(c);}return result;}
std::optional<std::string> decoded(std::string_view value,std::size_t characters,bool empty=false){
 if(value.size()>4096)return {};std::string text;for(std::size_t i=0;i<value.size();++i){if(value[i]=='%'){if(i+2>=value.size()||hex(value[i+1])<0||hex(value[i+2])<0)return {};text+=static_cast<char>(hex(value[i+1])*16+hex(value[i+2]));i+=2;}else text+=value[i];}
 if(text.empty())return empty?std::optional<std::string>(text):std::nullopt;
 std::size_t count=0;bool nonspace=false;
 for(std::size_t i=0;i<text.size();){auto c=static_cast<unsigned char>(text[i++]);std::uint32_t cp=c;int trailing=0;
  if(c>=0xc2&&c<=0xdf){cp=c&31;trailing=1;}else if(c>=0xe0&&c<=0xef){cp=c&15;trailing=2;}else if(c>=0xf0&&c<=0xf4){cp=c&7;trailing=3;}else if(c>=0x80)return {};
  for(int k=0;k<trailing;++k){if(i>=text.size())return {};auto next=static_cast<unsigned char>(text[i++]);if((next&0xc0)!=0x80)return {};cp=(cp<<6)|(next&63);}
  if((trailing==1&&cp<0x80)||(trailing==2&&cp<0x800)||(trailing==3&&cp<0x10000)||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff)||cp<32||(cp>=127&&cp<=159)||cp==0x2028||cp==0x2029)return {};
  if(++count>characters)return {};if(cp!=32&&cp!=0xa0&&cp!=0x3000)nonspace=true;
 }return nonspace||empty?std::optional<std::string>(text):std::nullopt;
}
std::optional<Ini> parseIni(std::string_view text){
 if(text.rfind("\xEF\xBB\xBF",0)==0)text.remove_prefix(3);
 if(text.empty()||text.size()>ExtensionBytesLimit||text.find('\0')!=text.npos)return {};Ini result;std::string section;std::size_t start=0,lines=0;
 while(start<text.size()){if(++lines>512)return {};auto end=text.find('\n',start);if(end==text.npos)end=text.size();auto line=trim(text.substr(start,end-start));start=end+1;if(line.empty()||line[0]=='#'||line[0]==';')continue;if(line.size()>4096)return {};
  if(line[0]=='['){if(line.size()<3||line.back()!=']'||result.size()>=11)return {};section=line.substr(1,line.size()-2);if(result.contains(section))return {};result.emplace(section,Fields{});continue;}
  auto equals=line.find('=');if(section.empty()||equals==line.npos||equals==0)return {};auto key=trim(std::string_view(line).substr(0,equals));if(result.at(section).size()>=16||key.empty()||result.at(section).contains(key))return {};result.at(section).emplace(key,trim(std::string_view(line).substr(equals+1)));
 }return result;
}
bool known(const Fields& fields,std::initializer_list<std::string_view> allowed){for(const auto& [key,value]:fields){(void)value;if(std::find(allowed.begin(),allowed.end(),key)==allowed.end())return false;}return true;}
std::optional<int> integer(std::string_view value,int low,int high){int n=0;auto [end,error]=std::from_chars(value.data(),value.data()+value.size(),n);return error==std::errc{}&&end==value.data()+value.size()&&n>=low&&n<=high?std::optional<int>(n):std::nullopt;}
std::optional<ExtensionManifest> manifest(const Ini& ini,bool installed){
 if(!ini.contains("AetherPlugin")||ini.at("AetherPlugin")!=Fields{{"version","1"}}||!ini.contains("plugin"))return {};
 const auto& meta=ini.at("plugin");if(!known(meta,{"id","name","description"})||!meta.contains("id")||!meta.contains("name")||!identifier(meta.at("id")))return {};
 auto name=decoded(meta.at("name"),80);if(!name)return {};ExtensionManifest result;result.id=meta.at("id");result.name=*name;
 if(meta.contains("description")){auto d=decoded(meta.at("description"),300,true);if(!d)return {};result.description=*d;}
 for(const auto& [section,f]:ini){
  if(section=="AetherPlugin"||section=="plugin"||(installed&&section=="AetherInstallation"))continue;
  if(!section.starts_with("widget:")||!identifier(std::string_view(section).substr(7))||result.widgets.size()>=ExtensionWidgetLimit||!known(f,{"type","label","action","pin","style","color","width","height","monitor","x","y","text","location"})||!f.contains("type")||!f.contains("label"))return {};
  ExtensionWidget w;w.id=section.substr(7);auto label=decoded(f.at("label"),80);if(!label)return {};w.label=*label;
  if(f.at("type")=="clock")w.kind=WidgetKind::Clock;else if(f.at("type")=="focused-app")w.kind=WidgetKind::FocusedApp;else if(f.at("type")=="quick-action")w.kind=WidgetKind::QuickAction;
  else if(f.at("type")=="media")w.kind=WidgetKind::Media;else if(f.at("type")=="battery")w.kind=WidgetKind::Battery;
  else if(f.at("type")=="resources")w.kind=WidgetKind::Resources;else if(f.at("type")=="note")w.kind=WidgetKind::Note;else if(f.at("type")=="weather")w.kind=WidgetKind::Weather;else return {};
  if(f.contains("text")){if(w.kind!=WidgetKind::Note)return {};auto text=decoded(f.at("text"),300,true);if(!text)return {};w.text=*text;}
  if(w.kind==WidgetKind::Weather){if(!f.contains("location")||f.at("location")!="seattle")return {};w.location="seattle";}
  else if(f.contains("location"))return {};
  if(w.kind==WidgetKind::QuickAction){if(!f.contains("action"))return {};auto a=f.at("action");if(a=="activities")w.action=WidgetAction::Activities;else if(a=="studio")w.action=WidgetAction::Studio;else if(a=="quick-settings")w.action=WidgetAction::QuickSettings;else if(a=="pin")w.action=WidgetAction::Pin;else return {};}
  else if(f.contains("action")||f.contains("pin"))return {};
  if(w.action==WidgetAction::Pin){if(!f.contains("pin"))return {};auto p=decoded(f.at("pin"),1024);if(!p)return {};w.pin=*p;}else if(f.contains("pin"))return {};
  if(f.contains("style")){auto style=f.at("style");if(style=="theme")w.style=WidgetStyle::Theme;else if(style=="minimal")w.style=WidgetStyle::Minimal;else if(style=="accent")w.style=WidgetStyle::Accent;else return {};}
  if(f.contains("color")){auto c=f.at("color");if(c.size()!=6)return {};std::uint32_t rgb=0;for(char h:c){if(hex(h)<0)return {};rgb=rgb*16+static_cast<unsigned>(hex(h));}w.accent=rgb;}
  auto number=[&](const char* key,int& field,int low,int high){if(!f.contains(key))return true;auto n=integer(f.at(key),low,high);if(!n)return false;field=*n;return true;};
  if(!number("width",w.width,120,480)||!number("height",w.height,64,240)||!number("x",w.placement.x,0,8192)||!number("y",w.placement.y,0,8192))return {};
  if((w.kind==WidgetKind::Media&&w.height<128)||(w.kind==WidgetKind::Weather&&w.height<120)||((w.kind==WidgetKind::Note||w.kind==WidgetKind::Resources)&&w.height<88))return {};
  if(f.contains("monitor")){auto m=decoded(f.at("monitor"),128,true);if(!m)return {};w.placement.monitor=*m;}
  result.widgets.push_back(std::move(w));
 }if(result.widgets.empty())return {};return result;
}
const char* actionName(WidgetAction a){switch(a){case WidgetAction::Activities:return "activities";case WidgetAction::Studio:return "studio";case WidgetAction::QuickSettings:return "quick-settings";case WidgetAction::Pin:return "pin";default:return "none";}}
const char* kindName(WidgetKind k){switch(k){case WidgetKind::Clock:return "clock";case WidgetKind::FocusedApp:return "focused-app";case WidgetKind::QuickAction:return "quick-action";case WidgetKind::Media:return "media";case WidgetKind::Battery:return "battery";case WidgetKind::Resources:return "resources";case WidgetKind::Note:return "note";case WidgetKind::Weather:return "weather";}return "unknown";}
}
std::optional<ExtensionManifest> parseExtension(std::string_view text,std::string* error){auto ini=parseIni(text);auto result=ini?manifest(*ini,false):std::nullopt;if(!result)fail(error,"Extension contains unsupported, duplicate, malformed or out-of-bounds native data.");else if(error)error->clear();return result;}
std::string serializeExtension(const ExtensionManifest& m){
 std::ostringstream out;out<<"[AetherPlugin]\nversion="<<m.version<<"\n[plugin]\nid="<<m.id<<"\nname="<<encode(m.name)<<"\ndescription="<<encode(m.description)<<"\n";
 for(const auto& w:m.widgets){out<<"\n[widget:"<<w.id<<"]\ntype="<<kindName(w.kind)<<"\nlabel="<<encode(w.label)<<"\nstyle="<<(w.style==WidgetStyle::Theme?"theme":w.style==WidgetStyle::Minimal?"minimal":"accent")<<"\nwidth="<<w.width<<"\nheight="<<w.height<<"\nmonitor="<<encode(w.placement.monitor)<<"\nx="<<w.placement.x<<"\ny="<<w.placement.y<<"\n";if(w.kind==WidgetKind::QuickAction){out<<"action="<<actionName(w.action)<<"\n";if(w.action==WidgetAction::Pin)out<<"pin="<<encode(w.pin)<<"\n";}if(w.kind==WidgetKind::Note)out<<"text="<<encode(w.text)<<"\n";if(w.kind==WidgetKind::Weather)out<<"location="<<w.location<<"\n";if(w.accent)out<<"color="<<std::hex<<std::uppercase<<std::setfill('0')<<std::setw(6)<<*w.accent<<std::dec<<"\n";}return out.str();
}
std::optional<InstalledExtension> parseInstalledExtension(std::string_view text,std::string* error){
 auto bad=[&]()->std::optional<InstalledExtension>{fail(error,"Stored extension installation metadata or manifest is invalid.");return {};};auto ini=parseIni(text);if(!ini||!ini->contains("AetherInstallation"))return bad();const auto& f=ini->at("AetherInstallation");
 if(f.size()!=3||!f.contains("version")||f.at("version")!="1"||!f.contains("enabled")||!f.contains("order"))return bad();auto enabled=integer(f.at("enabled"),0,1),order=integer(f.at("order"),0,1023);auto m=manifest(*ini,true);if(!enabled||!order||!m)return bad();if(error)error->clear();return InstalledExtension{"",std::move(*m),*enabled==1,*order};
}
std::string serializeInstalledExtension(const InstalledExtension& extension){return serializeExtension(extension.manifest)+"\n[AetherInstallation]\nversion=1\nenabled="+(extension.enabled?std::string("1"):std::string("0"))+"\norder="+std::to_string(extension.order)+"\n";}
std::vector<ExtensionManifest> exampleExtensions(){
 ExtensionWidget clock;clock.id="clock";clock.label="Time and date";clock.placement={"",24,24};
 ExtensionWidget focus;focus.id="focus";focus.label="Focused app";focus.kind=WidgetKind::FocusedApp;focus.placement={"",24,164};
 std::vector<ExtensionWidget> actions;const char* labels[]={"Activities","Desktop Studio","Quick settings"};WidgetAction targets[]={WidgetAction::Activities,WidgetAction::Studio,WidgetAction::QuickSettings};
 for(int i=0;i<3;++i){ExtensionWidget w;w.id="action"+std::to_string(i);w.label=labels[i];w.kind=WidgetKind::QuickAction;w.action=targets[i];w.height=88;w.style=WidgetStyle::Accent;w.placement={"",24,304+i*108};actions.push_back(std::move(w));}
 return {{1,"aether-clock","Clock and date","Native local time from Aether status.",{clock}},{1,"aether-focus","Focused app","The active app from Aether window events.",{focus}},{1,"aether-actions","Desktop shortcuts","Explicit actions for Activities, Studio and Quick settings.",actions}};
}
ExtensionManifest sleekDeskExtension(int index){
 index=std::clamp(index,0,3);const char* ids[]={"graphite","linen","aurora","ember"};const char* names[]={"Graphite Desk","Linen Desk","Aurora Desk","Ember Desk"};
 ExtensionManifest m{1,std::string("aether-sleek-")+ids[index],names[index],"Matching clock, focused app and desktop shortcuts.",{}};
 auto add=[&](const char* id,const char* label,WidgetKind kind,WidgetAction action,int x,int y,int width,int height){ExtensionWidget w;w.id=id;w.label=label;w.kind=kind;w.action=action;w.width=width;w.height=height;w.placement={"",x,y};w.accent=preset(5+index).accent;m.widgets.push_back(std::move(w));};
 add("activities","Apps",WidgetKind::QuickAction,WidgetAction::Activities,24,248,152,88);
 add("clock","Today",WidgetKind::Clock,WidgetAction::None,24,24,320,104);
 add("focus","In focus",WidgetKind::FocusedApp,WidgetAction::None,24,144,320,88);
 add("quick-settings","Controls",WidgetKind::QuickAction,WidgetAction::QuickSettings,192,248,152,88);
 add("studio","Customize",WidgetKind::QuickAction,WidgetAction::Studio,24,352,320,88);
 for(auto& w:m.widgets)if(w.kind==WidgetKind::QuickAction&&w.id!="studio")w.style=WidgetStyle::Accent;
 return m;
}
ExtensionManifest productivityExtension(){
 ExtensionManifest m{1,"aether-productivity","Productivity","Media, battery, system CPU and Aether memory, quick notes and Seattle forecast. Weather contacts api.weather.gov only when visible and enabled.",{}};
 auto add=[&](const char* id,const char* label,WidgetKind kind,int x,int y,int height){ExtensionWidget w;w.id=id;w.label=label;w.kind=kind;w.width=320;w.height=height;w.placement={"",x,y};m.widgets.push_back(std::move(w));};
 add("battery","Power",WidgetKind::Battery,380,304,88);
 add("media","Now playing",WidgetKind::Media,380,24,144);
 add("note","Quick note",WidgetKind::Note,380,408,140);m.widgets.back().text="Click here or press Enter to edit your note.";
 add("resources","Resources",WidgetKind::Resources,380,184,104);
 add("weather","Seattle forecast - NWS",WidgetKind::Weather,728,24,160);m.widgets.back().location="seattle";
 return m;
}
Rect extensionBounds(const ExtensionWidget& widget,Rect work,int dpi){
 dpi=std::clamp(dpi,48,768);auto scaled=[&](int v){return static_cast<int>((static_cast<long long>(v)*dpi+48)/96);};auto width=std::min(std::max(1,work.width()),scaled(std::clamp(widget.width,120,480)));auto height=std::min(std::max(1,work.height()),scaled(std::clamp(widget.height,64,240)));
 auto left=std::clamp(work.left+scaled(std::clamp(widget.placement.x,0,8192)),work.left,std::max(work.left,work.right-width));auto top=std::clamp(work.top+scaled(std::clamp(widget.placement.y,0,8192)),work.top,std::max(work.top,work.bottom-height));return {left,top,left+width,top+height};
}
bool extensionVisible(bool enabled,bool paused,bool hideFullscreen,bool monitorFullscreen){return enabled&&!paused&&!(hideFullscreen&&monitorFullscreen);}
}
