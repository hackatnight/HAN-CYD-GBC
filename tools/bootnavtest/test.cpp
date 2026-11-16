// Runs the firmware's BOOT-button menu control (bootNav in src/main.cpp, used on boards with
// no touch screen) against simulated presses. Usage: ./build.sh
#include <cstdint>
#include <cstdio>
enum NavEvent { NAV_NONE, NAV_UP, NAV_DOWN, NAV_LEFT, NAV_RIGHT, NAV_OK, NAV_BACK, NAV_SYS, NAV_SELECT };
static uint32_t now_ms = 0;
static bool boot = false, navMode = true;
static uint32_t millis() { return now_ms; }
static bool bootRaw() { return boot; }
static bool bootNavMode() { return navMode; }
static bool bootBtnDown = false, bootBtnUsed = true;
static uint32_t bootBtnT0 = 0;
#include "bootnav.inc"

// hold BOOT for `ms`, polling every 10 ms like the menus do; returns the events seen
static int press(int ms, int *events, int max) {
  int n = 0;
  boot = true;
  for (int t = 0; t < ms; t += 10) { now_ms += 10; int e = bootNav(); if (e && n < max) events[n++] = e; }
  boot = false;
  for (int t = 0; t < 100; t += 10) { now_ms += 10; int e = bootNav(); if (e && n < max) events[n++] = e; }
  return n;
}

int main() {
  int fails = 0;
  auto expect = [&](const char *what, int ms, int want) {
    int ev[4]; int n = press(ms, ev, 4);
    bool ok = (want == NAV_NONE) ? n == 0 : (n == 1 && ev[0] == want);
    const char *names[] = { "none", "up", "down", "left", "right", "OK", "back", "sys", "select" };
    printf("  %-34s %5d ms -> %-5s %s\n", what, ms, n ? names[ev[0]] : "none", ok ? "ok" : "WRONG");
    if (!ok) fails++;
  };
  bootBtnDown = false; bootBtnUsed = true;
  expect("tap", 120, NAV_DOWN);
  expect("tap (quick)", 30, NAV_DOWN);
  expect("hold, let go", 800, NAV_OK);
  expect("hold, let go just before back", 1950, NAV_OK);
  expect("hold 2 s", 2500, NAV_BACK);
  expect("hold very long: back only once", 6000, NAV_BACK);
  // a press that was already down when the screen opened (btNavReset) does nothing
  bootBtnDown = true; bootBtnUsed = true; boot = true;
  for (int t = 0; t < 3000; t += 10) { now_ms += 10; if (bootNav()) { printf("  held from before: fired\n"); fails++; break; } }
  boot = false; now_ms += 50; if (bootNav()) { printf("  held from before: fired on release\n"); fails++; }
  printf("  %-34s          -> none  ok\n", "held from the screen before");
  // with a controller connected BOOT is not a menu button
  navMode = false;
  expect("controller connected: tap", 120, NAV_NONE);
  expect("controller connected: hold", 2500, NAV_NONE);
  printf(fails ? "BOOT menu control: %d FAILED\n" : "BOOT menu control: all OK\n", fails);
  return fails != 0;
}
