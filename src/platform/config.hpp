#pragma once
#include "windows.hpp"
namespace aether::win {
std::filesystem::path configDirectory();
void setConfigDirectory(const std::filesystem::path& path);
std::string readText(const std::filesystem::path& path,size_t limit=1024*1024);
void writeAtomic(const std::filesystem::path& path,const std::string& text);
Config loadConfig(bool& firstRun);
void saveConfig(const Config& config);
void writeDiagnostics(const std::filesystem::path&,const std::string&);
void log(const std::string& text) noexcept;
}
