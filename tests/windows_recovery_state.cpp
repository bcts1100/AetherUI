#include "platform/recovery_engine.hpp"
#include "platform/safety.hpp"
#include <iostream>
using namespace aether::win;
namespace {
int checks=0,failures=0;
void check(bool good,const char* name){++checks;std::cout<<(good?"PASS ":"FAIL ")<<name<<"\n";if(!good)++failures;}
WorkAreaSnapshot originalAreas();
// These taskbar-only fixtures never change physical geometry. Workspace failure
// and mutation behavior is exercised by the dedicated WorkAreaFixture below.
struct UnchangedWorkspaceBackend:RecoveryBackend {
 bool captureWorkAreaOriginals(WorkAreaSnapshot& out)noexcept override{out=originalAreas();return true;}
 WorkAreaResult restoreOriginalWorkAreas(const WorkAreaSnapshot&)noexcept override{return WorkAreaResult::Complete;}
 WorkAreaResult originalWorkAreasRestored(const WorkAreaSnapshot&)noexcept override{return WorkAreaResult::Complete;}
};
struct FixtureBackend final:UnchangedWorkspaceBackend {
 bool state=false,visible=false,valid=true,refuse=false;int requests=0,states=0,waits=0;
 bool validEntry(const RecoveryEntry&)noexcept override{return valid;}
 void requestVisibility(const RecoveryEntry&)noexcept override{++requests;if(!refuse&&requests>=2)visible=true;}
 bool visibilityRestored(const RecoveryEntry&)noexcept override{return visible;}
 void requestState(DWORD)noexcept override{++states;if(!refuse)state=true;}
 bool stateRestored(DWORD)noexcept override{return state;}
 void delay(DWORD)noexcept override{++waits;}
};
RecoverySnapshot armed(){RecoverySnapshot s{};s.armed=1;s.count=1;s.entries[0].hwnd=42;s.entries[0].pid=7;s.workAreas=originalAreas();s.workAreasCaptured=1;return s;}
WorkAreaSnapshot originalAreas(){
 WorkAreaSnapshot result;result.count=2;
 std::copy_n(L"FixtureLeft",12,result.entries[0].device);result.entries[0].bounds={-1920,0,0,1080};result.entries[0].work={-1920,0,0,1040};
 std::copy_n(L"FixturePrimary",15,result.entries[1].device);result.entries[1].bounds={0,0,1920,1080};result.entries[1].work={0,0,1920,1040};return result;
}
struct WorkAreaFixture final:WorkAreaBackend {
 WorkAreaSnapshot current=originalAreas();unsigned captures=0,requests=0;int failIndex=-1;bool available=true,defer=false,changeTopologyAfterFirst=false;
 bool captureCurrent(WorkAreaSnapshot& out)noexcept override{++captures;if(!available)return false;out=current;return true;}
 bool requestCaptured(const WorkAreaEntry& entry)noexcept override{
  ++requests;for(DWORD i=0;i<current.count;++i)if(wcscmp(entry.device,current.entries[i].device)==0){
   if(static_cast<int>(i)==failIndex||!EqualRect(&entry.bounds,&current.entries[i].bounds))return false;
   if(!defer)current.entries[i].work=entry.work;
   if(changeTopologyAfterFirst&&requests==1)current.entries[1].bounds.right+=100;return true;
  }return false;
 }
 void removeReservations()noexcept{for(DWORD i=0;i<current.count;++i)current.entries[i].work=current.entries[i].bounds;}
};
struct WorkspaceProtocolBackend final:RecoveryBackend {
 WorkAreaFixture workspace;DWORD state=0;bool visible=true,publishedBeforeHide=true;int hides=0,visibilityRequests=0;ULONGLONG birth=1;
 bool delayedWorkspaceReset=false,persistentWorkspaceReset=false,resetDuringPostQuietStateQuery=false,workspaceResetDone=false;unsigned quietWaits=0;
 DWORD quietMilliseconds=100,lateWorkspaceResetAfter=0,elapsedSinceVisibility=0;
 bool validEntry(const RecoveryEntry& entry)noexcept override{return entry.hwnd==42&&entry.pid==7&&entry.processBirth==birth;}
 void requestVisibility(const RecoveryEntry& entry)noexcept override{++visibilityRequests;visible=entry.visible!=0;}
 bool visibilityRestored(const RecoveryEntry& entry)noexcept override{return visible==(entry.visible!=0);}
 void requestState(DWORD original)noexcept override{state=original;}
 bool stateRestored(DWORD original)noexcept override{
  if(resetDuringPostQuietStateQuery&&quietWaits&&!workspaceResetDone){workspace.removeReservations();workspaceResetDone=true;}
  return state==original;
 }
 DWORD restorationQuietMilliseconds()const noexcept override{return quietMilliseconds;}
 void delay(DWORD milliseconds)noexcept override{
  elapsedSinceVisibility+=milliseconds;
  if(lateWorkspaceResetAfter&&elapsedSinceVisibility>=lateWorkspaceResetAfter&&!workspaceResetDone){workspace.removeReservations();workspaceResetDone=true;}
  if(milliseconds<100)return;++quietWaits;
  if(delayedWorkspaceReset&&(!workspaceResetDone||persistentWorkspaceReset)){workspace.removeReservations();workspaceResetDone=true;}
 }
 bool captureWorkAreaOriginals(WorkAreaSnapshot& out)noexcept override{return captureWorkAreas(out,workspace);}
 WorkAreaResult restoreOriginalWorkAreas(const WorkAreaSnapshot& original)noexcept override{return restoreWorkAreas(original,workspace);}
 WorkAreaResult originalWorkAreasRestored(const WorkAreaSnapshot& original)noexcept override{
  WorkAreaSnapshot current;return captureWorkAreas(current,workspace)?compareWorkAreas(original,current):WorkAreaResult::Unavailable;
 }
 bool captureOriginals(RecoverySnapshot& s)noexcept override{
  if(!s.armed)s.taskbarState=state;s.entries[0]={42,7,visible?1L:0L,birth};s.count=1;return true;
 }
 bool hideCaptured(RecoverySnapshot& s)noexcept override{
  ++hides;publishedBeforeHide=publishedBeforeHide&&s.workAreasCaptured==1&&compareWorkAreas(s.workAreas,originalAreas())==WorkAreaResult::Complete;
  state=s.taskbarState|ABS_AUTOHIDE;visible=false;workspace.removeReservations();return true;
 }
};
}
int main(){
 {
  auto s=armed();FixtureBackend b;auto report=restoreRecovery(&s,b,4,0);
  check(report.complete&&s.armed==0&&s.restored==1&&b.requests==2,"restoration waits for actual visibility confirmation and retries");
 }
 {
  auto s=armed();FixtureBackend b;b.refuse=true;auto report=restoreRecovery(&s,b,3,0);
  check(!report.complete&&s.armed==1&&s.restored==0&&b.requests==3,"failed restore retains armed ownership instead of losing recovery");
 }
 {
  auto s=armed();FixtureBackend b;b.valid=false;auto report=restoreRecovery(&s,b,2,0);
  check(report.complete&&report.skipped==1&&b.requests==0,"stale entry skipped without mutating a replacement window");
 }
 {
  auto s=armed();s.count=33;FixtureBackend b;auto report=restoreRecovery(&s,b,4,0);
  check(!report.complete&&b.requests==0&&b.states==0&&s.armed==1,"corrupt capacity fails closed without native backend effects");
 }
 {
  auto s=armed();--s.version;FixtureBackend b;auto report=restoreRecovery(&s,b,4,0);
  check(!report.complete&&b.requests==0&&b.states==0,"wrong protocol generation cannot mutate taskbars");
 }
 {
  RecoverySnapshot s{};FixtureBackend b;auto report=restoreRecovery(&s,b,4,0);
  check(report.complete&&b.requests==0&&b.states==0,"unarmed helper never touches backend");
 }
 {
  auto s=armed();s.heartbeat=1000;s.leaseMilliseconds=5000;
  check(!recoveryLeaseExpired(&s,5999)&&recoveryLeaseExpired(&s,6000),"live parent heartbeat has a bounded expiry");
  s.armed=0;check(!recoveryLeaseExpired(&s,9000),"unarmed helper has no heartbeat timer recovery");
 }
 {
  auto s=armed();s.restoring=1;FixtureBackend b;auto report=restoreRecovery(&s,b,4,0);
  check(report.complete&&s.armed==0,"abandoned parent restore marker cannot block helper recovery");
 }
 {
  struct PendingHideBackend final:UnchangedWorkspaceBackend {
   bool visible=true,pendingHide=true,pendingRestore=false;int requests=0;
   bool validEntry(const RecoveryEntry&)noexcept override{return true;}
   void requestVisibility(const RecoveryEntry&)noexcept override{++requests;pendingRestore=true;}
   bool visibilityRestored(const RecoveryEntry&)noexcept override{return visible;}
   void requestState(DWORD)noexcept override{}
   bool stateRestored(DWORD)noexcept override{return true;}
   void delay(DWORD)noexcept override{if(pendingHide){visible=false;pendingHide=false;}if(pendingRestore){visible=true;pendingRestore=false;}}
  } b;
  auto s=armed();auto report=restoreRecovery(&s,b,4,0);b.delay(0);
  check(report.complete&&b.requests>0&&b.visible,"restore overrides a queued asynchronous hide even when original visibility currently matches");
 }
 {
  struct ProtocolBackend final:UnchangedWorkspaceBackend {
   DWORD state=ABS_ALWAYSONTOP;bool visible=true,available=true,captureFail=false,publishedBeforeHide=true;int captures=0,hides=0,restores=0;ULONGLONG birth=1;
   bool validEntry(const RecoveryEntry& entry)noexcept override{return entry.hwnd==42&&entry.pid==7&&entry.processBirth==birth;}
   void requestVisibility(const RecoveryEntry& entry)noexcept override{++restores;visible=entry.visible!=0;}
   bool visibilityRestored(const RecoveryEntry& entry)noexcept override{return visible==(entry.visible!=0);}
   void requestState(DWORD value)noexcept override{if(available)state=value;}
   bool stateRestored(DWORD value)noexcept override{return available&&state==value;}
   void delay(DWORD)noexcept override{}
   bool captureWorkAreaOriginals(WorkAreaSnapshot& out)noexcept override{out=originalAreas();return true;}
   WorkAreaResult restoreOriginalWorkAreas(const WorkAreaSnapshot&)noexcept override{return WorkAreaResult::Complete;}
   WorkAreaResult originalWorkAreasRestored(const WorkAreaSnapshot&)noexcept override{return WorkAreaResult::Complete;}
   bool captureOriginals(RecoverySnapshot& s)noexcept override{
    ++captures;if(captureFail||!available)return false;
    if(s.armed==0)s.taskbarState=state;
    if(s.count==0||s.entries[0].processBirth!=birth){s.entries[0]={42,7,visible?1L:0L,birth};InterlockedExchange(&s.count,1);}
    return true;
   }
   bool hideCaptured(RecoverySnapshot& s)noexcept override{++hides;publishedBeforeHide=publishedBeforeHide&&s.armed==1&&s.count==1&&s.entries[0].processBirth==birth;state=s.taskbarState|ABS_AUTOHIDE;visible=false;return true;}
  } b;
  RecoverySnapshot s{};s.heartbeat=1000;s.leaseMilliseconds=5000;
  check(executeRecoveryCommand(&s,b,RecoveryCommand::Arm,1000)&&s.armed==1&&!b.visible&&b.publishedBeforeHide&&s.taskbarState==ABS_ALWAYSONTOP,"helper Arm publishes complete originals before its first native mutation");
  check(executeRecoveryCommand(&s,b,RecoveryCommand::Refresh,2000)&&b.hides==2&&s.taskbarState==ABS_ALWAYSONTOP,"helper Refresh preserves original appbar bits instead of recapturing its own auto-hide mutation");
  check(!executeRecoveryCommand(&s,b,RecoveryCommand::Refresh,6000)&&s.armed==0&&s.revoked==1&&b.visible&&b.state==ABS_ALWAYSONTOP,"expired lease restores originals before rejecting an already pending refresh");
  const auto hides=b.hides;s.heartbeat=9000;
  check(!executeRecoveryCommand(&s,b,RecoveryCommand::Arm,9000)&&!executeRecoveryCommand(&s,b,RecoveryCommand::Refresh,9000)&&b.hides==hides,"late heartbeat and resumed controller cannot rearm the revoked helper lifetime");
  s=RecoverySnapshot{};s.heartbeat=1000;b=ProtocolBackend{};
  check(executeRecoveryCommand(&s,b,RecoveryCommand::Arm,1000),"new helper lifetime can explicitly acquire fresh originals");
  b.captureFail=true;
  check(!executeRecoveryCommand(&s,b,RecoveryCommand::Refresh,2000)&&s.armed==0&&s.revoked==1&&b.visible&&b.state==ABS_ALWAYSONTOP,"discovery or capacity failure restores the entire captured batch and revokes hiding");
  s=RecoverySnapshot{};s.heartbeat=1000;b=ProtocolBackend{};executeRecoveryCommand(&s,b,RecoveryCommand::Arm,1000);b.available=false;
  check(!executeRecoveryCommand(&s,b,RecoveryCommand::Restore,2000)&&s.armed==1&&s.revoked==1,"missing primary cannot confirm restoration or discard original global appbar state");
  b.available=true;
  check(executeRecoveryCommand(&s,b,RecoveryCommand::Restore,3000)&&s.armed==0&&b.state==ABS_ALWAYSONTOP,"returning primary receives pre-Aether global state from retained restoration ownership");
  s=RecoverySnapshot{};s.heartbeat=1000;b=ProtocolBackend{};executeRecoveryCommand(&s,b,RecoveryCommand::Arm,1000);b.birth=2;b.visible=true;
  check(executeRecoveryCommand(&s,b,RecoveryCommand::Refresh,2000)&&s.entries[0].processBirth==2&&s.entries[0].visible==1&&s.taskbarState==ABS_ALWAYSONTOP,"recreated taskbar captures new visible identity while preserving the original global bitmask");
  check(executeRecoveryCommand(&s,b,RecoveryCommand::Restore,3000)&&b.visible&&b.state==ABS_ALWAYSONTOP,"recreated taskbar restores its captured visibility and pre-Aether global state");
 }
 {
  RecoverySnapshot s{};s.nativeAllowed=2;FixtureBackend b;
  check(!recoverySnapshotValid(&s)&&!executeRecoveryCommand(&s,b,RecoveryCommand::Arm,1000)&&b.requests==0,"invalid native permission cannot enter the recovery protocol");
 }
 {
  auto s=armed();s.entries[0].visible=1;s.entries[0].processBirth=2;s.phase=static_cast<LONG>(RecoveryPhase::Armed);
  observeRecoveryWindowEvent(&s,EVENT_OBJECT_DESTROY,42);
  check(s.revoked==1&&s.armed==1&&s.count==1&&s.entries[0].hwnd==42&&s.entries[0].visible==1&&s.entries[0].processBirth==2,"delayed old Destroy after replacement capture retains the visible original and revokes the ambiguous lifetime");
 }
 {
  auto s=armed();s.entries[0].visible=1;s.entries[0].processBirth=2;s.phase=static_cast<LONG>(RecoveryPhase::Armed);
  observeRecoveryWindowEvent(&s,EVENT_OBJECT_CREATE,42);
  check(s.revoked==1&&s.entries[0].hwnd==42&&s.entries[0].visible==1,"matching Create while armed cannot recapture an already hidden HWND as its original");
 }
 {
  struct AsyncHideBackend final:RecoveryHideBackend {
   bool visible[2]{true,true},pending[2]{false,false};int requests[2]{},waits=0;DWORD state=0;
   bool secondaryReappears=false,persistentReappearance=false;RecoverySnapshot* revokeAfterWait=nullptr;
   bool prepareHiddenState(const RecoverySnapshot&,DWORD requested)noexcept override{state=requested;return true;}
   bool requestHidden(const RecoveryEntry& entry)noexcept override{
    const unsigned i=entry.hwnd==42?0:1;if(visible[i]){++requests[i];pending[i]=true;}return true;
   }
   RecoveryHideObservation observeHidden(DWORD requested)noexcept override{
    return !visible[0]&&!visible[1]&&state==requested?RecoveryHideObservation::Confirmed:RecoveryHideObservation::Pending;
   }
   void delay(DWORD)noexcept override{
    for(unsigned i=0;i<2;++i)if(pending[i]){visible[i]=false;pending[i]=false;}
    if(secondaryReappears&&(waits==0||persistentReappearance))visible[1]=true;
    ++waits;if(revokeAfterWait)InterlockedExchange(&revokeAfterWait->revoked,1);
   }
  };
  auto s=armed();s.count=2;s.entries[1]={43,7,1,1};s.taskbarState=ABS_ALWAYSONTOP;AsyncHideBackend b;
  check(hideRecovery(&s,b,4,0)==RecoveryHideResult::Complete&&b.requests[0]==1&&b.requests[1]==1&&b.state==(ABS_ALWAYSONTOP|ABS_AUTOHIDE),"asynchronous hide waits for both monitors and the complete requested appbar mask");
  s=armed();s.count=2;s.entries[1]={43,7,1,1};b=AsyncHideBackend{};b.secondaryReappears=true;
  check(hideRecovery(&s,b,4,0)==RecoveryHideResult::Complete&&b.requests[0]==1&&b.requests[1]==2&&!b.visible[1],"delayed secondary Shell re-show receives a new hide before bounded confirmation completes");
  s=armed();s.count=2;s.entries[1]={43,7,1,1};b=AsyncHideBackend{};b.secondaryReappears=true;b.persistentReappearance=true;
  check(hideRecovery(&s,b,4,0)==RecoveryHideResult::Deadline&&b.waits==3&&s.armed==1&&s.count==2,"persistent Shell re-show fails the bounded hide deadline without weakening visibility or losing originals");
  s=armed();s.count=2;s.entries[1]={43,7,1,1};b=AsyncHideBackend{};b.secondaryReappears=true;b.revokeAfterWait=&s;
  check(hideRecovery(&s,b,4,0)==RecoveryHideResult::Revoked&&b.requests[0]==1&&b.requests[1]==1&&b.waits==1,"concurrent revocation prevents any deferred hide retry after restoration ownership is requested");
 }
 {
  struct TransientObservationBackend final:RecoveryHideBackend {
   unsigned observations=0,waits=0,writes=0;bool permanent=false,allowed=true,stopAfterWait=false,ownerDies=false,stopDuringProof=false;
   RecoverySnapshot* snapshot=nullptr;
   bool prepareHiddenState(const RecoverySnapshot&,DWORD)noexcept override{return true;}
   bool requestHidden(const RecoveryEntry&)noexcept override{++writes;return true;}
   RecoveryHideObservation observeHidden(DWORD)noexcept override{
    ++observations;if(stopDuringProof)allowed=false;return permanent||observations<3?RecoveryHideObservation::Unavailable:RecoveryHideObservation::Confirmed;
   }
   void delay(DWORD)noexcept override{++waits;if(stopAfterWait)InterlockedExchange(&snapshot->revoked,1);if(ownerDies)allowed=false;}
   bool hideAllowed(const RecoverySnapshot&)const noexcept override{return allowed;}
  };
  auto s=armed();TransientObservationBackend b;b.snapshot=&s;const auto original=s.entries[0];
  check(hideRecovery(&s,b,4,0)==RecoveryHideResult::Complete&&b.observations==3&&b.waits==2&&s.entries[0].hwnd==original.hwnd&&s.entries[0].visible==original.visible,"two incomplete Shell observations are retried within the same generation and only a fresh complete proof succeeds");
  s=armed();b=TransientObservationBackend{};b.snapshot=&s;b.permanent=true;
  check(hideRecovery(&s,b,4,0)==RecoveryHideResult::ObservationUnavailable&&b.observations==4&&b.waits==3,"persistent unavailable observations exhaust the existing budget without acknowledging hidden success");
  s=armed();b=TransientObservationBackend{};b.snapshot=&s;b.stopAfterWait=true;
  check(hideRecovery(&s,b,4,0)==RecoveryHideResult::Revoked&&b.observations==1&&b.writes==1&&b.waits==1,"revocation during an unavailable observation prevents every subsequent hide write");
  s=armed();b=TransientObservationBackend{};b.snapshot=&s;b.ownerDies=true;
  check(hideRecovery(&s,b,4,0)==RecoveryHideResult::Revoked&&b.writes==1&&s.revoked==1,"parent death, Stop or lease rejection during retry revokes before another visibility write");
  s=armed();b=TransientObservationBackend{};b.snapshot=&s;b.allowed=false;
  check(hideRecovery(&s,b,4,0)==RecoveryHideResult::Revoked&&b.writes==0,"a stopped or expired owner cannot begin a retried hide");
  s=armed();b=TransientObservationBackend{};b.snapshot=&s;b.observations=2;b.stopDuringProof=true;
  check(hideRecovery(&s,b,4,0)==RecoveryHideResult::Revoked&&s.revoked==1,"a complete native proof cannot override lifetime rejection delivered by the synchronous query");
 }
 {
  struct PerBarStateBackend final:UnchangedWorkspaceBackend {
   bool secondaryModified=true;int stateRequests=0;DWORD requestedMask=0;
   bool validEntry(const RecoveryEntry&)noexcept override{return true;}
   void requestVisibility(const RecoveryEntry&)noexcept override{}
   bool visibilityRestored(const RecoveryEntry&)noexcept override{return true;}
   void requestState(DWORD state)noexcept override{++stateRequests;requestedMask=state;secondaryModified=false;}
   // ABM_GETSTATE reports the global setting and ignores the target HWND.
   bool stateRestored(DWORD)noexcept override{return true;}
   void delay(DWORD)noexcept override{}
  } b;
  auto s=armed();s.taskbarState=ABS_ALWAYSONTOP;
  const auto report=restoreRecovery(&s,b,4,0);
  check(report.complete&&!b.secondaryModified&&b.stateRequests==1&&b.requestedMask==ABS_ALWAYSONTOP,"matching global appbar mask still restores the per-bar state changed on the secondary monitor");
 }
 {
  struct ShellStateEchoBackend final:RecoveryHideBackend {
   bool initialCapture=true;std::uint32_t newIdentities=3;DWORD state=0;
   bool visible[2]{true,true},pendingShow[2]{};unsigned stateWrites[2]{};
   bool prepareHiddenState(const RecoverySnapshot& s,DWORD requested)noexcept override{
    const bool matches=state==requested;
    for(LONG i=0;i<s.count;++i)if(recoveryHiddenStateRequired(initialCapture,matches,(newIdentities&(std::uint32_t{1}<<static_cast<unsigned>(i)))!=0)){
     state=requested;++stateWrites[i];pendingShow[i]=true;
    }return true;
   }
   bool requestHidden(const RecoveryEntry& entry)noexcept override{visible[entry.hwnd==42?0:1]=false;return true;}
   RecoveryHideObservation observeHidden(DWORD requested)noexcept override{
    return state==requested&&!visible[0]&&!visible[1]?RecoveryHideObservation::Confirmed:RecoveryHideObservation::Pending;
   }
   void delay(DWORD)noexcept override{}
   void drainShellNotifications()noexcept{for(unsigned i=0;i<2;++i)if(pendingShow[i]){visible[i]=true;pendingShow[i]=false;}}
  };
  auto s=armed();s.count=2;s.entries[1]={43,7,1,1};s.taskbarState=ABS_ALWAYSONTOP;ShellStateEchoBackend b;
  check(hideRecovery(&s,b,4,0)==RecoveryHideResult::Complete&&b.stateWrites[0]==1&&b.stateWrites[1]==1&&b.state==(ABS_ALWAYSONTOP|ABS_AUTOHIDE),"first acquisition applies the complete hidden-state mask to every captured monitor");
  b.drainShellNotifications();b.initialCapture=false;b.newIdentities=0;
  const auto refreshed=hideRecovery(&s,b,4,0);b.drainShellNotifications();
  check(refreshed==RecoveryHideResult::Complete&&!b.visible[0]&&!b.visible[1]&&b.stateWrites[0]==1&&b.stateWrites[1]==1,"SHOW reconciliation rehides known bars without another appbar state write scheduling a later SHOW");
  b=ShellStateEchoBackend{};b.initialCapture=false;b.newIdentities=0;b.state=s.taskbarState|ABS_AUTOHIDE;b.visible[0]=b.visible[1]=false;
  const auto duplicate=hideRecovery(&s,b,4,0);b.drainShellNotifications();
  check(duplicate==RecoveryHideResult::Complete&&!b.visible[0]&&!b.visible[1]&&b.stateWrites[0]==0&&b.stateWrites[1]==0,"duplicate hidden Arm or Refresh does not disturb the acknowledged helper lifetime");
  b=ShellStateEchoBackend{};b.initialCapture=false;b.newIdentities=2;b.state=s.taskbarState|ABS_AUTOHIDE;b.visible[0]=false;
  const auto newMonitor=hideRecovery(&s,b,4,0);b.drainShellNotifications();
  check(newMonitor==RecoveryHideResult::Complete&&b.stateWrites[0]==0&&b.stateWrites[1]==1&&!b.visible[0]&&s.taskbarState==ABS_ALWAYSONTOP&&s.entries[1].visible==1,"new secondary identity gets its state transition while an existing hidden monitor and originals remain undisturbed");
  b=ShellStateEchoBackend{};b.initialCapture=false;b.newIdentities=0;b.state=ABS_ALWAYSONTOP;
  check(hideRecovery(&s,b,4,0)==RecoveryHideResult::Complete&&b.stateWrites[0]==1&&b.stateWrites[1]==1&&b.state==(ABS_ALWAYSONTOP|ABS_AUTOHIDE),"external global appbar-mask drift reapplies the captured complete hidden-state mask");
 }
 {
  struct ShellQueryBackend final:RecoveryHiddenObservationBackend {
   bool visible=false,showDuringQuery=true,stateMatches=true,primaryPresent=true,removePrimaryDuringQuery=false;
   RecoveryHideObservation observeNativeVisibility()noexcept override{if(!primaryPresent)return RecoveryHideObservation::Unavailable;return visible?RecoveryHideObservation::Pending:RecoveryHideObservation::Confirmed;}
   RecoveryHideObservation observeNativeState(DWORD)noexcept override{if(showDuringQuery)visible=true;if(removePrimaryDuringQuery)primaryPresent=false;return stateMatches?RecoveryHideObservation::Confirmed:RecoveryHideObservation::Pending;}
  } b;
  const auto changedDuringQuery=observeRecoveryHidden(b,ABS_AUTOHIDE);const bool actuallyShown=b.visible;
  b=ShellQueryBackend{};b.showDuringQuery=false;b.stateMatches=false;
  check(changedDuringQuery==RecoveryHideObservation::Pending&&actuallyShown&&observeRecoveryHidden(b,ABS_AUTOHIDE)==RecoveryHideObservation::Pending,"visibility is sampled after the synchronous Shell-state query and the complete mask is still required");
  b=ShellQueryBackend{};b.showDuringQuery=false;b.removePrimaryDuringQuery=true;
  check(observeRecoveryHidden(b,ABS_AUTOHIDE)==RecoveryHideObservation::Unavailable&&!b.primaryPresent,"primary disappearance during the synchronous Shell query cannot confirm an empty native scan");
 }
 {
  struct OmittedTopStateBackend final:UnchangedWorkspaceBackend,RecoveryHideBackend {
   DWORD setterState=ABS_ALWAYSONTOP;bool visible=true;unsigned stateRequests=0;
   // Windows 7+ omits ALWAYSONTOP from GETSTATE even while that semantic state
   // remains on. This models reservation eligibility, not actual Shell rcWork.
   DWORD rawState()const noexcept{return setterState&~static_cast<DWORD>(ABS_ALWAYSONTOP);}
   bool reservationEligible()const noexcept{return (setterState&ABS_ALWAYSONTOP)!=0&&(setterState&ABS_AUTOHIDE)==0;}
   bool validEntry(const RecoveryEntry&)noexcept override{return true;}
   void requestVisibility(const RecoveryEntry& entry)noexcept override{visible=entry.visible!=0;}
   bool visibilityRestored(const RecoveryEntry& entry)noexcept override{return visible==(entry.visible!=0);}
   void requestState(DWORD raw)noexcept override{++stateRequests;setterState=nativeTaskbarSetterMask(raw);}
   bool stateRestored(DWORD raw)noexcept override{return rawState()==raw;}
   bool prepareHiddenState(const RecoverySnapshot&,DWORD raw)noexcept override{requestState(raw);return true;}
   bool requestHidden(const RecoveryEntry&)noexcept override{visible=false;return true;}
   RecoveryHideObservation observeHidden(DWORD raw)noexcept override{return !visible&&rawState()==raw?RecoveryHideObservation::Confirmed:RecoveryHideObservation::Pending;}
   void delay(DWORD)noexcept override{}
  } b;
  auto s=armed();s.taskbarState=0;s.entries[0].visible=1;
  const auto report=restoreRecovery(&s,b,4,0);
  check(report.complete&&b.visible&&b.stateRequests==1&&b.rawState()==0&&s.taskbarState==0&&b.setterState==ABS_ALWAYSONTOP&&b.reservationEligible(),"omitted always-on-top getter bit restores with setter2 while preserving original raw0 and reservation eligibility");
  s=armed();s.taskbarState=0;s.entries[0].visible=1;b=OmittedTopStateBackend{};
  check(hideRecovery(&s,b,4,0)==RecoveryHideResult::Complete&&!b.visible&&b.rawState()==ABS_AUTOHIDE&&s.taskbarState==0&&b.setterState==(ABS_AUTOHIDE|ABS_ALWAYSONTOP),"raw hidden mask1 uses setter3 so auto-hide preserves Windows always-on-top semantics");
 }
 {
  RecoverySnapshot s{};s.heartbeat=1000;WorkspaceProtocolBackend b;
  const bool acquired=executeRecoveryCommand(&s,b,RecoveryCommand::Arm,1000);
  check(acquired&&b.publishedBeforeHide&&s.workAreasCaptured==1&&s.workAreas.count==2&&s.taskbarState==0&&b.workspace.captures==1,"V4 publishes exact bounded workspace originals before the first state or visibility mutation");
  const auto original=s.workAreas;
  check(executeRecoveryCommand(&s,b,RecoveryCommand::Refresh,2000)&&b.workspace.captures==1&&compareWorkAreas(s.workAreas,original)==WorkAreaResult::Complete&&s.workAreas.entries[0].work.left==-1920,"Refresh never recaptures the owner's changed workspace or loses negative virtual-screen coordinates");
  s=RecoverySnapshot{};s.heartbeat=1000;b=WorkspaceProtocolBackend{};b.workspace.available=false;
  check(!executeRecoveryCommand(&s,b,RecoveryCommand::Arm,1000)&&b.hides==0&&s.armed==0&&s.workAreasCaptured==0,"unavailable original workspace refuses native Arm before any mutation");
 }
 {
  auto s=armed();s.entries[0]={42,7,1,1};s.workAreasCaptured=1;s.workAreas=originalAreas();const auto original=s.workAreas;
  WorkspaceProtocolBackend b;b.visible=false;b.workspace.removeReservations();b.workspace.defer=true;
  const auto report=restoreRecovery(&s,b,3,0);
  check(!report.complete&&s.armed==1&&s.restored==0&&s.revoked==1&&b.visible&&compareWorkAreas(s.workAreas,original)==WorkAreaResult::Complete,"matching state and visibility cannot retire recovery ownership while workspace requests remain pending");
 }
 {
  auto s=armed();s.entries[0]={42,7,1,1};s.workAreasCaptured=1;s.workAreas=originalAreas();const auto original=s.workAreas;
  WorkspaceProtocolBackend b;b.visible=false;b.workspace.removeReservations();b.workspace.failIndex=1;
  const auto failed=restoreRecovery(&s,b,3,0);
  check(!failed.complete&&s.armed==1&&s.restored==0&&b.workspace.requests>0&&EqualRect(&b.workspace.current.entries[0].work,&original.entries[0].work)&&!EqualRect(&b.workspace.current.entries[1].work,&original.entries[1].work)&&compareWorkAreas(s.workAreas,original)==WorkAreaResult::Complete,"partial workspace failure retains ownership and immutable originals after the first monitor succeeds");
  b.workspace.failIndex=-1;
  check(restoreRecovery(&s,b,3,0).complete&&s.armed==0&&s.restored==1&&compareWorkAreas(b.workspace.current,original)==WorkAreaResult::Complete&&s.taskbarState==0,"a later exact workspace restoration completes the retained generation without changing original raw state");
 }
 {
  auto s=armed();s.entries[0]={42,7,1,1};s.workAreasCaptured=1;s.workAreas=originalAreas();const auto original=s.workAreas;
  WorkspaceProtocolBackend b;b.workspace.removeReservations();b.workspace.current.entries[1].bounds.right+=100;
  const auto report=restoreRecovery(&s,b,3,0);
  check(!report.complete&&s.armed==1&&b.workspace.requests==0&&compareWorkAreas(s.workAreas,original)==WorkAreaResult::Complete,"changed physical topology refuses every workspace write and retains the original generation");
 }
 {
  auto s=armed();s.entries[0]={42,7,1,1};s.workAreasCaptured=1;s.workAreas=originalAreas();
  WorkspaceProtocolBackend b;b.birth=2;b.workspace.removeReservations();b.workspace.defer=true;
  const auto report=restoreRecovery(&s,b,3,0);
  check(!report.complete&&s.armed==1&&report.skipped==1&&b.visibilityRequests==0,"workspace recovery preserves stale HWND PID-birth exclusion while pending geometry retains ownership");
 }
 {
  auto s=armed();s.entries[0]={42,7,1,1};s.workAreasCaptured=1;s.workAreas=originalAreas();s.workAreas.count=WorkAreaCapacity+1;
  WorkspaceProtocolBackend b;
  check(!restoreRecovery(&s,b,3,0).complete&&s.armed==1&&b.visibilityRequests==0&&b.workspace.requests==0,"corrupt published workspace capacity refuses native recovery effects");
 }
 {
  const auto original=originalAreas();WorkAreaFixture b;b.removeReservations();
  check(restoreWorkAreas(original,b)==WorkAreaResult::Complete&&b.requests==2&&compareWorkAreas(original,b.current)==WorkAreaResult::Complete&&original.entries[0].work.left==-1920,"shared workspace restoration requests only the exact captured rectangles on both physical monitors");
  b=WorkAreaFixture{};b.removeReservations();b.changeTopologyAfterFirst=true;
  check(restoreWorkAreas(original,b)==WorkAreaResult::TopologyChanged&&b.requests==1,"topology is rechecked before each captured-rectangle write so a mid-batch change stops the second monitor");
  b=WorkAreaFixture{};
  check(restoreWorkAreas(original,b)==WorkAreaResult::Complete&&b.requests==0,"already exact workspace restoration performs no native writes");
  auto output=original;b.available=false;
  check(!captureWorkAreas(output,b)&&compareWorkAreas(original,output)==WorkAreaResult::Complete,"failed physical capture leaves an existing immutable original snapshot untouched");
  b=WorkAreaFixture{};b.current.entries[1].device[0]=L'X';
  check(restoreWorkAreas(original,b)==WorkAreaResult::TopologyChanged&&b.requests==0,"changed physical device identity cannot be substituted by monitor position");
 }
 {
  auto s=armed();s.entries[0]={42,7,1,1};const auto original=s.workAreas;
  WorkspaceProtocolBackend b;b.visible=false;b.workspace.removeReservations();b.delayedWorkspaceReset=true;
  const auto report=restoreRecovery(&s,b,4,50);
  // Deliver any queued visibility-driven Shell reflow even if the old engine
  // already discarded ownership on its first exact geometry observation.
  b.delay(100);
  check(report.complete&&s.armed==0&&compareWorkAreas(original,b.workspace.current)==WorkAreaResult::Complete&&b.workspaceResetDone&&b.visibilityRequests==1&&compareWorkAreas(original,s.workAreas)==WorkAreaResult::Complete,"delayed visibility-driven workspace reset is repaired before owner retirement without rescheduling a matching visibility request");
 }
 {
  auto s=armed();s.entries[0]={42,7,1,1};const auto original=s.workAreas;
  WorkspaceProtocolBackend b;b.visible=false;b.workspace.removeReservations();b.delayedWorkspaceReset=true;b.persistentWorkspaceReset=true;
  const auto report=restoreRecovery(&s,b,3,50);
  check(!report.complete&&s.armed==1&&s.restored==0&&s.revoked==1&&b.quietWaits==3&&b.visibilityRequests==1&&compareWorkAreas(original,s.workAreas)==WorkAreaResult::Complete,"persistent post-request workspace reflow exhausts the bounded quiet confirmation while retaining immutable recovery ownership");
 }
 {
  auto s=armed();s.entries[0]={42,7,1,1};const auto original=s.workAreas;
  WorkspaceProtocolBackend b;b.visible=false;b.workspace.removeReservations();b.resetDuringPostQuietStateQuery=true;
  const auto report=restoreRecovery(&s,b,4,50);
  b.delay(100);b.stateRestored(s.taskbarState);
  check(report.complete&&s.armed==0&&b.workspaceResetDone&&compareWorkAreas(original,b.workspace.current)==WorkAreaResult::Complete&&b.visibilityRequests==1,"post-quiet Shell-state query precedes final workspace observation so query-driven reflow cannot become stale completion");
 }
 {
  auto s=armed();s.entries[0]={42,7,1,1};const auto original=s.workAreas;
  WorkspaceProtocolBackend b;b.visible=false;b.workspace.removeReservations();b.quietMilliseconds=1000;b.lateWorkspaceResetAfter=250;
  const auto report=restoreRecovery(&s,b,4,50);
  // The fallback's queued reflow can arrive after the helper's shorter fence.
  // Delivery after an early return exposes lost ownership rather than allowing
  // a currently exact rectangle to count as asynchronous completion.
  b.delay(250);
  check(report.complete&&s.armed==0&&b.workspaceResetDone&&compareWorkAreas(original,b.workspace.current)==WorkAreaResult::Complete&&b.visibilityRequests==1&&compareWorkAreas(original,s.workAreas)==WorkAreaResult::Complete,"asynchronous fallback's requested quiet fence retains ownership through a workspace reset later than the helper's 100ms fence");
 }
 {
  struct BoundaryBackend final:RecoveryBackend,RecoveryHideBackend {
   RecoverySnapshot* snapshot=nullptr;ULONGLONG clock=1000;DWORD state=ABS_AUTOHIDE;
   unsigned captures=0,workspaceCaptures=0,hideRequests=0,restoreRequests=0,elapsed=0;
   unsigned revokeAt=0,deathAt=0,identityAt=0,observations=0;bool visible=false,delayedShow=true,showDelivered=false,identityOnFinalProof=false;
   bool persistentPending=false,unavailable=false,ownerAlive=true,entriesMatch=true;
   unsigned finalPending=0;bool finalAlwaysPending=false,revokeOnFinalPending=false;
   bool validEntry(const RecoveryEntry&)noexcept override{return entriesMatch;}
   void requestVisibility(const RecoveryEntry& entry)noexcept override{++restoreRequests;visible=entry.visible!=0;}
   bool visibilityRestored(const RecoveryEntry& entry)noexcept override{return visible==(entry.visible!=0);}
   void requestState(DWORD original)noexcept override{state=original;}
   bool stateRestored(DWORD original)noexcept override{return state==original;}
   void delay(DWORD milliseconds)noexcept override{
    clock+=milliseconds;elapsed+=milliseconds;
    if(delayedShow&&!showDelivered&&elapsed>=450){visible=true;showDelivered=true;}
    if(revokeAt&&elapsed>=revokeAt)InterlockedExchange(&snapshot->revoked,1);
    if(deathAt&&elapsed>=deathAt)ownerAlive=false;
    if(identityAt&&elapsed>=identityAt)entriesMatch=false;
   }
   bool captureWorkAreaOriginals(WorkAreaSnapshot& out)noexcept override{++workspaceCaptures;out=originalAreas();return true;}
   WorkAreaResult restoreOriginalWorkAreas(const WorkAreaSnapshot&)noexcept override{return WorkAreaResult::Complete;}
   WorkAreaResult originalWorkAreasRestored(const WorkAreaSnapshot&)noexcept override{return WorkAreaResult::Complete;}
   bool captureOriginals(RecoverySnapshot&)noexcept override{++captures;return true;}
   bool hideCaptured(RecoverySnapshot& s)noexcept override{return hideRecovery(&s,*this)==RecoveryHideResult::Complete;}
   bool confirmHiddenCaptured(RecoverySnapshot& s)noexcept override{return confirmHiddenRecovery(&s,*this)==RecoveryHideResult::Complete;}
   bool prepareHiddenState(const RecoverySnapshot&,DWORD target)noexcept override{state=target;return true;}
   bool requestHidden(const RecoveryEntry&)noexcept override{if(visible){++hideRequests;visible=false;}return true;}
   RecoveryHideObservation observeHidden(DWORD target)noexcept override{
    ++observations;if(identityOnFinalProof&&observations==3)entriesMatch=false;
    if(observations>=3&&(finalPending||finalAlwaysPending)){
     if(finalPending)--finalPending;visible=true;
     if(revokeOnFinalPending)InterlockedExchange(&snapshot->revoked,1);
     return RecoveryHideObservation::Pending;
    }
    if(unavailable)return RecoveryHideObservation::Unavailable;
    return !visible&&!persistentPending&&state==target?RecoveryHideObservation::Confirmed:RecoveryHideObservation::Pending;
   }
   bool confirmationAllowed()const noexcept override{return ownerAlive;}
   bool confirmationEntriesValid(const RecoverySnapshot&)noexcept override{return entriesMatch;}
   ULONGLONG confirmationNow()const noexcept override{return clock;}
  };
  auto generation=[](){auto s=armed();s.entries[0]={42,7,1,1};s.phase=static_cast<LONG>(RecoveryPhase::Armed);s.heartbeat=1000;return s;};
  auto s=generation();const auto original=s.workAreas;BoundaryBackend b;b.snapshot=&s;b.visible=true;
  const bool stable=executeRecoveryCommand(&s,b,RecoveryCommand::ConfirmHidden,1000);
  check(stable&&b.showDelivered&&!b.visible&&b.hideRequests==2&&b.elapsed>=1000&&b.captures==0&&b.workspaceCaptures==0&&s.armed==1&&s.revoked==0&&s.taskbarState==0&&s.entries[0].visible==1&&compareWorkAreas(s.workAreas,original)==WorkAreaResult::Complete,"existing-generation boundary drains delayed SHOW before final hide without recapturing originals or workspace");
  s=generation();b=BoundaryBackend{};b.snapshot=&s;b.persistentPending=true;
  check(!executeRecoveryCommand(&s,b,RecoveryCommand::ConfirmHidden,1000)&&s.armed==0&&s.revoked==1&&b.visible&&b.state==0,"persistent boundary visibility cannot acknowledge success and restores the captured generation");
  s=generation();b=BoundaryBackend{};b.snapshot=&s;b.unavailable=true;
  check(!executeRecoveryCommand(&s,b,RecoveryCommand::ConfirmHidden,1000)&&s.armed==0&&s.revoked==1&&b.visible,"unavailable final boundary observation restores originals rather than claiming hidden success");
  s=generation();b=BoundaryBackend{};b.snapshot=&s;b.revokeAt=500;
  check(!executeRecoveryCommand(&s,b,RecoveryCommand::ConfirmHidden,1000)&&s.armed==0&&s.revoked==1&&b.hideRequests==0&&b.visible,"revocation during boundary quiet prevents a later hide and transfers to original restoration");
  s=generation();b=BoundaryBackend{};b.snapshot=&s;b.deathAt=500;
  check(!executeRecoveryCommand(&s,b,RecoveryCommand::ConfirmHidden,1000)&&s.armed==0&&s.revoked==1&&b.hideRequests==0&&b.visible,"parent death or stop during boundary quiet wins over post-quiet hiding and acknowledgment");
  s=generation();s.leaseMilliseconds=1000;b=BoundaryBackend{};b.snapshot=&s;
  check(!executeRecoveryCommand(&s,b,RecoveryCommand::ConfirmHidden,1000)&&s.armed==0&&s.revoked==1&&b.hideRequests==0&&b.visible,"controller lease expiry during boundary quiet cannot be revived by the pending confirmation");
  s=generation();b=BoundaryBackend{};b.snapshot=&s;b.identityAt=500;
  check(!executeRecoveryCommand(&s,b,RecoveryCommand::ConfirmHidden,1000)&&s.armed==0&&s.revoked==1&&b.hideRequests==0&&b.captures==0,"new or ambiguous taskbar identity during boundary quiet refuses acquisition by confirmation");
  s=generation();b=BoundaryBackend{};b.snapshot=&s;b.identityOnFinalProof=true;const auto finalQueryOriginal=s.workAreas;
  check(!executeRecoveryCommand(&s,b,RecoveryCommand::ConfirmHidden,1000)&&b.observations==3&&s.armed==0&&s.revoked==1&&b.state==0&&b.captures==0&&b.workspaceCaptures==0&&b.restoreRequests==0&&s.entries[0].hwnd==42&&s.entries[0].visible==1&&compareWorkAreas(s.workAreas,finalQueryOriginal)==WorkAreaResult::Complete,"identity replacement during final Shell-query proof rejects stale hidden success without recapture or writing the replacement visibility");
  s=generation();b=BoundaryBackend{};b.snapshot=&s;b.delayedShow=false;b.finalPending=1;
  check(executeRecoveryCommand(&s,b,RecoveryCommand::ConfirmHidden,1000)&&b.observations>3&&b.hideRequests==1&&!b.visible&&b.captures==0&&b.workspaceCaptures==0&&s.armed==1&&s.revoked==0,"late same-generation SHOW in final boundary query retries to a fresh hidden proof without recapturing originals");
  s=generation();b=BoundaryBackend{};b.snapshot=&s;b.delayedShow=false;b.finalAlwaysPending=true;
  check(!executeRecoveryCommand(&s,b,RecoveryCommand::ConfirmHidden,1000)&&b.observations>3&&b.observations<=22&&s.armed==0&&s.revoked==1&&b.visible&&b.state==0,"persistent late boundary SHOW exhausts a bounded final proof budget and restores originals");
  s=generation();b=BoundaryBackend{};b.snapshot=&s;b.delayedShow=false;b.finalPending=1;b.revokeOnFinalPending=true;
  check(!executeRecoveryCommand(&s,b,RecoveryCommand::ConfirmHidden,1000)&&b.observations==3&&b.hideRequests==0&&s.armed==0&&s.revoked==1&&b.visible,"revocation in the late final query prevents any retry hide and restores originals");
  s=generation();s.armed=0;b=BoundaryBackend{};b.snapshot=&s;
  const bool unarmedRejected=!executeRecoveryCommand(&s,b,RecoveryCommand::ConfirmHidden,1000);
  s=generation();s.revoked=1;const bool revokedRejected=!executeRecoveryCommand(&s,b,RecoveryCommand::ConfirmHidden,1000);
  s=generation();s.restoring=1;const bool restoringRejected=!executeRecoveryCommand(&s,b,RecoveryCommand::ConfirmHidden,1000);
  check(unarmedRejected&&revokedRejected&&restoringRejected&&b.captures==0&&b.workspaceCaptures==0&&b.hideRequests==0,"boundary confirmation never acquires an unarmed lifetime or revives revoked/restoring ownership");
 }
 check(RecoverySnapshot{}.nativeAllowed==0&&!TaskbarGuardOptions{}.enableNativeForValidation,"ordinary recovery fixtures default to unpermitted native operations");
 std::cout<<checks-failures<<"/"<<checks<<" recovery-state checks passed; injected fixture backend never accesses native taskbars.\n";
 return failures?1:0;
}
