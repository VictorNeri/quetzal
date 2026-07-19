#ifndef ELECHOUSE_CC1101_ESP32DIV_COMPAT_H
#define ELECHOUSE_CC1101_ESP32DIV_COMPAT_H

// Compatibility shim for the ESP32-DIV source tree.
// The firmware historically included a locally named/forked CC1101 header:
//   <ELECHOUSE_CC1101_ESP32DIV.h>
// PlatformIO installs the upstream SmartRC/ELECHOUSE library, whose public
// header is:
//   <ELECHOUSE_CC1101_SRC_DRV.h>
// Keeping this shim avoids touching the firmware call sites during migration.
//
// SmartRC's header declares its API in terms of the Arduino 8-bit `byte`. On the
// ESP32-C5 core (C++17) std::byte exists, and because wificonfig.h does
// `using namespace std;`, an unqualified `byte` becomes ambiguous between
// ::byte and std::byte wherever both are visible. Force `byte` to the Arduino
// type for the duration of this include only; it is identical (uint8_t) on both
// boards, and the original ESP32 core (C++11) has no std::byte to clash with.
#include <Arduino.h>
#define byte uint8_t
#include <ELECHOUSE_CC1101_SRC_DRV.h>
#undef byte

#endif // ELECHOUSE_CC1101_ESP32DIV_COMPAT_H
