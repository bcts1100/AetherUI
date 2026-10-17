#include "personalization.hpp"
#include <algorithm>
#include <array>
#include <cmath>
namespace aether {
namespace {
double luminance(std::uint32_t rgb){auto linear=[](unsigned c){double v=c/255.;return v<=.04045?v/12.92:std::pow((v+.055)/1.055,2.4);};return .2126*linear((rgb>>16)&255)+.7152*linear((rgb>>8)&255)+.0722*linear(rgb&255);}
std::uint32_t mix(std::uint32_t a,std::uint32_t b,double amount){std::uint32_t result=0;for(int shift:{0,8,16})result|=static_cast<std::uint32_t>(std::lround(((a>>shift)&255)*(1-amount)+((b>>shift)&255)*amount))<<shift;return result;}
}
double contrastRatio(std::uint32_t a,std::uint32_t b){double x=luminance(a),y=luminance(b);return (std::max(x,y)+.05)/(std::min(x,y)+.05);}
std::array<std::uint8_t,4> colorRefBgra(std::uint32_t colorRef){return {static_cast<std::uint8_t>(colorRef>>16),static_cast<std::uint8_t>(colorRef>>8),static_cast<std::uint8_t>(colorRef),255};}
std::vector<Theme> wallpaperPalettes(const Theme& base,std::span<const std::uint8_t> bytes,unsigned width,unsigned height){
 const std::uint64_t total=static_cast<std::uint64_t>(width)*height;if(!total||total>500000||bytes.size()!=total*4)return {};
 struct Bin{unsigned count=0,r=0,g=0,b=0;};std::array<Bin,4096> bins{};
 const auto count=static_cast<unsigned>(std::min<std::uint64_t>(8192,total));unsigned accepted=0,opaque=0,sumR=0,sumG=0,sumB=0;
 for(unsigned n=0;n<count;++n){auto offset=static_cast<std::size_t>(n*total/count)*4;if(bytes[offset+3]<240)continue;unsigned b=bytes[offset],g=bytes[offset+1],r=bytes[offset+2];++opaque;sumR+=r;sumG+=g;sumB+=b;const unsigned high=std::max({r,g,b}),low=std::min({r,g,b});if(high<36||low>235)continue;auto& bin=bins[(r>>4)*256+(g>>4)*16+(b>>4)];++bin.count;bin.r+=r;bin.g+=g;bin.b+=b;++accepted;}
 if(!opaque)return {};std::uint32_t dominant=((sumR/opaque)<<16)|((sumG/opaque)<<8)|(sumB/opaque),vivid=dominant;double best=-1,vividBest=-1;
 for(const auto& bin:bins){if(!bin.count)continue;unsigned r=bin.r/bin.count,g=bin.g/bin.count,b=bin.b/bin.count;double saturation=(std::max({r,g,b})-std::min({r,g,b}))/255.;double score=bin.count*(.35+saturation*1.4);auto rgb=(r<<16)|(g<<8)|b;if(score>best){best=score;dominant=rgb;}if(bin.count>=std::max(1u,accepted/100)&&score*(.3+saturation*2)>vividBest){vividBest=score*(.3+saturation*2);vivid=rgb;}}
 std::vector<Theme> result;
 for(int variant=0;variant<3;++variant){Theme t=base;const bool light=variant==1;const auto accent=variant==2?vivid:dominant;t.name=variant==0?"Wallpaper Tonal":light?"Wallpaper Light":"Wallpaper Vivid";
  t.background=mix(light?0xFAFCF8u:0x0E1412u,accent,light?.07:.12);t.surface=mix(t.background,light?0xFFFFFFu:accent,light?.5:.16);t.raised=mix(t.surface,light?accent:0xFFFFFFu,light?.12:.065);
  t.foreground=light?0x142019:0xEDF5EE;t.muted=light?0x3F5145:0xB5CABC;t.accent=accent;
  for(int n=0;n<64&&std::min({contrastRatio(t.accent,t.background),contrastRatio(t.accent,t.surface),contrastRatio(t.accent,t.raised)})<3.;++n)t.accent=mix(t.accent,light?0x000000u:0xFFFFFFu,.06);
  result.push_back(std::move(t));
 }
 return result;
}
std::vector<Rect> barShapes(BarStyle style,bool top,int width,int height,int padding,int slots,bool vertical,int slotSize){
 if(width<=0||height<=0)return {};padding=std::clamp(padding,0,12);
 if(style==BarStyle::Classic||style==BarStyle::Solid||style==BarStyle::Accent)return {{0,0,width,height}};
 const int inset=std::min(padding,std::max(0,(std::min(width,height)-24)/2));
 if(style==BarStyle::Floating||(top&&width<1200))return {{inset,inset,width-inset,height-inset}};
 if(top){const int left=std::min(420,width*28/100);return {{inset,inset,left,height-inset},{width/2-170,inset,width/2+170,height-inset},{width-220,inset,width-inset,height-inset}};}
 slots=std::clamp(slots,1,132);const int length=vertical?height:width,cross=vertical?width:height;slotSize=std::clamp(slotSize,1,std::max(1,std::min(cross,length/slots)));const int start=(length-slots*slotSize)/2;
 std::vector<Rect> result;for(int n=0;n<slots;++n){int a=start+n*slotSize+2,b=start+(n+1)*slotSize-2;if(b<=a){a=start+n*slotSize;b=a+slotSize;}result.push_back(vertical?Rect{inset,a,width-inset,b}:Rect{a,inset,b,height-inset});}return result;
}
Theme withPaletteColors(const Theme& base,const Theme& palette){auto t=base;t.background=palette.background;t.surface=palette.surface;t.raised=palette.raised;t.foreground=palette.foreground;t.muted=palette.muted;t.accent=palette.accent;return t;}
bool moveTopModule(DisplayOptions& d,TopModule module,int zone,std::optional<TopModule> before){
 if(before&&*before==module)return false;const int id=static_cast<int>(module);if(id<0||id>=5||zone<0||zone>2)return false;
 auto copy=d;std::vector<int> order;for(auto n:d.moduleOrder)if(n!=id)order.push_back(n);
 auto at=before?std::find(order.begin(),order.end(),static_cast<int>(*before)):order.end();order.insert(at,id);std::copy(order.begin(),order.end(),d.moduleOrder.begin());d.moduleZones[id]=zone;return d!=copy;
}
std::vector<ModuleBox> topModuleBoxes(const DisplayOptions& d,BarStyle style,int width,int height,int padding){
 if(width<1||height<1)return {};auto shapes=barShapes(style,true,width,height,padding);std::array<Rect,3> zones;
 if(shapes.size()==3){std::copy(shapes.begin(),shapes.end(),zones.begin());}else{auto r=shapes.front();const int gap=width>=900?12:3;const int third=r.width()/3;zones={Rect{r.left,r.top,r.left+third-gap,r.bottom},Rect{r.left+third+gap,r.top,r.right-third-gap,r.bottom},Rect{r.right-third+gap,r.top,r.right,r.bottom}};}
 std::vector<ModuleBox> result;constexpr int desired[]={136,180,280,122,42};
 for(int zone=0;zone<3;++zone){std::vector<int> modules;int total=0;for(int id:d.moduleOrder){if(id<0||id>=5||d.moduleZones[id]!=zone||(id==1&&!d.focusedTitle)||(id==2&&!d.clock)||(id==3&&!d.status))continue;modules.push_back(id);total+=desired[id];}if(modules.empty())continue;auto r=zones[zone];r.left+=4;r.right-=4;const int usable=std::max(1,r.width());int length=std::min(total,usable),x=zone==1?r.left+(usable-length)/2:zone==2?r.right-length:r.left;int used=0;for(std::size_t n=0;n<modules.size();++n){int id=modules[n],next=n+1==modules.size()?length:used+std::max(1,desired[id]*length/total);result.push_back({static_cast<TopModule>(id),{x+used,r.top,x+next,r.bottom},zone});used=next;}}
 return result;
}

}
