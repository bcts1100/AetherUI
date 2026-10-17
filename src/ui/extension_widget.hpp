#pragma once
#include "extensions.hpp"
namespace aether::ui {
class ExtensionWidgetSurface final:public Surface {
 App& app_;ExtensionRuntime& runtime_;std::string storageId_,packageName_;ExtensionWidget widget_;
 std::unique_ptr<Accessibility> accessibility_;win::Monitor monitor_;bool monitorValid_=false;
 bool dragging_=false,dragMoved_=false,resizing_=false;POINT dragOrigin_{};Rect dragBounds_{};std::wstring value_;bool actionAvailable_=true;
 bool hovered_=false;
 void paint(Canvas&) override;void click(float,float) override;
 void pointer(float,float) override;void leave() override;void mediaCommand(std::wstring_view);
 LRESULT event(UINT,WPARAM,LPARAM,bool&) override;
 void snapshot();void invoke();void savePosition();void saveSize();void keyboardMove(int,int);
public:
 ExtensionWidgetSurface(Graphics&,App&,ExtensionRuntime&,const InstalledExtension&,const ExtensionWidget&);
 ~ExtensionWidgetSurface() override;
 bool matches(std::string_view id,std::string_view widget) const{return storageId_==id&&widget_.id==widget;}
 WidgetKind kind()const{return widget_.kind;}
 void refresh(const ExtensionWidget&,bool paused);void cancelDrag();
};
}
