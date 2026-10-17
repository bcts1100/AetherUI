#pragma once
#include "canvas.hpp"
#include "accessibility.hpp"
#include "platform/marketplace.hpp"
#include "platform/desktop.hpp"
namespace aether{class App;}
namespace aether::ui{
class Marketplace final:public Surface{
 App& app_;std::unique_ptr<Accessibility> accessibility_;std::vector<win::InstalledPackage> installed_;
 std::vector<AccessibleNode> actions_;std::vector<D2D1_RECT_F> rects_;int selected_=0,filter_=0,focused_=0;float factor_=1;
 std::wstring notice_;
 void paint(Canvas&)override;void click(float,float)override;LRESULT event(UINT,WPARAM,LPARAM,bool&)override;
 void button(Canvas&,D2D1_RECT_F,std::wstring,std::function<void()>,bool enabled=true);
 void refresh();void install();void apply();void remove();void disable();
 const win::InstalledPackage* receipt()const;std::string widgetId()const;
public:
 Marketplace(Graphics&,App&);~Marketplace()override;void open(const win::Monitor&);
};
}
