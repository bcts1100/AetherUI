#pragma once
#include "integration.hpp"
#include <deque>
namespace aether {
struct TerminalCell {char32_t glyph=U' ';std::uint32_t foreground=0xFFFFFF,background=0;bool bold=false;bool operator==(const TerminalCell&)const=default;};
class TerminalGrid {
 int columns_=80,rows_=24,x_=0,y_=0,savedX_=0,savedY_=0,top_=0,bottom_=23;bool bold_=false,inverse_=false,alternate_=false,wrap_=false;
 std::uint32_t foreground_=0xFFFFFF,background_=0;Theme theme_;std::array<std::uint32_t,16> palette_{};
 std::vector<TerminalCell> cells_,normal_;int normalX_=0,normalY_=0;
 std::deque<std::vector<TerminalCell>> history_;std::string escape_;int state_=0;char32_t utf_=0;int remaining_=0;char32_t minimum_=0;
 void codepoint(char32_t);void newline();void scroll(int);void csi(char);TerminalCell blank()const;void put(char32_t);
 std::string response_;
public:
 bool cursorVisible=true,bracketedPaste=false,applicationCursor=false;
 TerminalGrid();void theme(const Theme&);void resize(int columns,int rows);void feed(std::string_view bytes);
 int columns()const{return columns_;}int rows()const{return rows_;}int cursorX()const{return x_;}int cursorY()const{return y_;}
 const std::vector<TerminalCell>& cells()const{return cells_;}const std::deque<std::vector<TerminalCell>>& history()const{return history_;}
 std::string takeResponse(){auto v=std::move(response_);response_.clear();return v;}
 std::u32string plainText()const;
};
}
