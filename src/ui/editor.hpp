#pragma once
#include "canvas.hpp"
#include "accessibility.hpp"
#include "platform/desktop.hpp"
#include "platform/theme_library.hpp"
#include "platform/layout_library.hpp"
#include "platform/wallpaper.hpp"
#include "platform/workspaces.hpp"
#include "core/extensions.hpp"
#include <functional>
namespace aether {class App;}
namespace aether::ui {
class Editor final:public Surface {
 std::unique_ptr<Accessibility> accessibility_;std::vector<AccessibleNode> accessibleActions_;std::string tilingNotice_;
 void action(D2D1_RECT_F,std::function<void()>,std::wstring,long role=ROLE_SYSTEM_PUSHBUTTON,long state=STATE_SYSTEM_FOCUSABLE,std::wstring detail=L"");void accessibleSnapshot();void focusIndex(int);void snapLayouts(Canvas&,float);void editSnapLayout(const std::string& id="");void removeSnapLayout(const std::string&);void tilingChange(const std::function<void(TilingOptions&)>&);
 App& app_;int page_=0;std::string displayName_;
 std::vector<win::Monitor> displayMap_;
 std::vector<win::SavedThemePreset> savedThemes_;
 std::vector<Theme> palettePreviews_;ThemeUndo paletteUndo_;int paletteChoice_=0;
 std::wstring palettePath_,paletteNotice_=L"Build three palettes, preview them, then apply your favorite.";
 std::uint64_t paletteRequest_=0;bool paletteWaitingImage_=false;
 std::vector<win::IconPack> iconPacks_;std::string packFilter_;std::wstring packNotice_;
 int desktopChoice_=0,desktopAppliedBackground_=0;bool arrangePresetWidgets_=false;std::optional<Config> desktopUndo_,desktopApplied_;GUID desktopWorkspace_{};std::optional<win::WorkspaceAppearance> desktopWallpaperUndo_;
 struct WidgetUndo {std::string package,id;WidgetPlacement before,after;};std::vector<WidgetUndo> desktopWidgetUndo_;
 void integration(Canvas&,float);void terminalSettings(Canvas&,float);
 void packs(Canvas&,float);void importPack();void applyDesktopPreset();void undoDesktopPreset();
 void personalize(Canvas&,float);void chooseInstalledFont(FontPart part=FontPart::General);void buildPalette(bool chooseImage=false);void updatePaletteImage();void applyPalette();
 std::string themeLibraryNotice_,layoutLibraryNotice_;
 std::vector<win::SavedLayoutPreset> savedLayouts_;
 std::vector<std::pair<D2D1_RECT_F,std::function<void()>>> actions_;
 float pointerX_=-1,pointerY_=-1;float scroll_=0;int focusedAction_=-1;
 float layoutScale() const;
 void focusAction(int direction);
 std::vector<win::Monitor> connectedDisplays() const;
 void reloadThemePresets();void saveThemePreset();void applyThemePreset(const std::string&);void deleteThemePreset(const std::string&);void renameThemePreset(const std::string&);
 void reloadLayoutPresets();void saveLayoutPreset();void applyLayoutPreset(const std::string&);void deleteLayoutPreset(const std::string&);void renameLayoutPreset(const std::string&);
 float contentHeight() const;
 void copyDisplayProfile();void resetDisplayProfile();
 void paint(Canvas&) override;void click(float,float) override;
 void pointer(float,float) override;void leave() override;
 LRESULT event(UINT,WPARAM,LPARAM,bool&) override;
 void button(Canvas&,D2D1_RECT_F,const std::wstring&,std::function<void()>,bool selected=false,std::wstring accessibleName=L"",long role=ROLE_SYSTEM_PUSHBUTTON);
 void toggle(Canvas&,float,const std::wstring&,const std::wstring&,bool,std::function<void()>);
 void appearanceDetails(Canvas&,float);void renamePin(const Pin&);void choosePinIcon(const Pin&);void resetPinIcon(const Pin&);
 void appearance(Canvas&,float);void layouts(Canvas&,float);void behavior(Canvas&,float);void pins(Canvas&,float);
 void layoutChange(const std::function<void(DisplayOptions&)>&);
 void importTheme();void exportTheme();void chooseColor(std::uint32_t Theme::* member);
public:
 Editor(Graphics&,App&);~Editor() override{if(accessibility_)accessibility_->detach();destroy();}
 void open(const win::Monitor&);void update();
 void openTerminal(){page_=9;scroll_=0;focusedAction_=-1;invalidate();}
 void openIntegration(){page_=6;scroll_=0;focusedAction_=-1;invalidate();}
 void paletteCompletion(const win::WallpaperCompletion&);
};
}
