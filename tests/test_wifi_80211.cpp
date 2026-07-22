#include "wifi_80211.h"
#include <cassert>
#include <cstdint>
#include <cstring>

using namespace Wifi80211;

static void test_beacon_and_security_ies() {
  uint8_t frame[128] = {};
  frame[0] = 0x80;  // beacon
  const uint8_t destination[6] = {0xff,0xff,0xff,0xff,0xff,0xff};
  const uint8_t ap[6] = {0x02,0x11,0x22,0x33,0x44,0x55};
  std::memcpy(frame + 4, destination, 6);
  std::memcpy(frame + 10, ap, 6);
  std::memcpy(frame + 16, ap, 6);
  size_t offset = 36;
  const uint8_t ssid[] = {0,4,'L','A','B','1'};
  std::memcpy(frame + offset, ssid, sizeof(ssid)); offset += sizeof(ssid);
  const uint8_t rsn[] = {
    48,20, 1,0, 0,0x0f,0xac,4, 1,0, 0,0x0f,0xac,4,
    1,0, 0,0x0f,0xac,2, 0xc0,0x00
  };
  std::memcpy(frame + offset, rsn, sizeof(rsn)); offset += sizeof(rsn);
  const uint8_t wps[] = {221,9,0x00,0x50,0xf2,0x04,0x10,0x44,0x00,0x01,0x02};
  std::memcpy(frame + offset, wps, sizeof(wps)); offset += sizeof(wps);

  ParsedFrame parsed{};
  assert(parseFrame(frame, offset, parsed));
  assert(parsed.type == FrameType::Management);
  assert(parsed.subtype == 8);
  assert(parsed.ieOffset == 36);
  assert(parsed.bssid == MacAddress(ap));

  SecurityInfo security{};
  assert(parseSecurityIes(frame + parsed.ieOffset, offset - parsed.ieOffset, security));
  assert(std::strcmp(security.ssid, "LAB1") == 0);
  assert(security.hasRsn);
  assert(security.pmfCapable);
  assert(security.pmfRequired);
  assert(security.hasWps);
  assert(security.wpsConfigured);
}

static void test_eapol_data_frame() {
  uint8_t frame[64] = {};
  frame[0] = 0x08;  // data
  const uint8_t llc[] = {0xaa,0xaa,0x03,0x00,0x00,0x00,0x88,0x8e};
  std::memcpy(frame + 24, llc, sizeof(llc));
  frame[32] = 2; frame[33] = 3; frame[34] = 0; frame[35] = 5;
  ParsedFrame parsed{};
  assert(parseFrame(frame, 40, parsed));
  size_t eapolOffset = 0;
  assert(findEapol(frame, 40, parsed, eapolOffset));
  assert(eapolOffset == 32);
}

static void test_malformed_ies_are_rejected() {
  const uint8_t ies[] = {0, 10, 'x'};
  SecurityInfo security{};
  assert(!parseSecurityIes(ies, sizeof(ies), security));
}

static void test_order_bit_only_adds_ht_control_to_qos_data() {
  uint8_t legacy[48] = {};
  legacy[0] = 0x08;
  legacy[1] = 0x80;  // Strict ordering, not HT Control, on non-QoS data.
  const uint8_t llc[] = {0xaa,0xaa,0x03,0x00,0x00,0x00,0x88,0x8e};
  std::memcpy(legacy + 24, llc, sizeof(llc));
  ParsedFrame parsed{};
  assert(parseFrame(legacy, sizeof(legacy), parsed));
  assert(parsed.headerLength == 24);
  size_t offset = 0;
  assert(findEapol(legacy, sizeof(legacy), parsed, offset));
  assert(offset == 32);

  uint8_t qos[64] = {};
  qos[0] = 0x88;
  qos[1] = 0x80;  // HT Control follows QoS Control.
  std::memcpy(qos + 30, llc, sizeof(llc));
  assert(parseFrame(qos, sizeof(qos), parsed));
  assert(parsed.headerLength == 30);
  assert(findEapol(qos, sizeof(qos), parsed, offset));
  assert(offset == 38);
}

int main() {
  test_beacon_and_security_ies();
  test_eapol_data_frame();
  test_malformed_ies_are_rejected();
  test_order_bit_only_adds_ht_control_to_qos_data();
  return 0;
}
