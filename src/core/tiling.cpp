#include "tiling.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <set>
#include <sstream>
namespace aether {
namespace {
bool normalized(Rect r){return r.left>=0&&r.top>=0&&r.right<=10000&&r.bottom<=10000&&r.left<r.right&&r.top<r.bottom;}
bool area(Rect r){auto w=static_cast<std::int64_t>(r.right)-r.left,h=static_cast<std::int64_t>(r.bottom)-r.top;return w>0&&h>0&&w<=1000000&&h<=1000000;}
bool identifier(std::string_view id){if(id.empty()||id.size()>40)return false;for(unsigned char c:id)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'))return false;return true;}
bool dataName(std::string_view name,std::size_t units=80){
 if(name.empty()||name.size()>320||name.find_first_not_of(" \t")==name.npos)return false;
 std::size_t count=0;for(std::size_t i=0;i<name.size();){unsigned char c=static_cast<unsigned char>(name[i++]);unsigned cp=c,more=0;
  if(c<0x80){if(c<32||c==127)return false;}
  else if(c>=0xc2&&c<=0xdf){cp=c&31;more=1;}else if(c>=0xe0&&c<=0xef){cp=c&15;more=2;}else if(c>=0xf0&&c<=0xf4){cp=c&7;more=3;}else return false;
  for(unsigned j=0;j<more;++j){if(i==name.size())return false;unsigned char next=static_cast<unsigned char>(name[i++]);if((next&0xc0)!=0x80)return false;cp=(cp<<6)|(next&63);}
  if((more==1&&cp<0x80)||(more==2&&cp<0x800)||(more==3&&cp<0x10000)||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff)||(cp>=0x80&&cp<=0x9f)||cp==0x2028||cp==0x2029)return false;
  count+=cp>0xffff?2:1;if(count>units)return false;
 }return true;
}
int gapPixels(int dips,int dpi){return static_cast<int>((static_cast<std::int64_t>(std::clamp(dips,0,32))*std::clamp(dpi,48,480)+48)/96);}
std::optional<Rect> inset(Rect r,int gap,int minWidth,int minHeight){
 if(!area(r)||minWidth<1||minHeight<1)return {};
 if(static_cast<std::int64_t>(r.width())-2*gap<minWidth||static_cast<std::int64_t>(r.height())-2*gap<minHeight)return {};
 return Rect{r.left+gap,r.top+gap,r.right-gap,r.bottom-gap};
}
std::string encode(std::string_view text){std::string out;const char* digits="0123456789ABCDEF";for(unsigned char c:text){if(c<=32||c=='%'||c=='='||c=='['||c==']'){out+='%';out+=digits[c>>4];out+=digits[c&15];}else out+=static_cast<char>(c);}return out;}
int hex(char c){if(c>='0'&&c<='9')return c-'0';if(c>='A'&&c<='F')return c-'A'+10;if(c>='a'&&c<='f')return c-'a'+10;return -1;}
std::optional<std::string> decode(std::string_view text){std::string out;for(std::size_t i=0;i<text.size();++i){if(text[i]=='%'){if(i+2>=text.size()||hex(text[i+1])<0||hex(text[i+2])<0)return {};out+=static_cast<char>(hex(text[i+1])*16+hex(text[i+2]));i+=2;}else out+=text[i];}return out;}
std::string trim(std::string_view text){auto first=text.find_first_not_of(" \t\r");return first==text.npos?std::string{}:std::string(text.substr(first,text.find_last_not_of(" \t\r")-first+1));}
bool number(std::string_view text,int& value,int low,int high){auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),value);return error==std::errc{}&&end==text.data()+text.size()&&value>=low&&value<=high;}
}
TilingOptions Config::tilingForDisplay(const std::string& name) const{auto found=tilingDisplays.find(name);return found==tilingDisplays.end()?tilingDefaults:found->second;}
bool validSnapLayout(const SnapLayout& layout){
 if(!identifier(layout.id)||layout.id=="balanced"||layout.id=="master"||!dataName(layout.name)||layout.zones.empty()||layout.zones.size()>SnapZoneLimit||layout.gapDips<0||layout.gapDips>32)return false;
 for(std::size_t i=0;i<layout.zones.size();++i){auto r=layout.zones[i];if(!normalized(r))return false;for(std::size_t j=0;j<i;++j){auto other=layout.zones[j];if(std::max(r.left,other.left)<std::min(r.right,other.right)&&std::max(r.top,other.top)<std::min(r.bottom,other.bottom))return false;}}
 return true;
}
const SnapLayout* findSnapLayout(const Config& config,std::string_view id){static const SnapLayout balanced{"balanced","Balanced grid",{},8},master{"master","Master and stack",{},8};if(id=="balanced")return &balanced;if(id=="master")return &master;for(const auto& layout:config.snapLayouts)if(layout.id==id&&validSnapLayout(layout))return &layout;return nullptr;}
bool validTilingConfig(const Config& config){
 if(config.snapLayouts.size()>SnapLayoutLimit||config.tilingDisplays.size()>TilingDisplayLimit)return false;std::set<std::string> ids;
 for(const auto& layout:config.snapLayouts)if(!validSnapLayout(layout)||!ids.insert(layout.id).second)return false;
 if(!findSnapLayout(config,config.tilingDefaults.layoutId))return false;
 for(const auto& [display,policy]:config.tilingDisplays)if(!dataName(display,256)||display.size()>256||!findSnapLayout(config,policy.layoutId))return false;return true;
}
std::optional<Rect> zoneRect(Rect work,Rect zone,int gapDips,int dpi,int minWidth,int minHeight){
 if(!area(work)||!normalized(zone)||gapDips<0||gapDips>32)return {};
 auto x=[&](int value){return work.left+static_cast<int>(static_cast<std::int64_t>(work.width())*value/10000);};auto y=[&](int value){return work.top+static_cast<int>(static_cast<std::int64_t>(work.height())*value/10000);};
 return inset({x(zone.left),y(zone.top),x(zone.right),y(zone.bottom)},gapPixels(gapDips,dpi),minWidth,minHeight);
}
std::vector<Rect> tileRects(Rect work,std::size_t count,const SnapLayout& layout,int dpi,int minWidth,int minHeight){
 std::vector<Rect> result;if(!area(work)||!count||count>TilingWindowLimit||layout.gapDips<0||layout.gapDips>32)return result;
 const int n=static_cast<int>(count),gap=gapPixels(layout.gapDips,dpi);
 auto add=[&](Rect r){auto cell=inset(r,gap,minWidth,minHeight);if(!cell)return false;result.push_back(*cell);return true;};
 if(!layout.zones.empty()){if(!validSnapLayout(layout)||count>layout.zones.size())return {};for(std::size_t i=0;i<count;++i){auto r=zoneRect(work,layout.zones[i],layout.gapDips,dpi,minWidth,minHeight);if(!r)return {};result.push_back(*r);}return result;}
 if(layout.id=="master"&&n>1){bool portrait=work.height()>work.width();int split=portrait?work.top+work.height()*3/5:work.left+work.width()*3/5;
  if(!add(portrait?Rect{work.left,work.top,work.right,split}:Rect{work.left,work.top,split,work.bottom}))return {};
  for(int i=0;i<n-1;++i){if(portrait){int a=work.left+static_cast<int>(static_cast<std::int64_t>(work.width())*i/(n-1)),b=work.left+static_cast<int>(static_cast<std::int64_t>(work.width())*(i+1)/(n-1));if(!add({a,split,b,work.bottom}))return {};}else{int a=work.top+static_cast<int>(static_cast<std::int64_t>(work.height())*i/(n-1)),b=work.top+static_cast<int>(static_cast<std::int64_t>(work.height())*(i+1)/(n-1));if(!add({split,a,work.right,b}))return {};}}
 }else{
  if(layout.id!="balanced"&&layout.id!="master")return {};
  int columns=std::clamp(static_cast<int>(std::lround(std::sqrt(static_cast<double>(n)*work.width()/work.height()))),1,n);int rows=(n+columns-1)/columns;
  for(int row=0,index=0;row<rows;++row){int inRow=std::min(columns,n-index);int top=work.top+static_cast<int>(static_cast<std::int64_t>(work.height())*row/rows),bottom=work.top+static_cast<int>(static_cast<std::int64_t>(work.height())*(row+1)/rows);
   for(int column=0;column<inRow;++column,++index){int left=work.left+static_cast<int>(static_cast<std::int64_t>(work.width())*column/inRow),right=work.left+static_cast<int>(static_cast<std::int64_t>(work.width())*(column+1)/inRow);if(!add({left,top,right,bottom}))return {};}
  }
 }return result;
}
std::optional<std::size_t> adjacentMonitorIndex(const std::vector<Rect>& monitors,std::size_t source,MoveDirection direction){
 if(source>=monitors.size()||!area(monitors[source]))return {};auto origin=monitors[source];double ox=(static_cast<double>(origin.left)+origin.right)/2,oy=(static_cast<double>(origin.top)+origin.bottom)/2;std::optional<std::size_t> best;double score=std::numeric_limits<double>::max();
 for(std::size_t i=0;i<monitors.size();++i){if(i==source||!area(monitors[i]))continue;auto target=monitors[i];double dx=(static_cast<double>(target.left)+target.right)/2-ox,dy=(static_cast<double>(target.top)+target.bottom)/2-oy;double along=direction==MoveDirection::Left?-dx:direction==MoveDirection::Right?dx:direction==MoveDirection::Up?-dy:dy;if(along<=0)continue;bool horizontal=direction==MoveDirection::Left||direction==MoveDirection::Right;double across=horizontal?dy:dx;bool overlap=horizontal?std::max(origin.top,target.top)<std::min(origin.bottom,target.bottom):std::max(origin.left,target.left)<std::min(origin.right,target.right);double candidate=along*along+across*across*4+(overlap?0:1e12);if(candidate<score){score=candidate;best=i;}}
 return best;
}
std::optional<Rect> transferRect(Rect source,Rect target,Rect bounds,int sourceDpi,int targetDpi,int minWidth,int minHeight){
 if(!area(source)||!area(target)||!area(bounds)||minWidth<1||minHeight<1||target.width()<minWidth||target.height()<minHeight)return {};
 double scale=static_cast<double>(std::clamp(targetDpi,48,480))/std::clamp(sourceDpi,48,480);
 int width=std::clamp(static_cast<int>(std::min(1000000.0,std::round(bounds.width()*scale))),minWidth,target.width()),height=std::clamp(static_cast<int>(std::min(1000000.0,std::round(bounds.height()*scale))),minHeight,target.height());
 double cx=std::clamp(((static_cast<double>(bounds.left)+bounds.right)/2-source.left)/source.width(),0.0,1.0),cy=std::clamp(((static_cast<double>(bounds.top)+bounds.bottom)/2-source.top)/source.height(),0.0,1.0);
 int left=std::clamp(target.left+static_cast<int>(std::lround(cx*target.width()))-width/2,target.left,target.right-width),top=std::clamp(target.top+static_cast<int>(std::lround(cy*target.height()))-height/2,target.top,target.bottom-height);return Rect{left,top,left+width,top+height};
}
bool parseTilingSettings(Config& output,std::string_view text){
 auto fail=[&]{output.tilingDefaults={};output.tilingDisplays.clear();output.snapLayouts.clear();return false;};if(text.size()>1024*1024)return fail();
 std::map<std::string,std::string> values;bool in=false,found=false;std::size_t offset=0,lines=0;
 while(offset<text.size()){if(++lines>12000)return fail();auto end=text.find('\n',offset);if(end==text.npos)end=text.size();auto line=trim(text.substr(offset,end-offset));offset=end+1;if(line.empty()||line[0]=='#'||line[0]==';')continue;
  if(line.front()=='['){if(line=="[tiling]"){if(found)return fail();in=true;found=true;}else in=false;continue;}if(!in)continue;auto equal=line.find('=');if(equal==line.npos||equal==0||line.size()>8192||values.size()>512)return fail();auto key=trim(std::string_view(line).substr(0,equal));if(values.contains(key))return fail();values[key]=trim(std::string_view(line).substr(equal+1));
 }
 if(!found){output.tilingDefaults={};output.tilingDisplays.clear();output.snapLayouts.clear();return true;}
 Config parsed;std::set<std::string> consumed;
 auto take=[&](const std::string& key)->std::optional<std::string>{auto foundValue=values.find(key);if(foundValue==values.end())return {};consumed.insert(key);return foundValue->second;};
 auto integer=[&](const std::string& key,int& value,int low,int high){auto v=take(key);return v&&number(*v,value,low,high);};
 auto policy=[&](const std::string& prefix,TilingOptions& policyValue){int enabled=0;if(!integer(prefix+"enabled",enabled,0,1))return false;auto id=take(prefix+"layoutId");if(!id||!identifier(*id))return false;policyValue={enabled!=0,*id};return true;};
 int displays=0,layouts=0;if(!policy("",parsed.tilingDefaults)||!integer("displayCount",displays,0,32)||!integer("layoutCount",layouts,0,16))return fail();
 for(int n=0;n<displays;++n){auto prefix="display"+std::to_string(n)+".";auto encoded=take(prefix+"name");auto name=encoded?decode(*encoded):std::nullopt;TilingOptions policyValue;if(!name||parsed.tilingDisplays.contains(*name)||!policy(prefix,policyValue))return fail();parsed.tilingDisplays.emplace(*name,policyValue);}
 for(int n=0;n<layouts;++n){auto prefix="layout"+std::to_string(n)+".";auto id=take(prefix+"id"),encoded=take(prefix+"name");auto name=encoded?decode(*encoded):std::nullopt;SnapLayout layout;int zones=0;if(!id||!name||!integer(prefix+"gapDips",layout.gapDips,0,32)||!integer(prefix+"zoneCount",zones,1,12))return fail();layout.id=*id;layout.name=*name;
  for(int z=0;z<zones;++z){auto value=take(prefix+"zone"+std::to_string(z));if(!value)return fail();Rect r;int* coords[]={&r.left,&r.top,&r.right,&r.bottom};std::size_t start=0;for(int k=0;k<4;++k){auto comma=value->find(',',start);if(k==3){if(comma!=value->npos)return fail();comma=value->size();}else if(comma==value->npos)return fail();if(!number(std::string_view(*value).substr(start,comma-start),*coords[k],0,10000))return fail();start=comma+1;}layout.zones.push_back(r);}parsed.snapLayouts.push_back(std::move(layout));
 }
 if(consumed.size()!=values.size()||!validTilingConfig(parsed))return fail();output.tilingDefaults=parsed.tilingDefaults;output.tilingDisplays=std::move(parsed.tilingDisplays);output.snapLayouts=std::move(parsed.snapLayouts);return true;
}
std::string serializeTilingSettings(const Config& input){
 Config fallback;const Config& config=validTilingConfig(input)?input:fallback;std::ostringstream out;out<<"\n[tiling]\nenabled="<<config.tilingDefaults.enabled<<"\nlayoutId="<<config.tilingDefaults.layoutId<<"\ndisplayCount="<<config.tilingDisplays.size()<<"\nlayoutCount="<<config.snapLayouts.size()<<'\n';std::size_t index=0;
 for(const auto& [display,policy]:config.tilingDisplays){auto prefix="display"+std::to_string(index++)+".";out<<prefix<<"name="<<encode(display)<<'\n'<<prefix<<"enabled="<<policy.enabled<<'\n'<<prefix<<"layoutId="<<policy.layoutId<<'\n';}
 for(std::size_t i=0;i<config.snapLayouts.size();++i){const auto& layout=config.snapLayouts[i];auto prefix="layout"+std::to_string(i)+".";out<<prefix<<"id="<<layout.id<<'\n'<<prefix<<"name="<<encode(layout.name)<<'\n'<<prefix<<"gapDips="<<layout.gapDips<<'\n'<<prefix<<"zoneCount="<<layout.zones.size()<<'\n';for(std::size_t z=0;z<layout.zones.size();++z){auto r=layout.zones[z];out<<prefix<<"zone"<<z<<'='<<r.left<<','<<r.top<<','<<r.right<<','<<r.bottom<<'\n';}}
 return out.str();
}
}
