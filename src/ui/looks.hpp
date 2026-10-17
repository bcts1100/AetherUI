#pragma once
#include "canvas.hpp"
#include "accessibility.hpp"
#include "platform/desktop.hpp"
#include <functional>
namespace aether {class App;}
namespace aether::ui {
class Looks final:public Surface {
 App& app_;int selected_=0,focused_=-1;float drawingScale_=1;
 ThemeUndo undo_;std::wstring notice_;
 std::unique_ptr<Accessibility> accessibility_;
 std::vector<AccessibleNode> actions_;std::vector<D2D1_RECT_F> rects_;
 void paint(Canvas&) override;void click(float,float) override;
 LRESULT event(UINT,WPARAM,LPARAM,bool&) override;
 void button(Canvas&,D2D1_RECT_F,const std::wstring&,std::function<void()>,bool enabled=true);
 void apply();void undo();void addWidgets(bool productivity);void focus(int);
public:
 Looks(Graphics&,App&);~Looks() override;
 void open(const win::Monitor&);void update();
};
}
