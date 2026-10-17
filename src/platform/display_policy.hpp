#pragma once
#include "desktop.hpp"
#include <set>
namespace aether::win {
std::set<std::wstring> fullscreenDisplays(const std::vector<Window>& windows,const std::vector<Monitor>& monitors);
bool sameDisplayTopology(const std::vector<Monitor>& a,const std::vector<Monitor>& b);
}
