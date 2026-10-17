#include "model.hpp"
#include "tiling.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cwctype>
#include <iomanip>
#include <limits>
#include <sstream>
#include <tuple>
namespace aether {
namespace {
using Ini=std::map<std::string,std::map<std::string,std::string>>;
std::string trim(std::string_view s){auto a=s.find_first_not_of(" \r\t\n");if(a==s.npos)return {};return std::string(s.substr(a,s.find_last_not_of(" \r\t\n")-a+1));}
std::string escape(std::string_view s){std::string r;const char* h="0123456789ABCDEF";for(unsigned char c:s){if(c<=32||c=='%'||c=='='||c=='['||c==']'){r+='%';r+=h[c>>4];r+=h[c&15];}else r+=static_cast<char>(c);}return r;}
int hex(char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;}
std::string unescape(std::string_view s){std::string r;for(size_t i=0;i<s.size();++i){if(s[i]=='%'&&i+2<s.size()&&hex(s[i+1])>=0&&hex(s[i+2])>=0){r+=static_cast<char>(hex(s[i+1])*16+hex(s[i+2]));i+=2;}else r+=s[i];}return r;}
Ini readIni(std::string_view text){Ini out;if(text.size()>1024*1024)return out;std::string section;size_t p=0;int lines=0;while(p<text.size()&&++lines<12000){auto end=text.find('\n',p);if(end==text.npos)end=text.size();auto s=trim(text.substr(p,end-p));p=end+1;if(s.empty()||s[0]=='#'||s[0]==';'||s.size()>8192)continue;if(s[0]=='['&&s.back()==']'){section=s.substr(1,s.size()-2);if(out.size()>512)break;continue;}auto eq=s.find('=');if(eq!=s.npos)out[section][trim(s.substr(0,eq))]=trim(s.substr(eq+1));}return out;}
std::string get(const Ini& i,const std::string& s,const std::string& k,const std::string& def={}){auto a=i.find(s);if(a==i.end())return def;auto b=a->second.find(k);return b==a->second.end()?def:b->second;}
int integer(const std::string& s,int def,int lo,int hi){int v=0;auto [p,e]=std::from_chars(s.data(),s.data()+s.size(),v);return e==std::errc{}&&p==s.data()+s.size()?std::clamp(v,lo,hi):def;}
bool boolean(const std::string& s,bool def){if(s=="1"||s=="true")return true;if(s=="0"||s=="false")return false;return def;}
std::optional<std::uint32_t> color(const std::string& s){if(s.size()!=6)return {};std::uint32_t x=0;for(auto c:s){int n=hex(c);if(n<0)return {};x=(x<<4)|static_cast<unsigned>(n);}return x;}
std::string colorText(std::uint32_t c){std::ostringstream s;s<<std::hex<<std::uppercase<<std::setfill('0')<<std::setw(6)<<(c&0xFFFFFF);return s.str();}
Theme readTheme(const Ini& i){
 Theme t;auto name=unescape(get(i,"theme","name",t.name));if(!name.empty()&&name.size()<=80)t.name=name;
 auto apply=[&](const char* k,std::uint32_t& c){auto v=color(get(i,"theme",k));if(v)c=*v;};
 apply("background",t.background);apply("surface",t.surface);apply("raised",t.raised);apply("foreground",t.foreground);apply("muted",t.muted);apply("accent",t.accent);
 t.radius=integer(get(i,"theme","radius"),16,0,28);
 auto family=unescape(get(i,"theme","fontFamily",t.fontFamily));if(validFontFamily(family))t.fontFamily=family;
 t.fontScale=integer(get(i,"theme","fontScale"),100,85,125);t.borderWidth=integer(get(i,"theme","borderWidth"),1,0,3);
 t.gradientStrength=integer(get(i,"theme","gradientStrength"),0,0,100);t.dockHighlight=integer(get(i,"theme","dockHighlight"),100,0,100);
 auto style=get(i,"theme","style");if(style=="frosted")t.style=ThemeStyle::Frosted;else if(style=="minimal")t.style=ThemeStyle::Minimal;
 auto bar=[&](const char* key){auto value=get(i,"theme",key);for(int n=0;n<5;++n)if(barStyleName(static_cast<BarStyle>(n))==value)return static_cast<BarStyle>(n);return BarStyle::Classic;};
 t.topBarStyle=bar("topBarStyle");t.dockStyle=bar("dockStyle");t.topBarHeight=integer(get(i,"theme","topBarHeight"),34,28,64);t.barPadding=integer(get(i,"theme","barPadding"),6,0,12);t.surfaceOpacity=integer(get(i,"theme","surfaceOpacity"),68,20,100);t.fontWeight=integer(get(i,"theme","fontWeight"),400,300,700);t.fontItalic=boolean(get(i,"theme","fontItalic"),false);
 auto indicator=get(i,"theme","indicator");if(indicator=="line")t.indicator=IndicatorStyle::Line;else if(indicator=="square")t.indicator=IndicatorStyle::Square;
 for(int n=1;n<static_cast<int>(FontPart::Count);++n){auto key="font."+fontPartKey(static_cast<FontPart>(n));auto& f=t.fonts[n];auto partFamily=unescape(get(i,"theme",key+".family"));if(validFontFamily(partFamily))f.family=partFamily;f.weight=integer(get(i,"theme",key+".weight"),400,300,700);f.scale=integer(get(i,"theme",key+".scale"),100,85,125);f.italic=boolean(get(i,"theme",key+".italic"),false);}
 return t;
}
void writeTheme(std::ostream& s,const Theme& t){
 s<<"[theme]\nname="<<escape(t.name)<<"\nbackground="<<colorText(t.background)<<"\nsurface="<<colorText(t.surface)<<"\nraised="<<colorText(t.raised)<<"\nforeground="<<colorText(t.foreground)<<"\nmuted="<<colorText(t.muted)<<"\naccent="<<colorText(t.accent)<<"\nradius="<<std::clamp(t.radius,0,28)
  <<"\nfontFamily="<<escape(validFontFamily(t.fontFamily)?t.fontFamily:"Segoe UI")<<"\nfontScale="<<std::clamp(t.fontScale,85,125)<<"\nborderWidth="<<std::clamp(t.borderWidth,0,3)
  <<"\ngradientStrength="<<std::clamp(t.gradientStrength,0,100)<<"\ndockHighlight="<<std::clamp(t.dockHighlight,0,100)
  <<"\nstyle="<<(t.style==ThemeStyle::Frosted?"frosted":t.style==ThemeStyle::Minimal?"minimal":"classic")
  <<"\ntopBarStyle="<<barStyleName(t.topBarStyle)<<"\ndockStyle="<<barStyleName(t.dockStyle)<<"\ntopBarHeight="<<std::clamp(t.topBarHeight,28,64)<<"\nbarPadding="<<std::clamp(t.barPadding,0,12)<<"\nsurfaceOpacity="<<std::clamp(t.surfaceOpacity,20,100)<<"\nfontWeight="<<std::clamp(t.fontWeight,300,700)<<"\nfontItalic="<<t.fontItalic
  <<"\nindicator="<<(t.indicator==IndicatorStyle::Line?"line":t.indicator==IndicatorStyle::Square?"square":"dots")<<'\n';
 for(int n=1;n<static_cast<int>(FontPart::Count);++n){const auto& f=t.fonts[n];const auto key="font."+fontPartKey(static_cast<FontPart>(n));s<<key<<".family="<<escape(validFontFamily(f.family)?f.family:"")<<'\n'<<key<<".weight="<<std::clamp(f.weight,300,700)<<'\n'<<key<<".scale="<<std::clamp(f.scale,85,125)<<'\n'<<key<<".italic="<<f.italic<<'\n';}
}
DisplayOptions readDisplay(const Ini& i,const std::string& sec,DisplayOptions d){auto edge=get(i,sec,"edge");if(edge=="left")d.edge=Edge::Left;else if(edge=="right")d.edge=Edge::Right;else if(edge=="bottom")d.edge=Edge::Bottom;d.iconSize=integer(get(i,sec,"iconSize"),d.iconSize,32,72);d.dockGap=integer(get(i,sec,"dockGap"),d.dockGap,0,32);
 d.topBar=boolean(get(i,sec,"topBar"),d.topBar);d.dock=boolean(get(i,sec,"dock"),d.dock);d.autoHide=boolean(get(i,sec,"autoHide"),d.autoHide);d.monitorOnly=boolean(get(i,sec,"monitorOnly"),d.monitorOnly);d.reserveDock=boolean(get(i,sec,"reserveDock"),d.reserveDock);d.clock=boolean(get(i,sec,"clock"),d.clock);d.focusedTitle=boolean(get(i,sec,"focusedTitle"),d.focusedTitle);d.status=boolean(get(i,sec,"status"),d.status);std::array<bool,5> seen{};bool valid=true;for(int n=0;n<5;++n){auto order=integer(get(i,sec,"moduleOrder"+std::to_string(n)),n,-1,5);if(order<0||order>=5||seen[order]){valid=false;break;}seen[order]=true;d.moduleOrder[n]=order;}if(!valid)d.moduleOrder={0,1,2,3,4};for(int n=0;n<5;++n)d.moduleZones[n]=integer(get(i,sec,"moduleZone"+std::to_string(n)),d.moduleZones[n],0,2);return d;}
void writeDisplay(std::ostream& s,const DisplayOptions& d){s<<"edge="<<edgeName(d.edge)<<"\niconSize="<<std::clamp(d.iconSize,32,72)<<"\ndockGap="<<std::clamp(d.dockGap,0,32)<<"\ntopBar="<<d.topBar<<"\ndock="<<d.dock<<"\nautoHide="<<d.autoHide<<"\nmonitorOnly="<<d.monitorOnly<<"\nreserveDock="<<d.reserveDock<<"\nclock="<<d.clock<<"\nfocusedTitle="<<d.focusedTitle<<"\nstatus="<<d.status<<'\n';for(int n=0;n<5;++n)s<<"moduleOrder"<<n<<'='<<d.moduleOrder[n]<<"\nmoduleZone"<<n<<'='<<std::clamp(d.moduleZones[n],0,2)<<'\n';}
std::wstring lower(std::wstring_view s){std::wstring r(s);std::transform(r.begin(),r.end(),r.begin(),[](wchar_t c){return static_cast<wchar_t>(std::towlower(c));});return r;}
}
DisplayOptions Config::forDisplay(const std::string& s) const{auto it=displays.find(s);return it==displays.end()?defaults:it->second;}
std::string fontPartKey(FontPart part){static const char* names[]={"general","activities","title","clock","status","dock","overview","studio","widgets","popups"};const int n=static_cast<int>(part);return n>=0&&n<10?names[n]:"general";}
bool validFontFamily(std::string_view family){
 if(family.empty()||family.size()>128||family.front()==' '||family.back()==' '||family.front()=='@')return false;
 for(std::size_t n=0;n<family.size();){unsigned char c=static_cast<unsigned char>(family[n]);if(c<32||c==127||c=='/'||c==92||c==':'||c=='['||c==']'||c=='=')return false;if(c<128){++n;continue;}int length=c>=0xC2&&c<=0xDF?2:c>=0xE0&&c<=0xEF?3:c>=0xF0&&c<=0xF4?4:0;if(!length||n+length>family.size())return false;unsigned char second=static_cast<unsigned char>(family[n+1]);if((c==0xE0&&second<0xA0)||(c==0xED&&second>=0xA0)||(c==0xF0&&second<0x90)||(c==0xF4&&second>=0x90))return false;for(int j=1;j<length;++j)if((static_cast<unsigned char>(family[n+j])&0xC0)!=0x80)return false;n+=length;}return true;
}
std::string barStyleName(BarStyle style){switch(style){case BarStyle::Solid:return "solid";case BarStyle::Floating:return "floating";case BarStyle::Islands:return "islands";case BarStyle::Accent:return "accent";default:return "classic";}}
bool validLocalIconPath(std::string_view path){
 if(path.size()<7||path.size()>4096||!((path[0]>='a'&&path[0]<='z')||(path[0]>='A'&&path[0]<='Z'))||path[1]!=':'||(path[2]!='\\'&&path[2]!='/'))return false;
 for(std::size_t n=0;n<path.size();++n)if(static_cast<unsigned char>(path[n])<32||path[n]==127||(n!=1&&path[n]==':'))return false;
 auto dot=path.find_last_of('.');if(dot==path.npos)return false;auto ext=std::string(path.substr(dot));
 for(char& c:ext)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');return ext==".png"||ext==".ico";
}
std::uint32_t textOnAccent(std::uint32_t color){auto channel=[](std::uint32_t value){const double v=value/255.0;return v<=.04045?v/12.92:std::pow((v+.055)/1.055,2.4);};const double luminance=.2126*channel((color>>16)&255)+.7152*channel((color>>8)&255)+.0722*channel(color&255);return (luminance+.05)/.05>=1.05/(luminance+.05)?0x000000:0xFFFFFF;}
Theme preset(int i){
 if(i>=5&&i<=8){
  static const Theme palettes[]={
   {"Aether Graphite",0x0F1217,0x181C24,0x242B36,0xEEF2F8,0xAEB9C8,0x83B7FF,14},
   {"Aether Linen",0xEBE8E1,0xFAF8F3,0xE8E4DA,0x20252D,0x525B65,0x345D83,14},
   {"Aether Aurora",0x151624,0x1F2334,0x30364B,0xF1F2FA,0xB7C1D8,0xB9A4F8,14},
   {"Aether Ember",0x1D1720,0x29212C,0x3A2D3B,0xFFF4ED,0xCCBBB9,0xF6AE78,14}};
  auto t=palettes[i-5];t.fontFamily="Segoe UI Variable";t.borderWidth=0;t.dockHighlight=45;t.indicator=IndicatorStyle::Line;
  t.style=i<=6?ThemeStyle::Minimal:i==7?ThemeStyle::Frosted:ThemeStyle::Classic;t.gradientStrength=i==7?12:i==8?6:0;return t;
 }
 if(i==1)return {"Midnight",0x151923,0x1F2735,0x303D52,0xF0F5FF,0xA3B2C9,0x89B4FA,16};
 if(i==2)return {"Paper",0xEEE9E4,0xFCF9F5,0xDFD7CF,0x292431,0x635869,0xB44A23,16};
 if(i==3){Theme t{"Frosted",0x172129,0x263741,0x49606D,0xF1F8FA,0xB5C9D1,0x83D3D8,20};t.style=ThemeStyle::Frosted;t.gradientStrength=65;t.dockHighlight=70;t.indicator=IndicatorStyle::Line;return t;}
 if(i==4){Theme t{"Minimal",0x17191B,0x202326,0x363C42,0xF1F3F5,0xA4ABB3,0xD9E3EC,8};t.style=ThemeStyle::Minimal;t.borderWidth=0;t.dockHighlight=35;t.indicator=IndicatorStyle::Square;return t;}
 return {};
}
std::string edgeName(Edge e){return e==Edge::Left?"left":e==Edge::Right?"right":"bottom";}
bool validIconPackId(std::string_view id){return id.size()<=64&&id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-")==id.npos;}
Config parseSettings(std::string_view text){Config c;const auto i=readIni(text);c.theme=readTheme(i);c.defaults=readDisplay(i,"layout",c.defaults);c.hideTaskbar=boolean(get(i,"behavior","hideTaskbar","1"),false);c.hideFullscreen=boolean(get(i,"behavior","hideFullscreen"),true);c.clock24=boolean(get(i,"behavior","clock24"),false);c.windowPreviews=boolean(get(i,"behavior","windowPreviews"),true);c.reduceMotion=boolean(get(i,"behavior","reduceMotion"),false);c.autoWallpaperColors=boolean(get(i,"behavior","autoWallpaperColors"),false);c.wallpaperColorScheme=integer(get(i,"behavior","wallpaperColorScheme"),0,0,2);c.animatedWallpaper=integer(get(i,"behavior","animatedWallpaper"),0,0,3);c.widgetsLocked=boolean(get(i,"behavior","widgetsLocked"),true);c.widgetGrid=integer(get(i,"behavior","widgetGrid"),12,4,48);c.iconPack=get(i,"behavior","iconPack");if(!validIconPackId(c.iconPack))c.iconPack.clear();
 for(const auto& [sec,values]:i){(void)values;if(sec.rfind("display:",0)==0&&c.displays.size()<32)c.displays[unescape(sec.substr(8))]=readDisplay(i,sec,c.defaults);}
 for(int n=0;n<128;++n){auto sec="pin:"+std::to_string(n);auto target=unescape(get(i,sec,"target"));if(target.empty()||target.size()>4096)continue;auto name=unescape(get(i,sec,"name","Application"));auto identity=unescape(get(i,sec,"identity"));auto iconPath=unescape(get(i,sec,"iconPath"));if(!validLocalIconPath(iconPath))iconPath.clear();std::uint64_t revision=0;auto rev=get(i,sec,"iconRevision","0");auto [end,error]=std::from_chars(rev.data(),rev.data()+rev.size(),revision);if(error!=std::errc{}||end!=rev.data()+rev.size())revision=0;if(name.size()<=512&&identity.size()<=1024)c.pins.push_back({name,target,identity,iconPath,revision});}parseTilingSettings(c,text);return c;}
std::string serializeSettings(const Config& c){std::ostringstream s;s<<"; Aether UI 0.7 - UTF-8, data only\n[AetherSettings]\nversion=4\n";writeTheme(s,c.theme);s<<"\n[behavior]\nhideTaskbar="<<c.hideTaskbar<<"\nhideFullscreen="<<c.hideFullscreen<<"\nclock24="<<c.clock24<<"\nwindowPreviews="<<c.windowPreviews<<"\nreduceMotion="<<c.reduceMotion<<"\nanimatedWallpaper="<<std::clamp(c.animatedWallpaper,0,3)<<"\nautoWallpaperColors="<<c.autoWallpaperColors<<"\nwallpaperColorScheme="<<std::clamp(c.wallpaperColorScheme,0,2)<<"\nwidgetsLocked="<<c.widgetsLocked<<"\nwidgetGrid="<<c.widgetGrid<<"\niconPack="<<(validIconPackId(c.iconPack)?c.iconPack:"")<<"\n\n[layout]\n";writeDisplay(s,c.defaults);
 for(const auto& [name,d]:c.displays){s<<"\n[display:"<<escape(name)<<"]\n";writeDisplay(s,d);}for(size_t n=0;n<c.pins.size()&&n<128;++n){auto& p=c.pins[n];s<<"\n[pin:"<<n<<"]\nname="<<escape(p.name)<<"\ntarget="<<escape(p.target)<<"\nidentity="<<escape(p.identity)<<"\niconPath="<<escape(validLocalIconPath(p.iconPath)?p.iconPath:"")<<"\niconRevision="<<p.iconRevision<<'\n';}s<<serializeTilingSettings(c);return s.str();}
std::optional<Theme> parseTheme(std::string_view text,std::string* error){
 auto fail=[&](const char* why)->std::optional<Theme>{if(error)*error=why;return {};};if(text.size()>65536)return fail("Theme exceeds the 64 KiB limit.");
 auto i=readIni(text);auto version=get(i,"AetherTheme","version");if(version!="1"&&version!="2"&&version!="3"&&version!="4")return fail("Expected an AetherTheme version=1, version=2, version=3 or version=4 header.");
 for(auto k:{"background","surface","raised","foreground","muted","accent"})if(!color(get(i,"theme",k)))return fail("Theme colors must contain exactly six hexadecimal digits.");
 auto section=i.find("theme");if(section==i.end())return fail("Theme tokens are missing.");
 auto present=[&](const char* key){return section->second.contains(key);};
 if(version!="1")for(auto key:{"fontFamily","fontScale","borderWidth","gradientStrength","dockHighlight","style","indicator"})if(!present(key))return fail("Version 2 themes require complete appearance tokens.");
 if(version=="3"||version=="4")for(auto key:{"topBarStyle","dockStyle","topBarHeight","barPadding","surfaceOpacity","fontWeight","fontItalic"})if(!present(key))return fail("Version 3 themes require complete typography and bar designs.");
 if(present("fontFamily")&&!validFontFamily(unescape(get(i,"theme","fontFamily"))))return fail("Font family must be a valid, bounded family name.");
 for(const auto& bound:std::vector<std::tuple<const char*,int,int>>{{"fontWeight",300,700},{"topBarHeight",28,64},{"barPadding",0,12},{"surfaceOpacity",20,100},{"fontScale",85,125},{"borderWidth",0,3},{"gradientStrength",0,100},{"dockHighlight",0,100}}){
  auto [key,lo,hi]=bound;if(!present(key))continue;auto textValue=get(i,"theme",key);int value=0;auto [end,result]=std::from_chars(textValue.data(),textValue.data()+textValue.size(),value);
  if(result!=std::errc{}||end!=textValue.data()+textValue.size()||value<lo||value>hi)return fail("Appearance numbers are outside their supported bounds.");
 }
 auto style=get(i,"theme","style","classic"),indicator=get(i,"theme","indicator","dots");
 if(style!="classic"&&style!="frosted"&&style!="minimal")return fail("Unknown native surface style.");
 if(indicator!="dots"&&indicator!="line"&&indicator!="square")return fail("Unknown running indicator style.");
 for(auto key:{"topBarStyle","dockStyle"})if(present(key)){bool valid=false;for(int n=0;n<5;++n)valid=valid||get(i,"theme",key)==barStyleName(static_cast<BarStyle>(n));if(!valid)return fail("Unknown bar design.");}
 if(present("fontItalic")&&get(i,"theme","fontItalic")!="0"&&get(i,"theme","fontItalic")!="1")return fail("Font italic must be 0 or 1.");
 for(int n=1;n<static_cast<int>(FontPart::Count);++n){auto key="font."+fontPartKey(static_cast<FontPart>(n));auto family=unescape(get(i,"theme",key+".family"));if(!family.empty()&&!validFontFamily(family))return fail("Invalid component font family.");for(auto field:{".weight",".scale"}){auto value=get(i,"theme",key+field);if(value.empty())continue;int number=0;auto [end,ec]=std::from_chars(value.data(),value.data()+value.size(),number);int lo=std::string_view(field)==".weight"?300:85,hi=std::string_view(field)==".weight"?700:125;if(ec!=std::errc{}||end!=value.data()+value.size()||number<lo||number>hi)return fail("Invalid component font scale or weight.");}auto italic=get(i,"theme",key+".italic");if(!italic.empty()&&italic!="0"&&italic!="1")return fail("Invalid component italic.");}
 auto t=readTheme(i);if(error)error->clear();return t;
}
std::string serializeTheme(const Theme& t){std::ostringstream s;s<<"[AetherTheme]\nversion=4\n";writeTheme(s,t);return s.str();}
int searchScore(std::wstring_view query,std::wstring_view text){auto q=lower(query.substr(0,256)),t=lower(text.substr(0,2048));auto first=q.find_first_not_of(L" \t\r\n");if(first==q.npos)return 1;q=q.substr(first,q.find_last_not_of(L" \t\r\n")-first+1);if(q==t)return 10000;if(t.rfind(q,0)==0)return 8000-static_cast<int>(std::min<size_t>(t.size(),1000));if(t.find(q)!=t.npos)return 6000-static_cast<int>(std::min<size_t>(t.find(q),1000));
 std::wistringstream words(q);std::wstring token;bool all=true;while(words>>token)if(t.find(token)==t.npos){all=false;break;}if(all)return 3000;size_t p=0;for(auto ch:q){if(std::iswspace(ch))continue;p=t.find(ch,p);if(p==t.npos)return 0;++p;}return 500;
}
Rect dockGeometry(Rect w,Edge edge,int slots,int icon,int dpi,int gap){double scale=std::clamp(dpi,48,480)/96.0;auto px=[&](int v){return static_cast<int>(std::lround(v*scale));};int margin=px(std::clamp(gap,0,32)),thickness=px(std::clamp(icon,32,72)+24);int length=px(std::clamp(slots,1,1024)*(std::clamp(icon,32,72)+8)+24);
 int ww=std::max(1,w.width()),hh=std::max(1,w.height());if(edge==Edge::Bottom){int width=std::min(length,std::max(1,ww-2*margin));int height=std::min(thickness,std::max(1,hh-2*margin));int bottom=w.bottom-std::min(margin,hh-height);return {w.left+(ww-width)/2,bottom-height,w.left+(ww+width)/2,bottom};}
 int width=std::min(thickness,std::max(1,ww-2*margin));int height=std::min(length,std::max(1,hh-2*margin));int x=edge==Edge::Left?w.left+std::min(margin,ww-width):w.right-width-std::min(margin,ww-width);return {x,w.top+(hh-height)/2,x+width,w.top+(hh+height)/2};}
Rect dockGeometry(Rect work,Edge edge,const Page& page,int icon,int dpi,int gap){
 double scale=std::clamp(dpi,48,480)/96.0;
 auto px=[&](double v){return static_cast<int>(std::lround(v*scale));};
 int slots=page.count+1+(page.previous?1:0)+(page.next?1:0);
 int margin=px(std::clamp(gap,0,32));
 int length=std::max(1,px(slots*page.slotSize+page.padding));
 int thickness=std::max(1,px(std::min(static_cast<float>(std::clamp(icon,32,72)+24),page.slotSize+16.f)));
 int ww=std::max(1,work.width()),hh=std::max(1,work.height());
 if(edge==Edge::Bottom){
  int width=std::min(length,std::max(1,ww-2*margin));
  int height=std::min(thickness,std::max(1,hh-2*margin));
  int left=work.left+(ww-width)/2,bottom=work.bottom-std::min(margin,hh-height);
  return {left,bottom-height,left+width,bottom};
 }
 int width=std::min(thickness,std::max(1,ww-2*margin));
 int height=std::min(length,std::max(1,hh-2*margin));
 int x=edge==Edge::Left?work.left+std::min(margin,ww-width):work.right-width-std::min(margin,ww-width);
 int top=work.top+(hh-height)/2;return {x,top,x+width,top+height};
}
Page pageFor(int count,int offset,int available,int slot,int padding){
 count=std::max(0,count);available=std::max(0,available);slot=std::max(1,slot);
 if(!available)return {};
 // A middle page needs the launcher, both arrows and at least one app.
 int minimumSlots=count>=3?4:count+1;
 float inset=static_cast<float>(std::clamp(padding,0,std::max(0,available-minimumSlots)));
 float effectiveSlot=std::min(static_cast<float>(slot),(available-inset)/minimumSlots);
 double fit=(static_cast<double>(available)-inset)/effectiveSlot;
 int fitted=std::max(minimumSlots,static_cast<int>(std::min(fit,static_cast<double>(std::numeric_limits<int>::max()))));
 int capacity=std::max(1,fitted-(static_cast<long long>(count)+1<=fitted?1:3));
 int pages=count?((count-1)/capacity)+1:1;
 offset=std::clamp(offset,0,std::max(0,(pages-1)*capacity));offset=(offset/capacity)*capacity;
 int shown=std::min(capacity,std::max(0,count-offset));
 return {offset,shown,capacity,offset>0,offset+shown<count,effectiveSlot,inset};
}
Edge nearestEdge(Rect w,int x,int y){int left=std::abs(x-w.left),right=std::abs(x-w.right),bottom=std::abs(y-w.bottom);return bottom<=left&&bottom<=right?Edge::Bottom:left<=right?Edge::Left:Edge::Right;}
Rect snapRect(Rect w,int zone,int gap){int mx=w.left+w.width()/2,my=w.top+w.height()/2;Rect r=w;switch(zone){case 0:r.right=mx;break;case 1:r.left=mx;break;case 2:r.right=mx;r.bottom=my;break;case 3:r.left=mx;r.bottom=my;break;case 4:r.right=mx;r.top=my;break;case 5:r.left=mx;r.top=my;break;default:break;}int g=std::clamp(gap,0,std::max(0,std::min(r.width(),r.height())/4));r.left+=g;r.top+=g;r.right-=g;r.bottom-=g;return r;}


namespace {
std::string dockKey(std::string s){
 for(char& c:s){if(c=='\\')c='/';if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');}
 return s;
}
std::string taskAppName(const TaskRecord& task){
 auto slash=task.target.find_last_of("/\\");
 auto file=task.target.substr(slash==std::string::npos?0:slash+1);
 auto suffix=file.find_last_of('.');
 if(suffix!=std::string::npos){
  auto extension=dockKey(file.substr(suffix));
  if(extension==".exe"||extension==".lnk"){
   auto name=file.substr(0,suffix),key=dockKey(name);
   // A shared Shell host names the container; its app title is a better fallback
   // until the installed-app catalog supplies a friendly AUMID label.
   if(dockKey(task.identity).rfind("aumid:",0)==0&&
      (key=="applicationframehost"||key=="runtimebroker"||key=="wwahost"||key=="dllhost"||key=="rundll32")&&
      !task.name.empty())return task.name;
   return name;
  }
 }
 return task.name;
}
}
std::vector<DockGroup> groupDock(const std::vector<Pin>& pins,
 const std::vector<TaskRecord>& tasks, const std::string& display, bool monitorOnly) {
 std::vector<DockGroup> result;
 for(std::size_t i=0;i<pins.size();++i)result.push_back({pins[i],{},static_cast<int>(i),false});
 for(std::size_t i=0;i<tasks.size();++i){
  const auto& task=tasks[i];if(monitorOnly&&task.display!=display)continue;
  auto identity=dockKey(task.identity),path=dockKey(task.target);
  auto it=std::find_if(result.begin(),result.end(),[&](const DockGroup& g){
   auto other=dockKey(g.app.identity);
   if(!identity.empty()&&other==identity)return true;
   // Distinct packaged apps can share the same host executable.
   if(identity.rfind("aumid:",0)==0&&other.rfind("aumid:",0)==0)return false;
   return !path.empty()&&(dockKey(g.app.target)==path||other==path);
  });
  if(it==result.end()){
   result.push_back({{taskAppName(task),task.target,task.identity},{},-1,false});it=std::prev(result.end());
  }else if(it->pinIndex<0){
   auto name=taskAppName(task);if(dockKey(name)<dockKey(it->app.name))it->app.name=std::move(name);
  }
  it->tasks.push_back(i);it->active=it->active||task.active;
 }
 std::stable_sort(result.begin()+static_cast<std::ptrdiff_t>(pins.size()),result.end(),
  [](const DockGroup& a,const DockGroup& b){
   auto an=dockKey(a.app.name),bn=dockKey(b.app.name);if(an!=bn)return an<bn;
   auto ai=dockKey(a.app.identity),bi=dockKey(b.app.identity);if(ai!=bi)return ai<bi;
   return dockKey(a.app.target)<dockKey(b.app.target);
  });
 return result;
}
std::optional<std::size_t> findPinByTarget(const Config& config,const Pin& pin){
 auto target=dockKey(pin.target);if(target.empty())return {};
 for(std::size_t i=0;i<config.pins.size();++i)if(dockKey(config.pins[i].target)==target)return i;
 return {};
}
int advanceSelection(int selected,int delta,int count){
 if(count<=0)return -1;
 return static_cast<int>(std::clamp(static_cast<long long>(selected)+delta,0LL,static_cast<long long>(count)-1));
}
bool reorderPin(Config& config,std::size_t from,std::size_t to){
 if(from>=config.pins.size()||to>=config.pins.size()||from==to)return false;
 auto item=std::move(config.pins[from]);
 config.pins.erase(config.pins.begin()+static_cast<std::ptrdiff_t>(from));
 config.pins.insert(config.pins.begin()+static_cast<std::ptrdiff_t>(to),std::move(item));return true;
}
Rect popupBounds(Rect work,int desiredWidth,int desiredHeight,int dpi){
 double scale=std::clamp(dpi,48,480)/96.0;
 int margin=static_cast<int>(std::lround(16*scale));
 int w=std::min(static_cast<int>(std::lround(std::clamp(desiredWidth,1,4096)*scale)),std::max(1,work.width()-2*margin));
 int h=std::min(static_cast<int>(std::lround(std::clamp(desiredHeight,1,4096)*scale)),std::max(1,work.height()-2*margin));
 int left=work.left+(work.width()-w)/2,top=work.top+(work.height()-h)/2;
 return {left,top,left+w,top+h};
}
bool RefreshGate::request(){if(paused||pending)return false;pending=true;return true;}
int scaledRadius(int radiusDips,int dpi){return static_cast<int>(std::lround(std::clamp(radiusDips,0,28)*std::clamp(dpi,48,480)/96.0));}
IconSize boundedIconSize(int width,int height,int limit){
 if(width<=0||height<=0||limit<=0)return {};
 limit=std::clamp(limit,1,128);int largest=std::max(width,height);
 if(largest<=limit)return {width,height};
 return {std::max(1,static_cast<int>(static_cast<long long>(width)*limit/largest)),
         std::max(1,static_cast<int>(static_cast<long long>(height)*limit/largest))};
}
bool recoveryReady(bool processAlive,bool acknowledged){return processAlive&&acknowledged;}
}
