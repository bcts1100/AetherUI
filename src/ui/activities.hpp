#pragma once
#include "canvas.hpp"
#include "accessibility.hpp"
#include "platform/desktop.hpp"
#include "platform/catalog.hpp"
#include "platform/window_preview.hpp"
#include "platform/workspaces.hpp"
#include <commctrl.h>
#include "core/overview.hpp"
#include <array>
#include <span>
#include <functional>
namespace aether {class App;}
namespace aether::ui {
class OverviewPreviews final {
 std::array<win::WindowPreview,12> tiles_;
public:
 void sync(HWND owner,std::span<const win::Window> sources,const OverviewGrid& grid,int offset,float scale,bool enabled,BYTE opacity=255) noexcept;
 void hide() noexcept;
 const win::WindowPreview& at(int slot) const{return tiles_.at(static_cast<std::size_t>(slot));}
};
class Activities final:public Surface {
 std::unique_ptr<Accessibility> accessibility_;enum class Focus {Search,Applications,Windows,Results,Close,Tools,Workspaces};Focus focus_=Focus::Search;int focusedControl_=20;
 void accessibleSnapshot();void focusPart(Focus);void tabFocus(bool reverse);void switchTab(bool);bool keyboard(WPARAM,bool search);
 App& app_;HWND edit_{};HFONT font_{};HBRUSH editBrush_{};int fontDpi_=0,fontScale_=0;std::string fontFamily_;std::uint32_t brushColor_=0;
 std::wstring monitorName_;bool windowsTab_=false;
 std::wstring filterMonitor_,groupKey_,notice_;bool grouped_=false;
 win::Workspaces& workspaces_;int workspacePage_=0;bool workspaceEditing_=false;
 struct Drag {std::optional<win::Window> window;std::optional<GUID> workspace;POINT origin{};bool active=false;int target=-1;};Drag drag_;
 struct Control {int id;std::wstring label;D2D1_RECT_F bounds;bool selected=false,enabled=true;};
 std::vector<Control> controls()const;void control(int);void monitorMenu();void closeSelected();void workspaceAction(int,bool move);void workspaceMenu(int,bool renameOnly=false);
 void cancelDrag();void dragPointer(float,float);int workspaceHit(float,float)const;
 void changedView();D2D1_RECT_F closeBounds(int index)const;
 bool fullscreen_=false,closing_=false;float presentation_=1.f,transitionFrom_=1.f,transitionTo_=1.f;
 ULONGLONG transitionStarted_=0;DWORD transitionDuration_=0;
 ULONG_PTR animationFrames_=0;
 std::function<void()> afterClose_;
 bool motionAllowed()const;void transition(float target);void animate();void paintOverview(Canvas&);
 OverviewPreviews previews_;
 struct Layout {float listWidth=0,listBottom=0;OverviewGrid grid;};
 Layout layout() const;void syncPreview();
 struct Result {win::Application app;std::optional<win::Window> window;int score=0;bool pinned=false;std::optional<Pin> presentation;std::vector<win::Window> members;};
 std::vector<Result> results_;int selected_=0,offset_=0,hover_=-1;
 static LRESULT CALLBACK editProcedure(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);
 void rebuild(bool resetSelection=true);void positionEdit();void select(int);void navigate(OverviewDirection);void activateSelected();void pinSelected();
 int hitRow(float x,float y) const;
 void paint(Canvas&) override;void pointer(float,float) override;void leave() override;
 void click(float,float) override;void context(POINT) override;
 LRESULT event(UINT,WPARAM,LPARAM,bool&) override;
 int visibleRows() const;
public:
 Activities(Graphics&,App&);~Activities() override;
 void open(const win::Monitor&,bool openWindows=false);
 void hide();void dismiss();bool closing()const{return closing_;}
 void update();
 bool workspaceOverviewActive()const{return fullscreen_&&windowsTab_&&visible()&&!closing_&&!workspaceEditing_&&GetForegroundWindow()==hwnd_;}
 void workspaceHotkey(int index,bool move){if(workspaceOverviewActive())workspaceAction(index,move);}
};
}




