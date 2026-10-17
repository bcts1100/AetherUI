#pragma once
#include "provider_client.hpp"
#include "catalog.hpp"
#include "icons.hpp"
#include "shell_types.hpp"
namespace aether::win {
ProviderBytes encodePin(const Pin&);
std::optional<Pin> decodePin(const ProviderBytes&);
ProviderBytes encodeWindowMetadataRequests(const std::vector<WindowMetadataRequest>&);
std::optional<std::vector<WindowMetadataRequest>> decodeWindowMetadataRequests(const ProviderBytes&);
ProviderBytes encodeWindowMetadata(const std::vector<WindowMetadata>&);
std::optional<std::vector<WindowMetadata>> decodeWindowMetadata(const ProviderBytes&);
ProviderBytes encodeLaunch(const LaunchRequest&);
std::optional<LaunchRequest> decodeLaunch(const ProviderBytes&);
ProviderBytes encodeApplications(const std::vector<Application>&);
std::optional<std::vector<Application>> decodeApplications(const ProviderBytes&);
ProviderBytes encodeIcon(const IconPixels*);
std::shared_ptr<IconPixels> decodeIcon(const ProviderBytes&);
ProviderBytes encodeWorkspaceImage(const IconPixels*);
std::shared_ptr<IconPixels> decodeWorkspaceImage(const ProviderBytes&);
ProviderBytes encodeProviderTarget(std::wstring_view);
std::optional<std::wstring> decodeProviderTarget(const ProviderBytes&);
}
