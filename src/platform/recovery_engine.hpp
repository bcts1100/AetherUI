#pragma once
#include "safety.hpp"
namespace aether::win {
struct RecoveryReport {bool complete=false;LONG skipped=0;};
enum class RecoveryHideObservation {Unavailable,Pending,Confirmed};
enum class RecoveryHideResult {Invalid,Revoked,StateUnavailable,VisibilityRequestFailed,ObservationUnavailable,Deadline,Complete};
bool recoveryHiddenStateRequired(bool initialCapture,bool globalStateMatches,bool newIdentity) noexcept;
DWORD nativeTaskbarSetterMask(DWORD rawState) noexcept;
class RecoveryHiddenObservationBackend {
public:
 virtual ~RecoveryHiddenObservationBackend()=default;
 virtual RecoveryHideObservation observeNativeVisibility() noexcept=0;
 virtual RecoveryHideObservation observeNativeState(DWORD) noexcept=0;
};
RecoveryHideObservation observeRecoveryHidden(RecoveryHiddenObservationBackend&,DWORD) noexcept;
class RecoveryHideBackend {
public:
 virtual ~RecoveryHideBackend()=default;
 virtual bool prepareHiddenState(const RecoverySnapshot&,DWORD) noexcept=0;
 virtual bool requestHidden(const RecoveryEntry&) noexcept=0;
 virtual RecoveryHideObservation observeHidden(DWORD) noexcept=0;
 virtual void delay(DWORD) noexcept=0;
 // Retried observations must not outlive their controller, Stop or lease.
 virtual bool hideAllowed(const RecoverySnapshot&) const noexcept{return true;}
 // Only the explicit UI boundary command uses this quiet/provenance contract.
 virtual bool confirmationAllowed() const noexcept{return true;}
 virtual bool confirmationEntriesValid(const RecoverySnapshot&) noexcept{return true;}
 virtual ULONGLONG confirmationNow() const noexcept{return GetTickCount64();}
 virtual void delayConfirmation(DWORD milliseconds) noexcept{delay(milliseconds);}
};
RecoveryHideResult hideRecovery(RecoverySnapshot*,RecoveryHideBackend&,unsigned attempts=10,DWORD delayMilliseconds=50) noexcept;
RecoveryHideResult confirmHiddenRecovery(RecoverySnapshot*,RecoveryHideBackend&,DWORD quietMilliseconds=1000) noexcept;
class RecoveryBackend {
public:
 virtual ~RecoveryBackend()=default;
 virtual bool validEntry(const RecoveryEntry&) noexcept=0;
 virtual void requestVisibility(const RecoveryEntry&) noexcept=0;
 virtual bool visibilityRestored(const RecoveryEntry&) noexcept=0;
 virtual void requestState(DWORD) noexcept=0;
 virtual bool stateRestored(DWORD) noexcept=0;
 virtual void delay(DWORD) noexcept=0;
 // Restoration may need a longer quiet fence when visibility is asynchronous.
 virtual DWORD restorationQuietMilliseconds() const noexcept{return 100;}
 // Only the helper executes these operations while it owns native writes.
 virtual bool captureWorkAreaOriginals(WorkAreaSnapshot&) noexcept{return false;}
 virtual WorkAreaResult restoreOriginalWorkAreas(const WorkAreaSnapshot&) noexcept{return WorkAreaResult::Unavailable;}
 virtual WorkAreaResult originalWorkAreasRestored(const WorkAreaSnapshot&) noexcept{return WorkAreaResult::Unavailable;}
 virtual bool captureOriginals(RecoverySnapshot&) noexcept{return false;}
 virtual bool hideCaptured(RecoverySnapshot&) noexcept{return false;}
 virtual bool confirmHiddenCaptured(RecoverySnapshot&) noexcept{return false;}
};
bool recoverySnapshotValid(const RecoverySnapshot*) noexcept;
RecoveryReport restoreRecovery(RecoverySnapshot*,RecoveryBackend&,unsigned attempts=10,DWORD delayMilliseconds=50) noexcept;
bool recoveryLeaseExpired(const RecoverySnapshot*,ULONGLONG now) noexcept;
bool observeRecoveryWindowEvent(RecoverySnapshot*,DWORD event,std::uint64_t hwnd) noexcept;
bool executeRecoveryCommand(RecoverySnapshot*,RecoveryBackend&,RecoveryCommand,ULONGLONG now) noexcept;
}
