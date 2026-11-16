// minimal Arduino String shim, then the firmware's own parser
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cstdint>
#include <algorithm>
struct String {
  std::string s;
  String() {}
  String(const char *c) : s(c) {}
  String(const std::string &x) : s(x) {}
  size_t length() const { return s.size(); }
  char operator[](size_t i) const { return s[i]; }
  int indexOf(char c, int from = 0) const { auto p = s.find(c, from); return p == std::string::npos ? -1 : (int)p; }
  String substring(int a, int b = -1) const { if (b < 0) b = s.size(); if (a > (int)s.size()) a = s.size(); return String(s.substr(a, std::max(0, b - a))); }
  void trim() { size_t a = s.find_first_not_of(" \t\r\n"); size_t b = s.find_last_not_of(" \t\r\n"); s = a == std::string::npos ? "" : s.substr(a, b - a + 1); }
  void toUpperCase() { for (auto &c : s) c = toupper(c); }
  void replace(const char *f, const char *t) { std::string F(f), T(t); size_t p = 0; while ((p = s.find(F, p)) != std::string::npos) { s.replace(p, F.size(), T); p += T.size(); } }
  const char *c_str() const { return s.c_str(); }
};
#include "parse_part.inc"
int main() {
  struct T { const char *code; bool nes; } tests[] = {
    { "010520C1", false },          // GameShark: C120 = 05
    { "9163E0D2", false },          // GBC bank 1: D2E0 = 63
    { "01FF12C3+01FF13C3", false }, // two codes
    { "C0A2=09", false },
    { "0D3-F2E-E66", false },       // GB Game Genie -> not usable
    { "0075:09", true },            // NES PAR raw
    { "007509", true },
    { "00075009", true },
    { "SXIOPO", true },             // NES Game Genie -> not usable
    { "6010=FF", true },
  };
  for (auto &t : tests) {
    Cheat c; c.code = t.code;
    bool ok = parseCheat(c, t.nes);
    printf("%-20s %-3s ok=%d", t.code, t.nes ? "NES" : "GB", ok);
    for (int k = 0; k < c.n; k++) printf("  [bank %02X] %04X=%02X", c.c[k].bank, c.c[k].addr, c.c[k].val);
    printf("\n");
  }
}
