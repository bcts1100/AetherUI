#include "canvas.hpp"
#include "platform/config.hpp"
#include "platform/backdrop.hpp"
#include <cmath>
namespace aether::ui {
Graphics::Graphics(){if(FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,factory_.GetAddressOf())))throw std::runtime_error("Direct2D could not initialize.");if(FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(writer_.GetAddressOf()))))throw std::runtime_error("DirectWrite could not initialize.");}
IDWriteTextFormat* Graphics::font(int size,bool bold,int align,bool symbol,std::wstring_view family,int weight,bool italic){
 size=std::clamp(size,1,64);align=std::clamp(align,0,2);
 auto name=symbol?std::wstring(L"Segoe MDL2 Assets"):validFontFamily(win::utf8(family))?std::wstring(family):std::wstring(L"Segoe UI");
 weight=symbol?400:std::clamp(bold?std::max(600,weight):weight,300,700);italic=italic&&!symbol;auto key=std::make_tuple(name,size,weight,align,symbol,italic);auto it=fonts_.find(key);if(it!=fonts_.end())return it->second.Get();
 ComPtr<IDWriteTextFormat> f;HRESULT hr=writer_->CreateTextFormat(name.c_str(),nullptr,static_cast<DWRITE_FONT_WEIGHT>(weight),italic?DWRITE_FONT_STYLE_ITALIC:DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,static_cast<float>(size),L"en-us",&f);if(FAILED(hr))return nullptr;
 f->SetTextAlignment(align==1?DWRITE_TEXT_ALIGNMENT_CENTER:align==2?DWRITE_TEXT_ALIGNMENT_TRAILING:DWRITE_TEXT_ALIGNMENT_LEADING);f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
 DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER,0,0};ComPtr<IDWriteInlineObject> ellipsis;if(SUCCEEDED(writer_->CreateEllipsisTrimmingSign(f.Get(),&ellipsis)))f->SetTrimming(&trim,ellipsis.Get());
 // Family and size changes cannot grow the shared cache without a bound.
 if(fonts_.size()>=192)fonts_.clear();auto* result=f.Get();fonts_.emplace(std::move(key),std::move(f));return result;
}
bool Canvas::begin(HWND h,std::uint32_t bg,bool translucent){if(translucent_!=translucent){release();translucent_=translucent;}if(!target_){RECT r{};GetClientRect(h,&r);float dpi=static_cast<float>(win::dpiFor(h));auto props=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,translucent_?D2D1_ALPHA_MODE_PREMULTIPLIED:D2D1_ALPHA_MODE_IGNORE),dpi,dpi);auto hp=D2D1::HwndRenderTargetProperties(h,D2D1::SizeU(static_cast<UINT>(std::max(1L,r.right)),static_cast<UINT>(std::max(1L,r.bottom))));if(FAILED(graphics_.factory()->CreateHwndRenderTarget(props,hp,&target_)))return false;if(FAILED(target_->CreateSolidColorBrush(color(0),&brush_))){release();return false;}}
 drawingScale_=1.f;target_->SetTransform(D2D1::Matrix3x2F::Identity());target_->BeginDraw();target_->Clear(color(bg,translucent_?0.f:1.f));target_->SetTextAntialiasMode(translucent_?D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE:D2D1_TEXT_ANTIALIAS_MODE_DEFAULT);return true;}
