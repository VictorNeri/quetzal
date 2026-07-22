#pragma once

#include <cstddef>
#include <cstdint>

namespace BleAssessmentLogic {

constexpr uint16_t MESH_PROVISIONING_UUID = 0x1827;
constexpr uint16_t MESH_PROXY_UUID = 0x1828;
constexpr size_t MAX_FINGERPRINT_BYTES = 96;

uint32_t fnv1a(const uint8_t* data, size_t length);
uint32_t fingerprintAdvertisement(const uint8_t* payload, size_t length,
                                  uint16_t appearance, uint8_t addressType);
constexpr bool isMeshService(uint16_t uuid) {
  return uuid == MESH_PROVISIONING_UUID || uuid == MESH_PROXY_UUID;
}

constexpr bool hasAdType(const uint8_t* payload, size_t length, uint8_t adType) {
  if (payload == nullptr) return false;
  size_t offset = 0;
  while (offset < length) {
    const uint8_t fieldLength = payload[offset];
    if (fieldLength == 0) break;
    if (offset + 1u + fieldLength > length) return false;
    if (payload[offset + 1] == adType) return true;
    offset += 1u + fieldLength;
  }
  return false;
}
size_t boundedCopy(uint8_t* destination, size_t capacity,
                   const uint8_t* source, size_t length);

}  // namespace BleAssessmentLogic
