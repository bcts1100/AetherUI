#include "display_policy.hpp"
namespace aether::win {
std::set<std::wstring> fullscreenDisplays(const std::vector<Window>& windows,const std::vector<Monitor>& monitors){
 std::set<std::wstring> result;
 for(const auto& monitor:monitors)for(const auto& window:windows){
  if(window.minimized)continue;const auto& r=window.bounds;const auto& m=monitor.bounds;
  if(r.right<=m.left||r.left>=m.right||r.bottom<=m.top||r.top>=m.bottom)continue;
  if(window.fullscreen&&r.left<=m.left&&r.top<=m.top&&r.right>=m.right&&r.bottom>=m.bottom)result.insert(monitor.name);
  break; // First intersecting eligible window is frontmost on this display.
 }
 return result;
}
bool sameDisplayTopology(const std::vector<Monitor>& a,const std::vector<Monitor>& b){
 if(a.size()!=b.size())return false;
 for(std::size_t i=0;i<a.size();++i)if(a[i].name!=b[i].name||a[i].handle!=b[i].handle||a[i].bounds!=b[i].bounds||a[i].dpi!=b[i].dpi||a[i].primary!=b[i].primary)return false;
 return true;
}
}
