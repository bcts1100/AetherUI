#pragma once
#include "canvas.hpp"
#include "dock_flyout_geometry.hpp"
#include "platform/desktop.hpp"
#include "platform/window_preview.hpp"
#include <array>
#include <functional>
#include <commctrl.h>
namespace aether::ui {
class DockFlyout final:public Surface {
 win::IconCache& icons_;std::function<void()> onClosed_;bool showing_=false;win::Monitor monitor_;Pin application_;Rect anchor_{};Edge edge_=Edge::Bottom;
 std::vector<win::Window> windows_;DockFlyoutGeometry geometry_{};
 std::array<win::WindowPreview,6> previews_;
 std::array<HWND,6> cards_{};HWND previous_{},next_{},close_{};HFONT font_{};int fontDpi_=0;
 int offset_=0,selected_=0,hover_=-1;bool previewsEnabled_=true,arranging_=false,escapeRegistered_=false;
 static LRESULT CALLBACK buttonProcedure(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);
 std::vector<win::Window> validWindows(const std::vector<win::Window>&) const;
 void releasePreviews();void arrange();void positionControls();void syncPreviews();
 D2D1_RECT_F cardRect(int slot) const;D2D1_RECT_F previewRect(int slot) const;
 int hit(float,float) const;void select(int delta,bool focus=true);void changePage(int delta,bool focus=true);
 void activate(int index);void focusSelected();void key(WPARAM);void drawButton(const DRAWITEMSTRUCT&);
 bool pointerInAnchor() const;
 void paint(Canvas&) override;void pointer(float,float) override;void leave() override;
 void click(float,float) override;LRESULT event(UINT,WPARAM,LPARAM,bool&) override;
public:
 DockFlyout(Graphics&,win::IconCache&,std::function<void()> onClosed={});~DockFlyout() override;
 void open(const win::Monitor&,const Pin&,Rect physicalAnchor,Edge,const std::vector<win::Window>& groupWindows,const Theme&,bool previews,bool focus=false);
 void update(const std::vector<win::Window>& groupWindows,const Theme&,bool previews);
 void close();bool containsPointer() const;
 std::size_t registeredPreviewCount() const;
 const Pin& application() const{return application_;}
 const std::wstring& monitorName() const{return monitor_.name;}
};
}

