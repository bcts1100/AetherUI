#pragma once
#include "desktop.hpp"
#include "core/tiling.hpp"
namespace aether::win {
struct TilingStats {std::uint64_t plans=0,moves=0,suppressed=0,released=0,failures=0;};
class TilingService {
 struct Impl;std::unique_ptr<Impl> impl_;TilingStats stats_;
public:
 explicit TilingService(bool ownedFixturesOnly=false);~TilingService();
 TilingService(const TilingService&)=delete;TilingService& operator=(const TilingService&)=delete;
 void allowFixture(HWND,DWORD);void observeUserMove(HWND);void forgetWindow(HWND);void reset();
 void update(const Config&,const std::vector<Monitor>&,const std::vector<Window>&,bool suppressed=false,const std::wstring& retileDisplay={});
 bool snapWindow(const Window&,const Config&,const std::vector<Monitor>&,int zone,bool custom=false);
 bool snapActive(const Config&,const std::vector<Monitor>&,int zone,bool custom=false);
 bool moveWindow(const Window&,const std::vector<Monitor>&,MoveDirection);
 bool moveActive(const std::vector<Monitor>&,MoveDirection);
 const TilingStats& statistics() const{return stats_;}
};
}
