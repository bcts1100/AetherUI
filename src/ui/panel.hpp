#pragma once
#include "canvas.hpp"
#include "core/personalization.hpp"
#include "platform/desktop.hpp"
namespace aether {class App;}
namespace aether::ui {
class Panel final:public Surface {
 App& app_;win::Monitor monitor_;bool top_;
 bool arranging_=false;std::optional<TopModule> moduleDrag_;float moduleDragX_=0;POINT moduleDragStart_{};std::vector<ModuleBox> moduleBoxes_;
 DisplayOptions options_;win::Appbar appbar_;
 Rect full_{},placed_{};bool placedValid_=false,suppressed_=false,collapsed_=false;int regionRadius_=-1;
 std::vector<Rect> barRegions_;int barRegionRadius_=-1;void paintBar(Canvas&);
 std::vector<DockGroup> groups_;Page page_{};
 struct Hit {D2D1_RECT_F rect;int item;};
 std::vector<Hit> hits_;int hover_=-99;
 std::map<int,float> hoverFrom_;int animationTarget_=-99;std::uint64_t animationStart_=0;bool animationArmed_=false;
 float hoverAmount(int) const;void setHover(int,bool animate=false);void cancelAnimation();
 std::optional<Pin> previewPin_,dismissedPin_;Rect previewAnchor_{};POINT dismissedPoint_{};
 bool hoverTimerArmed_=false,hoverDirty_=false,hoverSyncPosted_=false,closingForLayout_=false,previewOwned_=false;
 Rect previewAnchor(int item) const;void cancelHoverTimer();void cancelPreviewTimers();
 void reconcileHover();void closeForLayout();
 bool dragging_=false,edgeDragging_=false;std::optional<Pin> dragPin_;POINT dragStart_{};
 HWND tooltip_{};std::wstring tooltipText_;
 void paint(Canvas&) override;
 void pointer(float,float) override;void leave() override;
 void click(float,float) override;void context(POINT) override;
 LRESULT event(UINT,WPARAM,LPARAM,bool&) override;
 int hit(float,float) const;
 void collapse(bool);
 void tip(const std::wstring&);
 std::wstring hoverText() const;
 void turnPage(int direction);
public:
 Panel(Graphics&,App&,const win::Monitor&,bool top);
 ~Panel() override;
 void layout(const win::Monitor&,bool suppressed);
 void setGroups(std::vector<DockGroup> groups);
 void previewClosed();
 void refreshHover();
 bool top() const{return top_;}
 const std::wstring& monitorName() const{return monitor_.name;}
};
}


