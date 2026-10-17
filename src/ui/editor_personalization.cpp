#include "editor.hpp"
#include "app.hpp"
#include "extensions.hpp"
#include "core/personalization.hpp"
#include "platform/dialogs.hpp"
#include <commdlg.h>
#include <limits>
namespace aether::ui {
namespace {constexpr std::uint64_t PaletteScope=std::numeric_limits<std::uint64_t>::max();struct Interaction{App& app;explicit Interaction(App& a):app(a){app.beginInteraction();}~Interaction(){app.endInteraction();}};}
void Editor::chooseInstalledFont(FontPart part){
 const auto index=static_cast<std::size_t>(part);const auto& choice=theme_.fonts[index];const bool custom=part!=FontPart::General&&!choice.family.empty();
 LOGFONTW font{};font.lfWeight=custom?choice.weight:theme_.fontWeight;font.lfItalic=static_cast<BYTE>(custom?choice.italic:theme_.fontItalic);auto family=win::wide(custom?choice.family:theme_.fontFamily);std::copy_n(family.data(),std::min(family.size(),static_cast<std::size_t>(LF_FACESIZE-1)),font.lfFaceName);
 CHOOSEFONTW dialog{sizeof(dialog)};dialog.hwndOwner=hwnd_;dialog.lpLogFont=&font;dialog.Flags=CF_INITTOLOGFONTSTRUCT|CF_SCREENFONTS|CF_FORCEFONTEXIST|CF_NOSIZESEL|CF_NOSCRIPTSEL|CF_NOVERTFONTS;
 Interaction interaction(app_);if(ChooseFontW(&dialog)){auto config=app_.config();auto name=win::utf8(font.lfFaceName);if(validFontFamily(name)){if(part!=FontPart::General){auto& selected=config.theme.fonts[index];selected.family=name;selected.weight=std::clamp(static_cast<int>(font.lfWeight),300,700);selected.italic=font.lfItalic!=0;}else{config.theme.fontFamily=name;config.theme.fontWeight=std::clamp(static_cast<int>(font.lfWeight),300,700);config.theme.fontItalic=font.lfItalic!=0;}app_.apply(std::move(config));paletteNotice_=L"Installed font applied. Fonts are never downloaded or installed by Aether.";}}invalidate();
}
void Editor::buildPalette(bool chooseImage){
 paletteRequest_=0;paletteWaitingImage_=false;palettePreviews_.clear();paletteChoice_=0;
 if(chooseImage){const COMDLG_FILTERSPEC filters[]={{L"Wallpaper images",L"*.png;*.jpg;*.jpeg;*.bmp"}};Interaction interaction(app_);auto selected=win::chooseFile(hwnd_,false,L"Choose an image for palette preview",filters,1);if(!selected){paletteNotice_=L"Image selection canceled. Your appearance is unchanged.";invalidate();return;}palettePath_=selected->wstring();}
 else{
  int mode=0;for(const auto& item:app_.workspaces().items())if(item.current){const auto appearance=app_.workspaces().appearance(item.id);mode=appearance.background;palettePath_=appearance.image;break;}
  if(mode>=8)palettePath_=(win::executablePath().parent_path()/L"assets"/L"wallpapers"/L"woodland-zen-v1.png").wstring();
  else if((mode>=1&&mode<=6)||(!mode&&app_.config().animatedWallpaper)){
   const int scene=mode?mode<=3?mode:mode-3:app_.config().animatedWallpaper;const std::uint32_t colors[]={scene==1?0x397F91u:scene==2?0x765DB8u:0xC16D57u,scene==1?0x52BC97u:scene==2?0x3D86B0u:0x896293u,0x081321};std::vector<std::uint8_t> pixels;for(auto rgb:colors){pixels.push_back(static_cast<std::uint8_t>(rgb));pixels.push_back(static_cast<std::uint8_t>(rgb>>8));pixels.push_back(static_cast<std::uint8_t>(rgb>>16));pixels.push_back(255);}palettePreviews_=wallpaperPalettes(app_.config().theme,pixels,3,1);paletteNotice_=L"Three previews from the current scene colors. Apply when ready.";invalidate();return;
  }else if(mode!=7){palettePath_.clear();paletteRequest_=app_.wallpaper().requestQuery(app_.displays(),PaletteScope,true);paletteNotice_=paletteRequest_?L"Reading the current Windows wallpaper…":L"Wallpaper service is busy. Try again or choose an image.";invalidate();return;}
 }
 paletteWaitingImage_=!palettePath_.empty();paletteNotice_=L"Reading local artwork for three palette previews…";updatePaletteImage();invalidate();
}
void Editor::paletteCompletion(const win::WallpaperCompletion& completion){
 if(!paletteRequest_||completion.id!=paletteRequest_||completion.scope!=PaletteScope)return;paletteRequest_=0;
 if(completion.outcome!=win::ProviderOutcome::Ok||completion.result.status!=WallpaperStatus::Ok||!completion.result.after){paletteNotice_=L"Current wallpaper could not be read. Choose an image instead.";invalidate();return;}
 const auto& snapshot=*completion.result.after;std::vector<Rect> bounds;for(const auto& monitor:app_.displays())bounds.push_back(monitor.bounds);auto mapping=mapWallpaperDisplays(snapshot,bounds);
 if(!mapping||mapping->empty()){paletteNotice_=L"Display mapping changed. Try generating the palette again.";invalidate();return;}
 auto selected=std::find_if(snapshot.monitors.begin(),snapshot.monitors.end(),[&](const auto& monitor){return monitor.display.device==mapping->front().device;});
 if(selected==snapshot.monitors.end()||selected->path.empty()){const auto pixel=colorRefBgra(snapshot.color);palettePreviews_=wallpaperPalettes(app_.config().theme,pixel,1,1);paletteNotice_=L"Three previews from the Windows solid background. Apply when ready.";}else{palettePath_=selected->path;paletteWaitingImage_=true;paletteNotice_=L"Reading the primary display's Windows wallpaper…";updatePaletteImage();}invalidate();
}
void Editor::updatePaletteImage(){
 if(!paletteWaitingImage_)return;auto image=app_.icons().requestWorkspaceImage(palettePath_);if(image){palettePreviews_=wallpaperPalettes(app_.config().theme,image->bytes,image->width,image->height);paletteWaitingImage_=false;paletteNotice_=palettePreviews_.empty()?L"This image has no usable opaque colors. Choose another image.":L"Three local previews are ready. Nothing changes until you apply one.";invalidate();}else if(app_.icons().workspaceImageFailed(palettePath_)){paletteWaitingImage_=false;paletteNotice_=L"Image unavailable. Choose a PNG, JPEG or BMP up to 8 MiB and 4096 × 4096.";invalidate();}
}
void Editor::applyPalette(){
 if(paletteChoice_<0||paletteChoice_>=static_cast<int>(palettePreviews_.size()))return;auto config=app_.config();const auto before=config.theme;const auto& palette=palettePreviews_[static_cast<std::size_t>(paletteChoice_)];config.theme=withPaletteColors(config.theme,palette);config.theme.name=palette.name;paletteUndo_.record(before,config.theme);app_.apply(std::move(config));paletteNotice_=L"Palette applied. Fonts, bar designs, layout, wallpaper and taskbar hiding are preserved.";invalidate();
}
void Editor::personalize(Canvas& c,float y){
 const float width=c.size().width-244,card=(width-24)/3,step=(width-24)/5;const auto& theme=app_.config().theme;
 if(page_==4){
 c.text(L"COLORS FROM YOUR WALLPAPER",box(222,y,width,24),theme_.muted,10,true);
 button(c,box(220,y+33,width/2-6,36),L"Build from current wallpaper",[this]{buildPalette();});button(c,box(232+width/2,y+33,width/2-6,36),L"Choose palette image…",[this]{buildPalette(true);});
 c.text(paletteNotice_,box(222,y+78,width,38),theme_.muted,11);
 for(int n=0;n<3;++n){auto r=box(220+n*(card+12),y+123,card,112);if(n>=static_cast<int>(palettePreviews_.size())){c.fill(r,theme_.surface,10);c.text(n==0?L"Tonal":n==1?L"Light":L"Vivid",r,theme_.muted,14,true,1);continue;}const auto& t=palettePreviews_[static_cast<std::size_t>(n)];c.fill(r,t.background,10);c.text(n==0?L"Tonal":n==1?L"Light":L"Vivid",box(r.left+10,r.top+9,card-20,30),t.foreground,14,true);const std::uint32_t colors[]={t.surface,t.raised,t.foreground,t.muted,t.accent};for(int k=0;k<5;++k)c.circle(r.left+18+k*(card-36)/4,r.top+70,6,colors[k]);if(n==paletteChoice_)c.stroke(r,theme_.accent,10,2);action(r,[this,n]{paletteChoice_=n;invalidate();},n==0?L"Preview Tonal palette":n==1?L"Preview Light palette":L"Preview Vivid palette",ROLE_SYSTEM_LISTITEM,STATE_SYSTEM_FOCUSABLE|STATE_SYSTEM_SELECTABLE|(n==paletteChoice_?STATE_SYSTEM_SELECTED:0));}
 if(!palettePreviews_.empty())button(c,box(220,y+248,width/2-6,36),L"Apply selected palette",[this]{applyPalette();});
 if(paletteUndo_.available(theme))button(c,box(232+width/2,y+248,width/2-6,36),L"Undo wallpaper palette",[this]{auto previous=paletteUndo_.take(app_.config().theme);if(previous){auto config=app_.config();config.theme=*previous;app_.apply(std::move(config));paletteNotice_=L"Previous palette restored.";}invalidate();});
 c.text(L"WORKSPACE COLORS",box(222,y+328,width,24),theme_.muted,10,true);
 button(c,box(220,y+364,width,36),app_.config().autoWallpaperColors?L"Automatic wallpaper colors: on":L"Automatic wallpaper colors: off",[this]{auto config=app_.config();config.autoWallpaperColors=!config.autoWallpaperColors;app_.apply(std::move(config));},app_.config().autoWallpaperColors,L"Toggle automatic workspace colors");
 const wchar_t* schemeNames[]={L"Tonal",L"Light",L"Vivid"};for(int n=0;n<3;++n)button(c,box(220+n*(card+12),y+412,card,36),schemeNames[n],[this,n]{auto config=app_.config();config.wallpaperColorScheme=n;app_.apply(std::move(config));},app_.config().wallpaperColorScheme==n,std::wstring(L"Automatic color scheme: ")+schemeNames[n]);
 c.text(L"Automatic colors follow workspace changes. Turning them off restores your saved palette.",box(222,y+460,width,38),theme_.muted,11);

 button(c,box(220,y+570,width,38),L"Open background library",[this]{app_.openBackgrounds();});
 }else if(page_==7){
 c.text(L"INSTALLED FONTS",box(222,y+0,width,24),theme_.muted,10,true);c.text(win::wide(theme.fontFamily)+L" · Aa Bb 0123",box(222,y+33,width,42),theme_.foreground,22,true);
 button(c,box(220,y+88,width,36),L"Browse installed fonts…",[this]{chooseInstalledFont();},false,L"Browse installed fonts");
 const int weights[]={300,400,500,600,700};const wchar_t* weightNames[]={L"Light",L"Regular",L"Medium",L"Semibold",L"Bold"};
 for(int n=0;n<5;++n)button(c,box(220+n*(step+6),y+136,step,34),weightNames[n],[this,n]{auto config=app_.config();const int values[]={300,400,500,600,700};config.theme.fontWeight=values[n];app_.apply(std::move(config));},theme.fontWeight==weights[n],std::wstring(L"Font weight: ")+weightNames[n]);
 button(c,box(220,y+181,width,34),theme.fontItalic?L"Italic: on":L"Italic: off",[this]{auto config=app_.config();config.theme.fontItalic=!config.theme.fontItalic;app_.apply(std::move(config));},theme.fontItalic,L"Toggle italic text");
 button(c,box(220,y+248,width/2-6,34),L"Smaller general text",[this]{auto config=app_.config();config.theme.fontScale=std::max(85,config.theme.fontScale-5);app_.apply(std::move(config));});
 button(c,box(232+width/2,y+248,width/2-6,34),L"Larger general text",[this]{auto config=app_.config();config.theme.fontScale=std::min(125,config.theme.fontScale+5);app_.apply(std::move(config));});
 c.text(L"FONTS FOR EACH PART",box(222,y+300,width,24),theme_.muted,10,true);
 const wchar_t* parts[]={L"General",L"Activities label",L"Focused title",L"Clock",L"Status text",L"Dock and previews",L"Window overview",L"Desktop Studio",L"Desktop widgets",L"Other popups"};
 for(int n=1;n<static_cast<int>(FontPart::Count);++n){const auto& f=theme.fonts[n];float row=y+340+(n-1)*57;auto label=std::wstring(parts[n])+L": "+(f.family.empty()?L"Inherit general":win::wide(f.family));button(c,box(220,row,width-250,38),label,[this,n]{chooseInstalledFont(static_cast<FontPart>(n));},false,std::wstring(L"Choose font for ")+parts[n]);button(c,box(220+width-242,row,76,38),L"Reset",[this,n]{auto config=app_.config();config.theme.fonts[n]={};app_.apply(std::move(config));},false,std::wstring(L"Reset font for ")+parts[n]);button(c,box(220+width-158,row,40,38),L"−",[this,n]{auto config=app_.config();auto& f=config.theme.fonts[n];if(f.family.empty())f={config.theme.fontFamily,config.theme.fontWeight,config.theme.fontScale,config.theme.fontItalic};f.scale=std::max(85,f.scale-5);app_.apply(std::move(config));},false,std::wstring(L"Smaller font for ")+parts[n]);c.text(std::to_wstring(f.family.empty()?theme.fontScale:f.scale)+L"%",box(220+width-113,row,64,38),theme_.muted,11,false,1);button(c,box(220+width-42,row,42,38),L"+",[this,n]{auto config=app_.config();auto& f=config.theme.fonts[n];if(f.family.empty())f={config.theme.fontFamily,config.theme.fontWeight,config.theme.fontScale,config.theme.fontItalic};f.scale=std::min(125,f.scale+5);app_.apply(std::move(config));},false,std::wstring(L"Larger font for ")+parts[n]);}

 }else if(page_==8){
 c.text(L"TOP BAR AND DOCK",box(222,y+0,width,24),theme_.muted,10,true);
 auto designs=[&](float row,const wchar_t* label,BarStyle Theme::*member){c.text(label,box(222,row,width,22),theme_.foreground,13,true);const wchar_t* names[]={L"Classic",L"Flat",L"Floating",L"Segmented",L"Accent rail"};for(int n=0;n<5;++n)button(c,box(220+n*(step+6),row+30,step,36),names[n],[this,n,member]{auto config=app_.config();config.theme.*member=static_cast<BarStyle>(n);app_.apply(std::move(config));},theme.*member==static_cast<BarStyle>(n),std::wstring(label)+L": "+names[n]);};
 designs(y+36,L"Top bar design",&Theme::topBarStyle);designs(y+120,L"Dock design",&Theme::dockStyle);
 const ThemeStyle materials[]={ThemeStyle::Classic,ThemeStyle::Frosted,ThemeStyle::Minimal};const wchar_t* materialNames[]={L"Solid",L"Glass",L"Minimal"};for(int n=0;n<3;++n)button(c,box(220+n*(card+12),y+214,card,36),materialNames[n],[this,n]{auto config=app_.config();const ThemeStyle choices[]={ThemeStyle::Classic,ThemeStyle::Frosted,ThemeStyle::Minimal};config.theme.style=choices[n];app_.apply(std::move(config));},theme.style==materials[n],std::wstring(L"Surface material: ")+materialNames[n]);
 auto number=[&](float row,const wchar_t* label,int Theme::*member,int minimum,int maximum,int increment){c.text(label,box(222,row,205,34),theme_.foreground,13,true);button(c,box(440,row,42,34),L"−",[this,member,minimum,increment]{auto config=app_.config();config.theme.*member=std::max(minimum,config.theme.*member-increment);app_.apply(std::move(config));},false,std::wstring(L"Decrease ")+label);c.text(std::to_wstring(theme.*member),box(486,row,70,34),theme_.foreground,12,false,1);button(c,box(562,row,42,34),L"+",[this,member,maximum,increment]{auto config=app_.config();config.theme.*member=std::min(maximum,config.theme.*member+increment);app_.apply(std::move(config));},false,std::wstring(L"Increase ")+label);};
 number(y+273,L"Top bar height",&Theme::topBarHeight,28,64,2);number(y+319,L"Bar inset",&Theme::barPadding,0,12,1);number(y+365,L"Glass tint",&Theme::surfaceOpacity,20,100,5);
 c.text(L"STARTING POINTS",box(222,y+469,width,24),theme_.muted,10,true);const wchar_t* recipes[]={L"Ubuntu-like",L"Floating glass",L"Islands"};for(int n=0;n<3;++n)button(c,box(220+n*(card+12),y+505,card,38),recipes[n],[this,n]{auto config=app_.config();config.theme.topBarStyle=n==0?BarStyle::Solid:n==1?BarStyle::Floating:BarStyle::Islands;config.theme.dockStyle=n==0?BarStyle::Classic:n==1?BarStyle::Floating:BarStyle::Islands;config.theme.style=n==0?ThemeStyle::Minimal:ThemeStyle::Frosted;config.theme.radius=n==0?8:n==1?16:18;app_.apply(std::move(config));},false,std::wstring(L"Bar recipe: ")+recipes[n]);
 c.text(L"Colors, fonts and designs save with your palette. Layout controls remain under Layout & displays.",box(222,y+562,width,42),theme_.muted,11);c.text(L"Glass uses native blur where available. Narrow top bars keep a single floating group.",box(222,y+607,width,35),theme_.muted,11);

 c.text(L"ARRANGE TOP BAR MODULES",box(222,y+680,width,24),theme_.muted,10,true);
 c.text(L"Right-click a top bar > Arrange modules. Drag modules between left, center and right, then finish arranging. Placement saves per monitor. Clock/status/title toggles remain under Layout & displays.",box(222,y+714,width,68),theme_.muted,11);

 number(y+810,L"Surface border",&Theme::borderWidth,0,3,1);number(y+858,L"Gradient strength",&Theme::gradientStrength,0,100,10);number(y+906,L"Dock highlight",&Theme::dockHighlight,0,100,10);
 c.text(L"Running indicator",box(222,y+960,width,24),theme_.muted,10,true);
 const wchar_t* indicatorNames[]={L"Dots",L"Line",L"Square"};for(int n=0;n<3;++n)button(c,box(220+n*(card+12),y+998,card,34),indicatorNames[n],[this,n]{auto config=app_.config();config.theme.indicator=static_cast<IndicatorStyle>(n);app_.apply(std::move(config));},theme.indicator==static_cast<IndicatorStyle>(n),std::wstring(L"Running indicator: ")+indicatorNames[n]);
 }else if(page_==10){
 button(c,box(220,y+50,width,38),app_.config().widgetsLocked?L"Widget layout: locked":L"Widget layout: editing",[this]{auto config=app_.config();config.widgetsLocked=!config.widgetsLocked;app_.apply(std::move(config));app_.extensions().update();},app_.config().widgetsLocked,L"Toggle widget layout lock");
 button(c,box(220,y+100,width,36),L"Widget snap grid: "+std::to_wstring(app_.config().widgetGrid)+L" DIP",[this]{auto config=app_.config();config.widgetGrid=config.widgetGrid==12?24:config.widgetGrid==24?8:12;app_.apply(std::move(config));},false,L"Change widget snap grid");
 c.text(L"Unlock only while arranging. Ordinary hover never moves widgets; lost-button drags cancel.",box(222,y+145,width,46),theme_.muted,11);

 button(c,box(220,y+240,width,38),L"Widgets and extensions",[this]{app_.openExtensions();});
 button(c,box(220,y+292,width,38),L"Plugin marketplace",[this]{app_.openMarketplace();});
 c.text(L"Install bundled packages, then choose Apply / enable. Desktop layouts stay locked until you choose to edit.",box(222,y+350,width,64),theme_.muted,12);
 }
}
}
