#include "accessibility.hpp"
#include <atomic>
#include <cmath>
namespace aether::ui {
namespace {
// Values from the installed Windows SDK oleacc.h. Not exported by every uuid.lib.
constexpr CLSID AccPropertyServices{0xb5f8350b,0x0548,0x48b1,{0xa6,0xee,0x88,0xbd,0x00,0xb4,0xa5,0xe7}};
constexpr GUID AccNameProperty{0x608d3df8,0x8128,0x4aa7,{0xa4,0x28,0xf5,0x5e,0x49,0x26,0x72,0x91}};
}
AccessibleNode accessibleNode(HWND owner,long id,std::wstring name,long role,long state,D2D1_RECT_F r,float drawingScale){
 POINT origin{};ClientToScreen(owner,&origin);const float factor=win::dpiFor(owner)/96.f*drawingScale;
 AccessibleNode node;node.id=id;node.name=std::move(name);node.role=role;node.state=state;
 node.bounds={origin.x+static_cast<LONG>(std::lround(r.left*factor)),origin.y+static_cast<LONG>(std::lround(r.top*factor)),origin.x+static_cast<LONG>(std::lround(r.right*factor)),origin.y+static_cast<LONG>(std::lround(r.bottom*factor))};return node;
}
void accessibleNativeName(HWND h,const std::wstring& name){
 win::ComPtr<IAccPropServices> properties;
 if(SUCCEEDED(CoCreateInstance(AccPropertyServices,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&properties))))properties->SetHwndPropStr(h,static_cast<DWORD>(OBJID_CLIENT),CHILDID_SELF,AccNameProperty,name.c_str());
}
struct Accessibility::State {HWND owner{};std::wstring name;long role=ROLE_SYSTEM_CLIENT,focused=0,selected=0;bool attached=true;std::vector<AccessibleNode> nodes;};
namespace {
class Provider final:public IAccessible,public IEnumVARIANT {
 std::atomic<ULONG> references_{1};std::shared_ptr<Accessibility::State> state_;ULONG position_=0;
 HRESULT alive() const{return state_->attached&&IsWindow(state_->owner)?S_OK:CO_E_OBJNOTCONNECTED;}
 HRESULT node(VARIANT id,const AccessibleNode*& out) const {out=nullptr;if(FAILED(alive()))return CO_E_OBJNOTCONNECTED;if(id.vt!=VT_I4)return E_INVALIDARG;if(id.lVal==CHILDID_SELF)return S_OK;auto found=std::find_if(state_->nodes.begin(),state_->nodes.end(),[&](const auto& item){return item.id==id.lVal;});if(found==state_->nodes.end())return E_INVALIDARG;out=&*found;return S_OK;}
 RECT screenBounds(const AccessibleNode& item) const{auto bounds=item.bounds;POINT origin{};ClientToScreen(state_->owner,&origin);OffsetRect(&bounds,origin.x,origin.y);return bounds;}
 HRESULT string(VARIANT id,BSTR* out,int kind){if(!out)return E_POINTER;*out=nullptr;const AccessibleNode* item{};auto hr=node(id,item);if(FAILED(hr))return hr;const auto& value=kind==0?(item?item->name:state_->name):kind==1?(item?item->description:empty()):kind==2?(item?item->defaultAction:empty()):kind==3?(item?item->value:empty()):(item?item->shortcut:empty());if(value.empty())return S_FALSE;*out=SysAllocStringLen(value.data(),static_cast<UINT>(value.size()));return *out?S_OK:E_OUTOFMEMORY;}
 static const std::wstring& empty(){static const std::wstring value;return value;}
public:
 explicit Provider(std::shared_ptr<Accessibility::State> s):state_(std::move(s)){}
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override{if(!out)return E_POINTER;*out=nullptr;if(id==IID_IUnknown||id==IID_IDispatch||id==IID_IAccessible)*out=static_cast<IAccessible*>(this);else if(id==IID_IEnumVARIANT)*out=static_cast<IEnumVARIANT*>(this);else return E_NOINTERFACE;AddRef();return S_OK;}
 ULONG STDMETHODCALLTYPE AddRef() override{return ++references_;}ULONG STDMETHODCALLTYPE Release() override{auto result=--references_;if(!result)delete this;return result;}
 HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT* n) override{if(!n)return E_POINTER;*n=0;return S_OK;}
 HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT,LCID,ITypeInfo** p) override{if(!p)return E_POINTER;*p=nullptr;return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID,LPOLESTR*,UINT,LCID,DISPID*) override{return DISP_E_UNKNOWNNAME;}
 HRESULT STDMETHODCALLTYPE Invoke(DISPID,REFIID,LCID,WORD,DISPPARAMS*,VARIANT*,EXCEPINFO*,UINT*) override{return DISP_E_MEMBERNOTFOUND;}
 HRESULT STDMETHODCALLTYPE get_accParent(IDispatch** out) override{if(!out)return E_POINTER;*out=nullptr;if(FAILED(alive()))return CO_E_OBJNOTCONNECTED;return AccessibleObjectFromWindow(state_->owner,static_cast<DWORD>(OBJID_WINDOW),IID_IDispatch,reinterpret_cast<void**>(out));}
 HRESULT STDMETHODCALLTYPE get_accChildCount(long* out) override{if(!out)return E_POINTER;*out=0;if(FAILED(alive()))return CO_E_OBJNOTCONNECTED;*out=static_cast<long>(state_->nodes.size());return S_OK;}
 HRESULT STDMETHODCALLTYPE get_accChild(VARIANT id,IDispatch** out) override{if(!out)return E_POINTER;*out=nullptr;const AccessibleNode* item{};auto hr=node(id,item);if(FAILED(hr))return hr;if(!item||!item->native)return S_FALSE;return AccessibleObjectFromWindow(item->native,static_cast<DWORD>(OBJID_CLIENT),IID_IDispatch,reinterpret_cast<void**>(out));}
 HRESULT STDMETHODCALLTYPE get_accName(VARIANT id,BSTR* out) override{return string(id,out,0);}
 HRESULT STDMETHODCALLTYPE get_accValue(VARIANT id,BSTR* out) override{return string(id,out,3);}
 HRESULT STDMETHODCALLTYPE get_accDescription(VARIANT id,BSTR* out) override{return string(id,out,1);}
 HRESULT STDMETHODCALLTYPE get_accRole(VARIANT id,VARIANT* out) override{if(!out)return E_POINTER;VariantInit(out);const AccessibleNode* item{};auto hr=node(id,item);if(FAILED(hr))return hr;out->vt=VT_I4;out->lVal=item?item->role:state_->role;return S_OK;}
 HRESULT STDMETHODCALLTYPE get_accState(VARIANT id,VARIANT* out) override{if(!out)return E_POINTER;VariantInit(out);const AccessibleNode* item{};auto hr=node(id,item);if(FAILED(hr))return hr;out->vt=VT_I4;out->lVal=item?item->state:0;if(!IsWindowVisible(state_->owner))out->lVal|=STATE_SYSTEM_INVISIBLE|STATE_SYSTEM_OFFSCREEN;if(item){if(state_->focused==item->id)out->lVal|=STATE_SYSTEM_FOCUSED;if(state_->selected==item->id)out->lVal|=STATE_SYSTEM_SELECTED;}return S_OK;}
 HRESULT STDMETHODCALLTYPE get_accHelp(VARIANT,BSTR* out) override{if(!out)return E_POINTER;*out=nullptr;return S_FALSE;}
 HRESULT STDMETHODCALLTYPE get_accHelpTopic(BSTR* out,VARIANT,long* topic) override{if(!out||!topic)return E_POINTER;*out=nullptr;*topic=-1;return S_FALSE;}
 HRESULT STDMETHODCALLTYPE get_accKeyboardShortcut(VARIANT id,BSTR* out) override{return string(id,out,4);}
 HRESULT STDMETHODCALLTYPE get_accFocus(VARIANT* out) override{if(!out)return E_POINTER;VariantInit(out);if(FAILED(alive()))return CO_E_OBJNOTCONNECTED;if(!state_->focused)return S_FALSE;out->vt=VT_I4;out->lVal=state_->focused;return S_OK;}
 HRESULT STDMETHODCALLTYPE get_accSelection(VARIANT* out) override{if(!out)return E_POINTER;VariantInit(out);if(FAILED(alive()))return CO_E_OBJNOTCONNECTED;if(!state_->selected)return S_FALSE;out->vt=VT_I4;out->lVal=state_->selected;return S_OK;}
 HRESULT STDMETHODCALLTYPE get_accDefaultAction(VARIANT id,BSTR* out) override{return string(id,out,2);}
 HRESULT STDMETHODCALLTYPE accSelect(long flags,VARIANT id) override{const AccessibleNode* item{};auto hr=node(id,item);if(FAILED(hr))return hr;if(!item)return E_INVALIDARG;if(flags==0||(flags&~(SELFLAG_TAKEFOCUS|SELFLAG_TAKESELECTION)))return E_INVALIDARG;if(item->state&STATE_SYSTEM_UNAVAILABLE)return S_FALSE;auto action=item->focus;if(!action)return S_FALSE;action();return S_OK;}
 HRESULT STDMETHODCALLTYPE accLocation(long* x,long* y,long* w,long* h,VARIANT id) override{if(!x||!y||!w||!h)return E_POINTER;*x=*y=*w=*h=0;const AccessibleNode* item{};auto hr=node(id,item);if(FAILED(hr))return hr;RECT bounds{};if(item)bounds=screenBounds(*item);else GetWindowRect(state_->owner,&bounds);*x=bounds.left;*y=bounds.top;*w=bounds.right-bounds.left;*h=bounds.bottom-bounds.top;return S_OK;}
 HRESULT STDMETHODCALLTYPE accNavigate(long direction,VARIANT id,VARIANT* out) override{if(!out)return E_POINTER;VariantInit(out);const AccessibleNode* item{};auto hr=node(id,item);if(FAILED(hr))return hr;if(state_->nodes.empty())return S_FALSE;long index=item?static_cast<long>(item-state_->nodes.data()):-1;if(direction==NAVDIR_FIRSTCHILD&&!item)index=0;else if(direction==NAVDIR_LASTCHILD&&!item)index=static_cast<long>(state_->nodes.size())-1;else if(direction==NAVDIR_NEXT&&item)++index;else if(direction==NAVDIR_PREVIOUS&&item)--index;else return S_FALSE;if(index<0||static_cast<std::size_t>(index)>=state_->nodes.size())return S_FALSE;out->vt=VT_I4;out->lVal=state_->nodes[static_cast<std::size_t>(index)].id;return S_OK;}
 HRESULT STDMETHODCALLTYPE accHitTest(long x,long y,VARIANT* out) override{if(!out)return E_POINTER;VariantInit(out);if(FAILED(alive()))return CO_E_OBJNOTCONNECTED;POINT point{x,y};for(const auto& item:state_->nodes)if(!(item.state&(STATE_SYSTEM_OFFSCREEN|STATE_SYSTEM_INVISIBLE))&&[&]{auto bounds=screenBounds(item);return PtInRect(&bounds,point)!=FALSE;}()){out->vt=VT_I4;out->lVal=item.id;return S_OK;}RECT bounds{};GetWindowRect(state_->owner,&bounds);if(PtInRect(&bounds,point)){out->vt=VT_I4;out->lVal=CHILDID_SELF;return S_OK;}return S_FALSE;}
 HRESULT STDMETHODCALLTYPE accDoDefaultAction(VARIANT id) override{const AccessibleNode* item{};auto hr=node(id,item);if(FAILED(hr))return hr;if(!item)return S_FALSE;if(item->state&(STATE_SYSTEM_UNAVAILABLE|STATE_SYSTEM_OFFSCREEN|STATE_SYSTEM_INVISIBLE))return S_FALSE;auto action=item->invoke;if(!action)return S_FALSE;action();return S_OK;}
 HRESULT STDMETHODCALLTYPE put_accName(VARIANT,BSTR) override{return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE put_accValue(VARIANT id,BSTR text) override{const AccessibleNode* item{};auto hr=node(id,item);if(FAILED(hr))return hr;if(!item||!item->setValue)return E_NOTIMPL;if(item->state&STATE_SYSTEM_UNAVAILABLE)return S_FALSE;auto action=item->setValue;return action(text?text:L"")?S_OK:E_INVALIDARG;}
 HRESULT STDMETHODCALLTYPE Next(ULONG count,VARIANT* out,ULONG* fetched) override{if(!out||(!fetched&&count!=1))return E_POINTER;if(fetched)*fetched=0;if(FAILED(alive()))return CO_E_OBJNOTCONNECTED;ULONG done=0;while(done<count&&position_<state_->nodes.size()){VariantInit(&out[done]);out[done].vt=VT_I4;out[done].lVal=state_->nodes[position_++].id;++done;}if(fetched)*fetched=done;return done==count?S_OK:S_FALSE;}
 HRESULT STDMETHODCALLTYPE Skip(ULONG count) override{if(FAILED(alive()))return CO_E_OBJNOTCONNECTED;auto next=static_cast<std::size_t>(position_)+count;position_=static_cast<ULONG>(std::min(next,state_->nodes.size()));return next<=state_->nodes.size()?S_OK:S_FALSE;}
 HRESULT STDMETHODCALLTYPE Reset() override{position_=0;return alive();}
 HRESULT STDMETHODCALLTYPE Clone(IEnumVARIANT** out) override{if(!out)return E_POINTER;auto* clone=new Provider(state_);clone->position_=position_;*out=clone;return S_OK;}
};
}
Accessibility::Accessibility(HWND owner):state_(std::make_shared<State>()){state_->owner=owner;provider_.Attach(new Provider(state_));}
Accessibility::~Accessibility(){detach();}
void Accessibility::detach(){if(!state_||!state_->attached)return;state_->attached=false;state_->nodes.clear();state_->owner=nullptr;provider_.Reset();}
void Accessibility::snapshot(std::wstring name,long role,std::vector<AccessibleNode> nodes,long focused,long selected){
 if(!state_->attached)return;bool reordered=nodes.size()!=state_->nodes.size();if(!reordered)for(std::size_t i=0;i<nodes.size();++i)if(nodes[i].id!=state_->nodes[i].id||nodes[i].name!=state_->nodes[i].name){reordered=true;break;}
 std::vector<long> changed,values;for(const auto& item:nodes){auto old=std::find_if(state_->nodes.begin(),state_->nodes.end(),[&](const auto& n){return n.id==item.id;});if(old!=state_->nodes.end()){if(old->state!=item.state)changed.push_back(item.id);if(old->value!=item.value)values.push_back(item.id);}}
 POINT origin{};ClientToScreen(state_->owner,&origin);for(auto& item:nodes)OffsetRect(&item.bounds,-origin.x,-origin.y);
 const long previousFocus=state_->focused,previousSelection=state_->selected;state_->name=std::move(name);state_->role=role;state_->nodes=std::move(nodes);state_->focused=focused;state_->selected=selected;
 if(reordered)NotifyWinEvent(EVENT_OBJECT_REORDER,state_->owner,OBJID_CLIENT,CHILDID_SELF);
 for(auto id:changed)NotifyWinEvent(EVENT_OBJECT_STATECHANGE,state_->owner,OBJID_CLIENT,id);
 for(auto id:values)NotifyWinEvent(EVENT_OBJECT_VALUECHANGE,state_->owner,OBJID_CLIENT,id);
 if(selected!=previousSelection){if(previousSelection)NotifyWinEvent(EVENT_OBJECT_STATECHANGE,state_->owner,OBJID_CLIENT,previousSelection);if(selected)NotifyWinEvent(EVENT_OBJECT_SELECTION,state_->owner,OBJID_CLIENT,selected);}
 if(focused&&focused!=previousFocus)NotifyWinEvent(EVENT_OBJECT_FOCUS,state_->owner,OBJID_CLIENT,focused);
}
LRESULT Accessibility::object(WPARAM w,LPARAM l,bool& handled){handled=false;if(static_cast<DWORD>(l)!=static_cast<DWORD>(OBJID_CLIENT)||!state_->attached||state_->name.empty())return 0;handled=true;return LresultFromObject(IID_IAccessible,w,provider_.Get());}
}
