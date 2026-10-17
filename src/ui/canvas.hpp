#pragma once
#include "platform/windows.hpp"
#include "platform/icons.hpp"
#include <d2d1.h>
#include <dwrite.h>
#include <map>
#include <tuple>
#include <unordered_map>
namespace aether::ui {
using win::ComPtr;
inline D2D1_COLOR_F color(std::uint32_t rgb,float alpha=1.f){return D2D1::ColorF(((rgb>>16)&255)/255.f,((rgb>>8)&255)/255.f,(rgb&255)/255.f,alpha);}
inline D2D1_RECT_F box(float x,float y,float w,float h){return D2D1::RectF(x,y,x+w,y+h);}
inline bool contains(D2D1_RECT_F r,float x,float y){return x>=r.left&&x<r.right&&y>=r.top&&y<r.bottom;}
class Graphics {
 ComPtr<ID2D1Factory> factory_;ComPtr<IDWriteFactory> writer_;
 std::map<std::tuple<std::wstring,int,int,int,bool,bool>,ComPtr<IDWriteTextFormat>> fonts_;
public:
 Graphics();D2D1_SIZE_F cellSize(std::wstring_view family,int size);ID2D1Factory* factory(){return factory_.Get();}
 IDWriteTextFormat* font(int size,bool bold=false,int align=0,bool symbol=false,std::wstring_view family=L"Segoe UI",int weight=400,bool italic=false);
};
class Canvas {
 Graphics& graphics_;ComPtr<ID2D1HwndRenderTarget> target_;
 ComPtr<ID2D1SolidColorBrush> brush_;Theme textTheme_;FontPart fontPart_=FontPart::General;bool translucent_=false;
 ComPtr<ID2D1LinearGradientBrush> gradient_;std::pair<std::uint32_t,std::uint32_t> gradientColors_{};
 std::unordered_map<std::uint64_t,ComPtr<ID2D1Bitmap>> images_;
public:
 float drawingScale_=1.f;
 explicit Canvas(Graphics& g):graphics_(g){}
 bool begin(HWND hwnd,std::uint32_t background,bool translucent=false);void end(HWND hwnd);void resize(HWND hwnd);void release();
 bool translucent()const{return translucent_;}
 void setScale(float scale);
 void fontPart(FontPart part){fontPart_=part;}
 void setTheme(const Theme& theme){if(textTheme_.surfaceOpacity!=theme.surfaceOpacity){gradient_.Reset();gradientColors_={};}textTheme_=theme;}
 void surface(D2D1_RECT_F r,const Theme& theme,float radius=0);
 D2D1_SIZE_F size() const{auto s=target_?target_->GetSize():D2D1::SizeF();return D2D1::SizeF(s.width/drawingScale_,s.height/drawingScale_);}
 void fill(D2D1_RECT_F r,std::uint32_t c,float radius=0,float alpha=1);
 void stroke(D2D1_RECT_F r,std::uint32_t c,float radius=0,float width=1,float alpha=1);
 void line(float x1,float y1,float x2,float y2,std::uint32_t c,float width=1);
 void circle(float x,float y,float r,std::uint32_t c);
 void ribbon(float width,float y,float height,float bend,float phase,std::uint32_t color,float alpha);
 void text(std::wstring_view text,D2D1_RECT_F r,std::uint32_t c,int size=14,bool bold=false,int align=0,bool symbol=false);
 void pushClip(D2D1_RECT_F r);void popClip();
 void glyph(wchar_t glyph,D2D1_RECT_F r,std::uint32_t c,int size=18);
 void grid(D2D1_RECT_F r,std::uint32_t c);
 void icon(const std::shared_ptr<const win::IconPixels>& image,D2D1_RECT_F r,std::uint32_t fallback);
 void imageRegion(const std::shared_ptr<const win::IconPixels>& image,D2D1_RECT_F destination,D2D1_RECT_F source,float opacity);
 void ellipse(float x,float y,float rx,float ry,std::uint32_t rgb,float opacity,bool outline=false);
};
class Surface {
 static LRESULT CALLBACK procedure(HWND,UINT,WPARAM,LPARAM);
protected:
 HWND hwnd_{};Canvas canvas_;bool tracking_=false;Theme theme_;FontPart fontPart_=FontPart::Popups;
 bool backdropRequested_=false,backdropActive_=false,backdropKnown_=false;
 int roundedRadius_=-1,roundedDpi_=0;Rect roundedBounds_{};void updateRoundedRegion();
 virtual void paint(Canvas&)=0;
 virtual LRESULT event(UINT,WPARAM,LPARAM,bool& handled);
 virtual void pointer(float,float){};virtual void leave(){};
 virtual void click(float,float){};virtual void context(POINT){};
public:
 explicit Surface(Graphics& g,FontPart part=FontPart::Popups):canvas_(g),fontPart_(part){}
 virtual ~Surface(){destroy();}
 Surface(const Surface&)=delete;Surface& operator=(const Surface&)=delete;
 bool create(const wchar_t* title,DWORD style,DWORD ex,Rect bounds);
 void destroy();void show(bool active=false);void hide();
 void move(Rect bounds,bool show=true);void invalidate();
 void setTheme(const Theme& theme){theme_=theme;invalidate();}
 void roundedCorners(int radius){roundedRadius_=std::clamp(radius,0,28);updateRoundedRegion();}
 HWND hwnd() const{return hwnd_;}bool visible() const{return hwnd_&&IsWindowVisible(hwnd_);}
 float scale() const{return win::dpiFor(hwnd_)/96.f;}
 bool backdropActive()const{return backdropActive_;}
};
}
