#pragma once
#include "ui/canvas.hpp"
#include "platform/catalog.hpp"
#include "platform/config.hpp"
#include "platform/desktop.hpp"
#include "platform/safety.hpp"
#include "platform/work_area.hpp"
#include "platform/session_policy.hpp"
#include "platform/status.hpp"
#include "platform/shell_requests.hpp"
#include "platform/tiling.hpp"
#include "platform/wallpaper.hpp"
#include "platform/windows_key.hpp"
#include "platform/workspaces.hpp"
#include "platform/appearance.hpp"
#include <memory>
namespace aether {
// Owned controller diagnostics; only startup/shutdown transitions publish values.
inline constexpr wchar_t AppLifecycleProperty[]=L"Aether.LifecyclePhase";
enum class AppLifecyclePhase:ULONG_PTR {Ready=1,CloseRequested,RestoringDesktop,SurfacesRemoved,StoppingWallpaper,StoppingShell,StoppingCatalog,StoppingIcons,SavingDiagnostics,DestroyingController};
namespace ui {class Panel; class Activities; class Editor; class QuickSettings; class DockFlyout; class Backgrounds; class Extensions; class ExtensionRuntime;class Looks;class LiveWallpaper;class Marketplace;}
class App {
 ui::Graphics graphics_;
 Config config_;
 std::unique_ptr<win::AppearanceBridge> appearance_;
 std::optional<Theme> automaticTheme_;std::string automaticKey_;std::wstring automaticPath_;std::uint64_t automaticRequest_=0,automaticRevision_=0;bool automaticWaiting_=false;
 void updateAutomaticColors();void automaticColorsCompletion(const win::WallpaperCompletion&);void acceptAutomaticColors(const std::vector<Theme>&);
 HWND controller_{};
 win::IconCache icons_;
 win::Catalog catalog_;
 win::TaskbarGuard taskbar_;
 win::WindowsKeyHook windowsKey_;
 win::WindowEvents events_;
 win::WindowCollector collector_;
 win::ShellRequests shell_;
 win::TilingService tiling_;
 win::WallpaperService wallpaper_;win::Workspaces workspaces_;std::wstring workspaceWallpaperNotice_;
 struct PendingPin {bool add=false;std::uint64_t revision=0;Pin expected;HWND owner=nullptr;DWORD ownerPid=0;};
 std::map<std::uint64_t,PendingPin> pendingPins_;std::uint64_t pinRevision_=0;
 std::wstring tilingShortcutNotice_;
 std::shared_ptr<const std::vector<win::Application>> catalogSnapshot_;
 bool appearanceReady_=false;ULONGLONG appearanceSettleUntil_=0;bool appearanceRebuild_=false;
 std::uint64_t layoutPasses_=0,unchangedSnapshots_=0,statusPolls_=0;bool quitRequested_=false;int quitExitCode_=0;
 std::vector<win::Monitor> displays_;
 std::vector<win::Window> windows_;
 std::vector<std::unique_ptr<ui::Panel>> panels_;
 std::unique_ptr<ui::Activities> activities_;
 std::unique_ptr<ui::Editor> editor_;
 std::unique_ptr<ui::Looks> looks_;
 std::unique_ptr<ui::LiveWallpaper> liveWallpaper_;std::unique_ptr<ui::Marketplace> marketplace_;
 std::unique_ptr<ui::QuickSettings> quick_;
 std::unique_ptr<ui::DockFlyout> dockFlyout_;
 std::unique_ptr<ui::Backgrounds> backgrounds_;
 std::unique_ptr<ui::ExtensionRuntime> extensionRuntime_;
 std::unique_ptr<ui::Extensions> extensions_;
 win::Status status_;win::StatusService statusService_;
 bool taskbarStartupPreference_=false;
 win::ProviderProcessOptions pluginProviders_;
 UINT taskbarCreated_{};
 bool running_=false, paused_=false, relayoutPending_=false, emergencyRegistered_=false;
 win::SessionPausePolicy sessionPause_;bool taskbarSessionSuspended_=false;
 win::WorkAreaSnapshot originalWorkspace_{};
 bool workAreasCaptured_=false,workspaceQuiesced_=true,workspaceRestorePending_=false,workspaceRestoreReported_=false,layoutInProgress_=false;
 bool taskbarRetirementPending_=false;
 bool firstRun_=false;
 unsigned interactionDepth_=0;bool layoutAfterInteraction_=false,rebuildAfterInteraction_=false;
 static LRESULT CALLBACK procedure(HWND,UINT,WPARAM,LPARAM);
 LRESULT event(UINT,WPARAM,LPARAM);
 void layout(bool rebuild=false);
 void refreshWindows(bool reconcile=false);
 void queueShellMetadata();void shellCompletions();void refreshPinIdentities();
 bool tilingSuppressed() const;void updateTiling();
 void updateStatus();
 void statusCompletion();
 void updateWorkspaceWallpaper();
 void updateTaskbarFailure(bool wait=false);
 void refreshTaskbar();
 bool confirmActiveTaskbar();
 void withdrawPanels();
 void logOriginalWorkspace(const char* stage) const noexcept;
 bool restoreWorkspace();
 void resumeWorkspace();
 void queueWorkspaceRetry();
 void retireDesktop(bool wait=true);
 bool armTaskbar();
 void registerTray();
 void shutdown();
 void markLifecycle(AppLifecyclePhase phase) const {if(controller_)SetPropW(controller_,AppLifecycleProperty,reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(phase)));}
public:
 explicit App(win::ProviderProcessOptions providers={},win::ProviderProcessOptions wallpaperProviders={},win::ProviderProcessOptions pluginProviders={});~App();
 int run();
 const Config& config() const{return config_;}
 const Theme& theme() const{return automaticTheme_?*automaticTheme_:config_.theme;}
 const std::vector<win::Monitor>& displays() const{return displays_;}
 const std::vector<win::Window>& windows() const{return windows_;}
 const win::Status& status() const{return status_;}
 const win::ProviderProcessOptions& pluginProviders()const{return pluginProviders_;}
 win::IconCache& icons(){return icons_;}
 win::Catalog& catalog(){return catalog_;}
 HWND controller() const{return controller_;}
 win::Monitor display(const std::wstring& name=L"") const;
 std::vector<DockGroup> dockGroups(const std::wstring& display) const;
 void apply(Config config);
 bool taskbarStartupRequested()const{return taskbarStartupPreference_;}
 void setTaskbarHiding(bool enabled){taskbarStartupPreference_=enabled;auto next=config_;next.hideTaskbar=enabled;apply(std::move(next));}
 void requestVolume(int value){statusService_.volume(value);}
 void requestMute(){statusService_.mute();}
 void scheduleLayout();
 void beginInteraction(){++interactionDepth_;}
 bool interactionActive()const{return interactionDepth_!=0;}
 void endInteraction();
 void openDockPreview(const std::wstring& display,const Pin&,Rect anchor,bool focus=false);
 void closeDockPreview();
 bool dockPreviewVisible() const;
 bool dockPreviewAllowed(const std::wstring& display) const;
 bool dockPreviewContainsPointer() const;
 void openActivities(const std::wstring& display=L"");
 void openEditor();
 win::AppearanceBridge& appearanceBridge(){return *appearance_;}
 void openLooks();void openMarketplace();
 std::wstring liveWallpaperNotice()const;
 void openBackgrounds();void openExtensions();
 win::WallpaperService& wallpaper(){return wallpaper_;}
 win::Workspaces& workspaces(){return workspaces_;}
 void workspaceAppearanceChanged();
 const std::wstring& workspaceWallpaperNotice()const{return workspaceWallpaperNotice_;}
 ui::ExtensionRuntime& extensions();
 void openQuick(const std::wstring& display=L"");
 void activateGroup(const DockGroup&,bool launchNew=false);
 void togglePin(const Pin& pin);
 void removePin(std::size_t index);
 void movePin(std::size_t from,std::size_t to);
 void setEdge(const std::wstring& display,Edge edge);
 void choosePin(HWND owner);
 void launchTarget(HWND owner,const std::wstring& target);
 void tileNow(const std::wstring& display=L"");
 const std::wstring& tilingShortcutNotice() const{return tilingShortcutNotice_;}
 void restoreNative();
 void quit();
 void globalMenu(HWND owner,POINT point);
 void redraw();
 void refreshStatus(){updateStatus();}
};
int runUiSmoke();
}

