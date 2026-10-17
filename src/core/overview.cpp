#include "overview.hpp"
#include <algorithm>
#include <cmath>
namespace aether {
float overviewEase(float progress) noexcept{if(!std::isfinite(progress))return 1.f;progress=std::clamp(progress,0.f,1.f);return 1.f-(1.f-progress)*(1.f-progress)*(1.f-progress);}
OverviewGrid fullscreenOverviewLayout(float width,float height,int count) noexcept{
 if(!std::isfinite(width)||!std::isfinite(height)||width<320.f||height<360.f)return {};
 const int shown=std::clamp(count,1,6);
 int columns=shown<=3?shown:shown<=4?2:3;
 columns=std::min(columns,std::max(1,static_cast<int>(std::min(3.f,std::floor((width-64.f)/320.f)))));
 const float gap=28.f;
 const float availableHeight=height-240.f;
 const int rows=std::min((shown+columns-1)/columns,std::max(1,static_cast<int>(std::min(3.f,std::floor((availableHeight+gap)/220.f)))));
 const float tileHeight=std::min(516.f,(availableHeight-gap*static_cast<float>(rows-1))/static_cast<float>(rows));
 const float tileWidth=std::min({760.f,(width-96.f-gap*static_cast<float>(columns-1))/static_cast<float>(columns),std::max(200.f,(tileHeight-60.f)*1.65f)});
 const float gridWidth=tileWidth*columns+gap*(columns-1),gridHeight=tileHeight*rows+gap*(rows-1);
 return {columns,rows,width,height,(width-gridWidth)/2.f,180.f+(availableHeight-gridHeight)/2.f,gap,tileWidth,tileHeight};
}
OverviewGrid overviewLayout(float width,float height) noexcept{
 if(!std::isfinite(width)||!std::isfinite(height)||width<160.f||height<296.f)return {};
 const auto columns=std::clamp(static_cast<int>(std::min(4.f,std::floor((width-28.f)/212.f))),1,4);
 const auto rows=std::clamp(static_cast<int>(std::min(3.f,std::floor((height-174.f)/172.f))),1,3);
 return {columns,rows,width,height};
}
OverviewGrid workspaceOverviewLayout(float width,float height,int count) noexcept{
 if(!std::isfinite(width)||!std::isfinite(height)||width<320.f||height<360.f)return {};
 const int shown=std::clamp(count,1,6);int columns=shown<=3?shown:shown<=4?2:3;
 columns=std::min(columns,std::max(1,static_cast<int>(std::min(3.f,std::floor((width-64.f)/320.f)))));
 const float top=height>=600.f?236.f:height>=440.f?208.f:174.f;
 const float available=height-top-(height>=440.f?128.f:90.f),gap=20.f;
 const int rows=std::min((shown+columns-1)/columns,available>=350.f?2:1);
 const float tileHeight=std::min(500.f,(available-gap*static_cast<float>(rows-1))/static_cast<float>(rows));
 const float tileWidth=std::min({760.f,(width-64.f-gap*static_cast<float>(columns-1))/static_cast<float>(columns),std::max(200.f,(tileHeight-60.f)*1.65f)});
 const float gridWidth=tileWidth*columns+gap*(columns-1),gridHeight=tileHeight*rows+gap*(rows-1);
 return {columns,rows,width,height,(width-gridWidth)/2.f,top+(available-gridHeight)/2.f,gap,tileWidth,tileHeight};
}
OverviewCell OverviewGrid::cell(int slot) const noexcept{
 if(columns<=0||rows<=0||slot<0||slot>=capacity())return {};
 const auto cellWidth=tileWidth>0?tileWidth:(width-40.f-gap*static_cast<float>(columns-1))/static_cast<float>(columns);
 const auto cellHeight=tileHeight>0?tileHeight:(height-186.f-gap*static_cast<float>(rows-1))/static_cast<float>(rows);
 const auto x=left+static_cast<float>(slot%columns)*(cellWidth+gap);
 const auto y=top+static_cast<float>(slot/columns)*(cellHeight+gap);
 return {x,y,x+cellWidth,y+cellHeight};
}
int OverviewGrid::hit(float x,float y) const noexcept{
 for(int slot=0;slot<capacity();++slot){const auto bounds=cell(slot);if(x>=bounds.left&&x<bounds.right&&y>=bounds.top&&y<bounds.bottom)return slot;}
 return -1;
}
int overviewPageOffset(int selected,int count,const OverviewGrid& grid) noexcept{
 if(count<=0||selected<0||grid.capacity()<=0)return 0;
 return std::clamp(selected,0,count-1)/grid.capacity()*grid.capacity();
}
int overviewNavigate(int selected,int count,const OverviewGrid& grid,OverviewDirection direction) noexcept{
 if(count<=0)return -1;
 selected=std::clamp(selected,0,count-1);if(grid.capacity()<=0)return selected;
 const int column=selected%grid.columns;
 switch(direction){
 case OverviewDirection::Left:return column>0?selected-1:selected;
 case OverviewDirection::Right:return column+1<grid.columns&&selected+1<count?selected+1:selected;
 case OverviewDirection::Up:return selected>=grid.columns?selected-grid.columns:selected;
 case OverviewDirection::Down:return selected+grid.columns<count?selected+grid.columns:selected;
 case OverviewDirection::PreviousPage:return std::max(0,selected-grid.capacity());
 case OverviewDirection::NextPage:return std::min(count-1,selected+grid.capacity());
 }
 return selected;
}
}