void Canvas::setScale(float scale){drawingScale_=std::clamp(scale,.1f,1.f);if(target_)target_->SetTransform(D2D1::Matrix3x2F::Scale(drawingScale_,drawingScale_));}
void Canvas::end(HWND h){if(target_&&FAILED(target_->EndDraw())){release();win::log("Direct2D target lost; it will be recreated on the next paint.");InvalidateRect(h,nullptr,FALSE);}}
void Canvas::resize(HWND h){if(target_){RECT r{};GetClientRect(h,&r);target_->SetDpi(static_cast<float>(win::dpiFor(h)),static_cast<float>(win::dpiFor(h)));if(FAILED(target_->Resize(D2D1::SizeU(static_cast<UINT>(std::max(1L,r.right)),static_cast<UINT>(std::max(1L,r.bottom))))))release();}}
void Canvas::release(){images_.clear();gradient_.Reset();gradientColors_={};brush_.Reset();target_.Reset();}
void Canvas::fill(D2D1_RECT_F r,std::uint32_t c,float radius,float alpha){if(!target_)return;brush_->SetColor(color(c,alpha));if(radius>0)target_->FillRoundedRectangle(D2D1::RoundedRect(r,radius,radius),brush_.Get());else target_->FillRectangle(r,brush_.Get());}
void Canvas::surface(D2D1_RECT_F r,const Theme& theme,float radius){
 if(!target_)return;radius=std::clamp(radius,0.f,28.f);int strength=std::clamp(theme.gradientStrength,0,100);
 if(strength){
  auto blend=[&](int shift){int a=(theme.surface>>shift)&255,b=(theme.raised>>shift)&255;return static_cast<std::uint32_t>((a*(100-strength)+b*strength)/100);};
  auto first=(blend(16)<<16)|(blend(8)<<8)|blend(0);auto key=std::make_pair(first,theme.surface);
  if(!gradient_||gradientColors_!=key){
   gradient_.Reset();D2D1_GRADIENT_STOP stops[]={{0,color(first,translucent_?std::clamp(theme.surfaceOpacity,20,100)/100.f:1.f)},{1,color(theme.surface,translucent_?std::clamp(theme.surfaceOpacity,20,100)/100.f:1.f)}};ComPtr<ID2D1GradientStopCollection> collection;
   if(SUCCEEDED(target_->CreateGradientStopCollection(stops,2,&collection))&&SUCCEEDED(target_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0,0),D2D1::Point2F(0,1)),collection.Get(),&gradient_)))gradientColors_=key;
  }
  if(gradient_){gradient_->SetStartPoint(D2D1::Point2F(r.left,r.top));gradient_->SetEndPoint(D2D1::Point2F(r.left,r.bottom));
   if(radius>0)target_->FillRoundedRectangle(D2D1::RoundedRect(r,radius,radius),gradient_.Get());else target_->FillRectangle(r,gradient_.Get());
  }else fill(r,theme.surface,radius);
 }else fill(r,theme.surface,radius,translucent_?std::clamp(theme.surfaceOpacity,20,100)/100.f:1.f);
 float border=static_cast<float>(std::clamp(theme.borderWidth,0,3));
 if(border>0&&r.right-r.left>border&&r.bottom-r.top>border){auto edge=D2D1::RectF(r.left+border/2,r.top+border/2,r.right-border/2,r.bottom-border/2);
  stroke(edge,theme.style==ThemeStyle::Frosted?theme.muted:theme.raised,radius,border,theme.style==ThemeStyle::Frosted?.5f:1.f);
 }
}
void Canvas::stroke(D2D1_RECT_F r,std::uint32_t c,float radius,float width,float alpha){if(!target_)return;brush_->SetColor(color(c,alpha));if(radius>0)target_->DrawRoundedRectangle(D2D1::RoundedRect(r,radius,radius),brush_.Get(),width);else target_->DrawRectangle(r,brush_.Get(),width);}
void Canvas::line(float x1,float y1,float x2,float y2,std::uint32_t c,float width){if(!target_)return;brush_->SetColor(color(c));target_->DrawLine(D2D1::Point2F(x1,y1),D2D1::Point2F(x2,y2),brush_.Get(),width);}
void Canvas::circle(float x,float y,float r,std::uint32_t c){if(!target_)return;brush_->SetColor(color(c));target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x,y),r,r),brush_.Get());}
void Canvas::ribbon(float width,float y,float height,float bend,float phase,std::uint32_t rgb,float alpha){
 if(!target_)return;ComPtr<ID2D1PathGeometry> path;ComPtr<ID2D1GeometrySink> sink;
 if(FAILED(graphics_.factory()->CreatePathGeometry(&path))||FAILED(path->Open(&sink)))return;
 const float a=std::sin(phase)*bend,b=std::cos(phase*.73f)*bend;
 sink->BeginFigure(D2D1::Point2F(-width*.1f,y),D2D1_FIGURE_BEGIN_FILLED);
 sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(width*.25f,y+a),D2D1::Point2F(width*.7f,y+b),D2D1::Point2F(width*1.1f,y+a*.3f)));
 sink->AddLine(D2D1::Point2F(width*1.1f,y+height+a*.3f));
 sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(width*.7f,y+height+b),D2D1::Point2F(width*.25f,y+height+a),D2D1::Point2F(-width*.1f,y+height)));
 sink->EndFigure(D2D1_FIGURE_END_CLOSED);if(FAILED(sink->Close()))return;brush_->SetColor(color(rgb,alpha));target_->FillGeometry(path.Get(),brush_.Get());
}
void Canvas::text(std::wstring_view s,D2D1_RECT_F r,std::uint32_t c,int size,bool bold,int align,bool symbol){if(!target_||s.empty())return;const auto& choice=textTheme_.fonts[static_cast<std::size_t>(fontPart_)];const bool custom=fontPart_!=FontPart::General&&!choice.family.empty();int scaled=symbol?size:static_cast<int>(std::lround(size*std::clamp(custom?choice.scale:textTheme_.fontScale,85,125)/100.f));auto* font=graphics_.font(scaled,custom?false:bold,align,symbol,win::wide(custom?choice.family:textTheme_.fontFamily),custom?choice.weight:textTheme_.fontWeight,custom?choice.italic:textTheme_.fontItalic);if(!font)return;brush_->SetColor(color(c));target_->DrawText(s.data(),static_cast<UINT32>(s.size()),font,r,brush_.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP);}
void Canvas::pushClip(D2D1_RECT_F r){if(target_)target_->PushAxisAlignedClip(r,D2D1_ANTIALIAS_MODE_ALIASED);}
void Canvas::popClip(){if(target_)target_->PopAxisAlignedClip();}
void Canvas::glyph(wchar_t ch,D2D1_RECT_F r,std::uint32_t c,int size){text(std::wstring(1,ch),r,c,size,false,1,true);}
void Canvas::grid(D2D1_RECT_F r,std::uint32_t c){float x=(r.left+r.right)/2,y=(r.top+r.bottom)/2;for(int row=-1;row<=1;++row)for(int col=-1;col<=1;++col)circle(x+col*7.f,y+row*7.f,1.6f,c);}
void Canvas::icon(const std::shared_ptr<const win::IconPixels>& image,D2D1_RECT_F r,std::uint32_t fallback){if(!target_)return;if(!image){glyph(L'\uE737',r,fallback,24);return;}auto it=images_.find(image->serial);if(it==images_.end()){if(images_.size()>=128)images_.clear();ComPtr<ID2D1Bitmap> bmp;auto props=D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));if(FAILED(target_->CreateBitmap(D2D1::SizeU(image->width,image->height),image->bytes.data(),image->width*4,props,&bmp)))return;it=images_.emplace(image->serial,std::move(bmp)).first;}target_->DrawBitmap(it->second.Get(),r,1.f,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);}
void Canvas::imageRegion(const std::shared_ptr<const win::IconPixels>& image,D2D1_RECT_F destination,D2D1_RECT_F source,float opacity){
 if(!target_||!image)return;auto found=images_.find(image->serial);if(found==images_.end())return;
 target_->DrawBitmap(found->second.Get(),destination,std::clamp(opacity,0.f,1.f),D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,source);
}
void Canvas::ellipse(float x,float y,float rx,float ry,std::uint32_t rgb,float opacity,bool outline){
 if(!target_)return;brush_->SetColor(color(rgb,std::clamp(opacity,0.f,1.f)));auto e=D2D1::Ellipse(D2D1::Point2F(x,y),rx,ry);if(outline)target_->DrawEllipse(e,brush_.Get(),.7f);else target_->FillEllipse(e,brush_.Get());
}
LRESULT Surface::event(UINT message,WPARAM,LPARAM,bool& handled){if(message==WM_CLOSE){hide();handled=true;return 0;}handled=false;return 0;}
bool Surface::create(const wchar_t* title,DWORD style,DWORD ex,Rect r){static bool registered=false;if(!registered){WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=procedure;wc.lpszClassName=L"Aether.Surface.v3";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=LoadIconW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(101));if(!wc.hIcon)wc.hIcon=LoadIconW(nullptr,IDI_APPLICATION);if(!RegisterClassExW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return false;registered=true;}hwnd_=CreateWindowExW(ex,L"Aether.Surface.v3",title,style,r.left,r.top,r.width(),r.height(),nullptr,nullptr,GetModuleHandleW(nullptr),this);return hwnd_!=nullptr;}
void Surface::destroy(){if(hwnd_){auto h=hwnd_;DestroyWindow(h);hwnd_=nullptr;}canvas_.release();}
void Surface::show(bool active){if(hwnd_){ShowWindow(hwnd_,active?SW_SHOW:SW_SHOWNOACTIVATE);if(active)SetForegroundWindow(hwnd_);}}
void Surface::hide(){if(hwnd_&&IsWindowVisible(hwnd_)){ShowWindow(hwnd_,SW_HIDE);auto controller=FindWindowW(L"Aether.Controller.v3",L"Aether UI Controller");DWORD owner=0;GetWindowThreadProcessId(controller,&owner);if(controller&&controller!=hwnd_&&owner==GetCurrentProcessId())PostMessageW(controller,win::RefreshMessage,0,0);}}
void Surface::move(Rect r,bool showWindow){if(hwnd_)SetWindowPos(hwnd_,nullptr,r.left,r.top,r.width(),r.height(),SWP_NOACTIVATE|SWP_NOZORDER|(showWindow?SWP_SHOWWINDOW:0));}
void Surface::updateRoundedRegion(){if(!hwnd_||roundedRadius_<0)return;RECT r{};GetClientRect(hwnd_,&r);int dpi=win::dpiFor(hwnd_);const Rect bounds{roundedRadius_,0,r.right,r.bottom};if(bounds==roundedBounds_&&dpi==roundedDpi_)return;roundedBounds_=bounds;roundedDpi_=dpi;win::roundedWindow(hwnd_,roundedRadius_);}
void Surface::invalidate(){if(hwnd_&&IsWindowVisible(hwnd_))InvalidateRect(hwnd_,nullptr,FALSE);}
LRESULT CALLBACK Surface::procedure(HWND h,UINT m,WPARAM w,LPARAM l){Surface* self=reinterpret_cast<Surface*>(GetWindowLongPtrW(h,GWLP_USERDATA));if(m==WM_NCCREATE){self=static_cast<Surface*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));self->hwnd_=h;}if(!self)return DefWindowProcW(h,m,w,l);
 try{bool handled=false;auto result=self->event(m,w,l,handled);if(handled)return result;switch(m){case WM_ERASEBKGND:return 1;case WM_SETTINGCHANGE:case WM_DWMCOMPOSITIONCHANGED:self->backdropKnown_=false;self->invalidate();return 0;case WM_PAINT:{PAINTSTRUCT ps{};BeginPaint(h,&ps);bool requested=self->theme_.style==ThemeStyle::Frosted;if(!self->backdropKnown_||requested!=self->backdropRequested_){self->backdropActive_=win::applyBackdrop(h,requested)==win::BackdropState::Blur;self->backdropRequested_=requested;self->backdropKnown_=true;SetPropW(h,L"Aether.BackdropActive",reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(self->backdropActive_?1:0)));}bool ready=self->canvas_.begin(h,self->theme_.background,self->backdropActive_);if(!ready&&self->backdropActive_){win::applyBackdrop(h,false);self->backdropActive_=false;RemovePropW(h,L"Aether.BackdropActive");ready=self->canvas_.begin(h,self->theme_.background,false);}if(ready){self->canvas_.setTheme(self->theme_);self->canvas_.fontPart(self->fontPart_);self->paint(self->canvas_);self->canvas_.end(h);}EndPaint(h,&ps);return 0;}case WM_SIZE:self->updateRoundedRegion();self->canvas_.resize(h);self->invalidate();return 0;case WM_DPICHANGED:{auto r=*reinterpret_cast<RECT*>(l);SetWindowPos(h,nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOACTIVATE|SWP_NOZORDER);self->updateRoundedRegion();self->canvas_.resize(h);self->invalidate();return 0;}case WM_MOUSEMOVE:if(!self->tracking_){TRACKMOUSEEVENT t{sizeof(t),TME_LEAVE,h,0};TrackMouseEvent(&t);self->tracking_=true;}self->pointer(GET_X_LPARAM(l)/self->scale(),GET_Y_LPARAM(l)/self->scale());return 0;case WM_MOUSELEAVE:self->tracking_=false;self->leave();return 0;case WM_LBUTTONUP:self->click(GET_X_LPARAM(l)/self->scale(),GET_Y_LPARAM(l)/self->scale());return 0;case WM_CONTEXTMENU:{POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};if(p.x==-1&&p.y==-1)GetCursorPos(&p);self->context(p);return 0;}case WM_NCDESTROY:SetWindowLongPtrW(h,GWLP_USERDATA,0);self->hwnd_=nullptr;break;}}catch(const std::exception& e){win::log(std::string("UI exception: ")+e.what());}catch(...){win::log("Unexpected UI exception.");}return DefWindowProcW(h,m,w,l);
}
}
