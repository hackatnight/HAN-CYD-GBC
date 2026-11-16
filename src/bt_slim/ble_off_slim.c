/*
 * H@N-CYD-GBC Bluetooth slimming (Bluetooth build only): no Bluetooth Low Energy.
 * The PS4 controller uses classic Bluetooth, so Bluepad32's LE scanning/pairing
 * (uni_bt_le.c), its LE "BP32" GATT service (uni_bt_service.c) and BTstack's LE
 * security manager (sm.c, pulled in by l2cap.c) are replaced by these no-op
 * versions. Because every symbol they export is defined here, the linker no longer
 * pulls the originals, nor the GATT/ATT/crypto code they depend on (~60 KB of flash).
 * The controller still runs in dual mode; LE simply never gets started.
 */
#if __has_include(<uni_hid_device.h>) && __has_include(<btstack_port_esp32.h>)
#include <stdbool.h>
#include <stdint.h>

#include "bt/uni_bt_le.h"
#include "bt/uni_bt_service.h"
#include "ble/sm.h"

/* ---- uni_bt_le.c ---- */
void uni_bt_le_on_hci_event_le_meta(const uint8_t* packet, uint16_t size) { (void)packet; (void)size; }
void uni_bt_le_on_hci_event_encryption_change(const uint8_t* packet, uint16_t size) { (void)packet; (void)size; }
void uni_bt_le_on_gap_event_advertising_report(const uint8_t* packet, uint16_t size) { (void)packet; (void)size; }
void uni_bt_le_on_hci_disconnection_complete(uint16_t channel, const uint8_t* packet, uint16_t size) {
    (void)channel; (void)packet; (void)size;
}
void uni_bt_le_scan_start(void) {}
void uni_bt_le_scan_stop(void) {}
void uni_bt_le_disconnect(uni_hid_device_t* d) { (void)d; }
void uni_bt_le_list_bonded_keys(void) {}
void uni_bt_le_delete_bonded_keys(void) {}
void uni_bt_le_setup(void) {}
void uni_bt_le_set_enabled(bool enabled) { (void)enabled; }
bool uni_bt_le_is_enabled(void) { return false; }

/* ---- uni_bt_service.c ---- */
void uni_bt_service_init(void) {}
void uni_bt_service_deinit(void) {}
bool uni_bt_service_is_enabled() { return false; }
void uni_bt_service_set_enabled(bool enabled) { (void)enabled; }
void uni_bt_service_on_device_ready(const uni_hid_device_t* d) { (void)d; }
void uni_bt_service_on_device_connected(const uni_hid_device_t* d) { (void)d; }
void uni_bt_service_on_device_disconnected(const uni_hid_device_t* d) { (void)d; }

/* ---- BTstack sm.c: the two calls l2cap.c makes (LE connections only) ---- */
void sm_add_event_handler(btstack_packet_callback_registration_t* callback_handler) { (void)callback_handler; }
void sm_request_pairing(hci_con_handle_t con_handle) { (void)con_handle; }

#endif
