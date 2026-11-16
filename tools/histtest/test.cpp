#include "stubs.h"
#include <vector>
#include <strings.h>
enum { K_GB = 0, K_NES = 1, K_DIR = 2, K_UP = 3, K_MORE = 4, K_CONT = 5, K_VFAV = 6, K_VRECENT = 7, K_VMOST = 8 };
#define SIZE_UNKNOWN 0xFFFFFFFFu
struct Ent { const char *name; uint32_t size; uint8_t kind; uint8_t fav; };
static std::vector<Ent> roms; static int romFiles = 0;
static String curDir("/Roms"), rootDir("/Roms");
static std::vector<std::string*> keep;
static const char *storeName(const String &n) { keep.push_back(new std::string(n.s)); return keep.back()->c_str(); }
static int romKind(const char *n) { const char *d = strrchr(n, '.'); if (!d) return -1; return !strcasecmp(d, ".nes") ? K_NES : K_GB; }
static String parentDir(const String &d) { int sl = d.lastIndexOf('/'); return sl <= 0 ? String("/") : d.substring(0, sl); }
#include "hist.inc"
static void dump(const char *t) { printf("%s:", t); for (auto &e : roms) printf(" [%s %u k%d f%d]", e.name, e.size, e.kind, e.fav); printf("\n"); roms.clear(); }
int main() {
  system("rm -rf card && mkdir -p card/gbc card/Roms/GB card/Roms/NES");
  for (const char *g : { "Roms/GB/Tetris.gb", "Roms/GB/Zelda.gbc", "Roms/NES/Mario.nes" }) { FILE *f = fopen((std::string("card/") + g).c_str(), "w"); fclose(f); }
  histUpdate("/Roms/GB/Tetris.gb", HIST_START); histUpdate("/Roms/GB/Tetris.gb", HIST_TIME, 3700);
  histUpdate("/Roms/NES/Mario.nes", HIST_START); histUpdate("/Roms/NES/Mario.nes", HIST_TIME, 120);
  printf("fav Zelda -> %d\n", histUpdate("/Roms/GB/Zelda.gbc", HIST_FAV));
  printf("fav Tetris -> %d\n", histUpdate("/Roms/GB/Tetris.gb", HIST_FAV));
  histUpdate("/Roms/GB/Gone.gb", HIST_START);   // a game that was deleted later
  histRootRows(); dump("root");
  curDir = "/:Recent"; histList(); dump("recent");
  curDir = "/:Most played"; histList(); dump("most");
  curDir = "/:Favorites"; histList(); dump("favorites");
  printf("unfav Tetris -> %d\n", histUpdate("/Roms/GB/Tetris.gb", HIST_FAV));
  curDir = "/Roms/GB"; roms.push_back({ "Tetris.gb", 0, K_GB }); roms.push_back({ "Zelda.gbc", 0, K_GB }); histMarkFavorites(); dump("GB folder stars");
  for (int i = 0; i < 400; i++) histUpdate(String(("/Roms/GB/x" + std::to_string(i) + ".gb").c_str()), HIST_START);
  int lines = 0; histForEach([&](const HistLine &) { lines++; });
  printf("after 400 more games: %d lines kept (favorite Zelda kept: %d)\n", lines,
         (int)(system("grep -q Zelda card/gbc/history.txt") == 0));
  system("head -3 card/gbc/history.txt");
}
