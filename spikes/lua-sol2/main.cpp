// SPIKE: Lua + sol2 as the fluxus scripting host.
// Proves: embed Lua, bind C++ engine calls, eval a LIVE code string (live-coding),
// capture print() output, handle a script error without crashing.
#include <iostream>
#include <string>
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>
#include "SpikeEngine.h"

static SpikeEngine &E = SpikeEngine::get();

static std::string g_console;   // captured Lua print() output

static void evalLive(sol::state &lua, const std::string &label, const std::string &code) {
  std::cout << ">>> " << label << "\n";
  g_console.clear();
  sol::protected_function_result r = lua.safe_script(code, sol::script_pass_on_error);
  if (!g_console.empty()) std::cout << "  [console] " << g_console << "\n";
  if (!r.valid()) {
    sol::error err = r;
    std::cout << "  [caught] " << err.what() << "\n";
  } else {
    std::cout << "  [result] ok\n";
  }
}

int main() {
  sol::state lua;
  lua.open_libraries(sol::lib::base, sol::lib::string, sol::lib::math);

  // bind engine API (same names as the s7 spike, lua-style)
  lua.set_function("clear",       [] { E.clear(); });
  lua.set_function("push",        [] { E.push(); });
  lua.set_function("pop",         [] { E.pop(); });
  lua.set_function("draw_cube",   [] { E.drawCube(); });
  lua.set_function("draw_sphere", [] { E.drawSphere(); });
  lua.set_function("translate",   [](double x,double y,double z){ E.translate(x,y,z); });
  lua.set_function("colour",      [](double r,double g,double b){ E.colour(r,g,b); });

  // capture print() into g_console (proves console output routing)
  lua.set_function("print", [](sol::variadic_args va) {
    for (auto v : va) g_console += v.get<std::string>() + " ";
  });

  std::cout << "=== Lua + sol2 spike ===\n";

  // 1) define frame (live-coding: what the user types)
  evalLive(lua, "define frame v1",
    "function frame()\n"
    "  clear(); colour(1,0,0); push(); translate(1,2,3)\n"
    "  draw_cube(); pop(); draw_sphere(); print('frame v1 ran')\n"
    "end");
  evalLive(lua, "call frame()", "frame()");
  std::cout << "  engine log:\n" << E.dump();

  // 2) HOT RE-EVAL: redefine frame at runtime, call again (live-coding)
  evalLive(lua, "redefine frame v2 (live edit)",
    "function frame()\n"
    "  clear(); colour(0,1,0); draw_cube(); draw_cube(); print('frame v2 ran')\n"
    "end");
  evalLive(lua, "call frame()", "frame()");
  std::cout << "  engine log:\n" << E.dump();

  // 3) ERROR handling: bad script must not crash the host
  evalLive(lua, "deliberate error", "translate(1, 2)");   // nil arg -> sol safety error

  std::cout << "=== Lua spike OK ===\n";
  return 0;
}
