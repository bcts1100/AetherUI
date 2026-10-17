#pragma once
#include "windows.hpp"
#include "work_area.hpp"
#include <cstdint>
namespace aether::win {
constexpr DWORD RecoveryMagic=0x41544833;
constexpr DWORD RecoveryVersion=5;
// V4 recovery passed supervised native acceptance; keep startup failure reversible.
constexpr bool TaskbarReplacementValidated=true;
enum class RecoveryCommand:LONG {None=0,Arm=1,Refresh=2,Restore=3,ConfirmHidden=4,AppearanceSettle=5};
enum class RecoveryPhase:LONG {Ready=0,Armed=1,Restoring=2,Restored=3,Failed=4};
struct RecoveryEntry {std::uint64_t hwnd{};DWORD pid{};LONG visible{};ULONGLONG processBirth{};};
struct RecoveryVisual {std::uint64_t hwnd{};DWORD exStyle{},colorKey{},flags{};BYTE alpha=255;bool captured=false,owned=false;};
struct RecoverySnapshot {
 DWORD magic=RecoveryMagic,version=RecoveryVersion,bytes=sizeof(RecoverySnapshot);
 volatile LONG armed=0,count=0,restored=0,restoring=0;DWORD taskbarState=0;
 alignas(8) volatile LONG64 heartbeat=0,helperHeartbeat=0;DWORD leaseMilliseconds=5000;
 volatile LONG command=0,commandSequence=0,acknowledgedSequence=0,commandSucceeded=0;
 volatile LONG phase=static_cast<LONG>(RecoveryPhase::Ready),revoked=0;
 DWORD nativeAllowed=0;
 RecoveryEntry entries[32]{};
 volatile LONG workAreasCaptured=0;WorkAreaSnapshot workAreas{};
 // Appended V5 journal; old geometry/identity offsets remain unchanged. The
 // original layer attributes are published before making a taskbar transparent.
 RecoveryVisual visuals[32]{};
};
struct TaskbarGuardOptions {bool enableNativeForValidation=false;DWORD leaseMilliseconds=5000;};
struct TaskbarGuardDiagnostics {
 LONG armed=0,count=0,restored=0,revoked=0;RecoveryPhase phase=RecoveryPhase::Ready;
 DWORD originalTaskbarState=0,leaseMilliseconds=0;ULONGLONG heartbeat=0,helperHeartbeat=0;
};
// The entry role is local process context, never a mutable IPC snapshot field.
enum class NativeRecoveryContext {ControllerFallback,ActiveHelper,RestoreOnlyHelper};
void restoreSnapshot(RecoverySnapshot* snapshot,NativeRecoveryContext context=NativeRecoveryContext::ControllerFallback) noexcept;
void showNativeTaskbars() noexcept;
bool nativeTaskbarWindow(HWND window) noexcept;
// Sole helper, published identities only: no discovery, appbar writes or queued hides.
bool maintainRecoveryVisibility(RecoverySnapshot*,HANDLE parent,HANDLE stop) noexcept;
bool runNativeRecoveryCommand(RecoverySnapshot*,RecoveryCommand,ULONGLONG now,HANDLE parent=nullptr,HANDLE stop=nullptr) noexcept;
class TaskbarGuard {
 Handle mapping_,ready_,parent_,helper_,stop_,command_,completed_;
 RecoverySnapshot* snapshot_=nullptr;
 TaskbarGuardOptions options_;bool revoked_=false;
 enum class RetirementStage {None,Starting,Stopping,Restoring,Terminating};
 RetirementStage retirementStage_=RetirementStage::None;ULONGLONG retirementDeadline_=0,nextRecoveryAttempt_=0;
 bool launch();
 bool startHelper(bool wait=true);
 bool shutdownHelper() noexcept;
 bool helperResponsive() const noexcept;
 bool restorationOwned() const noexcept;
 bool request(RecoveryCommand,bool wait,DWORD timeoutMilliseconds=2000) noexcept;
 void failClosed(bool asynchronous=false) noexcept;
public:
 explicit TaskbarGuard(TaskbarGuardOptions options={}):options_(options){}
 ~TaskbarGuard();
 bool setEnabled(bool enabled);
 // UI-thread progress: zero-time process/ack polls, no controller native fallback.
 // Child creation uses OS calls; false retains the context until complete retirement.
 bool disableAsync() noexcept;
 void refresh(bool asynchronousFailure=false);
 // Confirm settled hiding on the existing healthy generation; never Arm or query Shell here.
 bool refreshAndConfirm() noexcept;
 bool settleAppearance() noexcept;
 void heartbeat(bool asynchronousFailure=false) noexcept;
 bool enabled() const;
 bool healthy() const;
 DWORD helperProcessId() const noexcept;
 // True also for an unobservable held helper: local native writes stay blocked.
 bool recoveryOwnerAlive() const noexcept;
 TaskbarGuardDiagnostics diagnostics() const noexcept;
};
}
