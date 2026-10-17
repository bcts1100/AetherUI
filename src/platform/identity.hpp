#pragma once
#include "core/model.hpp"
#include <filesystem>
#include <optional>
namespace aether::win {
// Shortcut launch targets stay as .lnk files so the Shell retains their arguments.
std::optional<Pin> pinFromPath(const std::filesystem::path& path);
}