#pragma once
#include "provider_client.hpp"
namespace aether::win {
ProviderWireReply runProviderOperation(ProviderOperation operation,const ProviderBytes& payload);
}
