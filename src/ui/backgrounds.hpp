#pragma once
#include "canvas.hpp"
#include "accessibility.hpp"
#include "platform/wallpaper.hpp"
namespace aether {class App;}
namespace aether::ui {
class Backgrounds final:public Surface {
 App& app_;std::unique_ptr<Accessibility> accessibility_;HWND pathEdit_=nullptr;HFONT editFont_=nullptr;
 std::vector<win::Monitor> displays_;std::optional<WallpaperSnapshot> snapshot_;
 std::wstring selected_,displayName_,notice_=L"Choose an image, then Apply when ready.";
 WallpaperFit fit_=WallpaperFit::Fill;std::uint64_t scope_=0,pending_=0,previewRevision_=0;
 WallpaperKind pendingKind_=WallpaperKind::Query;int focused_=0;bool closing_=false;
 void paint(Canvas&) override;void click(float,float) override;
 LRESULT event(UINT,WPARAM,LPARAM,bool&) override;
 static LRESULT CALLBACK editProcedure(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);
 float layoutScale() const;void layoutEdit();void accessibleSnapshot();D2D1_RECT_F controlRect(int) const;
 bool enabled(int) const;void focusControl(int);void invoke(int);void query();void choose();void selectPath(const std::wstring&);
public:
 Backgrounds(Graphics&,App&);~Backgrounds() override;
 void open(const win::Monitor&);
 void update(const win::WallpaperCompletion&);
 void refreshTopology();
};
}
