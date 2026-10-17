#pragma once
#include "config.hpp"
#include "core/extensions.hpp"
namespace aether::win {
// All mutations address generated library IDs, never user manifest names or paths.
class ExtensionLibrary {
 std::vector<InstalledExtension> entries_;std::string notice_;
 bool persist(const InstalledExtension&,std::string*);
public:
 bool load(std::string* error=nullptr);
 const std::vector<InstalledExtension>& entries() const{return entries_;}
 const std::string& notice() const{return notice_;}
 bool addExamples(std::string* error=nullptr);
 std::optional<std::string> install(const ExtensionManifest&,std::string* error=nullptr);
 std::optional<std::string> importFile(const std::filesystem::path&,std::string* error=nullptr);
 bool exportFile(std::string_view storageId,const std::filesystem::path&,std::string* error=nullptr) const;
 bool remove(std::string_view storageId,std::string* error=nullptr);
 bool setEnabled(std::string_view storageId,bool,std::string* error=nullptr);
 bool move(std::string_view storageId,int direction,std::string* error=nullptr);
 bool setSize(std::string_view id,std::string_view widget,int width,int height,std::string* error=nullptr);
 bool setPlacement(std::string_view storageId,std::string_view widgetId,const WidgetPlacement&,std::string* error=nullptr);
 bool setNote(std::string_view storageId,std::string_view widgetId,std::string_view text,std::string* error=nullptr);
};
}
