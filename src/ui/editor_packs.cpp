#include "editor.hpp"
#include "app.hpp"
#include "extensions.hpp"
#include "core/desktop_presets.hpp"
#include "platform/dialogs.hpp"
#include <set>
namespace aether::ui {
void Editor::importPack(){
 const COMDLG_FILTERSPEC filters[]={{L"Aether icon pack manifest",L"*.aethericons"}};app_.beginInteraction();auto path=win::chooseFile(hwnd_,false,L"Import a local icon pack",filters,1);app_.endInteraction();if(!path)return;std::string error;auto id=win::importIconPack(*path,&error);packNotice_=id?L"Pack imported locally. Select it to apply.":win::wide(error);iconPacks_=win::iconPacks();invalidate();
}
void Editor::applyDesktopPreset(){
 const auto presets=desktopPresets();if(desktopChoice_<0||desktopChoice_>=static_cast<int>(presets.size()))return;const auto& selected=presets[desktopChoice_];auto next=desktopPresetConfig(app_.config(),desktopChoice_);
 if(std::none_of(iconPacks_.begin(),iconPacks_.end(),[&](const auto& p){return p.id==selected.iconPack;})){packNotice_=L"This preset's icon assets are missing. Restore the bundled assets before applying.";invalidate();return;}
 GUID plannedWorkspace=GUID_NULL;std::optional<win::WorkspaceAppearance> plannedWallpaper;std::vector<WidgetUndo> plannedWidgets;auto& workspaces=app_.workspaces();if(workspaces.refresh())for(const auto& item:workspaces.items())if(item.current){plannedWorkspace=item.id;plannedWallpaper=workspaces.appearance(item.id);break;}
 if(arrangePresetWidgets_){
  struct Cursor {int x=24,y=24,row=0;};std::map<std::wstring,Cursor> cursors;auto displays=app_.displays();if(displays.empty())displays=win::monitors();
  for(const auto& entry:app_.extensions().library().entries())if(entry.enabled)for(const auto& widget:entry.manifest.widgets){auto monitor=std::find_if(displays.begin(),displays.end(),[&](const auto& m){return widget.placement.monitor.empty()?m.primary:win::utf8(m.name)==widget.placement.monitor;});if(monitor==displays.end()){packNotice_=L"A widget's display is disconnected. Reconnect it or turn off widget arrangement.";invalidate();return;}auto& at=cursors[monitor->name];const int width=MulDiv(monitor->work.width(),96,monitor->dpi),height=MulDiv(monitor->work.height(),96,monitor->dpi);if(at.x+widget.width+24>width){at.x=24;at.y+=at.row+12;at.row=0;}if(at.y+widget.height+24>height){packNotice_=L"Widgets do not fit this display. Turn off arrangement or disable some widgets first.";invalidate();return;}WidgetPlacement after{win::utf8(monitor->name),at.x,at.y};plannedWidgets.push_back({entry.storageId,widget.id,widget.placement,after});at.x+=((widget.width+11)/12)*12+12;at.row=std::max(at.row,((widget.height+11)/12)*12);}
 }
 desktopWorkspace_=plannedWorkspace;desktopWallpaperUndo_=plannedWallpaper;desktopWidgetUndo_=std::move(plannedWidgets);desktopUndo_=app_.config();app_.apply(next);if(serializeSettings(app_.config())!=serializeSettings(next)){desktopUndo_.reset();packNotice_=L"Preset settings could not be saved; wallpaper and widgets were left unchanged.";invalidate();return;}
 std::string error;bool ok=true;if(desktopWallpaperUndo_)ok=workspaces.setAppearance(desktopWorkspace_,{selected.background,{}});
 if(ok)for(const auto& position:desktopWidgetUndo_)if(!app_.extensions().setPlacement(position.package,position.id,position.after,&error)){ok=false;break;}
 if(!ok){for(const auto& position:desktopWidgetUndo_)app_.extensions().setPlacement(position.package,position.id,position.before,&error);if(desktopWallpaperUndo_)workspaces.setAppearance(desktopWorkspace_,*desktopWallpaperUndo_);app_.apply(*desktopUndo_);desktopUndo_.reset();packNotice_=L"Preset could not finish; previous settings were restored.";}else{desktopApplied_=app_.config();desktopAppliedBackground_=selected.background;packNotice_=L"Applied "+win::wide(selected.name)+L". Undo is available until you change these settings.";}
 app_.workspaceAppearanceChanged();app_.extensions().update();invalidate();
}
void Editor::undoDesktopPreset(){
 if(!desktopUndo_||!desktopApplied_||serializeSettings(app_.config())!=serializeSettings(*desktopApplied_)){packNotice_=L"Settings changed after the preset; Undo will not overwrite them.";invalidate();return;}
 auto& workspaces=app_.workspaces();const auto expected=desktopAppliedBackground_;
 if(desktopWallpaperUndo_&&(workspaces.appearance(desktopWorkspace_)!=win::WorkspaceAppearance{expected,{}})){packNotice_=L"Workspace wallpaper changed after the preset; Undo will not overwrite it.";invalidate();return;}
 for(const auto& position:desktopWidgetUndo_){bool matches=false;for(const auto& entry:app_.extensions().library().entries())if(entry.storageId==position.package)for(const auto& widget:entry.manifest.widgets)if(widget.id==position.id)matches=widget.placement==position.after;if(!matches){packNotice_=L"Widget positions changed; Undo will not overwrite the new layout.";invalidate();return;}}
 std::string error;for(const auto& position:desktopWidgetUndo_)if(!app_.extensions().setPlacement(position.package,position.id,position.before,&error)){packNotice_=win::wide(error);invalidate();return;}
 if(desktopWallpaperUndo_&&!workspaces.setAppearance(desktopWorkspace_,*desktopWallpaperUndo_)){packNotice_=L"Could not restore workspace wallpaper.";invalidate();return;}
 app_.apply(*desktopUndo_);desktopUndo_.reset();desktopApplied_.reset();app_.workspaceAppearanceChanged();app_.extensions().update();packNotice_=L"Desktop preset undone.";invalidate();
}
void Editor::packs(Canvas& c,float y){
 const float width=c.size().width-246,card=(width-12)/2;auto presets=desktopPresets();
 c.text(L"COORDINATED DESKTOP PRESETS",box(222,y,width,24),theme_.muted,10,true);
 for(int n=0;n<static_cast<int>(presets.size());++n){const auto& p=presets[n];const float x=220+(n%2)*(card+12),top=y+36+(n/2)*96;auto r=box(x,top,card,84);c.fill(r,p.theme.surface,10);c.fill(box(x+12,top+12,8,58),p.theme.accent,3);c.text(win::wide(p.name),box(x+30,top+8,card-42,25),p.theme.foreground,14,true);c.text(win::wide(p.description),box(x+30,top+34,card-42,21),p.theme.muted,10);c.text(L"Icons: "+win::wide(p.iconPack),box(x+30,top+57,card-42,19),p.theme.muted,10);if(n==desktopChoice_)c.stroke(r,theme_.accent,10,2);action(r,[this,n]{desktopChoice_=n;invalidate();},L"Preview desktop preset "+win::wide(p.name));}
 button(c,box(220,y+336,card,36),arrangePresetWidgets_?L"Include widget grid: on":L"Include widget grid: off",[this]{arrangePresetWidgets_=!arrangePresetWidgets_;invalidate();},arrangePresetWidgets_,L"Toggle preset widget arrangement");
 button(c,box(220+card+12,y+336,card,36),L"Apply selected desktop preset",[this]{applyDesktopPreset();},false,L"Apply selected desktop preset");
 button(c,box(220,y+382,width,34),L"Undo desktop preset",[this]{undoDesktopPreset();});
 c.text(L"Preview selects a recipe. Apply changes colors, fonts, bars, icons and this workspace's background.",box(222,y+424,width,38),theme_.muted,11);
 c.text(L"Pins, notes, enabled plugins and taskbar recovery settings are preserved. Widget arrangement is optional.",box(222,y+464,width,38),theme_.muted,11);
 c.text(L"ICON PACKS",box(222,y+524,width,24),theme_.muted,10,true);
 button(c,box(220,y+560,card,34),L"Import local icon pack…",[this]{importPack();});
 button(c,box(220+card+12,y+560,card,34),L"Use original app icons",[this]{auto config=app_.config();config.iconPack.clear();app_.apply(std::move(config));packNotice_=L"Original icons restored; per-app overrides remain.";invalidate();});
 button(c,box(220,y+606,width,34),packFilter_.empty()?L"Search icon packs…":L"Search: "+win::wide(packFilter_),[this]{app_.beginInteraction();auto value=win::editNote(hwnd_,packFilter_,L"Search icon packs",L"Pack name; empty shows all packs",64);app_.endInteraction();if(value){packFilter_=*value;invalidate();}});
 c.text(packNotice_.empty()?L"Offline previews. Unmatched apps keep their original icons. Override individual icons in Pinned apps.":packNotice_,box(222,y+650,width,45),theme_.muted,11);
 float row=y+710;for(const auto& pack:iconPacks_){if(!packFilter_.empty()&&searchScore(win::wide(packFilter_),win::wide(pack.name))<=0)continue;c.surface(box(220,row,width,124),theme_,10);c.text(win::wide(pack.name),box(234,row+8,width-200,28),theme_.foreground,15,true);std::set<std::string> unique;int index=0;for(const auto& [alias,file]:pack.icons){(void)alias;if(unique.insert(file).second&&index<7){if(row+124>=108&&row<c.size().height-42)c.icon(app_.icons().requestImage((pack.directory/file).wstring()),box(234+index*49.f,row+43,38,38),theme_.muted);++index;}}
 c.text(std::to_wstring(unique.size())+L" icons · "+win::wide(pack.license),box(234,row+89,width-30,23),theme_.muted,10);
 auto id=pack.id;button(c,box(220+width-150,row+12,134,34),app_.config().iconPack==id?L"Selected":L"Use pack",[this,id]{auto config=app_.config();config.iconPack=id;app_.apply(std::move(config));packNotice_=L"Icon pack applied to Aether. Per-app overrides and Windows icons are preserved.";invalidate();},app_.config().iconPack==id,L"Use icon pack "+win::wide(pack.name));row+=136;}
}
}
