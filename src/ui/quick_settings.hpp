#pragma once
#include "canvas.hpp"
#include "accessibility.hpp"
#include "platform/desktop.hpp"
namespace aether {class App;}
namespace aether::ui {
class QuickSettings final:public Surface {
 std::unique_ptr<Accessibility> accessibility_;int focused_=-1;D2D1_RECT_F controlRect(int) const;void accessibleSnapshot();void focusControl(int);void invoke(int);void adjustVolume(int);
 App& app_;bool draggingVolume_=false;int previewVolume_=-1;
 void paint(Canvas&) override;void click(float,float) override;
 LRESULT event(UINT,WPARAM,LPARAM,bool&) override;
 void volumeAt(float);
public:
 QuickSettings(Graphics&,App&);~QuickSettings() override{if(accessibility_)accessibility_->detach();destroy();}
 void open(const win::Monitor&);
};
}
