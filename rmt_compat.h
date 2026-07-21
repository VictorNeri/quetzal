#ifndef RMT_COMPAT_H
#define RMT_COMPAT_H

// ═══════════════════════════════════════════════════════════════════════════
// RMT driver compatibility layer
// ═══════════════════════════════════════════════════════════════════════════
// SubGHz OOK transmission uses the ESP32 RMT peripheral. Older ESP-IDF
// versions (< 5) only have the legacy driver (driver/rmt.h, rmt_item32_t,
// rmt_driver_install, rmt_write_items).
//
// On the ESP32-C5 (Arduino core 3.x / ESP-IDF 5) the core already uses the new
// RMT driver ("driver_ng"). ESP-IDF forbids linking the legacy and new RMT
// drivers into the same image and aborts at startup:
//     E rmt(legacy): CONFLICT! driver_ng is not allowed to be used with the
//     legacy driver
// That abort is what boot-loops the C5. So on IDF 5 we must drop the legacy
// driver entirely and drive the SubGHz TX with the new driver/rmt_tx.h API.
//
// Conveniently, the new driver's rmt_symbol_word_t has the SAME bitfield layout
// and field names (level0/duration0/level1/duration1) as the legacy
// rmt_item32_t, so all of subghz.cpp's symbol-building code is reused as-is;
// only initRMT()/rmtTransmit()/cleanup switch implementations (see subghz.cpp).
// ═══════════════════════════════════════════════════════════════════════════

#include "board_config.h"
#include <stdint.h>
#include <stddef.h>
#include "esp_idf_version.h"

#if ESP_IDF_VERSION_MAJOR >= 5

#define RMT_COMPAT_NEW_DRIVER 1
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"

// Alias the legacy symbol type name to the new one; identical bitfields.
typedef rmt_symbol_word_t rmt_item32_t;

#else

#define RMT_COMPAT_NEW_DRIVER 0
#include "driver/rmt.h"  // legacy RMT peripheral driver (rmt_item32_t, etc.)

#endif

#endif // RMT_COMPAT_H
