#pragma once
#include "model.hpp"
#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace aether {
constexpr std::size_t ExtensionLimit=24, ExtensionWidgetLimit=8, ActiveWidgetLimit=48, ExtensionBytesLimit=65536;
enum class WidgetKind { Clock, FocusedApp, QuickAction, Media, Battery, Resources, Note, Weather };
enum class WidgetAction { None, Activities, Studio, QuickSettings, Pin };
enum class WidgetStyle { Theme, Minimal, Accent };
struct WidgetPlacement {
 std::string monitor;int x=24,y=24;bool operator==(const WidgetPlacement&) const=default;
};
struct ExtensionWidget {
 std::string id,label;WidgetKind kind=WidgetKind::Clock;WidgetAction action=WidgetAction::None;
 std::string pin;WidgetStyle style=WidgetStyle::Theme;std::optional<std::uint32_t> accent;
 int width=260,height=120;WidgetPlacement placement;bool operator==(const ExtensionWidget&) const=default;
 std::string text,location;
};
struct ExtensionManifest {
 int version=1;std::string id,name,description;std::vector<ExtensionWidget> widgets;
 bool operator==(const ExtensionManifest&) const=default;
};
struct InstalledExtension {
 std::string storageId;ExtensionManifest manifest;bool enabled=false;int order=0;
 bool operator==(const InstalledExtension&) const=default;
};
std::optional<ExtensionManifest> parseExtension(std::string_view,std::string* error=nullptr);
std::string serializeExtension(const ExtensionManifest&);
std::optional<InstalledExtension> parseInstalledExtension(std::string_view,std::string* error=nullptr);
std::string serializeInstalledExtension(const InstalledExtension&);
std::vector<ExtensionManifest> exampleExtensions();
ExtensionManifest sleekDeskExtension(int index);
ExtensionManifest productivityExtension();
Rect extensionBounds(const ExtensionWidget&,Rect work,int dpi);
bool extensionVisible(bool enabled,bool paused,bool hideFullscreen,bool monitorFullscreen);
}
