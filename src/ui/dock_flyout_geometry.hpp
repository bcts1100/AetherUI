#pragma once
#include "core/model.hpp"
#include <algorithm>
namespace aether::ui {
struct DockFlyoutGeometry {Rect bounds{};int columns=1,capacity=1;};
// All inputs/output are physical screen coordinates. Only preferred sizes are DIPs.
inline DockFlyoutGeometry dockFlyoutGeometry(Rect work,Rect anchor,Edge edge,int dpi,int count) noexcept {
 if(work.width()<=0||work.height()<=0||count<=0)return {{},1,0};
 const int scaleDpi=std::clamp(dpi,48,480);
 auto px=[&](int n){return std::max(1,static_cast<int>(static_cast<long long>(n)*scaleDpi/96));};
 int gap=px(8),padding=px(24),cardWidth=px(224),cardHeight=px(182),chrome=px(96);
 Rect area=work;
 // Prefer the inward side of the dock; tiny areas use the full bounded work area.
 if(edge==Edge::Bottom&&anchor.top-gap-work.top>=px(120))area.bottom=std::min(work.bottom,anchor.top-gap);
 else if(edge==Edge::Left&&work.right-anchor.right-gap>=px(180))area.left=std::max(work.left,anchor.right+gap);
 else if(edge==Edge::Right&&anchor.left-gap-work.left>=px(180))area.right=std::min(work.right,anchor.left-gap);
 int columns=std::clamp((area.width()-padding+gap)/(cardWidth+gap),1,std::min(3,count));
 int rows=std::clamp((area.height()-chrome-padding+gap)/(cardHeight+gap),1,2);
 rows=std::min(rows,(std::min(count,6)+columns-1)/columns);
 int width=std::min(area.width(),padding+columns*cardWidth+(columns-1)*gap);
 int height=std::min(area.height(),chrome+padding+rows*cardHeight+(rows-1)*gap);
 int left=edge==Edge::Left?area.left:edge==Edge::Right?area.right-width:anchor.left+(anchor.width()-width)/2;
 int top=edge==Edge::Bottom?area.bottom-height:anchor.top+(anchor.height()-height)/2;
 left=std::clamp(left,area.left,area.right-width);top=std::clamp(top,area.top,area.bottom-height);
 return {{left,top,left+width,top+height},columns,std::min(6,columns*rows)};
}
}
