#include "app.hpp"
#include "ui/activities.hpp"
#include "ui/editor.hpp"
#include "ui/quick_settings.hpp"
#include <oleacc.h>
#include <uiautomation.h>
#include <iostream>
using namespace aether;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
VARIANT child(long id){VARIANT value{};value.vt=VT_I4;value.lVal=id;return value;}
win::ComPtr<IAccessible> accessible(HWND h){win::ComPtr<IAccessible> result;require(SUCCEEDED(AccessibleObjectFromWindow(h,static_cast<DWORD>(OBJID_CLIENT),IID_IAccessible,reinterpret_cast<void**>(result.GetAddressOf()))),"Native MSAA query failed");return result;}
std::wstring name(IAccessible* a,long id){BSTR text{};auto hr=a->get_accName(child(id),&text);std::wstring result=text?text:L"";SysFreeString(text);if(FAILED(hr))throw std::runtime_error("Accessible name unavailable for child "+std::to_string(id));return result;}
long find(IAccessible* a,const std::wstring& text){
 long count{};require(SUCCEEDED(a->get_accChildCount(&count)),"Accessible child count unavailable");std::vector<VARIANT> children(static_cast<std::size_t>(count));long fetched{};require(SUCCEEDED(AccessibleChildren(a,0,count,children.data(),&fetched)),"Native MSAA child enumeration failed");long found=-1;
  for(long i=0;i<fetched;++i){auto& c=children[static_cast<std::size_t>(i)];if(c.vt==VT_I4&&name(a,c.lVal)==text)found=c.lVal;
  else if(c.vt==VT_DISPATCH&&c.pdispVal){win::ComPtr<IAccessible> native;if(SUCCEEDED(c.pdispVal->QueryInterface(IID_PPV_ARGS(&native)))&&name(native.Get(),CHILDID_SELF)==text){
   // The search EDIT is the first native child; custom controls retain their enumerated IDs.
   found=i+1;
  }}VariantClear(&c);}return found;
}
void pump(){MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}}
bool zoneDialogNamed=false,zoneDialogRejectsOverlap=false;
void CALLBACK fillZones(HWND,UINT,UINT_PTR, DWORD){
 struct Find{HWND dialog{};} found;EnumThreadWindows(GetCurrentThreadId(),[](HWND h,LPARAM p)->BOOL{if(win::windowText(h)==L"Snap layout editor"){reinterpret_cast<Find*>(p)->dialog=h;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&found));if(!found.dialog)return;
 auto dialog=found.dialog;auto input=GetDlgItem(dialog,201);win::ComPtr<IAccessible> edit;auto hr=AccessibleObjectFromWindow(input,static_cast<DWORD>(OBJID_CLIENT),IID_IAccessible,reinterpret_cast<void**>(edit.GetAddressOf()));if(SUCCEEDED(hr)){BSTR label{};if(SUCCEEDED(edit->get_accName(child(CHILDID_SELF),&label)))zoneDialogNamed=label&&std::wstring(label)==L"Snap layout name";SysFreeString(label);}
 SetDlgItemTextW(dialog,201,L"Accessible two columns");SetDlgItemTextW(dialog,202,L"0,0,60,100\r\n50,0,100,100");zoneDialogRejectsOverlap=!IsWindowEnabled(GetDlgItem(dialog,IDOK));SetDlgItemTextW(dialog,202,L"0,0,50,100\r\n50,0,100,100");SetDlgItemTextW(dialog,203,L"8");PostMessageW(dialog,WM_COMMAND,IDOK,0);
}
}
int main(){
 try{
 SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);win::ComApartment apartment;
 auto directory=std::filesystem::temp_directory_path()/(L"AetherAccessibility-"+std::to_wstring(GetCurrentProcessId()));std::filesystem::create_directories(directory);win::setConfigDirectory(directory);
 App app;auto config=app.config();config.hideTaskbar=false;config.pins={{"Accessible fixture one","C:\\AetherAccessibilityFixture1.exe","fixture1"},{"Accessible fixture two","C:\\AetherAccessibilityFixture2.exe","fixture2"}};app.apply(config);
 ui::Graphics graphics;auto displays=win::monitors();require(!displays.empty(),"No test display");
 win::ComPtr<IAccessible> retained;
 {
 ui::Activities activities(graphics,app);activities.open(displays.front());pump();UpdateWindow(activities.hwnd());auto a=accessible(activities.hwnd());
 require(find(a.Get(),L"Applications")>0&&find(a.Get(),L"Open windows")>0,"Activities tabs are absent from native accessibility tree");
 auto first=find(a.Get(),L"Accessible fixture one");require(first>0,"Activities result absent from native accessibility tree");
 VARIANT role{};require(SUCCEEDED(a->get_accRole(child(first),&role))&&role.vt==VT_I4&&role.lVal==ROLE_SYSTEM_LISTITEM,"Activities result role is incorrect");
 long x{},y{},w{},h{};require(SUCCEEDED(a->accLocation(&x,&y,&w,&h,child(first)))&&w>0&&h>0,"Result lacks screen bounds");VARIANT hit{};require(SUCCEEDED(a->accHitTest(x+w/2,y+h/2,&hit))&&hit.vt==VT_I4&&hit.lVal==first,"Accessible hit test targets wrong result");
 auto search=find(a.Get(),L"Search applications");require(search>0,"Search accessible name missing");win::ComPtr<IDispatch> nativeEdit;require(SUCCEEDED(a->get_accChild(child(search),&nativeEdit))&&nativeEdit,"Native EDIT accessibility lost");
 auto edit=GetDlgItem(activities.hwnd(),10);SetWindowTextW(edit,L"Accessible");SendMessageW(edit,EM_SETSEL,4,4);SendMessageW(edit,WM_KEYDOWN,VK_LEFT,0);DWORD start{},end{};SendMessageW(edit,EM_GETSEL,reinterpret_cast<WPARAM>(&start),reinterpret_cast<LPARAM>(&end));require(start==3&&end==3,"Search Left arrow did not move the native caret");
 SendMessageW(edit,WM_KEYDOWN,VK_TAB,0);SendMessageW(activities.hwnd(),WM_KEYDOWN,VK_TAB,0);SendMessageW(activities.hwnd(),WM_KEYDOWN,VK_TAB,0);SendMessageW(activities.hwnd(),WM_KEYDOWN,VK_END,0);a=accessible(activities.hwnd());VARIANT focus{};a->get_accFocus(&focus);require(focus.vt==VT_I4&&name(a.Get(),focus.lVal)==L"Accessible fixture two","Tab/End did not focus the final result");
 require(a->get_accName(child(999999),nullptr)==E_POINTER,"Null output was not rejected");BSTR invalid{};require(a->get_accName(child(999999),&invalid)==E_INVALIDARG,"Invalid child ID was accepted");retained=a;
 }
 long count{};require(FAILED(retained->get_accChildCount(&count)),"Retained accessible object remained attached after HWND destruction");
 ui::Editor editor(graphics,app);editor.open(displays.front());pump();UpdateWindow(editor.hwnd());auto studio=accessible(editor.hwnd());require(find(studio.Get(),L"Decrease corner radius")>0,"Studio symbol control lacks descriptive name");
  auto appearanceForMove=find(studio.Get(),L"Colors & themes");long moveX{},moveY{},moveW{},moveH{};require(SUCCEEDED(studio->accLocation(&moveX,&moveY,&moveW,&moveH,child(appearanceForMove))),"Retained Studio node has no initial bounds");RECT studioBounds{};GetWindowRect(editor.hwnd(),&studioBounds);SetWindowPos(editor.hwnd(),nullptr,studioBounds.left+17,studioBounds.top+11,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOREDRAW);long movedX{},movedY{},movedW{},movedH{};require(SUCCEEDED(studio->accLocation(&movedX,&movedY,&movedW,&movedH,child(appearanceForMove)))&&movedX==moveX+17&&movedY==moveY+11&&movedW==moveW&&movedH==moveH,"Retained COM bounds are stale after owner move without repaint");VARIANT movedHit{};require(SUCCEEDED(studio->accHitTest(movedX+movedW/2,movedY+movedH/2,&movedHit))&&movedHit.vt==VT_I4&&movedHit.lVal==appearanceForMove,"Retained COM hit testing uses stale owner origin");
 auto decrease=find(studio.Get(),L"Decrease corner radius");int prior=app.config().theme.radius;require(SUCCEEDED(studio->accSelect(SELFLAG_TAKEFOCUS,child(decrease))),"Native accessibility client could not focus Studio control");require(SUCCEEDED(studio->accDoDefaultAction(child(decrease)))&&app.config().theme.radius==std::max(0,prior-2),"MSAA default action did not change the actual Studio setting");
 auto appearance=find(studio.Get(),L"Colors & themes");VARIANT role{};studio->get_accRole(child(appearance),&role);require(role.vt==VT_I4&&role.lVal==ROLE_SYSTEM_PAGETAB,"Studio page is not exposed as a tab");
  auto layoutPage=find(studio.Get(),L"Displays & layout");require(SUCCEEDED(studio->accDoDefaultAction(child(layoutPage))),"Accessible page action did not switch Studio pages");UpdateWindow(editor.hwnd());studio=accessible(editor.hwnd());auto automatic=find(studio.Get(),L"Automatic tiling");require(automatic>0,"Studio tiling policy is missing from accessibility tree");require(SUCCEEDED(studio->accSelect(SELFLAG_TAKEFOCUS,child(automatic))),"Could not scroll focused tiling control into view");VARIANT automaticState{};studio->get_accState(child(automatic),&automaticState);require(automaticState.vt==VT_I4&&!(automaticState.lVal&STATE_SYSTEM_CHECKED),"Automatic tiling default is not off");
 auto add=find(studio.Get(),L"Add snap layout");require(add>0&&SUCCEEDED(studio->accSelect(SELFLAG_TAKEFOCUS,child(add))),"Custom zone editor action not keyboard reachable");auto zoneTimer=SetTimer(nullptr,0,20,fillZones);auto zoneHr=studio->accDoDefaultAction(child(add));KillTimer(nullptr,zoneTimer); if(zoneHr!=S_OK||!zoneDialogNamed||!zoneDialogRejectsOverlap){VARIANT state{};studio->get_accState(child(add),&state);std::cerr<<"zone editor diagnostic hr="<<std::hex<<zoneHr<<std::dec<<" named="<<zoneDialogNamed<<" overlapDisabled="<<zoneDialogRejectsOverlap<<" state="<<(state.vt==VT_I4?state.lVal:-1)<<'\n';}
 require(zoneHr==S_OK,"Native zone editor default action was not invoked");require(zoneDialogNamed,"Native snap name EDIT accessibility label is missing");require(zoneDialogRejectsOverlap,"Native snap editor Save stayed enabled for overlapping zones");require(app.config().snapLayouts.size()==1&&app.config().snapLayouts.front().name=="Accessible two columns"&&app.config().snapLayouts.front().zones.size()==2,"Accessible zone dialog did not save actual normalized layout");
 UpdateWindow(editor.hwnd());studio=accessible(editor.hwnd());auto remove=find(studio.Get(),L"Remove snap layout Accessible two columns");require(remove>0&&SUCCEEDED(studio->accSelect(SELFLAG_TAKEFOCUS,child(remove)))&&SUCCEEDED(studio->accDoDefaultAction(child(remove)))&&app.config().snapLayouts.empty(),"Accessible Remove did not delete saved custom zones");
 ui::QuickSettings quick(graphics,app);quick.open(displays.front());pump();UpdateWindow(quick.hwnd());auto settings=accessible(quick.hwnd());require(find(settings.Get(),L"Output volume")>0&&find(settings.Get(),L"Desktop Studio")>0,"Quick Settings actions absent from native accessibility tree");
 auto volume=find(settings.Get(),L"Output volume");VARIANT volumeRole{};settings->get_accRole(child(volume),&volumeRole);require(volumeRole.vt==VT_I4&&volumeRole.lVal==ROLE_SYSTEM_SLIDER,"Output volume has no slider semantics");BSTR volumeValue{};require(SUCCEEDED(settings->get_accValue(child(volume),&volumeValue))&&volumeValue&&SysStringLen(volumeValue)>0,"Output volume has no accessible value");SysFreeString(volumeValue);
 require(SUCCEEDED(settings->accSelect(SELFLAG_TAKEFOCUS,child(find(settings.Get(),L"Desktop Studio")))),"Quick Settings action could not receive accessible focus");SendMessageW(quick.hwnd(),WM_KEYDOWN,VK_TAB,0);VARIANT quickFocus{};settings=accessible(quick.hwnd());settings->get_accFocus(&quickFocus);require(quickFocus.vt==VT_I4&&name(settings.Get(),quickFocus.lVal)==(app.status().volume>=0?L"Mute output":L"Network settings"),"Quick Settings Tab does not wrap to an enabled action");
 win::ComPtr<IUIAutomation> automation;if(SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&automation)))){win::ComPtr<IUIAutomationElement> element;require(SUCCEEDED(automation->ElementFromHandle(quick.hwnd(),&element))&&element,"Windows UIA bridge could not discover Quick Settings");std::cout<<"PASS Windows UIA bridge root discovery (legacy MSAA provider)\n";}else std::cout<<"SKIP UIA client unavailable\n";
 win::setConfigDirectory({});std::error_code error;std::filesystem::remove_all(directory,error);std::cout<<"PASS native accessibility, keyboard focus and retained lifetime\n";return 0;
 }catch(const std::exception& e){win::setConfigDirectory({});std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
