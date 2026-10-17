#pragma once
namespace aether {
struct OverviewCell {float left{},top{},right{},bottom{};};
enum class OverviewDirection {Left,Right,Up,Down,PreviousPage,NextPage};
struct OverviewGrid {
 int columns{},rows{};float width{},height{};
 float left=20.f,top=146.f,gap=12.f,tileWidth=0.f,tileHeight=0.f;
 int capacity() const noexcept{return columns*rows;}
 OverviewCell cell(int slot) const noexcept;
 int hit(float x,float y) const noexcept;
};
OverviewGrid overviewLayout(float width,float height) noexcept;
OverviewGrid fullscreenOverviewLayout(float width,float height,int count) noexcept;
OverviewGrid workspaceOverviewLayout(float width,float height,int count) noexcept;
float overviewEase(float progress) noexcept;
int overviewPageOffset(int selected,int count,const OverviewGrid&) noexcept;
int overviewNavigate(int selected,int count,const OverviewGrid&,OverviewDirection) noexcept;
}
