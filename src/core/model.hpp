#pragma once
#include <cstdint>
#include <array>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace aether {
enum class Edge { Bottom, Left, Right };
struct Rect { int left{}, top{}, right{}, bottom{}; int width() const {return right-left;} int height() const {return bottom-top;} bool operator==(const Rect&) const = default; };
enum class ThemeStyle {Classic,Frosted,Minimal};
enum class IndicatorStyle {Dots,Line,Square};
enum class BarStyle {Classic,Solid,Floating,Islands,Accent};
enum class FontPart {General,Activities,WindowTitle,Clock,Status,Dock,Overview,Studio,Widgets,Popups,Count};
struct FontChoice {std::string family;int weight=400,scale=100;bool italic=false;bool operator==(const FontChoice&) const = default;};
std::string fontPartKey(FontPart);
struct Theme {
 std::string name="Ubuntu";
 std::uint32_t background=0x211C28, surface=0x2D2636, raised=0x403448,
  foreground=0xF5F1F7, muted=0xBFB3C9, accent=0xE97843;
 int radius=16;
 std::string fontFamily="Segoe UI";int fontScale=100,borderWidth=1,gradientStrength=0,dockHighlight=100;
 ThemeStyle style=ThemeStyle::Classic;IndicatorStyle indicator=IndicatorStyle::Dots;
 BarStyle topBarStyle=BarStyle::Classic,dockStyle=BarStyle::Classic;
 int topBarHeight=34,barPadding=6,surfaceOpacity=68,fontWeight=400;bool fontItalic=false;
 std::array<FontChoice,static_cast<std::size_t>(FontPart::Count)> fonts{};
 bool operator==(const Theme&) const = default;
};
enum class TopModule {Activities,Title,Clock,Status,Settings};
struct DisplayOptions {
 Edge edge=Edge::Bottom; int iconSize=48, dockGap=10; bool topBar=true, dock=true,
  autoHide=false, monitorOnly=true, reserveDock=true, clock=true, focusedTitle=true, status=true;
 std::array<int,5> moduleOrder{0,1,2,3,4},moduleZones{0,0,1,2,2};
 bool operator==(const DisplayOptions&) const = default;
};
struct Pin {std::string name,target,identity,iconPath;std::uint64_t iconRevision=0; bool operator==(const Pin&) const = default;};
struct SnapLayout {
 std::string id,name;std::vector<Rect> zones;int gapDips=8;
 bool operator==(const SnapLayout&) const = default;
};
struct TilingOptions {
 bool enabled=false;std::string layoutId="balanced";
 bool operator==(const TilingOptions&) const = default;
};
struct Config {
 Theme theme; DisplayOptions defaults;
 TilingOptions tilingDefaults;std::map<std::string,TilingOptions> tilingDisplays;std::vector<SnapLayout> snapLayouts;
 std::map<std::string,DisplayOptions> displays;
 std::vector<Pin> pins;
 bool hideTaskbar=true, hideFullscreen=true, clock24=false, windowPreviews=true, reduceMotion=false;
 int animatedWallpaper=0;bool autoWallpaperColors=false;int wallpaperColorScheme=0;
 bool widgetsLocked=true;int widgetGrid=12;std::string iconPack;
 DisplayOptions forDisplay(const std::string& name) const;
 TilingOptions tilingForDisplay(const std::string& name) const;
};
struct Page {int offset{},count{},capacity{}; bool previous{},next{}; float slotSize{},padding{};};
Theme preset(int index);
std::uint32_t textOnAccent(std::uint32_t color);
struct ThemeUndo {
 std::optional<Theme> before,after;
 void record(const Theme& original,const Theme& applied){before=original;after=applied;}
 bool available(const Theme& current) const{return before&&after&&current==*after;}
 std::optional<Theme> take(const Theme& current){if(!available(current))return {};auto result=before;before.reset();after.reset();return result;}
};
bool validFontFamily(std::string_view family);
bool validIconPackId(std::string_view);
std::string barStyleName(BarStyle);
bool validLocalIconPath(std::string_view path);
Config parseSettings(std::string_view text);
std::string serializeSettings(const Config& c);
std::optional<Theme> parseTheme(std::string_view text,std::string* error=nullptr);
std::string serializeTheme(const Theme& t);
int searchScore(std::wstring_view query,std::wstring_view text);
Rect dockGeometry(Rect work,Edge edge,int slotCount,int iconSize,int dpi,int gap=10);
Rect dockGeometry(Rect work,Edge edge,const Page& page,int iconSize,int dpi,int gap=10);
Page pageFor(int itemCount,int requestedOffset,int availableLength,int slotSize,int padding=24);
Edge nearestEdge(Rect work,int x,int y);
Rect snapRect(Rect work,int zone,int gap=8);
std::string edgeName(Edge e);
struct TaskRecord {
 std::string name, target, identity, display; bool active=false;
};
struct DockGroup {
 Pin app; std::vector<std::size_t> tasks; int pinIndex=-1; bool active=false;
};
std::vector<DockGroup> groupDock(const std::vector<Pin>& pins,
 const std::vector<TaskRecord>& tasks, const std::string& display, bool monitorOnly);
int advanceSelection(int selected, int delta, int count);
bool reorderPin(Config& config, std::size_t from, std::size_t to);
std::optional<std::size_t> findPinByTarget(const Config& config, const Pin& pin);
Rect popupBounds(Rect work, int desiredWidth, int desiredHeight, int dpi);
struct RefreshGate {
 bool paused=false, pending=false;
 bool request();
 void consume(){pending=false;}
 void setPaused(bool value){paused=value; pending=false;}
};
int scaledRadius(int radiusDips,int dpi);
struct IconSize {int width{},height{};};
IconSize boundedIconSize(int width,int height,int limit=128);
bool recoveryReady(bool processAlive,bool acknowledged);
}
