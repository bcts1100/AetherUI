#pragma once
#include "model.hpp"
#include <cstddef>
namespace aether {
inline constexpr std::size_t SnapLayoutLimit=16,SnapZoneLimit=12,TilingDisplayLimit=32,TilingWindowLimit=64;
enum class MoveDirection {Left,Right,Up,Down};
bool validSnapLayout(const SnapLayout&);
bool validTilingConfig(const Config&);
const SnapLayout* findSnapLayout(const Config&,std::string_view id);
std::optional<Rect> zoneRect(Rect work,Rect normalized,int gapDips,int dpi,int minWidth=1,int minHeight=1);
std::vector<Rect> tileRects(Rect work,std::size_t count,const SnapLayout&,int dpi,int minWidth=1,int minHeight=1);
std::optional<std::size_t> adjacentMonitorIndex(const std::vector<Rect>&,std::size_t source,MoveDirection);
std::optional<Rect> transferRect(Rect sourceWork,Rect targetWork,Rect bounds,int sourceDpi,int targetDpi,int minWidth=1,int minHeight=1);
// One bounded [tiling] section. Absent data retains safe defaults; malformed data fails closed.
bool parseTilingSettings(Config&,std::string_view);
std::string serializeTilingSettings(const Config&);
}
