/*
 * H@N-CYD-GBC Bluetooth slimming (Bluetooth build only): no "BP32" BLE service.
 * Bluepad32 can advertise a BLE GATT service that phone apps use to configure it.
 * The emulator doesn't need it, so uni_bt_service.c is replaced by these no-ops and
 * the GATT server code it uses isn't linked in. BLE controllers still work: they use
 * the BLE scanning and HID-over-GATT client code, which stays.
 */
#if __has_include(<uni_hid_device.h>) && __has_include(<btstack_port_esp32.h>)
#include <stdbool.h>

#include "bt/uni_bt_service.h"

void uni_bt_service_init(void) {}
void uni_bt_service_deinit(void) {}
bool uni_bt_service_is_enabled() { return false; }
void uni_bt_service_set_enabled(bool enabled) { (void)enabled; }
void uni_bt_service_on_device_ready(const uni_hid_device_t* d) { (void)d; }
void uni_bt_service_on_device_connected(const uni_hid_device_t* d) { (void)d; }
void uni_bt_service_on_device_disconnected(const uni_hid_device_t* d) { (void)d; }

#endif
