// Minimal Arduino stand-ins so the firmware's USB transfer code runs on a PC.
// Serial = stdin/stdout, SD = a folder on disk, the screen does nothing.
#pragma once
#include <string>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstdarg>
#include <algorithm>
#include <poll.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/time.h>
using std::min; using std::max;

static uint32_t millis() { timeval t; gettimeofday(&t, nullptr); return t.tv_sec * 1000 + t.tv_usec / 1000; }
static void delay(int ms) { usleep(ms * 1000); }

struct String {
  std::string s;
  String() {}
  String(const char *c) : s(c ? c : "") {}
  String(const std::string &x) : s(x) {}
  String(int v) : s(std::to_string(v)) {}
  unsigned length() const { return s.size(); }
  const char *c_str() const { return s.c_str(); }
  char operator[](unsigned i) const { return s[i]; }
  String substring(unsigned a) const { return a >= s.size() ? String() : String(s.substr(a)); }
  String substring(unsigned a, unsigned b) const { return a >= s.size() ? String() : String(s.substr(a, b - a)); }
  int indexOf(const char *x) const { auto p = s.find(x); return p == std::string::npos ? -1 : (int)p; }
  int lastIndexOf(char c) const { auto p = s.rfind(c); return p == std::string::npos ? -1 : (int)p; }
  bool startsWith(const char *x) const { return s.rfind(x, 0) == 0; }
  void remove(unsigned i) { s.erase(i); }
  bool operator==(const char *x) const { return s == x; }
  String operator+(const char *x) const { return String(s + x); }
  String operator+(const String &x) const { return String(s + x.s); }
  String &operator+=(const char *x) { s += x; return *this; }
};

static bool CORRUPT = getenv("CORRUPT") != nullptr;
static bool DROP = getenv("DROP") != nullptr;
struct SerialT {
  uint32_t timeout = 1000;
  int peeked = -1;
  int available() {
    if (peeked >= 0) return 1;
    pollfd p = { 0, POLLIN, 0 };
    return poll(&p, 1, 0) > 0 && (p.revents & POLLIN);
  }
  int read() {
    if (peeked >= 0) { int c = peeked; peeked = -1; return c; }
    unsigned char c;
    return ::read(0, &c, 1) == 1 ? c : -1;
  }
  size_t readBytes(uint8_t *b, size_t n) {
    size_t got = 0;
    uint32_t t0 = millis();
    while (got < n && millis() - t0 < timeout) {
      pollfd p = { 0, POLLIN, 0 };
      if (poll(&p, 1, 10) > 0) { ssize_t r = ::read(0, b + got, n - got); if (r <= 0) break; got += r; }
    }
    static int k = 0;
    if (CORRUPT && got > 10 && (++k % 7) == 0) b[got / 2] ^= 0x55;   // damage every 7th chunk once
    static int j = 0;
    if (DROP && got > 10 && (++j % 9) == 0) return got - 5;           // lose 5 bytes of every 9th chunk
    return got;
  }
  void setTimeout(uint32_t t) { timeout = t; }
  void print(const char *s) { fputs(s, stdout); fflush(stdout); }
  void printf(const char *f, ...) { va_list a; va_start(a, f); vprintf(f, a); va_end(a); fflush(stdout); }
} Serial;

static std::string ROOT = "card";
struct File {
  FILE *fp = nullptr; DIR *dp = nullptr; std::string path;
  operator bool() const { return fp || dp; }
  size_t write(const uint8_t *b, size_t n) { return fwrite(b, 1, n, fp); }
  void close() { if (fp) fclose(fp); if (dp) closedir(dp); fp = nullptr; dp = nullptr; }
  size_t size() { struct stat st; return stat((ROOT + path).c_str(), &st) ? 0 : st.st_size; }
  bool isDirectory() { return dp != nullptr; }
  String getNextFileName(bool *isDir) {
    while (dirent *e = readdir(dp)) {
      if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
      *isDir = e->d_type == DT_DIR;
      return String(path + "/" + e->d_name);
    }
    return String();
  }
};
#define FILE_WRITE "w"
struct SDT {
  bool exists(const String &p) { struct stat st; return !stat((ROOT + p.s).c_str(), &st); }
  bool mkdir(const String &p) { return !::mkdir((ROOT + p.s).c_str(), 0755); }
  bool remove(const String &p) { return !::remove((ROOT + p.s).c_str()); }
  bool rename(const String &a, const String &b) { return !::rename((ROOT + a.s).c_str(), (ROOT + b.s).c_str()); }
  File open(const String &p, const char *mode = "r") {
    File f; f.path = p.s;
    struct stat st;
    std::string full = ROOT + p.s;
    if (!strcmp(mode, "r") && !stat(full.c_str(), &st) && S_ISDIR(st.st_mode)) f.dp = opendir(full.c_str());
    else f.fp = fopen(full.c_str(), strcmp(mode, "r") ? "wb" : "rb");
    return f;
  }
  uint64_t totalBytes() { return 8ull << 30; }
  uint64_t usedBytes() { return 1ull << 30; }
} SD;

// screen, LED and other firmware bits the transfer code touches
struct { void startWrite() {} void endWrite() {} void fillRect(int, int, int, int, int) {} void setFont(const void *) {}
  void setTextDatum(int) {} void setTextColor(int, int = 0) {} void drawString(const String &, int, int) {}
  int textWidth(const String &s) { return s.length() * 7; } void drawRoundRect(int, int, int, int, int, int) {}
  void fillRoundRect(int, int, int, int, int, int) {} } lcd;
namespace fonts { static int Font2; }
enum { middle_center, top_left };
static int SCR_W = 320, SCR_H = 240;
enum { C_BG, C_TEXT, C_DIM, C_ACCENT, C_RED = 0xF800, C_GREEN = 0x07E0 };
enum { LED_FLASH_SAVE = 1 };
static void ledFlash(int) {}
#define APP_VERSION "test"
#define HAVE_BT 0
