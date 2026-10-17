#pragma once
#include "provider_client.hpp"
#include "shell_types.hpp"
#include <optional>
namespace aether::win {
enum class ShellRequestKind {Launch,Pin,Metadata};
struct ShellCompletion {
 ShellRequestKind kind=ShellRequestKind::Launch;
 std::uint64_t id=0,revision=0;std::wstring target;HWND owner=nullptr;DWORD ownerPid=0;
 ProviderOutcome outcome=ProviderOutcome::Unavailable;
 std::optional<Pin> pin;std::vector<WindowMetadata> metadata;
};
class ShellRequests {
 struct Impl;std::unique_ptr<Impl> impl_;
public:
 explicit ShellRequests(ProviderProcessOptions options={});~ShellRequests();
 ShellRequests(const ShellRequests&)=delete;ShellRequests& operator=(const ShellRequests&)=delete;
 void start(HWND receiver);void stop();
 // Zero/false means stopped, invalid input, or the bounded queue is full.
 std::uint64_t requestLaunch(HWND owner,const std::wstring& target);
 std::uint64_t requestPin(const std::filesystem::path& path,std::uint64_t revision=0);
 bool requestMetadata(std::vector<WindowMetadataRequest> requests);
 std::vector<ShellCompletion> takeCompletions();
};
}