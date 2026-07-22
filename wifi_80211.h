#pragma once

#include <cstddef>
#include <cstdint>

namespace Wifi80211 {

enum class FrameType : uint8_t { Management = 0, Control = 1, Data = 2, Extension = 3 };

struct MacAddress {
  uint8_t bytes[6]{};
  MacAddress() = default;
  explicit MacAddress(const uint8_t* source);
  bool operator==(const MacAddress& other) const;
  bool operator!=(const MacAddress& other) const { return !(*this == other); }
  bool isZero() const;
};

struct ParsedFrame {
  FrameType type{FrameType::Management};
  uint8_t subtype{0};
  bool toDs{false};
  bool fromDs{false};
  bool retry{false};
  bool protectedFrame{false};
  uint16_t sequence{0};
  size_t headerLength{0};
  size_t ieOffset{0};
  MacAddress receiver;
  MacAddress transmitter;
  MacAddress destination;
  MacAddress source;
  MacAddress bssid;
};

struct SecurityInfo {
  char ssid[33]{};
  bool hidden{false};
  bool hasRsn{false};
  bool pmfCapable{false};
  bool pmfRequired{false};
  bool hasWps{false};
  bool wpsConfigured{false};
  bool hasBssLoad{false};
  uint8_t channelUtilization{0};
};

bool parseFrame(const uint8_t* frame, size_t length, ParsedFrame& out);
bool parseSecurityIes(const uint8_t* ies, size_t length, SecurityInfo& out);
bool findEapol(const uint8_t* frame, size_t length, const ParsedFrame& parsed,
                size_t& eapolOffset);

}  // namespace Wifi80211
