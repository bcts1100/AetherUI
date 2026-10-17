#include "core/wallpaper.hpp"
#include <algorithm>
#include <iostream>
using namespace aether;
namespace {
int checks=0,failures=0;
void check(bool ok,const char* name){++checks;std::cout<<(ok?"PASS ":"FAIL ")<<name<<'\n';if(!ok)++failures;}
// Only this owned in-memory backend is used by Apply/Undo checks.
struct OwnedBackend final:WallpaperBackend {
 WallpaperSnapshot state{{{{L"monitor-left",{-1920,0,0,1080}},L"C:\\old-left.jpg"},{{L"monitor-main",{0,0,2560,1440}},L"C:\\old-main.jpg"}},WallpaperFit::Center,0x123456,false};
 int changes=0,failAt=0,queries=0;bool queryFailsAfterMutation=false,restoreAllowed=true;
 std::optional<WallpaperSnapshot> query() override{++queries;if(queryFailsAfterMutation&&changes)return {};return state;}
 bool restorable(const WallpaperSnapshot&) override{return restoreAllowed;}
 bool permit(){++changes;return !failAt||changes!=failAt;}
 bool position(WallpaperFit value) override{if(!permit())return false;state.fit=value;return true;}
 bool background(std::uint32_t value) override{if(!permit())return false;state.color=value;return true;}
 bool image(const std::wstring& device,const std::wstring& path) override{if(!permit())return false;auto found=std::find_if(state.monitors.begin(),state.monitors.end(),[&](const auto& m){return m.display.device==device;});if(found==state.monitors.end())return false;found->path=path;return true;}
};
WallpaperRequest applyFor(const OwnedBackend& backend){WallpaperRequest request;request.kind=WallpaperKind::Apply;for(const auto& m:backend.state.monitors)request.displays.push_back(m.display);request.image=L"C:\\chosen.png";request.fit=WallpaperFit::Fill;request.selectedDevice=L"monitor-left";return request;}
void passiveQuery(){OwnedBackend b;auto result=executeWallpaper(b,{});check(result.status==WallpaperStatus::Ok&&result.after&&result.after->monitors.size()==2&&b.changes==0,"passive Query returns current paths without any mutation");}
void monitorIdentity(){
 OwnedBackend b;auto mapped=mapWallpaperDisplays(b.state,{{0,0,2560,1440},{-1920,0,0,1080}});
 check(mapped&&mapped->size()==2&&(*mapped)[0].device==L"monitor-main"&&(*mapped)[1].device==L"monitor-left","physical bounds map negative monitors independently of enumeration order");
 check(!mapWallpaperDisplays(b.state,{{0,0,1280,720},{-1920,0,0,1080}}),"DPI-scaled or stale physical bounds are rejected");
 auto duplicate=b.state;duplicate.monitors[1].display.bounds=duplicate.monitors[0].display.bounds;check(!mapWallpaperDisplays(duplicate,{{-1920,0,0,1080},{-1920,0,0,1080}}),"ambiguous mirrored monitor bounds reject targeting");
 auto request=applyFor(b);request.displays[0].bounds.right=1;auto result=executeWallpaper(b,request);check(result.status==WallpaperStatus::Stale&&b.changes==0,"stale monitor token prevents every wallpaper write");
}
void selectedApplyUndo(){
 OwnedBackend b;const auto old=b.state;auto result=executeWallpaper(b,applyFor(b));
 check(result.status==WallpaperStatus::Ok&&result.before&&result.after&&b.state.monitors[0].path==L"C:\\chosen.png"&&b.state.monitors[1].path==L"C:\\old-main.jpg"&&b.state.fit==WallpaperFit::Fill,"explicit selected Apply changes only target image and labels global position");
 WallpaperRequest undo;undo.kind=WallpaperKind::Undo;undo.displays=applyFor(b).displays;undo.before=result.before;undo.expected=result.after;auto restored=executeWallpaper(b,undo);
 check(restored.status==WallpaperStatus::Ok&&sameWallpaperState(old,b.state),"Undo restores prior per-monitor paths global position and background color");
}
void allApply(){OwnedBackend b;auto request=applyFor(b);request.selectedDevice.clear();auto result=executeWallpaper(b,request);check(result.status==WallpaperStatus::Ok&&b.state.monitors[0].path==L"C:\\chosen.png"&&b.state.monitors[1].path==L"C:\\chosen.png","explicit All Apply targets all known physical monitors");}
void staleUndo(){
 OwnedBackend b;auto result=executeWallpaper(b,applyFor(b));WallpaperRequest undo;undo.kind=WallpaperKind::Undo;undo.displays=applyFor(b).displays;undo.before=result.before;undo.expected=result.after;b.state.monitors[1].path=L"C:\\external-change.jpg";const auto writes=b.changes;auto rejected=executeWallpaper(b,undo);
 check(rejected.status==WallpaperStatus::Stale&&b.changes==writes&&b.state.monitors[1].path==L"C:\\external-change.jpg","Undo rejects external wallpaper change without overwriting user state");
}
void partialOutcome(){
 OwnedBackend b;auto request=applyFor(b);request.selectedDevice.clear();b.failAt=3;auto result=executeWallpaper(b,request);
 check(result.status==WallpaperStatus::Partial&&result.before&&result.after&&b.state.monitors[0].path==L"C:\\chosen.png"&&b.state.monitors[1].path==L"C:\\old-main.jpg","partial Apply reports observed state and never silently rolls back");
 check(b.changes==3,"failed write is attempted once and not retried");
 OwnedBackend uncertain;uncertain.queryFailsAfterMutation=true;result=executeWallpaper(uncertain,applyFor(uncertain));check(result.status==WallpaperStatus::Uncertain&&!result.after&&uncertain.changes==2,"unobservable post-write state reports uncertainty with no retry");
}
void restoreGuard(){OwnedBackend b;b.restoreAllowed=false;auto result=executeWallpaper(b,applyFor(b));check(result.status==WallpaperStatus::Invalid&&b.changes==0,"unrestorable prior snapshot rejects Apply before every wallpaper write");}
void limits(){
 OwnedBackend b;auto request=applyFor(b);request.image=L"relative.png";check(executeWallpaper(b,request).status==WallpaperStatus::Invalid&&b.changes==0,"relative image paths are rejected before any write");
 request=applyFor(b);request.image=std::wstring(4097,L'x');check(!validWallpaperRequest(request),"image path length is bounded");
 request=applyFor(b);request.fit=static_cast<WallpaperFit>(88);check(!validWallpaperRequest(request),"unknown fit values fail closed");
 request=applyFor(b);request.selectedDevice=L"not-connected";check(executeWallpaper(b,request).status==WallpaperStatus::Invalid&&b.changes==0,"unknown selected monitor cannot change global fit");
 b.state.slideshow=true;check(executeWallpaper(b,applyFor(b)).status==WallpaperStatus::Invalid&&b.changes==0,"active slideshow is preserved when reversible snapshot is unsupported");
 b.state.slideshow=false;b.state.monitors[0].path.clear();check(executeWallpaper(b,applyFor(b)).status==WallpaperStatus::Invalid&&b.changes==0,"solid-color state is preserved rather than accepting an unsafe Undo snapshot");
}
}
int main(){passiveQuery();monitorIdentity();selectedApplyUndo();allApply();staleUndo();partialOutcome();restoreGuard();limits();std::cout<<checks-failures<<'/'<<checks<<" wallpaper checks passed\n";return failures?1:0;}
