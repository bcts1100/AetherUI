#pragma once
#include "windows.hpp"
namespace aether::win {
constexpr DWORD WorkAreaCapacity=32;
struct WorkAreaEntry {wchar_t device[CCHDEVICENAME]{};RECT bounds{},work{};};
struct WorkAreaSnapshot {DWORD count=0;WorkAreaEntry entries[WorkAreaCapacity]{};};
enum class WorkAreaResult {Complete,Pending,TopologyChanged,Unavailable,Invalid};
class WorkAreaBackend {
public:
 virtual ~WorkAreaBackend()=default;
 virtual bool captureCurrent(WorkAreaSnapshot&) noexcept=0;
 virtual bool requestCaptured(const WorkAreaEntry&) noexcept=0;
};
bool workAreaSnapshotValid(const WorkAreaSnapshot&) noexcept;
bool captureWorkAreas(WorkAreaSnapshot&,WorkAreaBackend&) noexcept;
bool captureWorkAreas(WorkAreaSnapshot&) noexcept;
WorkAreaResult compareWorkAreas(const WorkAreaSnapshot& original,const WorkAreaSnapshot& current) noexcept;
WorkAreaResult restoreWorkAreas(const WorkAreaSnapshot&,WorkAreaBackend&) noexcept;
WorkAreaResult restoreWorkAreas(const WorkAreaSnapshot&) noexcept;
}
