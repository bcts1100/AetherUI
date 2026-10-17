#pragma once
#include "canvas.hpp"
#include "accessibility.hpp"
#include "platform/extensions.hpp"
#include "platform/desktop.hpp"
#include "platform/plugin_data.hpp"
namespace aether {class App;}
namespace aether::ui {
class ExtensionWidgetSurface;
class ExtensionRuntime {
 Graphics& graphics_;App& app_;win::ExtensionLibrary library_;
 std::vector<std::unique_ptr<ExtensionWidgetSurface>> widgets_;bool started_=false,paused_=false;win::PluginData data_;
 void synchronize();
public:
 ExtensionRuntime(Graphics&,App&);~ExtensionRuntime();
 bool start(std::string* error=nullptr);void update();void pause(bool);void shutdown();
 const win::ExtensionLibrary& library() const{return library_;}
 std::size_t widgetCount() const{return widgets_.size();}
 win::PluginData& data(){return data_;}bool resourcesActive() const;
 bool setEnabled(std::string_view,bool,std::string* error=nullptr);
 bool remove(std::string_view,std::string* error=nullptr);
 bool move(std::string_view,int,std::string* error=nullptr);
 bool setSize(std::string_view id,std::string_view widget,int width,int height,std::string* error=nullptr);
 bool setPlacement(std::string_view,std::string_view,const WidgetPlacement&,std::string* error=nullptr);
 bool addExamples(std::string* error=nullptr);
 std::optional<std::string> importFile(const std::filesystem::path&,std::string* error=nullptr);
 std::optional<std::string> install(const ExtensionManifest&,std::string* error=nullptr);
 bool setNote(std::string_view,std::string_view,std::string_view,std::string* error=nullptr);
 bool exportFile(std::string_view,const std::filesystem::path&,std::string* error=nullptr) const;
};
class Extensions final:public Surface {
 App& app_;std::unique_ptr<Accessibility> accessibility_;
 std::vector<AccessibleNode> actions_;std::vector<D2D1_RECT_F> rects_;std::vector<bool> scrolling_;int focused_=-1;float scroll_=0,drawingScale_=1.f;std::wstring notice_;
 void paint(Canvas&) override;void click(float,float) override;
 LRESULT event(UINT,WPARAM,LPARAM,bool&) override;
 void button(Canvas&,D2D1_RECT_F,const std::wstring&,std::function<void()>,bool enabled=true,bool scrolling=true);
 void snapshot();void focus(int);void invoke(int);float contentHeight() const;D2D1_SIZE_F logicalSize() const;void importManifest();void exportManifest(const std::string&);
 void result(bool,const std::string&);
public:
 Extensions(Graphics&,App&);~Extensions() override;
 void open(const win::Monitor&);void update();
};
}
