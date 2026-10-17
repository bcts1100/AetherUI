#pragma once
#include "platform/windows.hpp"
#include <oleacc.h>
#include <d2d1.h>
#include <functional>
#include <memory>
namespace aether::ui {
struct AccessibleNode {
 long id{};std::wstring name,description,defaultAction=L"Press",value,shortcut;
 long role=ROLE_SYSTEM_PUSHBUTTON,state=STATE_SYSTEM_FOCUSABLE;RECT bounds{};HWND native{};
 std::function<void()> invoke,focus;std::function<bool(const std::wstring&)> setValue;
};
AccessibleNode accessibleNode(HWND,long,std::wstring,long,long,D2D1_RECT_F,float drawingScale=1.f);
void accessibleNativeName(HWND,const std::wstring&);
class Accessibility final {
public: struct State;
private: std::shared_ptr<State> state_;win::ComPtr<IAccessible> provider_;
public:
 explicit Accessibility(HWND owner);~Accessibility();
 Accessibility(const Accessibility&)=delete;Accessibility& operator=(const Accessibility&)=delete;
 void snapshot(std::wstring name,long role,std::vector<AccessibleNode> nodes,long focused=0,long selected=0);
 LRESULT object(WPARAM,LPARAM,bool& handled);
 void detach();
};
}
