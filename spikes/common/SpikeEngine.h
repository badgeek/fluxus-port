#pragma once
// Fake stand-in for fluxus's Engine::Get() singleton. Records draw commands so a
// spike can PROVE a scripting language can: embed, eval a live string, call into
// C++, and produce observable engine effects. Same API bound by both spikes
// (s7 + Lua) so the comparison is apples-to-apples.
#include <string>
#include <vector>
#include <sstream>

class SpikeEngine {
public:
  static SpikeEngine &get() { static SpikeEngine e; return e; }

  void clear()                    { log.clear(); depth = 0; add("clear"); }
  void push()                     { ++depth; add("push  (depth=" + std::to_string(depth) + ")"); }
  void pop()                      { if (depth) --depth; add("pop   (depth=" + std::to_string(depth) + ")"); }
  void translate(double x,double y,double z) { add(fmt("translate", x, y, z)); }
  void colour(double r,double g,double b)    { add(fmt("colour",    r, g, b)); }
  void drawCube()                 { add(indent() + "draw-cube"); }
  void drawSphere()               { add(indent() + "draw-sphere"); }

  std::string dump() const {
    std::ostringstream o;
    for (auto &l : log) o << "    " << l << "\n";
    return o.str();
  }

private:
  std::vector<std::string> log;
  int depth = 0;

  void add(const std::string &s) { log.push_back(s); }
  std::string indent() const { return std::string(static_cast<size_t>(depth) * 2, ' '); }
  std::string fmt(const char *name, double a, double b, double c) const {
    std::ostringstream o;
    o << indent() << name << " " << a << " " << b << " " << c;
    return o.str();
  }
};
