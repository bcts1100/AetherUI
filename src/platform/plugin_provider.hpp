#pragma once
#include "provider_client.hpp"
namespace aether::win {
ProviderWireReply runPluginProviderOperation(ProviderOperation,const ProviderBytes&);
std::optional<std::wstring> weatherForecastPath(std::wstring_view);
std::optional<std::wstring> parseSeattleForecast(std::string_view);
}
