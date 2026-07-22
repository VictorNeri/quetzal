#include "wifi_80211.h"

#include <cstring>

namespace Wifi80211 {
namespace {
uint16_t readLe16(const uint8_t* value) {
  return static_cast<uint16_t>(value[0]) |
         (static_cast<uint16_t>(value[1]) << 8);
}
uint16_t readBe16(const uint8_t* value) {
  return (static_cast<uint16_t>(value[0]) << 8) |
         static_cast<uint16_t>(value[1]);
}
void setMac(MacAddress& target, const uint8_t* source) {
  std::memcpy(target.bytes, source, sizeof(target.bytes));
}

bool parseRsn(const uint8_t* data, size_t length, SecurityInfo& out) {
  if (length < 8 || readLe16(data) != 1) return false;
  size_t offset = 2 + 4;
  if (offset + 2 > length) return false;
  uint16_t pairwiseCount = readLe16(data + offset);
  offset += 2;
  if (pairwiseCount > (length - offset) / 4) return false;
  offset += static_cast<size_t>(pairwiseCount) * 4;
  if (offset + 2 > length) return false;
  uint16_t akmCount = readLe16(data + offset);
  offset += 2;
  if (akmCount > (length - offset) / 4) return false;
  offset += static_cast<size_t>(akmCount) * 4;
  uint16_t capabilities = 0;
  if (offset + 2 <= length) capabilities = readLe16(data + offset);
  out.hasRsn = true;
  out.pmfRequired = (capabilities & (1u << 6)) != 0;
  out.pmfCapable = (capabilities & (1u << 7)) != 0;
  return true;
}

void parseWps(const uint8_t* data, size_t length, SecurityInfo& out) {
  if (length < 4 || std::memcmp(data, "\x00\x50\xf2\x04", 4) != 0) return;
  out.hasWps = true;
  size_t offset = 4;
  while (offset + 4 <= length) {
    uint16_t attribute = readBe16(data + offset);
    uint16_t attributeLength = readBe16(data + offset + 2);
    offset += 4;
    if (attributeLength > length - offset) return;
    if (attribute == 0x1044 && attributeLength >= 1) {
      out.wpsConfigured = data[offset] == 2;
    }
    offset += attributeLength;
  }
}
}  // namespace

MacAddress::MacAddress(const uint8_t* source) {
  if (source) std::memcpy(bytes, source, sizeof(bytes));
}

bool MacAddress::operator==(const MacAddress& other) const {
  return std::memcmp(bytes, other.bytes, sizeof(bytes)) == 0;
}

bool MacAddress::isZero() const {
  static const uint8_t zero[6] = {};
  return std::memcmp(bytes, zero, sizeof(bytes)) == 0;
}

bool parseFrame(const uint8_t* frame, size_t length, ParsedFrame& out) {
  if (!frame || length < 2) return false;
  const uint16_t control = readLe16(frame);
  if ((control & 0x3) != 0) return false;
  out = ParsedFrame{};
  out.type = static_cast<FrameType>((control >> 2) & 0x3);
  out.subtype = static_cast<uint8_t>((control >> 4) & 0xf);
  out.toDs = (control & (1u << 8)) != 0;
  out.fromDs = (control & (1u << 9)) != 0;
  out.retry = (control & (1u << 11)) != 0;
  out.protectedFrame = (control & (1u << 14)) != 0;

  if (out.type == FrameType::Control) {
    if (length < 10) return false;
    out.headerLength = (out.subtype == 12 || out.subtype == 13) ? 10 : 16;
    if (length < out.headerLength) return false;
    setMac(out.receiver, frame + 4);
    if (out.headerLength >= 16) setMac(out.transmitter, frame + 10);
    return true;
  }

  if (length < 24) return false;
  setMac(out.receiver, frame + 4);
  setMac(out.transmitter, frame + 10);
  out.sequence = static_cast<uint16_t>(readLe16(frame + 22) >> 4);
  out.headerLength = 24;

  if (out.type == FrameType::Management) {
    setMac(out.destination, frame + 4);
    setMac(out.source, frame + 10);
    setMac(out.bssid, frame + 16);
    switch (out.subtype) {
      case 0: out.ieOffset = 28; break;   // association request
      case 1: out.ieOffset = 30; break;   // association response
      case 2: out.ieOffset = 34; break;   // reassociation request
      case 3: out.ieOffset = 30; break;   // reassociation response
      case 4: out.ieOffset = 24; break;   // probe request
      case 5: out.ieOffset = 36; break;   // probe response
      case 8: out.ieOffset = 36; break;   // beacon
      default: out.ieOffset = 0; break;
    }
    if (out.ieOffset > length) return false;
    return true;
  }

  if (out.type != FrameType::Data) return true;
  if (out.toDs && out.fromDs) {
    if (length < 30) return false;
    setMac(out.destination, frame + 16);
    setMac(out.source, frame + 24);
    out.headerLength = 30;
  } else if (out.toDs) {
    setMac(out.bssid, frame + 4);
    setMac(out.source, frame + 10);
    setMac(out.destination, frame + 16);
  } else if (out.fromDs) {
    setMac(out.destination, frame + 4);
    setMac(out.bssid, frame + 10);
    setMac(out.source, frame + 16);
  } else {
    setMac(out.destination, frame + 4);
    setMac(out.source, frame + 10);
    setMac(out.bssid, frame + 16);
  }
  if ((out.subtype & 0x8) != 0) {
    out.headerLength += 2;
    if ((control & (1u << 15)) != 0) out.headerLength += 4;
  }
  return out.headerLength <= length;
}

bool parseSecurityIes(const uint8_t* ies, size_t length, SecurityInfo& out) {
  if (!ies && length != 0) return false;
  out = SecurityInfo{};
  size_t offset = 0;
  while (offset < length) {
    if (length - offset < 2) return false;
    uint8_t id = ies[offset];
    uint8_t ieLength = ies[offset + 1];
    offset += 2;
    if (ieLength > length - offset) return false;
    const uint8_t* data = ies + offset;
    if (id == 0) {
      size_t copyLength = ieLength > 32 ? 32 : ieLength;
      std::memcpy(out.ssid, data, copyLength);
      out.ssid[copyLength] = '\0';
      out.hidden = copyLength == 0;
    } else if (id == 48) {
      if (!parseRsn(data, ieLength, out)) return false;
    } else if (id == 11 && ieLength >= 3) {
      out.hasBssLoad = true;
      out.channelUtilization = data[2];
    } else if (id == 221) {
      parseWps(data, ieLength, out);
    }
    offset += ieLength;
  }
  return true;
}

bool findEapol(const uint8_t* frame, size_t length, const ParsedFrame& parsed,
                size_t& eapolOffset) {
  static const uint8_t llcSnap[] = {0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00, 0x88, 0x8e};
  if (!frame || parsed.type != FrameType::Data || parsed.protectedFrame ||
      parsed.headerLength > length || length - parsed.headerLength < sizeof(llcSnap)) {
    return false;
  }
  if (std::memcmp(frame + parsed.headerLength, llcSnap, sizeof(llcSnap)) != 0) return false;
  eapolOffset = parsed.headerLength + sizeof(llcSnap);
  return true;
}

}  // namespace Wifi80211
