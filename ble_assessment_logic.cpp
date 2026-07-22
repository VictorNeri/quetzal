#include "ble_assessment_logic.h"

#include <algorithm>

namespace BleAssessmentLogic {

uint32_t fnv1a(const uint8_t* data, size_t length) {
  uint32_t hash = 2166136261u;
  if (data == nullptr) return hash;
  for (size_t i = 0; i < length; ++i) {
    hash ^= data[i];
    hash *= 16777619u;
  }
  return hash;
}

uint32_t fingerprintAdvertisement(const uint8_t* payload, size_t length,
                                  uint16_t appearance, uint8_t addressType) {
  const size_t bounded = std::min(length, MAX_FINGERPRINT_BYTES);
  uint32_t hash = fnv1a(payload, bounded);
  const uint8_t metadata[] = {
      static_cast<uint8_t>(appearance & 0xff),
      static_cast<uint8_t>(appearance >> 8), addressType};
  for (uint8_t byte : metadata) {
    hash ^= byte;
    hash *= 16777619u;
  }
  return hash;
}

size_t boundedCopy(uint8_t* destination, size_t capacity,
                   const uint8_t* source, size_t length) {
  if (destination == nullptr || source == nullptr || capacity == 0) return 0;
  const size_t copied = std::min(capacity, length);
  std::copy(source, source + copied, destination);
  return copied;
}

}  // namespace BleAssessmentLogic
