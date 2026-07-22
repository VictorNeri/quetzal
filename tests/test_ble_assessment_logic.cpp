#include "../ble_assessment_logic.h"

#include <cassert>
#include <cstdint>

int main() {
  using namespace BleAssessmentLogic;
  constexpr uint8_t payload[] = {0x02, 0x01, 0x06, 0x03, 0x03, 0x27, 0x18};
  static_assert(isMeshService(MESH_PROVISIONING_UUID));
  static_assert(hasAdType(payload, sizeof(payload), 0x03));
  static_assert(!hasAdType(payload, sizeof(payload), 0x29));
  assert(fnv1a(payload, sizeof(payload)) != fnv1a(payload, sizeof(payload) - 1));
  assert(fingerprintAdvertisement(payload, sizeof(payload), 0, 1) ==
         fingerprintAdvertisement(payload, sizeof(payload), 0, 1));
  assert(fingerprintAdvertisement(payload, sizeof(payload), 0, 1) !=
         fingerprintAdvertisement(payload, sizeof(payload), 1, 1));
  assert(isMeshService(MESH_PROVISIONING_UUID));
  assert(isMeshService(MESH_PROXY_UUID));
  assert(!isMeshService(0x180f));
  assert(hasAdType(payload, sizeof(payload), 0x03));
  assert(!hasAdType(payload, sizeof(payload), 0x29));
  constexpr uint8_t malformed[] = {5, 0x29, 1};
  static_assert(!hasAdType(malformed, sizeof(malformed), 0x29));
  assert(!hasAdType(malformed, sizeof(malformed), 0x29));

  uint8_t out[3] = {};
  assert(boundedCopy(out, sizeof(out), payload, sizeof(payload)) == sizeof(out));
  assert(out[0] == payload[0] && out[2] == payload[2]);
  assert(boundedCopy(nullptr, sizeof(out), payload, sizeof(payload)) == 0);
  return 0;
}
