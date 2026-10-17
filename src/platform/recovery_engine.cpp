#include "recovery_engine.hpp"
namespace aether::win {
namespace {
LONG load(const volatile LONG* value) noexcept{return InterlockedCompareExchange(const_cast<volatile LONG*>(value),0,0);}
void phase(RecoverySnapshot* s,RecoveryPhase value) noexcept{InterlockedExchange(&s->phase,static_cast<LONG>(value));}
}
bool recoverySnapshotValid(const RecoverySnapshot* s) noexcept {
 if(!s||s->magic!=RecoveryMagic||s->version!=RecoveryVersion||s->bytes!=sizeof(RecoverySnapshot)
  ||load(&s->count)<0||load(&s->count)>32||s->leaseMilliseconds<1000||s->leaseMilliseconds>60000
  ||s->nativeAllowed>1)return false;
 const LONG captured=load(&s->workAreasCaptured);
 return (captured==0||captured==1)&&(captured==0||workAreaSnapshotValid(s->workAreas))
  &&(load(&s->armed)==0||captured==1);
}
bool recoveryHiddenStateRequired(bool initialCapture,bool globalStateMatches,bool newIdentity) noexcept {
 return initialCapture||!globalStateMatches||newIdentity;
}
DWORD nativeTaskbarSetterMask(DWORD rawState) noexcept{return rawState|ABS_ALWAYSONTOP;}
RecoveryHideObservation observeRecoveryHidden(RecoveryHiddenObservationBackend& b,DWORD state) noexcept {
 const auto nativeState=b.observeNativeState(state);
 return nativeState==RecoveryHideObservation::Confirmed?b.observeNativeVisibility():nativeState;
}
RecoveryHideResult hideRecovery(RecoverySnapshot* s,RecoveryHideBackend& b,unsigned attempts,DWORD wait) noexcept {
 if(!recoverySnapshotValid(s)||load(&s->armed)==0)return RecoveryHideResult::Invalid;
 if(load(&s->revoked))return RecoveryHideResult::Revoked;
 if(!b.hideAllowed(*s)){InterlockedExchange(&s->revoked,1);return RecoveryHideResult::Revoked;}
 const DWORD hiddenState=s->taskbarState|ABS_AUTOHIDE;
 if(!b.prepareHiddenState(*s,hiddenState))return load(&s->revoked)?RecoveryHideResult::Revoked:RecoveryHideResult::StateUnavailable;
 const LONG count=load(&s->count);
 attempts=std::clamp(attempts,1u,40u);wait=std::min<DWORD>(wait,100);
 auto lastObservation=RecoveryHideObservation::Pending;
 for(unsigned pass=0;pass<attempts;++pass){
  // Shell may show a secondary bar during its auto-hide transition after
  // processing the first request. Retry only still-visible captured identities
  // within this same bounded confirmation; originals are never recaptured.
  for(LONG i=0;i<count;++i){
   if(load(&s->revoked)||!b.hideAllowed(*s)){InterlockedExchange(&s->revoked,1);return RecoveryHideResult::Revoked;}
   if(!b.requestHidden(s->entries[i]))return RecoveryHideResult::VisibilityRequestFailed;
  }
  if(load(&s->revoked)||!b.hideAllowed(*s)){InterlockedExchange(&s->revoked,1);return RecoveryHideResult::Revoked;}
  const auto observed=b.observeHidden(hiddenState);
  // A changing Shell scan is unknown, never success. Retry within the existing
  // budget without acquiring identities or altering the immutable originals.
  // The synchronous query can deliver Stop/parent/lease changes before its result.
  if(load(&s->revoked)||!b.hideAllowed(*s)){InterlockedExchange(&s->revoked,1);return RecoveryHideResult::Revoked;}
  lastObservation=observed;
  if(observed==RecoveryHideObservation::Confirmed)return RecoveryHideResult::Complete;
  if(pass+1<attempts){InterlockedExchange64(&s->helperHeartbeat,static_cast<LONG64>(GetTickCount64()));b.delay(wait);}
 }
 return lastObservation==RecoveryHideObservation::Unavailable?RecoveryHideResult::ObservationUnavailable:RecoveryHideResult::Deadline;
}
RecoveryHideResult confirmHiddenRecovery(RecoverySnapshot* s,RecoveryHideBackend& b,DWORD quietMilliseconds) noexcept {
 if(!recoverySnapshotValid(s)||load(&s->armed)==0||load(&s->count)==0
  ||load(&s->phase)!=static_cast<LONG>(RecoveryPhase::Armed))return RecoveryHideResult::Invalid;
 auto permitted=[&]() noexcept {
  if(load(&s->revoked)||load(&s->restoring))return false;
  if(!b.confirmationAllowed()||recoveryLeaseExpired(s,b.confirmationNow())){
   InterlockedExchange(&s->revoked,1);return false;
  }return true;
 };
 // Deliver already queued lifetime ambiguity before this command's first write.
 b.delayConfirmation(0);
 if(!permitted())return RecoveryHideResult::Revoked;
 if(!b.confirmationEntriesValid(*s))return RecoveryHideResult::ObservationUnavailable;
 const auto first=hideRecovery(s,b);if(first!=RecoveryHideResult::Complete)return first;
 // Only an explicit UI boundary pays this fence. Original state/visibility and
 // workspace remain immutable; neither this command nor its final hide acquires.
 quietMilliseconds=std::clamp<DWORD>(quietMilliseconds,100,1000);
 for(DWORD elapsed=0;elapsed<quietMilliseconds;){
  if(!permitted())return RecoveryHideResult::Revoked;
  InterlockedExchange64(&s->helperHeartbeat,static_cast<LONG64>(GetTickCount64()));
  const DWORD piece=std::min<DWORD>(100,quietMilliseconds-elapsed);
  b.delayConfirmation(piece);elapsed+=piece;
  if(!permitted())return RecoveryHideResult::Revoked;
 }
 // Deliver already queued lifecycle callbacks before validating this same set.
 b.delayConfirmation(0);
 if(!permitted())return RecoveryHideResult::Revoked;
 if(!b.confirmationEntriesValid(*s))return RecoveryHideResult::ObservationUnavailable;
 auto last=RecoveryHideObservation::Pending;
 for(unsigned pass=0;pass<10;++pass){
  // A secondary bar may SHOW while the final synchronous state query delivers
  // Shell work. Reconcile this same captured generation within one ten-attempt
  // budget rather than failing on its first late SHOW or nesting ten retries.
  const auto hidden=hideRecovery(s,b,1,0);
  if(hidden!=RecoveryHideResult::Complete&&hidden!=RecoveryHideResult::Deadline
   &&hidden!=RecoveryHideResult::ObservationUnavailable)return hidden;
  b.delayConfirmation(0);
  if(!permitted())return RecoveryHideResult::Revoked;
  if(!b.confirmationEntriesValid(*s))return RecoveryHideResult::ObservationUnavailable;
  last=b.observeHidden(s->taskbarState|ABS_AUTOHIDE);
  // Hidden geometry alone cannot approve a replacement generation. Lifetime
  // and exact identities are revalidated after every synchronous final query.
  if(!permitted())return RecoveryHideResult::Revoked;
  if(!b.confirmationEntriesValid(*s))return RecoveryHideResult::ObservationUnavailable;
  if(last==RecoveryHideObservation::Confirmed)return RecoveryHideResult::Complete;
  if(pass+1<10){
   InterlockedExchange64(&s->helperHeartbeat,static_cast<LONG64>(b.confirmationNow()));
   b.delayConfirmation(50);
   if(!permitted())return RecoveryHideResult::Revoked;
   if(!b.confirmationEntriesValid(*s))return RecoveryHideResult::ObservationUnavailable;
  }
 }
 return last==RecoveryHideObservation::Unavailable?RecoveryHideResult::ObservationUnavailable:RecoveryHideResult::Deadline;
}
RecoveryReport restoreRecovery(RecoverySnapshot* s,RecoveryBackend& b,unsigned attempts,DWORD wait) noexcept {
 RecoveryReport r;if(!recoverySnapshotValid(s))return r;
 InterlockedExchange(&s->revoked,1);
 if(load(&s->armed)==0){phase(s,RecoveryPhase::Restored);r.complete=true;return r;}
 // No controller native writes may race this owner. A dead helper permits fallback.
 InterlockedExchange(&s->restoring,1);phase(s,RecoveryPhase::Restoring);
 const LONG count=load(&s->count);
 const DWORD quietMilliseconds=std::clamp<DWORD>(b.restorationQuietMilliseconds(),100,1000);
 attempts=std::clamp(attempts,1u,40u);wait=std::min<DWORD>(wait,100);
 for(unsigned pass=0;pass<attempts;++pass){
  bool restored=true;r.skipped=0;
  // ABM_GETSTATE ignores HWND: a matching global bitmask does not establish
  // that every secondary bar received the original per-window state request.
  if(pass==0||!b.stateRestored(s->taskbarState)){b.requestState(s->taskbarState);restored=b.stateRestored(s->taskbarState);}
  for(LONG i=0;i<count;++i){
   const auto entry=s->entries[i];
   if(!b.validEntry(entry)){++r.skipped;continue;}
   // Always enqueue a target on the first pass, including a currently matching
   // window: an earlier asynchronous hide can still be in its message queue.
   if(pass==0||!b.visibilityRestored(entry))b.requestVisibility(entry);
   if(!b.visibilityRestored(entry))restored=false;
  }
  // Geometry is part of this same immutable generation. Matching taskbar bits
  // and visibility cannot retire a helper while a monitor remains changed.
  if(restored&&(b.restoreOriginalWorkAreas(s->workAreas)!=WorkAreaResult::Complete
   ||b.originalWorkAreasRestored(s->workAreas)!=WorkAreaResult::Complete))restored=false;
  if(restored){
   // A queued ShowWindowAsync can reset rcWork even when visibility already
   // matched. Keep this owner through bounded settling after the final write.
   InterlockedExchange64(&s->helperHeartbeat,static_cast<LONG64>(GetTickCount64()));
   b.delay(quietMilliseconds);
   // The Shell-state query can itself process queued work. Query it first,
   // then freshly confirm surviving identities and physical workspace last.
   restored=b.stateRestored(s->taskbarState);
   for(LONG i=0;i<count;++i){const auto entry=s->entries[i];if(b.validEntry(entry)&&!b.visibilityRestored(entry))restored=false;}
   if(b.originalWorkAreasRestored(s->workAreas)!=WorkAreaResult::Complete)restored=false;
  }
  if(restored){InterlockedExchange(&s->restored,1);InterlockedExchange(&s->armed,0);phase(s,RecoveryPhase::Restored);r.complete=true;break;}
  if(pass+1<attempts)b.delay(wait);
 }
 InterlockedExchange(&s->restoring,0);return r;
}
bool recoveryLeaseExpired(const RecoverySnapshot* s,ULONGLONG now) noexcept {
 if(!recoverySnapshotValid(s)||load(&s->armed)==0)return false;
 const auto heartbeat=static_cast<ULONGLONG>(InterlockedCompareExchange64(const_cast<volatile LONG64*>(&s->heartbeat),0,0));
 return now>=heartbeat&&now-heartbeat>=s->leaseMilliseconds;
}
bool observeRecoveryWindowEvent(RecoverySnapshot* s,DWORD event,std::uint64_t hwnd) noexcept {
 if(!recoverySnapshotValid(s)||!hwnd||load(&s->armed)==0||(event!=EVENT_OBJECT_DESTROY&&event!=EVENT_OBJECT_CREATE))return false;
 const LONG count=load(&s->count);
 for(LONG i=0;i<count;++i)if(s->entries[i].hwnd==hwnd){
  // A delayed event can describe the old lifetime of a reused HWND, including
  // reuse within the same Explorer PID/birth. Never erase a published original
  // or recapture the replacement after our hide. Revoke this ambiguous owner.
  InterlockedExchange(&s->revoked,1);return true;
 }
 return false;
}
bool executeRecoveryCommand(RecoverySnapshot* s,RecoveryBackend& b,RecoveryCommand command,ULONGLONG now) noexcept {
 if(!recoverySnapshotValid(s))return false;
 if(command==RecoveryCommand::Restore)return restoreRecovery(s,b).complete;
 if(command==RecoveryCommand::ConfirmHidden){
  // Confirmation is never acquisition, discovery or a late-heartbeat rearm.
  if(load(&s->armed)==0||load(&s->count)==0||load(&s->phase)!=static_cast<LONG>(RecoveryPhase::Armed)
   ||load(&s->revoked)||load(&s->restoring))return false;
  if(recoveryLeaseExpired(s,now)){restoreRecovery(s,b);return false;}
  if(!b.confirmHiddenCaptured(*s)){restoreRecovery(s,b);return false;}
  return true;
 }
 if(command!=RecoveryCommand::Arm&&command!=RecoveryCommand::Refresh)return false;
 // Revocation lasts for this helper lifetime. A resumed controller cannot
 // revive an expired lease by sending an Arm/Refresh or a late heartbeat.
 if(load(&s->revoked)!=0||load(&s->restoring)!=0)return false;
 if(recoveryLeaseExpired(s,now)){restoreRecovery(s,b);return false;}
 if(command==RecoveryCommand::Refresh&&load(&s->armed)==0)return false;
 if(load(&s->workAreasCaptured)==0){
  WorkAreaSnapshot original;
  if(!b.captureWorkAreaOriginals(original)||!workAreaSnapshotValid(original)){
   InterlockedExchange(&s->revoked,1);phase(s,RecoveryPhase::Failed);return false;
  }
  s->workAreas=original;
  // Publish the complete immutable workspace before any taskbar mutation.
  InterlockedExchange(&s->workAreasCaptured,1);
 }
 if(!b.captureOriginals(*s)){
  InterlockedExchange(&s->revoked,1);if(load(&s->armed))restoreRecovery(s,b);else phase(s,RecoveryPhase::Failed);return false;
 }
 // Captured entries and the original bitmask are published before any hide.
 InterlockedExchange(&s->restored,0);InterlockedExchange(&s->armed,1);phase(s,RecoveryPhase::Armed);
 if(load(&s->revoked)!=0||!b.hideCaptured(*s)){restoreRecovery(s,b);return false;}
 return true;
}
}
