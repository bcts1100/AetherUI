#include "app.hpp"
#include "core/personalization.hpp"
#include <limits>
#include "ui/panel.hpp"
#include "ui/dock_flyout.hpp"
#include "ui/activities.hpp"
#include "ui/editor.hpp"
#include "ui/looks.hpp"
#include "ui/live_wallpaper.hpp"
#include "ui/marketplace.hpp"
#include "ui/quick_settings.hpp"
#include "ui/backgrounds.hpp"
#include "ui/extensions.hpp"
#include "platform/dialogs.hpp"
#include <wtsapi32.h>
#include <set>
#include <sstream>
#include "platform/display_policy.hpp"
#include "platform/identity.hpp"
namespace aether {
namespace {
constexpr UINT StatusTimer=1,ReconcileTimer=2,EventsTimer=3,RecoveryTimer=4,WorkspaceTimer=5;
constexpr int LauncherHotkey=1,EditorHotkey=2,RecoveryHotkey=3;
constexpr UINT WindowsTapMessage=WM_APP+42;
}
App::App(win::ProviderProcessOptions providers,win::ProviderProcessOptions wallpaperProviders,win::ProviderProcessOptions pluginProviders):icons_(providers),catalog_(providers),shell_(providers),wallpaper_(wallpaperProviders),pluginProviders_(std::move(pluginProviders)){config_=win::loadConfig(firstRun_);icons_.setPack(config_.iconPack);}
App::~App(){shutdown();}
int App::run(){
 // Seal the physical desktop before the first Aether window or reservation.
 if(!win::captureWorkAreas(originalWorkspace_))
  throw std::runtime_error("The original monitor work areas could not be captured safely. Aether did not create desktop panels.");
 workAreasCaptured_=true;logOriginalWorkspace("startup-captured");
 WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.hInstance=GetModuleHandleW(nullptr);
 wc.lpszClassName=L"Aether.Controller.v3";wc.lpfnWndProc=procedure;
 if(!RegisterClassExW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)throw std::runtime_error("Controller registration failed.");
 controller_=CreateWindowExW(WS_EX_TOOLWINDOW,wc.lpszClassName,L"Aether UI Controller",WS_POPUP,0,0,1,1,nullptr,nullptr,wc.hInstance,this);
 if(!controller_)throw std::runtime_error("Controller window creation failed.");
 running_=true;taskbarCreated_=RegisterWindowMessageW(L"TaskbarCreated");
 if(!RegisterHotKey(controller_,LauncherHotkey,MOD_CONTROL|MOD_ALT|MOD_NOREPEAT,VK_SPACE))win::log("Activities shortcut is already assigned; use the panel.");
 if(!RegisterHotKey(controller_,EditorHotkey,MOD_CONTROL|MOD_ALT|MOD_NOREPEAT,'E'))win::log("Editor shortcut is already assigned; use the panel.");
 emergencyRegistered_=RegisterHotKey(controller_,RecoveryHotkey,MOD_CONTROL|MOD_ALT|MOD_NOREPEAT,VK_F12)!=FALSE;
 for(int i=0;i<7;++i)if(!RegisterHotKey(controller_,20+i,MOD_CONTROL|MOD_ALT|MOD_NOREPEAT,'1'+i))tilingShortcutNotice_+=L"A snap shortcut is already assigned. ";
 const UINT directionKeys[]{VK_LEFT,VK_RIGHT,VK_UP,VK_DOWN};
 for(int i=0;i<4;++i)if(!RegisterHotKey(controller_,40+i,MOD_CONTROL|MOD_ALT|MOD_SHIFT|MOD_NOREPEAT,directionKeys[i]))tilingShortcutNotice_+=L"A monitor movement shortcut is already assigned. ";
 for(int i=0;i<9;++i)if(!RegisterHotKey(controller_,50+i,MOD_CONTROL|MOD_ALT|MOD_SHIFT|MOD_NOREPEAT,'1'+i))tilingShortcutNotice_+=L"A custom zone shortcut is already assigned. ";
 if(!tilingShortcutNotice_.empty())win::log(win::utf8(tilingShortcutNotice_));
 if(!WTSRegisterSessionNotification(controller_,NOTIFY_FOR_THIS_SESSION))win::log("Session notifications unavailable.");
 icons_.start(controller_);catalog_.start(controller_);shell_.start(controller_);wallpaper_.start(controller_);refreshPinIdentities();
 if(!events_.start(controller_))win::log("Window events unavailable; using periodic reconciliation.");
 SetPropW(controller_,L"Aether.Workspace.Notifications",reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(workspaces_.watch(controller_))));registerTray();updateStatus();refreshWindows();
 // A fresh recovery generation must capture the reservation-free desktop.
 if(config_.hideTaskbar){
  if(!armTaskbar()){
   updateTaskbarFailure();
   win::message(controller_,L"Aether left the Windows taskbar visible because the original workspace, recovery shortcut, helper or heartbeat timer was unavailable.",MB_OK|MB_ICONWARNING);
  }
 }else resumeWorkspace();
 layout(true);
 extensionRuntime_=std::make_unique<ui::ExtensionRuntime>(graphics_,*this);std::string extensionError;if(!extensionRuntime_->start(&extensionError))win::log("Native extensions unavailable: "+extensionError);
 SetTimer(controller_,StatusTimer,15000,nullptr);SetTimer(controller_,ReconcileTimer,30000,nullptr);
 if(firstRun_)PostMessageW(controller_,win::OpenEditorMessage,0,0);
 // Surface reservations can make Shell taskbars visible after initial Arm.
 const bool initiallyConfirmed=confirmActiveTaskbar();
 if(initiallyConfirmed&&config_.hideTaskbar){
  // Shell reservation callbacks and the asynchronous catalog can already have
  // queued layout work while the helper settles startup. Deliver that work
  // before publishing Ready, then confirm the resulting workspace once more.
  // Keep this startup-only drain bounded and leave WM_QUIT for GetMessage.
  const auto beforeLayout=layoutPasses_,until=GetTickCount64()+500;
  unsigned dispatched=0;MSG queued{};
  while(dispatched<128&&GetTickCount64()<until&&!quitRequested_
   &&PeekMessageW(&queued,nullptr,0,0,PM_NOREMOVE)){
   if(queued.message==WM_QUIT)break;
   if(!PeekMessageW(&queued,nullptr,0,0,PM_REMOVE))break;
   TranslateMessage(&queued);DispatchMessageW(&queued);++dispatched;
  }
  win::log("Startup layout drain DIAG process="+std::to_string(GetCurrentProcessId())
   +" dispatched="+std::to_string(dispatched)+" layouts="+std::to_string(layoutPasses_-beforeLayout));
  if(!quitRequested_){layout();confirmActiveTaskbar();}
 }
 if(!quitRequested_){
  markLifecycle(AppLifecyclePhase::Ready);
  if(!windowsKey_.start(controller_,WindowsTapMessage,[this]{return running_&&!paused_&&!quitRequested_&&!interactionDepth_&&config_.hideTaskbar&&taskbar_.enabled();}))
   win::log("Windows-key Activities hook unavailable; Ctrl+Alt+Space remains available.");
 }
 MSG message{};BOOL result=0;
 while((result=GetMessageW(&message,nullptr,0,0))>0){TranslateMessage(&message);DispatchMessageW(&message);}
 int exitCode=result<0?1:static_cast<int>(message.wParam);shutdown();return exitCode;
}
void App::shutdown(){
 if(!controller_)return;windowsKey_.stop();running_=false;quitRequested_=true;taskbarSessionSuspended_=false;markLifecycle(AppLifecyclePhase::RestoringDesktop);
 // Withdraw all owned reservations before either recovery owner restores.
 workspaces_.unwatch();events_.stop();withdrawPanels();marketplace_.reset();liveWallpaper_.reset();if(extensionRuntime_)extensionRuntime_->shutdown();extensions_.reset();backgrounds_.reset();dockFlyout_.reset();panels_.clear();
 quick_.reset();activities_.reset();editor_.reset();looks_.reset();
 taskbarRetirementPending_=!taskbar_.setEnabled(false);restoreWorkspace();
 for(UINT id:{StatusTimer,ReconcileTimer,EventsTimer,RecoveryTimer,WorkspaceTimer})KillTimer(controller_,id);
 for(int id:{LauncherHotkey,EditorHotkey,RecoveryHotkey,20,21,22,23,24,25,26,40,41,42,43,50,51,52,53,54,55,56,57,58})UnregisterHotKey(controller_,id);
 WTSUnRegisterSessionNotification(controller_);
 NOTIFYICONDATAW n{};n.cbSize=sizeof(n);n.hWnd=controller_;n.uID=1;Shell_NotifyIconW(NIM_DELETE,&n);
 markLifecycle(AppLifecyclePhase::SurfacesRemoved);
 markLifecycle(AppLifecyclePhase::StoppingWallpaper);wallpaper_.stop();extensionRuntime_.reset();
 markLifecycle(AppLifecyclePhase::StoppingShell);shell_.stop();pendingPins_.clear();tiling_.reset();
 markLifecycle(AppLifecyclePhase::StoppingCatalog);catalog_.stop();
 markLifecycle(AppLifecyclePhase::StoppingIcons);icons_.stop();
 markLifecycle(AppLifecyclePhase::SavingDiagnostics);
 try{const auto& stats=collector_.statistics();std::ostringstream data;
  data<<"{\n\"WindowSnapshots\":"<<stats.snapshots<<",\n\"PathQueries\":"<<stats.pathQueries<<",\n\"IdentityQueries\":"<<stats.identityQueries<<",\n\"PathCacheHits\":"<<stats.pathCacheHits<<",\n\"IdentityCacheHits\":"<<stats.identityCacheHits<<",\n\"WindowScanMilliseconds\":"<<stats.totalMilliseconds<<",\n\"UnchangedSnapshots\":"<<unchangedSnapshots_<<",\n\"LayoutPasses\":"<<layoutPasses_<<",\n\"StatusPolls\":"<<statusPolls_<<",\n\"TilingPlans\":"<<tiling_.statistics().plans<<",\n\"TilingMoves\":"<<tiling_.statistics().moves<<",\n\"TilingReleased\":"<<tiling_.statistics().released<<",\n\"TilingFailures\":"<<tiling_.statistics().failures<<"\n}\n";
  win::writeAtomic(win::configDirectory()/L"diagnostics.json",data.str());
 }catch(...){win::log("Local performance counters could not be saved.");}
 // Remove the final owned window before the last local workspace check.
 // Tray/window removal can cause later Shell reflow even after appbars detach.
 markLifecycle(AppLifecyclePhase::DestroyingController);
 if(!DestroyWindow(controller_)){
  win::log("Controller removal was not confirmed; retaining passive teardown ownership.");
  while(IsWindow(controller_)&&!DestroyWindow(controller_))Sleep(1000);
 }
 controller_=nullptr;
 logOriginalWorkspace("shutdown-controller-removed");
 // Keep this immutable baseline through any independent owner's final exit.
 // A live helper can become terminal just after controller removal; transfer
 // alone cannot confirm that teardown reflow was included in its last sample.
 DWORD ownerBackoff=1000;bool ownerReported=false;
 for(;;){
  const bool retired=taskbar_.setEnabled(false);
  taskbarRetirementPending_=!retired;
  if(retired&&!taskbar_.recoveryOwnerAlive())break;
  // A dead helper can leave a still-armed parent fallback context. Retain both
  // originals until that context is retired too, so no guard destructor can
  // perform a later SET/SHOW after the final App workspace quiet sample.
  if(!ownerReported){win::log("Waiting passively for complete native recovery retirement before final local workspace settlement.");ownerReported=true;}
  Sleep(ownerBackoff);ownerBackoff=std::min<DWORD>(ownerBackoff+1000,5000);
 }
 // All independent native writers are now confirmed dead. Local settlement
 // requires a fresh exact physical snapshot after a quiet interval.
 DWORD backoff=1000;bool reported=false;
 while(workAreasCaptured_&&!taskbar_.recoveryOwnerAlive()){
  if(restoreWorkspace()){
   // Shell can deliver appbar-removal reflow after the last HWND is destroyed.
   // This empirical shutdown-only quiet margin adds no background polling.
   Sleep(1000);
   if(taskbar_.recoveryOwnerAlive())break;
   win::WorkAreaSnapshot current;
   if(win::captureWorkAreas(current)&&win::compareWorkAreas(originalWorkspace_,current)==win::WorkAreaResult::Complete)break;
   workspaceRestorePending_=true;
  }
  if(!reported){win::log("Final workspace quiet confirmation failed; retaining passive restoration ownership.");reported=true;}
  Sleep(backoff);backoff=std::min<DWORD>(backoff+1000,5000);
 }
 logOriginalWorkspace("shutdown-local-quiet-settled");
}
void App::registerTray(){
 NOTIFYICONDATAW n{};n.cbSize=sizeof(n);n.hWnd=controller_;n.uID=1;n.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;
 n.uCallbackMessage=win::TrayMessage;n.hIcon=LoadIconW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(101));if(!n.hIcon)n.hIcon=LoadIconW(nullptr,IDI_APPLICATION);
 wcscpy_s(n.szTip,128,L"Aether UI - Ctrl+Alt+E to customize");
 Shell_NotifyIconW(NIM_ADD,&n);n.uVersion=NOTIFYICON_VERSION_4;Shell_NotifyIconW(NIM_SETVERSION,&n);
}
win::Monitor App::display(const std::wstring& name) const{
 for(const auto& m:displays_)if(m.name==name)return m;
 POINT p{};GetCursorPos(&p);HMONITOR h=MonitorFromPoint(p,MONITOR_DEFAULTTOPRIMARY);
 for(const auto& m:displays_)if(m.handle==h)return m;
 if(!displays_.empty())return displays_.front();
 win::Monitor fallback;fallback.bounds={0,0,GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN)};fallback.work=fallback.bounds;return fallback;
}
std::vector<DockGroup> App::dockGroups(const std::wstring& name) const{
 std::vector<TaskRecord> tasks;tasks.reserve(windows_.size());
 for(const auto& w:windows_)tasks.push_back({win::utf8(w.title),win::utf8(w.path),win::utf8(w.identity),win::utf8(w.display),w.active});
 auto groups=groupDock(config_.pins,tasks,win::utf8(name),config_.forDisplay(win::utf8(name)).monitorOnly);
 if(catalogSnapshot_)for(auto& group:groups)if(group.pinIndex<0&&group.app.identity.rfind("aumid:",0)==0){
  auto identity=win::lower(win::wide(group.app.identity));auto found=std::find_if(catalogSnapshot_->begin(),catalogSnapshot_->end(),[&](const win::Application& app){return win::lower(app.identity)==identity;});
  if(found!=catalogSnapshot_->end()){group.app.name=win::utf8(found->name);group.app.target=win::utf8(found->target);}
 }
 return groups;
}
void App::layout(bool rebuild){
 if(!running_||quitRequested_||workspaceQuiesced_)return;
 if(config_.hideTaskbar&&(!taskbar_.enabled()||!taskbar_.healthy())){updateTaskbarFailure();return;}
 if(layoutInProgress_){scheduleLayout();return;}
 layoutInProgress_=true;struct LayoutScope{bool& busy;~LayoutScope(){busy=false;}} layoutScope{layoutInProgress_};
 if(interactionDepth_){layoutAfterInteraction_=true;rebuildAfterInteraction_=rebuildAfterInteraction_||rebuild;return;}
 rebuild=rebuild||rebuildAfterInteraction_;rebuildAfterInteraction_=false;
 ++layoutPasses_;auto latest=win::monitors();bool changed=!win::sameDisplayTopology(displays_,latest);
 displays_=std::move(latest);
 if(changed){
  closeDockPreview();
  if(activities_)activities_->hide();if(quick_)quick_->hide();
  if(editor_&&editor_->visible())editor_->open(display());if(backgrounds_)backgrounds_->refreshTopology();
 }
 if(rebuild||changed||panels_.empty()){
  closeDockPreview();
  panels_.clear();
  for(const auto& m:displays_){panels_.push_back(std::make_unique<ui::Panel>(graphics_,*this,m,true));panels_.push_back(std::make_unique<ui::Panel>(graphics_,*this,m,false));}
 }
 auto full=win::fullscreenDisplays(windows_,displays_);
 if(dockPreviewVisible()){
  const auto name=dockFlyout_->monitorName();
  if(paused_||(config_.hideFullscreen&&full.contains(name)))closeDockPreview();
  else{
   const auto pin=dockFlyout_->application();
   auto groups=dockGroups(name);
   auto match=std::find_if(groups.begin(),groups.end(),[&](const DockGroup& group){
    return (!pin.identity.empty()&&win::lower(win::wide(group.app.identity))==win::lower(win::wide(pin.identity)))
      ||win::lower(win::wide(group.app.target))==win::lower(win::wide(pin.target));});
   std::vector<win::Window> sources;
   if(match!=groups.end())for(auto index:match->tasks)if(index<windows_.size())sources.push_back(windows_[index]);
   dockFlyout_->update(sources,theme(),config_.windowPreviews);
  }
 }
 bool reservationActive=false;
 for(auto& panel:panels_){
  auto monitor=display(panel->monitorName());
  if(!panel->top())panel->setGroups(dockGroups(monitor.name));
  const bool suppressed=paused_||(config_.hideFullscreen&&full.contains(monitor.name));
  const auto options=config_.forDisplay(win::utf8(monitor.name));
  reservationActive=reservationActive||(!suppressed&&(panel->top()?options.topBar:options.dock&&options.reserveDock&&!options.autoHide));
  panel->layout(monitor,suppressed);
  panel->refreshHover();
 }
 // Removing the last appbar can discard a pre-existing custom work rectangle.
 // This local correction is forbidden while an independent native owner lives.
 if(!reservationActive&&!taskbar_.enabled()&&!taskbar_.recoveryOwnerAlive()&&!restoreWorkspace()){
  withdrawPanels();queueWorkspaceRetry();
 }
 if(!liveWallpaper_)liveWallpaper_=std::make_unique<ui::LiveWallpaper>(graphics_);
 if(!workspaceQuiesced_)updateWorkspaceWallpaper();
 updateTiling();
 if(editor_)editor_->update();
 if(activities_)activities_->update();
 if(quick_){quick_->setTheme(theme());quick_->invalidate();}
 if(extensionRuntime_)extensionRuntime_->update();if(extensions_)extensions_->update();
}
void App::endInteraction(){
 if(interactionDepth_)--interactionDepth_;
 if(!interactionDepth_&&quitRequested_){PostQuitMessage(quitExitCode_);return;}
 if(!interactionDepth_&&layoutAfterInteraction_){layoutAfterInteraction_=false;scheduleLayout();}
 if(!interactionDepth_)for(auto& panel:panels_)panel->refreshHover();
}
void App::scheduleLayout(){if(controller_&&!relayoutPending_){relayoutPending_=true;PostMessageW(controller_,win::RelayoutMessage,0,0);}}
void App::refreshWindows(bool reconcile){
 if(paused_)return;events_.acknowledge();for(auto hwnd:events_.takeIdentityInvalidations())collector_.invalidateIdentity(hwnd);for(auto hwnd:events_.takeLifecycleChanges()){collector_.forgetWindow(hwnd);tiling_.forgetWindow(hwnd);}refreshTaskbar();auto latest=collector_.snapshot(reconcile);
 queueShellMetadata();for(auto hwnd:events_.takeUserMoveEnds())tiling_.observeUserMove(hwnd);
 if(latest==windows_){++unchangedSnapshots_;updateTiling();return;}
 auto activeTitle=[](const std::vector<win::Window>& windows){for(const auto& window:windows)if(window.active)return window.title;return std::wstring{};};bool titleChanged=activeTitle(windows_)!=activeTitle(latest);
 windows_=std::move(latest);events_.acknowledge(windows_);if(!panels_.empty())layout();if(titleChanged)for(auto& panel:panels_)if(panel->top())panel->invalidate();if(extensionRuntime_)extensionRuntime_->update();
}
void App::workspaceAppearanceChanged(){if(!running_||quitRequested_)return;workspaces_.refresh();updateWorkspaceWallpaper();if(activities_)activities_->update();}
void App::updateWorkspaceWallpaper(){
 if(workspaceQuiesced_||paused_||quitRequested_||displays_.empty())return;
 win::WorkspaceAppearance style;for(const auto& item:workspaces_.items())if(item.current){style=workspaces_.appearance(item.id);break;}
 workspaceWallpaperNotice_.clear();int preset=config_.animatedWallpaper;bool still=false;std::shared_ptr<const win::IconPixels> image;
 if(style.background){
  still=style.background<=3||style.background==7||style.background==8;
  preset=style.background<=3?style.background:style.background<=6?style.background-3:style.background>=8?4:1;
  if(style.background>=7){
   const auto path=style.background==7?style.image:(win::executablePath().parent_path()/L"assets"/L"wallpapers"/L"woodland-zen-v1.png").wstring();
   image=icons_.requestWorkspaceImage(path);
   if(!image){const bool failed=icons_.workspaceImageFailed(path);workspaceWallpaperNotice_=failed?L"Workspace artwork unavailable. Re-select an image or restore the bundled assets.":L"Loading workspace image…";preset=0;}
  }
 }
 if(!liveWallpaper_)liveWallpaper_=std::make_unique<ui::LiveWallpaper>(graphics_);
 liveWallpaper_->update(preset,displays_,windows_,config_.reduceMotion,nullptr,std::move(image),still);
 updateAutomaticColors();
 SetPropW(controller_,L"Aether.Workspace.Background",reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(style.background)));
}
void App::acceptAutomaticColors(const std::vector<Theme>& palettes){
 automaticWaiting_=false;if(!config_.autoWallpaperColors||palettes.size()!=3)return;auto next=withPaletteColors(config_.theme,palettes[static_cast<std::size_t>(config_.wallpaperColorScheme)]);if(theme()==next)return;automaticTheme_=std::move(next);SetPropW(controller_,L"Aether.AutomaticColors.Background",reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(automaticTheme_->background)));SetPropW(controller_,L"Aether.AutomaticColors.Revision",reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(++automaticRevision_)));scheduleLayout();if(editor_)editor_->update();if(activities_)activities_->update();if(quick_)quick_->setTheme(theme());redraw();
}
void App::updateAutomaticColors(){
 if(!config_.autoWallpaperColors){if(automaticTheme_){automaticTheme_.reset();scheduleLayout();redraw();}automaticKey_.clear();automaticWaiting_=false;automaticRequest_=0;return;}
 std::string id;win::WorkspaceAppearance style;for(const auto& item:workspaces_.items())if(item.current){wchar_t text[40]{};StringFromGUID2(item.id,text,40);id=win::utf8(text);style=workspaces_.appearance(item.id);break;}
 const auto key=id+":"+std::to_string(style.background)+":"+win::utf8(style.image)+":"+std::to_string(config_.animatedWallpaper)+":"+std::to_string(config_.wallpaperColorScheme);
 if(key!=automaticKey_){automaticKey_=key;automaticPath_.clear();automaticWaiting_=false;automaticRequest_=0;
  if(style.background>=7){automaticPath_=style.background==7?style.image:(win::executablePath().parent_path()/L"assets"/L"wallpapers"/L"woodland-zen-v1.png").wstring();automaticWaiting_=true;}
  else if(style.background||config_.animatedWallpaper){const int scene=style.background?style.background<=3?style.background:style.background-3:config_.animatedWallpaper;const std::uint32_t colors[]={scene==1?0x397F91u:scene==2?0x765DB8u:0xC16D57u,scene==1?0x52BC97u:scene==2?0x3D86B0u:0x896293u,0x081321};std::vector<std::uint8_t> pixels;for(auto rgb:colors){pixels.push_back(static_cast<std::uint8_t>(rgb));pixels.push_back(static_cast<std::uint8_t>(rgb>>8));pixels.push_back(static_cast<std::uint8_t>(rgb>>16));pixels.push_back(255);}acceptAutomaticColors(wallpaperPalettes(config_.theme,pixels,3,1));}
  else automaticRequest_=wallpaper_.requestQuery(displays_,std::numeric_limits<std::uint64_t>::max()-1,true);
 }
 if(automaticWaiting_){auto image=icons_.requestWorkspaceImage(automaticPath_);if(image)acceptAutomaticColors(wallpaperPalettes(config_.theme,image->bytes,image->width,image->height));else if(icons_.workspaceImageFailed(automaticPath_))automaticWaiting_=false;}
}
void App::automaticColorsCompletion(const win::WallpaperCompletion& completion){
 if(!config_.autoWallpaperColors||!automaticRequest_||completion.id!=automaticRequest_||completion.scope!=std::numeric_limits<std::uint64_t>::max()-1)return;automaticRequest_=0;
 if(completion.outcome!=win::ProviderOutcome::Ok||completion.result.status!=WallpaperStatus::Ok||!completion.result.after)return;
 const auto& snapshot=*completion.result.after;std::vector<Rect> bounds;for(const auto& monitor:displays_)bounds.push_back(monitor.bounds);auto mapping=mapWallpaperDisplays(snapshot,bounds);if(!mapping||mapping->empty())return;
 auto selected=std::find_if(snapshot.monitors.begin(),snapshot.monitors.end(),[&](const auto& monitor){return monitor.display.device==mapping->front().device;});
 if(selected==snapshot.monitors.end()||selected->path.empty()){auto pixel=colorRefBgra(snapshot.color);acceptAutomaticColors(wallpaperPalettes(config_.theme,pixel,1,1));}else{automaticPath_=selected->path;automaticWaiting_=true;updateAutomaticColors();}
}
void App::updateStatus(){
 if(paused_)return;if(liveWallpaper_&&!workspaceQuiesced_)updateWorkspaceWallpaper();++statusPolls_;auto next=win::readStatus(config_.clock24,extensionRuntime_&&extensionRuntime_->resourcesActive());
 bool changed=next.clock!=status_.clock||next.volume!=status_.volume||next.muted!=status_.muted||next.network!=status_.network||next.power!=status_.power||next.battery!=status_.battery||next.resources!=status_.resources;
 status_=std::move(next);if(changed){for(auto& panel:panels_)if(panel->top())panel->invalidate();if(quick_)quick_->invalidate();if(extensionRuntime_)extensionRuntime_->update();}
}
void App::redraw(){
 if(paused_)return;for(auto& panel:panels_)panel->invalidate();
 if(activities_)activities_->invalidate();if(editor_)editor_->invalidate();if(looks_)looks_->update();if(quick_)quick_->invalidate();if(backgrounds_)backgrounds_->invalidate();if(extensions_)extensions_->update();if(extensionRuntime_)extensionRuntime_->update();
}
void App::apply(Config next){
 next=parseSettings(serializeSettings(next));
 // Configuration-only clients and suspended/closing instances never arm a
 // native helper. A paused Apply can revoke, but cannot create, a resume token.
 const bool active=running_&&!paused_&&!quitRequested_;
 if(active&&next.hideTaskbar&&!emergencyRegistered_){
  next.hideTaskbar=false;
  win::message(controller_,L"Ctrl+Alt+F12 is unavailable, so Aether will not hide the Windows taskbar. Free that shortcut and restart Aether to enable replacement.",MB_OK|MB_ICONWARNING);
 }
 if(active&&next.hideTaskbar&&!armTaskbar()){
  next.hideTaskbar=false;retireDesktop();
  win::message(controller_,L"The original workspace or recovery helper did not become ready. The Windows taskbar has been left visible.",MB_OK|MB_ICONWARNING);
 }
 if(!next.hideTaskbar){
  taskbarSessionSuspended_=false;
  if(running_&&(config_.hideTaskbar||workspaceQuiesced_||workspaceRestorePending_||taskbar_.recoveryOwnerAlive())){retireDesktop();resumeWorkspace();}
  else if(!running_)taskbar_.setEnabled(false);
 }
 try{win::saveConfig(next);}catch(const std::exception& e){
  taskbarSessionSuspended_=false;if(running_){retireDesktop();resumeWorkspace();}else taskbar_.setEnabled(false);
  if(controller_)KillTimer(controller_,RecoveryTimer);
  config_.hideTaskbar=false;scheduleLayout();
  if(active)win::message(controller_,win::wide(e.what()),MB_OK|MB_ICONERROR);else win::log(e.what());
  return;
 }
 if(next.pins!=config_.pins)++pinRevision_;
 closeDockPreview();config_=std::move(next);icons_.setPack(config_.iconPack);automaticKey_.clear();automaticRequest_=0;automaticWaiting_=false;automaticTheme_.reset();updateStatus();scheduleLayout();if(editor_)editor_->invalidate();redraw();
}
bool App::dockPreviewVisible()const{return dockFlyout_&&dockFlyout_->visible();}
bool App::dockPreviewAllowed(const std::wstring& name)const{
 if(paused_||quitRequested_||interactionDepth_)return false;
 if((activities_&&activities_->visible())||(quick_&&quick_->visible())||(editor_&&editor_->visible())||(backgrounds_&&backgrounds_->visible())||(extensions_&&extensions_->visible())||(looks_&&looks_->visible())||(marketplace_&&marketplace_->visible()))return false;
 auto monitor=std::find_if(displays_.begin(),displays_.end(),[&](const win::Monitor& item){return item.name==name;});
 return monitor!=displays_.end()&&config_.forDisplay(win::utf8(name)).dock&&
  (!config_.hideFullscreen||!win::fullscreenDisplays(windows_,displays_).contains(name));
}
bool App::dockPreviewContainsPointer()const{return dockFlyout_&&dockFlyout_->containsPointer();}
void App::closeDockPreview(){if(dockFlyout_)dockFlyout_->close();}
void App::openDockPreview(const std::wstring& name,const Pin& pin,Rect anchor,bool focus){
 if(paused_||quitRequested_)return;
 const auto monitor=std::find_if(displays_.begin(),displays_.end(),[&](const win::Monitor& item){return item.name==name;});
 if(monitor==displays_.end())return;
 if(config_.hideFullscreen&&win::fullscreenDisplays(windows_,displays_).contains(name))return;
 auto groups=dockGroups(name);
 auto match=std::find_if(groups.begin(),groups.end(),[&](const DockGroup& group){
  return (!pin.identity.empty()&&win::lower(win::wide(group.app.identity))==win::lower(win::wide(pin.identity)))
    ||win::lower(win::wide(group.app.target))==win::lower(win::wide(pin.target));});
 if(match==groups.end()||match->tasks.empty()){closeDockPreview();return;}
 std::vector<win::Window> sources;
 for(auto index:match->tasks)if(index<windows_.size())sources.push_back(windows_[index]);
 if(activities_)activities_->hide();if(quick_)quick_->hide();
 if((editor_&&editor_->visible())||(backgrounds_&&backgrounds_->visible())||(extensions_&&extensions_->visible()))return;
 if(!dockFlyout_)dockFlyout_=std::make_unique<ui::DockFlyout>(graphics_,icons_,[this]{for(auto& panel:panels_)panel->previewClosed();});
 dockFlyout_->open(*monitor,match->app,anchor,config_.forDisplay(win::utf8(name)).edge,sources,theme(),config_.windowPreviews,focus);
}
void App::openActivities(const std::wstring& name){
 if(paused_||quitRequested_)return;closeDockPreview();if(backgrounds_)backgrounds_->hide();if(extensions_)extensions_->hide();
 if(!activities_)activities_=std::make_unique<ui::Activities>(graphics_,*this);
 activities_->open(display(name));
}
void App::openEditor(){if(paused_||quitRequested_)return;closeDockPreview();if(backgrounds_)backgrounds_->hide();if(extensions_)extensions_->hide();if(!editor_)editor_=std::make_unique<ui::Editor>(graphics_,*this);editor_->open(display());}
void App::openLooks(){if(paused_||quitRequested_)return;closeDockPreview();if(!looks_)looks_=std::make_unique<ui::Looks>(graphics_,*this);looks_->open(display());}
void App::openQuick(const std::wstring& name){if(paused_||quitRequested_)return;closeDockPreview();if(backgrounds_)backgrounds_->hide();if(extensions_)extensions_->hide();if(!quick_)quick_=std::make_unique<ui::QuickSettings>(graphics_,*this);quick_->open(display(name));}
ui::ExtensionRuntime& App::extensions(){if(!extensionRuntime_)extensionRuntime_=std::make_unique<ui::ExtensionRuntime>(graphics_,*this);return *extensionRuntime_;}
std::wstring App::liveWallpaperNotice()const{return liveWallpaper_?liveWallpaper_->notice():L"Animated wallpaper has not started.";}
void App::openMarketplace(){if(paused_||quitRequested_)return;closeDockPreview();if(editor_)editor_->hide();if(looks_)looks_->hide();if(backgrounds_)backgrounds_->hide();if(extensions_)extensions_->hide();if(activities_)activities_->hide();if(quick_)quick_->hide();if(!marketplace_)marketplace_=std::make_unique<ui::Marketplace>(graphics_,*this);marketplace_->open(display());}
void App::openBackgrounds(){
 if(paused_||quitRequested_)return;closeDockPreview();if(editor_)editor_->hide();if(activities_)activities_->hide();if(quick_)quick_->hide();if(extensions_)extensions_->hide();
 if(!backgrounds_)backgrounds_=std::make_unique<ui::Backgrounds>(graphics_,*this);backgrounds_->open(display());
}
void App::openExtensions(){
 if(paused_||quitRequested_)return;closeDockPreview();if(editor_)editor_->hide();if(activities_)activities_->hide();if(quick_)quick_->hide();if(backgrounds_)backgrounds_->hide();
 std::string error;if(!extensions().start(&error)){win::log("Native extensions unavailable: "+error);return;}
 if(!extensions_)extensions_=std::make_unique<ui::Extensions>(graphics_,*this);extensions_->open(display());
}
void App::activateGroup(const DockGroup& group,bool launchNew){
 if(launchNew||group.tasks.empty()){launchTarget(controller_,win::wide(group.app.target));return;}
 std::vector<win::Window> candidates;
 for(auto index:group.tasks)if(index<windows_.size()&&win::windowAlive(windows_[index].hwnd,windows_[index].pid))candidates.push_back(windows_[index]);
 if(candidates.empty())return;
 if(candidates.size()==1){win::activate(candidates.front(),true);return;}
 auto active=std::find_if(candidates.begin(),candidates.end(),[](const win::Window& w){return w.hwnd==GetForegroundWindow();});
 auto next=active==candidates.end()?candidates.begin():std::next(active);if(next==candidates.end())next=candidates.begin();win::activate(*next);
}
void App::togglePin(const Pin& pin){
 if(pin.target.empty())return;auto config=config_;
 auto it=std::find_if(config.pins.begin(),config.pins.end(),[&](const Pin& p){return win::lower(win::wide(p.target))==win::lower(win::wide(pin.target));});
 if(it!=config.pins.end())config.pins.erase(it);else if(config.pins.size()<128)config.pins.push_back(pin);
 apply(std::move(config));
}
void App::removePin(std::size_t index){auto config=config_;if(index>=config.pins.size())return;config.pins.erase(config.pins.begin()+static_cast<std::ptrdiff_t>(index));apply(std::move(config));}
void App::movePin(std::size_t from,std::size_t to){auto config=config_;if(reorderPin(config,from,to))apply(std::move(config));}
void App::setEdge(const std::wstring& name,Edge edge){auto config=config_;auto key=win::utf8(name);auto options=config.forDisplay(key);options.edge=edge;config.displays[key]=options;apply(std::move(config));}
void App::choosePin(HWND owner){
 COMDLG_FILTERSPEC filter{L"Windows applications and shortcuts",L"*.exe;*.lnk"};
 auto path=win::chooseFile(owner,false,L"Pin an application",&filter,1);if(!path)return;
 const auto id=shell_.requestPin(*path,pinRevision_);
 if(id){DWORD pid=0;GetWindowThreadProcessId(owner,&pid);pendingPins_[id]={true,pinRevision_,Pin{"",win::utf8(path->native()),""},owner,pid};}
 else win::message(owner,L"The application resolver is busy. Try again shortly.",MB_OK|MB_ICONWARNING);
}
void App::launchTarget(HWND owner,const std::wstring& target){
 if(quitRequested_||!running_||target.empty())return;
 if(!shell_.requestLaunch(owner,target))win::log("Launch queue unavailable or full.");
}
void App::refreshPinIdentities(){
 for(const auto& pin:config_.pins){
  auto path=std::filesystem::path(win::wide(pin.target));
  if(win::lower(path.extension().wstring())!=L".lnk")continue;
  auto id=shell_.requestPin(path,pinRevision_);
  if(id)pendingPins_[id]={false,pinRevision_,pin,nullptr,0};
 }
}
void App::queueShellMetadata(){
 auto requests=collector_.takeMetadataRequests();
 if(!requests.empty()&&!shell_.requestMetadata(requests))collector_.rejectMetadata(requests);
}
void App::shellCompletions(){
 bool metadataChanged=false;
 for(auto& completion:shell_.takeCompletions()){
  if(quitRequested_)continue;
  if(completion.kind==win::ShellRequestKind::Metadata){
   metadataChanged=collector_.acceptMetadata(completion.metadata)||metadataChanged;
  }else if(completion.kind==win::ShellRequestKind::Pin){
   auto found=pendingPins_.find(completion.id);if(found==pendingPins_.end())continue;
   auto pending=found->second;pendingPins_.erase(found);
   if(completion.revision!=pending.revision||completion.target!=win::wide(pending.expected.target))continue;
   if(pending.add){
    if(pinRevision_!=pending.revision)continue;
    if(completion.outcome==win::ProviderOutcome::Ok&&completion.pin)togglePin(*completion.pin);
    else if(win::windowAlive(pending.owner,pending.ownerPid))win::message(pending.owner,L"That shortcut could not be resolved. Try again after checking it is available.",MB_OK|MB_ICONWARNING);
   }else if(completion.outcome==win::ProviderOutcome::Ok&&completion.pin){
    auto pin=std::find(config_.pins.begin(),config_.pins.end(),pending.expected);
    if(pin!=config_.pins.end()&&pin->identity!=completion.pin->identity){auto next=config_;next.pins[static_cast<std::size_t>(std::distance(config_.pins.begin(),pin))].identity=completion.pin->identity;apply(std::move(next));}
   }
  }else if(completion.kind==win::ShellRequestKind::Launch&&completion.outcome!=win::ProviderOutcome::Ok){
   win::log("Shell launch did not confirm success; no automatic retry was made.");
   if(completion.outcome==win::ProviderOutcome::Failed&&win::windowAlive(completion.owner,completion.ownerPid))
    win::message(completion.owner,L"Windows could not open that target.",MB_OK|MB_ICONWARNING);
  }
 }
 if(metadataChanged)refreshWindows();
 queueShellMetadata();
}
bool App::tilingSuppressed() const{
 return paused_||quitRequested_||interactionDepth_||events_.moving()||
  (activities_&&activities_->visible())||(editor_&&editor_->visible())||
  (quick_&&quick_->visible())||(backgrounds_&&backgrounds_->visible())||(extensions_&&extensions_->visible())||dockPreviewVisible();
}
void App::updateTiling(){
 if(!running_)return;
 tiling_.update(config_,displays_,windows_,tilingSuppressed());
}
void App::tileNow(const std::wstring& name){
 if(!running_||paused_||quitRequested_||interactionDepth_||events_.moving())return;
 auto monitor=display(name);refreshWindows();
 tiling_.update(config_,displays_,windows_,false,monitor.name);
}
void App::logOriginalWorkspace(const char* stage) const noexcept{
 try{
  std::ostringstream out;out<<"App original workspace DIAG stage="<<stage<<" process="<<GetCurrentProcessId()<<" tick="<<GetTickCount64()
   <<" captured="<<workAreasCaptured_<<" monitorCount="<<originalWorkspace_.count
   <<" controllerPresent="<<(controller_!=nullptr)<<" independentOwner="<<taskbar_.recoveryOwnerAlive();
  for(DWORD i=0;i<std::min<DWORD>(originalWorkspace_.count,win::WorkAreaCapacity);++i){const auto& m=originalWorkspace_.entries[i];
   out<<" [monitor="<<i<<" device="<<win::utf8(m.device)
    <<" bounds=("<<m.bounds.left<<","<<m.bounds.top<<","<<m.bounds.right<<","<<m.bounds.bottom
    <<") targetWork=("<<m.work.left<<","<<m.work.top<<","<<m.work.right<<","<<m.work.bottom<<")]";
  }win::log(out.str());
 }catch(...){}
}
void App::withdrawPanels(){
 workspaceQuiesced_=true;
 if(liveWallpaper_)liveWallpaper_->stop();if(marketplace_)marketplace_->hide();
 closeDockPreview();
 if(looks_)looks_->hide();
 // Keep objects alive while a context menu's nested loop unwinds.
 for(auto& panel:panels_)panel->layout(display(panel->monitorName()),true);
}
bool App::restoreWorkspace(){
 // An exited helper can still leave an armed immutable parent context.
 // Neither that pending context nor a live owner grants local SPI ownership.
 if(taskbarRetirementPending_||taskbar_.recoveryOwnerAlive()){workspaceRestorePending_=true;return false;}
 if(!workAreasCaptured_)return true;
 const auto result=win::restoreWorkAreas(originalWorkspace_);
 workspaceRestorePending_=result!=win::WorkAreaResult::Complete;
 if(workspaceRestorePending_&&!workspaceRestoreReported_){
  win::log("Original physical workspace restoration remains pending; Aether reservations stay withdrawn.");
  workspaceRestoreReported_=true;
 }
 if(!workspaceRestorePending_)workspaceRestoreReported_=false;
 return !workspaceRestorePending_;
}
void App::resumeWorkspace(){
 if(!restoreWorkspace()){
  queueWorkspaceRetry();
  return;
 }
 if(controller_)KillTimer(controller_,WorkspaceTimer);
 if(running_&&!paused_&&!quitRequested_){workspaceQuiesced_=false;scheduleLayout();}
}
void App::queueWorkspaceRetry(){
 if(!controller_||!running_||quitRequested_)return;
 if(SetTimer(controller_,WorkspaceTimer,1000,nullptr))return;
 win::log("Workspace retry timer unavailable; retaining the original desktop through passive shutdown recovery.");
 workspaceQuiesced_=true;quitRequested_=true;quitExitCode_=1;taskbarSessionSuspended_=false;
 if(interactionDepth_)EndMenu();else PostQuitMessage(quitExitCode_);
}
void App::retireDesktop(bool wait){
 withdrawPanels();
 if(controller_)KillTimer(controller_,RecoveryTimer);
 // Ordinary session/emergency/quit retain their acknowledged synchronous contract.
 // Failure recovery progresses on the temporary timer without blocking the UI.
 taskbarRetirementPending_=wait?!taskbar_.setEnabled(false):!taskbar_.disableAsync();
 if(!restoreWorkspace())queueWorkspaceRetry();
}
bool App::armTaskbar(){
 // Ordinary appearance Apply preserves an existing healthy generation.
 if(taskbar_.enabled()&&taskbar_.healthy())return true;
 withdrawPanels();
 const bool retired=taskbar_.setEnabled(false);
 taskbarRetirementPending_=!retired;
 if(!retired||taskbar_.recoveryOwnerAlive()||!restoreWorkspace())return false;
 if(!emergencyRegistered_||!taskbar_.setEnabled(true)||!SetTimer(controller_,RecoveryTimer,1000,nullptr))return false;
 workspaceRestorePending_=false;workspaceQuiesced_=false;KillTimer(controller_,WorkspaceTimer);
 return true;
}
void App::refreshTaskbar(){
 // Startup collects windows before the first generation exists. Once there
 // is a held generation, withdraw reservations before known failure recovery.
 const bool hadLifetime=config_.hideTaskbar&&(taskbar_.helperProcessId()!=0||taskbarRetirementPending_);
 if(hadLifetime&&(!taskbar_.enabled()||!taskbar_.healthy())){updateTaskbarFailure();return;}
 taskbar_.refresh(true);
 // The internal async failure path also closes the health-check/death race.
 if(hadLifetime&&!taskbar_.enabled())updateTaskbarFailure();
}
bool App::confirmActiveTaskbar(){
 if(!running_||paused_||quitRequested_||!config_.hideTaskbar)return true;
 if(!taskbar_.enabled()||!taskbar_.healthy()||!taskbar_.refreshAndConfirm()){
  win::log("Active taskbar refresh was not confirmed; requesting asynchronous desktop recovery.");
  updateTaskbarFailure();return false;
 }
 return true;
}
void App::updateTaskbarFailure(bool wait){
 taskbarSessionSuspended_=false;retireDesktop(wait);config_.hideTaskbar=false;
 try{win::saveConfig(config_);}catch(const std::exception& e){win::log(e.what());}
 resumeWorkspace();scheduleLayout();if(editor_){editor_->update();editor_->invalidate();}
}
void App::restoreNative(){
 // Withdraw reservations before the recovery owner restores its before-state.
 updateTaskbarFailure(true);
}
void App::quit(){if(controller_)PostMessageW(controller_,WM_CLOSE,0,0);}
void App::globalMenu(HWND owner,POINT point){
 closeDockPreview();
 HMENU menu=CreatePopupMenu();if(!menu)return;
 AppendMenuW(menu,MF_STRING,1,L"Activities\tCtrl+Alt+Space");
 AppendMenuW(menu,MF_STRING,2,L"Desktop Studio\tCtrl+Alt+E");
 AppendMenuW(menu,MF_STRING,8,L"Looks and widgets");AppendMenuW(menu,MF_STRING,9,L"Marketplace");
 AppendMenuW(menu,MF_STRING,6,L"Backgrounds");AppendMenuW(menu,MF_STRING,7,L"Native extensions");
 AppendMenuW(menu,MF_STRING,3,L"Quick settings");AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
 AppendMenuW(menu,MF_STRING,4,L"Restore Windows taskbar\tCtrl+Alt+F12");
 AppendMenuW(menu,MF_STRING,5,L"Quit Aether UI");
 SetForegroundWindow(owner);auto choice=TrackPopupMenuEx(menu,TPM_RETURNCMD|TPM_NONOTIFY|TPM_RIGHTBUTTON,point.x,point.y,owner,nullptr);DestroyMenu(menu);PostMessageW(owner,WM_NULL,0,0);
 switch(choice){case 1:openActivities();break;case 2:openEditor();break;case 3:openQuick();break;case 4:restoreNative();break;case 5:quit();break;case 6:openBackgrounds();break;case 7:openExtensions();break;case 8:openLooks();break;case 9:openMarketplace();break;default:break;}
}
LRESULT CALLBACK App::procedure(HWND h,UINT m,WPARAM w,LPARAM l){
 App* app=reinterpret_cast<App*>(GetWindowLongPtrW(h,GWLP_USERDATA));
 if(m==WM_NCCREATE){app=static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);app->controller_=h;SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));}
 if(!app)return DefWindowProcW(h,m,w,l);
 try{return app->event(m,w,l);}catch(const std::exception& e){win::log(std::string("Controller exception: ")+e.what());}catch(...){win::log("Unexpected controller exception.");}
 return DefWindowProcW(h,m,w,l);
}
LRESULT App::event(UINT m,WPARAM w,LPARAM l){
 if(taskbarCreated_&&m==taskbarCreated_){if(quitRequested_||!running_)return 0;registerTray();if(paused_)return 0;refreshTaskbar();workspaces_.unwatch();workspaces_.refresh();SetPropW(controller_,L"Aether.Workspace.Notifications",reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(workspaces_.watch(controller_))));refreshWindows(true);layout(true);return 0;}
 switch(m){
 case WM_CLOSE:markLifecycle(AppLifecyclePhase::CloseRequested);quitRequested_=true;taskbarSessionSuspended_=false;if(interactionDepth_)EndMenu();retireDesktop();if(!interactionDepth_)PostQuitMessage(quitExitCode_);return 0;
 case WM_ENDSESSION:if(w){quitRequested_=true;taskbarSessionSuspended_=false;if(interactionDepth_)EndMenu();retireDesktop();}else confirmActiveTaskbar();return 0;
 case WM_QUERYENDSESSION:confirmActiveTaskbar();return TRUE;
 case win::OpenEditorMessage:openEditor();return 0;
 case win::ApplyMessage:restoreNative();return 0;
 case win::RelayoutMessage:relayoutPending_=false;layout();return 0;
 case win::RefreshMessage:if(!paused_&&!quitRequested_){for(auto& panel:panels_)panel->refreshHover();SetTimer(controller_,EventsTimer,100,nullptr);}return 0;
 case win::CatalogMessage:catalogSnapshot_=catalog_.snapshot();scheduleLayout();if(activities_)activities_->update();return 0;
 case win::IconsMessage:if(editor_)editor_->update();updateWorkspaceWallpaper();if(activities_)activities_->update();redraw();return 0;
 case win::WorkspaceChangedMessage:
  if(running_&&!paused_&&!quitRequested_){SetPropW(controller_,L"Aether.Workspace.RefreshQueued",reinterpret_cast<HANDLE>(1));SetTimer(controller_,EventsTimer,100,nullptr);}else RemovePropW(controller_,L"Aether.Workspace.RefreshQueued");return 0;
 case win::ShellMessage:shellCompletions();return 0;
 case win::PluginDataMessage:if(!paused_&&!quitRequested_&&extensionRuntime_)extensionRuntime_->update();return 0;
 case win::WallpaperMessage:for(const auto& completion:wallpaper_.takeCompletions())if(!quitRequested_){automaticColorsCompletion(completion);if(backgrounds_)backgrounds_->update(completion);if(editor_)editor_->paletteCompletion(completion);}return 0;
 case WM_DISPLAYCHANGE:scheduleLayout();return 0;
 case WM_SETTINGCHANGE:scheduleLayout();return 0;
 case WM_TIMER:
  if(w==RecoveryTimer){
   if(paused_||quitRequested_||!running_)return 0;
   // Detect a dead/revoked helper before deciding whether this timer retires.
   if(!taskbar_.enabled()||!taskbar_.healthy()){updateTaskbarFailure();return 0;}
   taskbar_.heartbeat(true);if(!taskbar_.enabled())updateTaskbarFailure();
  }
  else if(w==WorkspaceTimer){
   if(!workspaceRestorePending_){KillTimer(controller_,WorkspaceTimer);return 0;}
   if(taskbar_.enabled()&&taskbar_.healthy()){KillTimer(controller_,WorkspaceTimer);return 0;}
   taskbarRetirementPending_=!taskbar_.disableAsync();resumeWorkspace();
  }
  else if(w==StatusTimer)updateStatus();
  else if(w==ReconcileTimer){
   if(paused_||quitRequested_||!running_)return 0;
   if(config_.hideTaskbar&&(!taskbar_.enabled()||!taskbar_.healthy()))updateTaskbarFailure();
   else{refreshTaskbar();if(config_.hideTaskbar&&!taskbar_.enabled())updateTaskbarFailure();}
   refreshWindows(true);
  }
  else if(w==EventsTimer){
   KillTimer(controller_,EventsTimer);const bool workspaceDirty=RemovePropW(controller_,L"Aether.Workspace.RefreshQueued")!=nullptr;
   if(workspaceDirty&&!paused_&&!quitRequested_){SetPropW(controller_,L"Aether.Workspace.Notifications",reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(workspaces_.watch(controller_))));workspaces_.refresh();SetPropW(controller_,L"Aether.Workspace.RefreshCount",reinterpret_cast<HANDLE>(reinterpret_cast<ULONG_PTR>(GetPropW(controller_,L"Aether.Workspace.RefreshCount"))+1));}
   refreshWindows();if(workspaceDirty&&!paused_&&!quitRequested_){updateWorkspaceWallpaper();if(activities_)activities_->update();}
  }
  return 0;
 case WM_WTSSESSION_CHANGE:{
  if(quitRequested_||!running_)return 0;
  win::SessionSignal signal;
  if(w==WTS_SESSION_LOCK)signal=win::SessionSignal::Lock;
  else if(w==WTS_SESSION_UNLOCK)signal=win::SessionSignal::Unlock;
  else if(w==WTS_CONSOLE_DISCONNECT||w==WTS_REMOTE_DISCONNECT)signal=win::SessionSignal::Disconnect;
  else if(w==WTS_CONSOLE_CONNECT||w==WTS_REMOTE_CONNECT)signal=win::SessionSignal::Connect;
  else return 0;
  const auto transition=sessionPause_.apply(signal);
  if(transition.enteredPause){
   // Capture before disabling: an expired/hung generation must never rearm.
   taskbarSessionSuspended_=config_.hideTaskbar&&taskbar_.enabled()&&taskbar_.healthy();
   paused_=true;if(interactionDepth_)EndMenu();retireDesktop();
   if(liveWallpaper_)liveWallpaper_->stop();if(marketplace_)marketplace_->hide();if(extensionRuntime_)extensionRuntime_->pause(true);if(backgrounds_)backgrounds_->hide();if(extensions_)extensions_->hide();closeDockPreview();events_.pause(true);KillTimer(controller_,StatusTimer);KillTimer(controller_,ReconcileTimer);KillTimer(controller_,EventsTimer);
   if(activities_)activities_->hide();if(quick_)quick_->hide();if(editor_)editor_->hide();if(looks_)looks_->hide();layout();
  }else if(transition.leftPause){
   const bool resume=taskbarSessionSuspended_;taskbarSessionSuspended_=false;
   if(config_.hideTaskbar){
    if(!resume||!armTaskbar())updateTaskbarFailure();
   }
   paused_=false;
   workspaces_.refresh();workspaces_.watch(controller_);
   if(workspaceQuiesced_)resumeWorkspace();
   if(extensionRuntime_)extensionRuntime_->pause(false);events_.pause(false);SetTimer(controller_,StatusTimer,15000,nullptr);SetTimer(controller_,ReconcileTimer,30000,nullptr);updateStatus();refreshWindows(true);layout();
   confirmActiveTaskbar();
  }
  return 0;
 }
 case WindowsTapMessage:
  if(running_&&!paused_&&!quitRequested_&&!interactionDepth_&&config_.hideTaskbar&&taskbar_.enabled()){
   if(activities_&&activities_->visible()&&!activities_->closing())activities_->dismiss();
   else {refreshWindows();closeDockPreview();if(editor_)editor_->hide();if(looks_)looks_->hide();if(quick_)quick_->hide();if(backgrounds_)backgrounds_->hide();if(extensions_)extensions_->hide();if(marketplace_)marketplace_->hide();
    if(!activities_)activities_=std::make_unique<ui::Activities>(graphics_,*this);activities_->open(display(),true);}
  }
  return 0;
 case WM_HOTKEY:
  if(activities_&&activities_->workspaceOverviewActive()&&((w>=20&&w<=26)||(w>=50&&w<=58))){activities_->workspaceHotkey(static_cast<int>(w>26?w-50:w-20),w>26);return 0;}
  if(w==LauncherHotkey)openActivities();else if(w==EditorHotkey)openEditor();else if(w==RecoveryHotkey)restoreNative();
    else if(!tilingSuppressed()&&w>=20&&w<=26)tiling_.snapActive(config_,displays_,static_cast<int>(w)-20);
  else if(!tilingSuppressed()&&w>=40&&w<=43)tiling_.moveActive(displays_,static_cast<MoveDirection>(w-40));
  else if(!tilingSuppressed()&&w>=50&&w<=58)tiling_.snapActive(config_,displays_,static_cast<int>(w)-50,true);
  return 0;
 case win::TrayMessage:{auto event=LOWORD(l);if(event==WM_CONTEXTMENU||event==WM_RBUTTONUP){POINT p{};GetCursorPos(&p);globalMenu(controller_,p);}else if(event==NIN_SELECT||event==WM_LBUTTONUP)openEditor();return 0;}
 default:return DefWindowProcW(controller_,m,w,l);
 }
}
}


