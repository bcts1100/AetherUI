#include "core/model.hpp"
#include "core/extensions.hpp"
#include <iostream>
#include <stdexcept>
using namespace aether;
int main(){try{
 auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
 const auto original=preset(0);ThemeUndo history;history.record(original,preset(5));
 check(textOnAccent(0xFFFFFF)==0x000000&&textOnAccent(0x000000)==0xFFFFFF&&textOnAccent(preset(5).accent)==0x000000,"Selected controls must use readable text for light and dark accents");
 check(history.available(preset(5))&&!history.available(preset(6)),"Undo must bind to the actual applied palette, not overwrite an unrelated later edit");
 check(!history.take(preset(7)),"Conflicting palette must remain unchanged");
 check(history.take(preset(5))==original&&!history.take(preset(5)),"Successful Undo must restore once only");
 for(int i=0;i<4;++i){auto palette=preset(i+5);check(parseTheme(serializeTheme(palette))==palette,"New palette must round-trip all style tokens");auto desk=sleekDeskExtension(i);check(parseExtension(serializeExtension(desk))==desk,"Matching desk must use the actual strict importer without losing ordering");}
 auto tools=productivityExtension();check(parseExtension(serializeExtension(tools))==tools,"All five new widget types must round-trip");
 auto oversized=tools;oversized.widgets[2].text=std::string(301,'x');check(!parseExtension(serializeExtension(oversized)),"Oversized note must be rejected");
 auto compact=tools;compact.widgets[1].height=64;check(!parseExtension(serializeExtension(compact)),"Media controls cannot be packed into an overlapping short card");
 auto invalid=tools;invalid.widgets[2].text="injected\nline";check(!parseExtension(serializeExtension(invalid)),"Control characters in note data must be rejected");
 invalid=tools;invalid.widgets[4].location="https://attacker.example";check(!parseExtension(serializeExtension(invalid)),"Weather location cannot become an arbitrary network target");
 for(int dpi:{96,144,192}){Rect work{-1280*dpi/96,0,0,720*dpi/96};std::vector<Rect> rectangles;auto desk=sleekDeskExtension(0);auto widgets=desk.widgets;widgets.insert(widgets.end(),tools.widgets.begin(),tools.widgets.end());for(const auto& widget:widgets){auto rectangle=extensionBounds(widget,work,dpi);for(auto other:rectangles)check(!(rectangle.left<other.right&&rectangle.right>other.left&&rectangle.top<other.bottom&&rectangle.bottom>other.top),"Combined desk and tools must start without overlap at supported work-area size");rectangles.push_back(rectangle);}}
 std::cout<<"Sleek core PASS: protected Undo, four palettes, typed packages, note bounds, network target rejection and combined DPI geometry.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
