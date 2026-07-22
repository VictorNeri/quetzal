#include "ble_assessment.h"

#include "ble_assessment_logic.h"
#include "ble_compat.h"
#include "ble_hid_inject.h"
#include "shared.h"
#include "Touchscreen.h"
#include "utils.h"

#include <LittleFS.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

extern "C" {
#include "host/ble_gatt.h"
#include "os/os_mbuf.h"
}

namespace BleAssessment {

using BleAssessmentLogic::MESH_PROVISIONING_UUID;

constexpr size_t MAX_DEVICES = 24;
constexpr size_t MAX_ADV_BYTES = 96;
constexpr size_t MAX_CHARACTERISTICS = 40;
constexpr size_t MAX_SUBSCRIPTIONS = 8;
constexpr size_t MAX_REPLAY_BYTES = 64;
constexpr uint8_t MAX_CONNECTION_ATTEMPTS = 10;
constexpr uint8_t MAX_ATT_REQUESTS = 24;
constexpr uint32_t ACTIVE_TEST_LIMIT_MS = 15000;
constexpr uint32_t NOTIFICATION_WINDOW_MS = 10000;
constexpr uint32_t CONNECT_INTERVAL_MS = 700;
constexpr char AUTHORIZATION_REQUIRED[] = "OWNED/AUTHORIZED TARGET ONLY";
constexpr char BASELINE_PATH[] = "/ble_baseline.bin";

const char* const TOOL_NAMES[] = {
    "Security Auditor", "GATT Permissions", "Privacy Analyzer",
    "Pairing Resilience", "Rogue Peripheral", "Notification Monitor",
    "ATT Robustness", "Connection Resilience", "Mesh Auditor",
    "Replay Tester"};
constexpr size_t TOOL_COUNT = sizeof(TOOL_NAMES) / sizeof(TOOL_NAMES[0]);
constexpr size_t TOOLS_PER_PAGE = 7;

struct DeviceRecord {
  BLEAddress address;
  char name[25]{};
  int8_t rssi = -127;
  uint8_t addressType = 0;
  uint8_t advType = 0;
  uint16_t appearance = 0;
  bool connectable = false;
  bool meshProvisioning = false;
  bool meshProxy = false;
  bool meshBeacon = false;
  bool meshMessage = false;
  uint8_t payload[MAX_ADV_BYTES]{};
  uint8_t payloadLength = 0;
  uint32_t fingerprint = 0;
};

struct CharacteristicRecord {
  BLERemoteCharacteristic* characteristic = nullptr;
  char uuid[38]{};
  bool readable = false;
  bool writable = false;
  bool writeNoResponse = false;
  bool notifiable = false;
  bool indicatable = false;
  bool readableBeforeSecurity = false;
  uint16_t observedLength = 0;
};

struct BaselineRecord {
  uint32_t magic = 0x51424c45;  // QBLE
  uint32_t fingerprint = 0;
  uint16_t appearance = 0;
  uint8_t addressType = 0;
  char name[25]{};
  char address[20]{};
};

enum class View : uint8_t {
  Suite, Devices, Security, Gatt, Privacy, Pairing,
  Rogue, Notifications, Att, Connections, Mesh, Replay
};

enum class ActiveStage : uint8_t { Idle, AwaitAuthorization, AwaitConfirmation, Running, Complete, Aborted };

DeviceRecord devices[MAX_DEVICES];
size_t deviceCount = 0;
size_t selectedDevice = 0;
bool targetSelected = false;
size_t devicePage = 0;
size_t toolPage = 0;
size_t selectedTool = 0;
View view = View::Suite;
ActiveStage activeStage = ActiveStage::Idle;
bool authorized = false;
bool confirmed = false;
BLEAddress authorizedTarget;
bool authorizedTargetValid = false;
bool touchWasDown = false;
bool suiteBlocked = false;
volatile bool callbacksEnabled = false;
uint32_t notificationGeneration = 0;


BLEScan* scan = nullptr;
BLEClient* client = nullptr;
CharacteristicRecord characteristics[MAX_CHARACTERISTICS];
size_t characteristicCount = 0;
BLERemoteCharacteristic* subscriptions[MAX_SUBSCRIPTIONS]{};
char subscriptionUuids[MAX_SUBSCRIPTIONS][38]{};
uint32_t subscriptionUnencryptedEvents[MAX_SUBSCRIPTIONS]{};
uint32_t subscriptionEncryptedEvents[MAX_SUBSCRIPTIONS]{};
size_t subscriptionCount = 0;
portMUX_TYPE notificationMux = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t notificationCount = 0;
volatile uint32_t notificationPayloadHash = 0;
volatile uint16_t notificationLastLength = 0;
uint32_t unencryptedNotificationCount = 0;
uint32_t encryptedNotificationCount = 0;
bool notificationSecuritySnapshot = false;
uint8_t subscriptionSecurityTransitions = 0;
char lastSecurityTransitionUuid[38]{};
char lastNotificationUuid[38]{};
uint32_t notificationUntil = 0;

struct RawReadContext {
  volatile bool done = false;
  int status = BLE_HS_EUNKNOWN;
  uint16_t length = 0;
  bool overflow = false;
  uint8_t data[MAX_REPLAY_BYTES]{};
};

RawReadContext rawRead;
portMUX_TYPE rawReadMux = portMUX_INITIALIZER_UNLOCKED;

bool operationFailed = false;
char operationMessage[80]{};
uint16_t serviceCount = 0;
uint16_t openReadCount = 0;
uint16_t deniedReadCount = 0;
uint16_t securityTriggeredReadCount = 0;
uint16_t writableCount = 0;
uint16_t notifyPropertyCount = 0;
bool connectionEncrypted = false;
bool connectionAuthenticated = false;
bool connectionBonded = false;
uint8_t connectionKeySize = 0;
bool pairingCallbackComplete = false;
uint32_t pairingDeadline = 0;

bool privacySameAddress = false;
bool privacyRotatedMatch = false;
uint32_t privacyOriginalFingerprint = 0;
char privacyOriginalAddress[20]{};

bool rogueBaselineLoaded = false;
bool rogueMatchSeen = false;
bool rogueFingerprintChanged = false;
BaselineRecord rogueBaseline;

uint8_t attRequests = 0;
uint8_t attSuccesses = 0;
bool attRecoveryConnected = false;
size_t attIndex = 0;
uint32_t attDeadline = 0;
uint8_t connectionAttempts = 0;
uint8_t connectionSuccesses = 0;
uint32_t nextConnectionAt = 0;

uint8_t replayData[MAX_REPLAY_BYTES]{};
size_t replayLength = 0;
BLERemoteCharacteristic* replayCharacteristic = nullptr;
bool replayCaptured = false;
bool replaySent = false;
size_t replayCandidateIndex = SIZE_MAX;
char replayUuid[38]{};
BLEAddress replayPeerAddress;
bool replayPeerValid = false;

portMUX_TYPE clientStateMux = portMUX_INITIALIZER_UNLOCKED;
bool clientDisconnected = true;

class AssessmentClientCallbacks final : public BLEClientCallbacks {
 public:
  void onConnect(BLEClient* source) override {
    if (source != client) return;
    portENTER_CRITICAL(&clientStateMux);
    clientDisconnected = false;
    portEXIT_CRITICAL(&clientStateMux);
  }

  void onConnectFail(BLEClient* source, int) override {
    if (source != client) return;
    portENTER_CRITICAL(&clientStateMux);
    clientDisconnected = true;
    portEXIT_CRITICAL(&clientStateMux);
  }

  void onDisconnect(BLEClient* source, int) override {
    if (source != client) return;
    portENTER_CRITICAL(&clientStateMux);
    clientDisconnected = true;
    portEXIT_CRITICAL(&clientStateMux);
  }

  void onAuthenticationComplete(NimBLEConnInfo& info) override {
    if (client == nullptr || info.getConnHandle() != client->getConnHandle()) return;
    portENTER_CRITICAL(&clientStateMux);
    connectionEncrypted = info.isEncrypted();
    connectionAuthenticated = info.isAuthenticated();
    connectionBonded = info.isBonded();
    connectionKeySize = info.getSecKeySize();
    pairingCallbackComplete = true;
    portEXIT_CRITICAL(&clientStateMux);
    portENTER_CRITICAL(&notificationMux);
    notificationSecuritySnapshot = info.isEncrypted();
    portEXIT_CRITICAL(&notificationMux);
  }
};

AssessmentClientCallbacks clientCallbacks;

bool isClientDisconnected() {
  portENTER_CRITICAL(&clientStateMux);
  const bool callbackSeen = clientDisconnected;
  portEXIT_CRITICAL(&clientStateMux);
  return callbackSeen &&
         (client == nullptr || client->getConnHandle() == BLE_HS_CONN_HANDLE_NONE);
}

void setMessage(const char* text) {
  std::snprintf(operationMessage, sizeof(operationMessage), "%s", text ? text : "");
}

void stopScan() {
  if (scan != nullptr && scan->isScanning()) scan->stop();
}

void unsubscribeAll() {
  portENTER_CRITICAL(&notificationMux);
  callbacksEnabled = false;
  ++notificationGeneration;
  portEXIT_CRITICAL(&notificationMux);
  if (client != nullptr && client->isConnected()) {
    for (size_t i = 0; i < subscriptionCount; ++i) {
      if (subscriptions[i] != nullptr) subscriptions[i]->unsubscribe(true);
    }
  }
  std::fill(std::begin(subscriptions), std::end(subscriptions), nullptr);
  subscriptionCount = 0;
}

void disconnectClient() {
  unsubscribeAll();
  if (client != nullptr && client->isConnected()) {
    client->disconnect();
  }
  if (client != nullptr && !isClientDisconnected()) {
    const uint32_t deadline = millis() + 1200;
    while (!isClientDisconnected() && static_cast<int32_t>(millis() - deadline) < 0) delay(1);
    if (!isClientDisconnected()) {
      operationFailed = true;
      setMessage("BLE disconnect timeout; client not reused");
    }
  }
  characteristicCount = 0;
  replayCharacteristic = nullptr;
}

void destroyClient() {
  disconnectClient();
  if (client != nullptr) {
    BLEDevice::deleteClient(client);
    client = nullptr;
  }
}

bool emergencyStop() {
  if (feature_exit_requested) return true;
  if (ts.touched()) {
    TS_Point p = ts.getPoint();
    const int y = ::map(p.y, TS_MAXY, TS_MINY, 0, 319);
    if (y < 250) return false;
    activeStage = ActiveStage::Aborted;
    setMessage("STOP requested; operation aborted");
    disconnectClient();
    return true;
  }
  return false;
}

void waitForTouchRelease() {
  const uint32_t deadline = millis() + 1000;
  while (ts.touched() && static_cast<int32_t>(millis() - deadline) < 0) {
    delay(5);
  }
  touchWasDown = false;
}

const char* addressPrivacy(const BLEAddress& address) {
  if (address.isPublic()) return "Public (stable)";
  if (address.isRpa()) return "Resolvable private";
  if (address.isNrpa()) return "Non-resolvable private";
  if (address.isStatic()) return "Random static";
  return "Unknown/random";
}

bool hasMeshUuid(const BLEAdvertisedDevice& adv, uint16_t uuid) {
  return adv.isAdvertisingService(BLEUUID(uuid));
}

bool scanInventory(uint32_t durationMs = 4000) {
  const bool preserveSelection = targetSelected && selectedDevice < deviceCount;
  BLEAddress previousAddress;
  if (preserveSelection) previousAddress = devices[selectedDevice].address;
  stopScan();
  disconnectClient();
  if (!BLEDevice::isInitialized() && !BLEDevice::init("Quetzal Assessment")) {
    setMessage("BLE controller initialization failed");
    operationFailed = true;
    return false;
  }
  scan = BLEDevice::getScan();
  if (scan == nullptr) {
    setMessage("BLE scanner unavailable");
    operationFailed = true;
    return false;
  }
  scan->setActiveScan(false);
  scan->setInterval(70);
  scan->setWindow(35);
  scan->setMaxResults(MAX_DEVICES);
  BLEScanResults results = scan->getResults(durationMs, false);
  deviceCount = 0;
  const int count = results.getCount();
  for (int i = 0; i < count && deviceCount < MAX_DEVICES; ++i) {
    const BLEAdvertisedDevice* adv = results.getDevice(i);
    if (adv == nullptr) continue;
    DeviceRecord& out = devices[deviceCount++];
    out = DeviceRecord{};
    out.address = adv->getAddress();
    out.addressType = adv->getAddressType();
    out.advType = adv->getAdvType();
    out.rssi = adv->getRSSI();
    out.appearance = adv->haveAppearance() ? adv->getAppearance() : 0;
    out.connectable = adv->isConnectable();
    out.meshProvisioning = hasMeshUuid(*adv, BleAssessmentLogic::MESH_PROVISIONING_UUID);
    out.meshProxy = hasMeshUuid(*adv, BleAssessmentLogic::MESH_PROXY_UUID);
    const std::string name = adv->getName();
    std::snprintf(out.name, sizeof(out.name), "%s", name.empty() ? "(unnamed)" : name.c_str());
    const std::vector<uint8_t>& payload = adv->getPayload();
    out.payloadLength = static_cast<uint8_t>(BleAssessmentLogic::boundedCopy(
        out.payload, sizeof(out.payload), payload.data(), payload.size()));
    out.meshBeacon = BleAssessmentLogic::hasAdType(out.payload, out.payloadLength, 0x2b) ||
                     BleAssessmentLogic::hasAdType(out.payload, out.payloadLength, 0x29);
    out.meshMessage = BleAssessmentLogic::hasAdType(out.payload, out.payloadLength, 0x2a);
    out.fingerprint = BleAssessmentLogic::fingerprintAdvertisement(
        out.payload, out.payloadLength, out.appearance, out.addressType);
  }
  targetSelected = false;
  selectedDevice = 0;
  if (preserveSelection) {
    for (size_t i = 0; i < deviceCount; ++i) {
      if (devices[i].address == previousAddress) {
        selectedDevice = i;
        targetSelected = true;
        break;
      }
    }
  }
  scan->clearResults();
  operationFailed = false;
  if (preserveSelection && !targetSelected) setMessage("Target not rediscovered; select again");
  else setMessage(deviceCount ? "Scan complete" : "No BLE devices retained");
  return deviceCount > 0;
}

bool connectSelected(bool discover = true) {
  stopScan();
  disconnectClient();
  if (!targetSelected || selectedDevice >= deviceCount || !devices[selectedDevice].connectable) {
    operationFailed = true;
    setMessage("Select a connectable target first");
    return false;
  }
  if (client != nullptr && !isClientDisconnected()) {
    operationFailed = true;
    setMessage("Previous BLE disconnect still pending");
    return false;
  }
  if (client == nullptr) {
    client = BLEDevice::createClient();
    if (client != nullptr) client->setClientCallbacks(&clientCallbacks, false);
  }
  if (client == nullptr) {
    operationFailed = true;
    setMessage("Unable to allocate BLE client");
    return false;
  }
  client->setConnectTimeout(5000);
  client->setConnectRetries(0);
  portENTER_CRITICAL(&clientStateMux);
  clientDisconnected = false;
  portEXIT_CRITICAL(&clientStateMux);
  if (!client->connect(devices[selectedDevice].address, true, false, true)) {
    portENTER_CRITICAL(&clientStateMux);
    clientDisconnected = true;
    portEXIT_CRITICAL(&clientStateMux);
    operationFailed = true;
    setMessage("Connection failed");
    return false;
  }
  if (discover && !client->discoverAttributes()) {
    setMessage("Connected; GATT discovery incomplete");
  } else {
    setMessage("Connected");
  }
  operationFailed = false;
  NimBLEConnInfo info = client->getConnInfo();
  connectionEncrypted = info.isEncrypted();
  connectionAuthenticated = info.isAuthenticated();
  connectionBonded = info.isBonded();
  connectionKeySize = info.getSecKeySize();
  return true;
}

int rawReadCallback(uint16_t, const ble_gatt_error* error, ble_gatt_attr* attr, void*) {
  int status = error != nullptr ? error->status : BLE_HS_EUNKNOWN;
  uint16_t length = 0;
  bool overflow = false;
  if (status == 0 && attr != nullptr && attr->om != nullptr) {
    const uint32_t actualLength = OS_MBUF_PKTLEN(attr->om);
    overflow = actualLength > MAX_REPLAY_BYTES;
    length = overflow ? 0 : static_cast<uint16_t>(actualLength);
    if (!overflow && length > 0 && os_mbuf_copydata(attr->om, 0, length, rawRead.data) != 0) {
      status = BLE_HS_EUNKNOWN;
      length = 0;
    }
  }
  portENTER_CRITICAL(&rawReadMux);
  rawRead.status = status;
  rawRead.length = length;
  rawRead.overflow = overflow;
  rawRead.done = true;
  portEXIT_CRITICAL(&rawReadMux);
  return status;
}

int rawLongReadCallback(uint16_t, const ble_gatt_error* error, ble_gatt_attr* attr, void*) {
  const int callbackStatus = error != nullptr ? error->status : BLE_HS_EUNKNOWN;
  if (callbackStatus == 0 && attr != nullptr && attr->om != nullptr) {
    const uint32_t chunkLength = OS_MBUF_PKTLEN(attr->om);
    portENTER_CRITICAL(&rawReadMux);
    const uint16_t currentLength = rawRead.length;
    portEXIT_CRITICAL(&rawReadMux);
    bool copyFailed = false;
    const bool overflow = chunkLength > MAX_REPLAY_BYTES - currentLength;
    if (!overflow && chunkLength > 0 &&
        os_mbuf_copydata(attr->om, 0, chunkLength, rawRead.data + currentLength) != 0) {
      copyFailed = true;
    }
    portENTER_CRITICAL(&rawReadMux);
    if (overflow) {
      rawRead.overflow = true;
      rawRead.status = BLE_HS_EAPP;
      rawRead.done = true;
    } else if (copyFailed) {
      rawRead.status = BLE_HS_EUNKNOWN;
      rawRead.done = true;
    } else rawRead.length = currentLength + static_cast<uint16_t>(chunkLength);
    portEXIT_CRITICAL(&rawReadMux);
    if (overflow) return BLE_HS_EAPP;
    return copyFailed ? BLE_HS_EUNKNOWN : 0;
  }

  portENTER_CRITICAL(&rawReadMux);
  rawRead.status = callbackStatus == BLE_HS_EDONE && !rawRead.overflow ? 0 : callbackStatus;
  rawRead.done = true;
  portEXIT_CRITICAL(&rawReadMux);
  return callbackStatus == BLE_HS_EDONE ? 0 : callbackStatus;
}

bool probeRead(uint16_t handle, bool allowStop) {
  if (client == nullptr || !client->isConnected()) return false;
  portENTER_CRITICAL(&rawReadMux);
  rawRead.done = false;
  rawRead.status = BLE_HS_EUNKNOWN;
  rawRead.length = 0;
  rawRead.overflow = false;
  std::memset(rawRead.data, 0, sizeof(rawRead.data));
  portEXIT_CRITICAL(&rawReadMux);
  const int startStatus = ble_gattc_read(client->getConnHandle(), handle, rawReadCallback, nullptr);
  if (startStatus != 0) {
    rawRead.status = startStatus;
    return false;
  }
  const uint32_t deadline = millis() + 5000;
  while (true) {
    bool done;
    int status;
    portENTER_CRITICAL(&rawReadMux);
    done = rawRead.done;
    status = rawRead.status;
    portEXIT_CRITICAL(&rawReadMux);
    if (done) return status == 0;
    if ((allowStop && emergencyStop()) || static_cast<int32_t>(millis() - deadline) >= 0) {
      disconnectClient();
      portENTER_CRITICAL(&rawReadMux);
      rawRead.status = BLE_HS_ETIMEOUT;
      portEXIT_CRITICAL(&rawReadMux);
      return false;
    }
    delay(1);
  }
}

bool probeReadLong(uint16_t handle) {
  if (client == nullptr || !client->isConnected()) return false;
  portENTER_CRITICAL(&rawReadMux);
  rawRead.done = false;
  rawRead.status = BLE_HS_EUNKNOWN;
  rawRead.length = 0;
  rawRead.overflow = false;
  std::memset(rawRead.data, 0, sizeof(rawRead.data));
  portEXIT_CRITICAL(&rawReadMux);
  const int startStatus = ble_gattc_read_long(client->getConnHandle(), handle, 0,
                                               rawLongReadCallback, nullptr);
  if (startStatus != 0) return false;
  const uint32_t deadline = millis() + 5000;
  while (true) {
    bool done;
    int status;
    portENTER_CRITICAL(&rawReadMux);
    done = rawRead.done;
    status = rawRead.status;
    portEXIT_CRITICAL(&rawReadMux);
    if (done) return status == 0;
    if (static_cast<int32_t>(millis() - deadline) >= 0) {
      disconnectClient();
      return false;
    }
    delay(1);
  }
}

void collectCharacteristics(bool attemptReads) {
  characteristicCount = 0;
  serviceCount = openReadCount = deniedReadCount = securityTriggeredReadCount = 0;
  writableCount = notifyPropertyCount = 0;
  if (client == nullptr || !client->isConnected()) return;
  const std::vector<BLERemoteService*>& services = client->getServices(true);
  for (BLERemoteService* service : services) {
    if (service == nullptr) continue;
    ++serviceCount;
    const std::vector<BLERemoteCharacteristic*>& chars = service->getCharacteristics(true);
    for (BLERemoteCharacteristic* characteristic : chars) {
      if (characteristic == nullptr || characteristicCount >= MAX_CHARACTERISTICS) break;
      CharacteristicRecord& out = characteristics[characteristicCount++];
      out = CharacteristicRecord{};
      out.characteristic = characteristic;
      std::snprintf(out.uuid, sizeof(out.uuid), "%s", characteristic->getUUID().toString().c_str());
      out.readable = characteristic->canRead();
      out.writable = characteristic->canWrite();
      out.writeNoResponse = characteristic->canWriteNoResponse();
      out.notifiable = characteristic->canNotify();
      out.indicatable = characteristic->canIndicate();
      if (out.writable || out.writeNoResponse) ++writableCount;
      if (out.notifiable || out.indicatable) ++notifyPropertyCount;
      const bool unencryptedBeforeRead = client->isConnected() && !client->getConnInfo().isEncrypted();
      if (attemptReads && out.readable && unencryptedBeforeRead) {
        const bool readAccepted = probeRead(characteristic->getHandle(), false);
        const bool encryptedAfterRead = client->isConnected() && client->getConnInfo().isEncrypted();
        if (encryptedAfterRead) {
          ++securityTriggeredReadCount;
        } else if (readAccepted) {
          out.readableBeforeSecurity = true;
          out.observedLength = rawRead.length;
          ++openReadCount;
        } else {
          ++deniedReadCount;
        }
      }
      if (client == nullptr || !client->isConnected()) break;
    }
    if (client == nullptr || !client->isConnected() || characteristicCount >= MAX_CHARACTERISTICS) break;
  }
}

void notificationCallback(uint32_t generation, BLERemoteCharacteristic* source,
                          uint8_t* data, size_t length) {
  const size_t bounded = std::min<size_t>(length, MAX_REPLAY_BYTES);
  const uint32_t hash = BleAssessmentLogic::fnv1a(data, bounded);
  portENTER_CRITICAL(&notificationMux);
  if (!callbacksEnabled || generation != notificationGeneration) {
    portEXIT_CRITICAL(&notificationMux);
    return;
  }
  notificationCount = notificationCount + 1;
  if (notificationSecuritySnapshot) ++encryptedNotificationCount;
  else ++unencryptedNotificationCount;
  for (size_t i = 0; i < subscriptionCount; ++i) {
    if (subscriptions[i] != source) continue;
    if (notificationSecuritySnapshot) ++subscriptionEncryptedEvents[i];
    else ++subscriptionUnencryptedEvents[i];
    std::snprintf(lastNotificationUuid, sizeof(lastNotificationUuid), "%s", subscriptionUuids[i]);
    break;
  }
  notificationLastLength = static_cast<uint16_t>(std::min<size_t>(length, 65535));
  notificationPayloadHash = hash;
  portEXIT_CRITICAL(&notificationMux);
}

void resetOperation() {
  disconnectClient();
  operationFailed = false;
  setMessage("");
  serviceCount = openReadCount = deniedReadCount = securityTriggeredReadCount = 0;
  writableCount = notifyPropertyCount = 0;
  connectionEncrypted = connectionAuthenticated = connectionBonded = false;
  connectionKeySize = 0;
  pairingCallbackComplete = false;
  pairingDeadline = 0;
  authorized = confirmed = false;
  authorizedTargetValid = false;
  activeStage = ActiveStage::Idle;
  attRequests = attSuccesses = 0;
  attRecoveryConnected = false;
  connectionAttempts = connectionSuccesses = 0;
  notificationUntil = 0;
  replayLength = 0;
  replayCaptured = replaySent = false;
  replayCandidateIndex = SIZE_MAX;
  replayUuid[0] = '\0';
  replayPeerValid = false;
}

void runSecurityAuditor() {
  resetOperation();
  if (connectSelected(true)) collectCharacteristics(true);
}

void runGattValidator() {
  resetOperation();
  if (connectSelected(true)) {
    // Read-only enforcement validation: writes are never automatic.
    collectCharacteristics(true);
  }
}

void runPrivacyAnalyzer() {
  resetOperation();
  if (!targetSelected || selectedDevice >= deviceCount) return;
  privacyOriginalFingerprint = devices[selectedDevice].fingerprint;
  std::snprintf(privacyOriginalAddress, sizeof(privacyOriginalAddress), "%s",
                devices[selectedDevice].address.toString().c_str());
  privacySameAddress = privacyRotatedMatch = false;
  scanInventory(5000);
  for (size_t i = 0; i < deviceCount; ++i) {
    const bool sameAddress = devices[i].address.toString() == privacyOriginalAddress;
    if (sameAddress) privacySameAddress = true;
    if (!sameAddress && devices[i].fingerprint == privacyOriginalFingerprint) privacyRotatedMatch = true;
  }
}

void runPairingTest() {
  activeStage = ActiveStage::Running;
  waitForTouchRelease();
  if (!connectSelected(true)) {
    activeStage = ActiveStage::Aborted;
    return;
  }
  portENTER_CRITICAL(&clientStateMux);
  pairingCallbackComplete = false;
  portEXIT_CRITICAL(&clientStateMux);
  pairingDeadline = millis() + ACTIVE_TEST_LIMIT_MS;
  if (!client->secureConnection(true)) {
    setMessage("Secure-connection request rejected");
    activeStage = ActiveStage::Complete;
    return;
  }
  setMessage("Pairing request active; bottom tap stops");
}

void processPairingTest() {
  if (activeStage != ActiveStage::Running) return;
  if (emergencyStop()) return;
  if (client == nullptr || !client->isConnected()) {
    setMessage("Pairing ended by disconnect");
    activeStage = ActiveStage::Complete;
    return;
  }
  bool complete;
  portENTER_CRITICAL(&clientStateMux);
  complete = pairingCallbackComplete;
  portEXIT_CRITICAL(&clientStateMux);
  if (complete) {
    setMessage(connectionAuthenticated ? "Authenticated pairing accepted" :
                                         (connectionEncrypted ? "Just Works / no MITM authentication" :
                                                                "Pairing completed without encryption"));
    activeStage = ActiveStage::Complete;
  } else if (static_cast<int32_t>(millis() - pairingDeadline) >= 0) {
    disconnectClient();
    setMessage("Pairing deadline reached; connection closed");
    activeStage = ActiveStage::Aborted;
  }
}

bool saveRogueBaseline() {
  if (!targetSelected || selectedDevice >= deviceCount || !LittleFS.begin(false)) return false;
  BaselineRecord record;
  record.fingerprint = devices[selectedDevice].fingerprint;
  record.appearance = devices[selectedDevice].appearance;
  record.addressType = devices[selectedDevice].addressType;
  std::snprintf(record.name, sizeof(record.name), "%s", devices[selectedDevice].name);
  std::snprintf(record.address, sizeof(record.address), "%s", devices[selectedDevice].address.toString().c_str());
  File file = LittleFS.open(BASELINE_PATH, "w");
  if (!file) return false;
  const bool ok = file.write(reinterpret_cast<const uint8_t*>(&record), sizeof(record)) == sizeof(record);
  file.close();
  return ok;
}

bool loadRogueBaseline() {
  rogueBaselineLoaded = false;
  if (!LittleFS.begin(false)) return false;
  File file = LittleFS.open(BASELINE_PATH, "r");
  if (!file || file.size() != sizeof(BaselineRecord)) {
    if (file) file.close();
    return false;
  }
  const bool ok = file.read(reinterpret_cast<uint8_t*>(&rogueBaseline), sizeof(rogueBaseline)) == sizeof(rogueBaseline) &&
                  rogueBaseline.magic == 0x51424c45;
  file.close();
  rogueBaselineLoaded = ok;
  return ok;
}

void runRogueComparison() {
  resetOperation();
  rogueMatchSeen = rogueFingerprintChanged = false;
  if (!loadRogueBaseline()) {
    setMessage("No enrolled baseline; tap action to save");
    return;
  }
  scanInventory(5000);
  for (size_t i = 0; i < deviceCount; ++i) {
    const bool sameAddress = devices[i].address.toString() == rogueBaseline.address;
    const bool namedIdentity = std::strncmp(rogueBaseline.name, "(unnamed)", sizeof(rogueBaseline.name)) != 0;
    const bool sameName = namedIdentity &&
        std::strncmp(devices[i].name, rogueBaseline.name, sizeof(rogueBaseline.name)) == 0;
    if (!sameAddress && !sameName) continue;
    rogueMatchSeen = true;
    if (devices[i].fingerprint != rogueBaseline.fingerprint ||
        devices[i].appearance != rogueBaseline.appearance) {
      rogueFingerprintChanged = true;
    }
  }
  setMessage(!rogueMatchSeen ? "Baseline not seen: inconclusive" :
             rogueFingerprintChanged ? "ALERT: enrolled identity changed" : "Baseline fingerprint matched");
}

void runNotificationMonitor() {
  disconnectClient();
  activeStage = ActiveStage::Running;
  operationFailed = false;
  waitForTouchRelease();
  if (!connectSelected(true)) {
    activeStage = ActiveStage::Aborted;
    return;
  }
  collectCharacteristics(false);
  uint32_t generation;
  portENTER_CRITICAL(&notificationMux);
  notificationCount = 0;
  notificationPayloadHash = 0;
  notificationLastLength = 0;
  unencryptedNotificationCount = 0;
  encryptedNotificationCount = 0;
  subscriptionSecurityTransitions = 0;
  lastSecurityTransitionUuid[0] = '\0';
  lastNotificationUuid[0] = '\0';
  notificationSecuritySnapshot = client->isConnected() && client->getConnInfo().isEncrypted();
  callbacksEnabled = true;
  generation = ++notificationGeneration;
  portEXIT_CRITICAL(&notificationMux);
  for (size_t i = 0; i < characteristicCount && subscriptionCount < MAX_SUBSCRIPTIONS; ++i) {
    CharacteristicRecord& record = characteristics[i];
    if (!record.notifiable && !record.indicatable) continue;
    const bool notifications = record.notifiable;
    const bool encryptedBefore = client->isConnected() && client->getConnInfo().isEncrypted();
    const size_t slot = subscriptionCount;
    portENTER_CRITICAL(&notificationMux);
    subscriptions[slot] = record.characteristic;
    std::snprintf(subscriptionUuids[slot], sizeof(subscriptionUuids[slot]), "%s", record.uuid);
    subscriptionUnencryptedEvents[slot] = 0;
    subscriptionEncryptedEvents[slot] = 0;
    ++subscriptionCount;
    portEXIT_CRITICAL(&notificationMux);
    const bool subscribed = record.characteristic->subscribe(
        notifications,
        [generation](BLERemoteCharacteristic* source, uint8_t* data, size_t length, bool) {
          notificationCallback(generation, source, data, length);
        },
        true);
    const bool encryptedAfter = client->isConnected() && client->getConnInfo().isEncrypted();
    if (!subscribed) {
      portENTER_CRITICAL(&notificationMux);
      --subscriptionCount;
      subscriptions[slot] = nullptr;
      subscriptionUuids[slot][0] = '\0';
      portEXIT_CRITICAL(&notificationMux);
      continue;
    }
    if (!encryptedBefore && encryptedAfter) {
      ++subscriptionSecurityTransitions;
      std::snprintf(lastSecurityTransitionUuid, sizeof(lastSecurityTransitionUuid), "%s",
                    subscriptionUuids[slot]);
    }
    portENTER_CRITICAL(&notificationMux);
    notificationSecuritySnapshot = encryptedAfter;
    portEXIT_CRITICAL(&notificationMux);
  }
  if (subscriptionCount == 0) {
    callbacksEnabled = false;
    notificationUntil = 0;
    activeStage = ActiveStage::Complete;
  } else {
    notificationUntil = millis() + NOTIFICATION_WINDOW_MS;
  }
  connectionEncrypted = client->isConnected() && client->getConnInfo().isEncrypted();
  if (!subscriptionCount) setMessage("No subscribable characteristics");
  else if (connectionEncrypted) setMessage("Subscription secured; not pre-security leakage");
  else setMessage("Monitoring pre-security metadata");
}

void startAttRobustness() {
  activeStage = ActiveStage::Running;
  attRequests = attSuccesses = 0;
  attRecoveryConnected = false;
  attIndex = 0;
  attDeadline = millis() + ACTIVE_TEST_LIMIT_MS;
  waitForTouchRelease();
  if (!connectSelected(true)) {
    activeStage = ActiveStage::Aborted;
    return;
  }
  collectCharacteristics(false);
  setMessage("Bounded read probes running; bottom=STOP");
}

void processAttRobustness() {
  if (activeStage != ActiveStage::Running) return;
  if (emergencyStop()) return;
  if (attRequests >= MAX_ATT_REQUESTS || attIndex >= characteristicCount ||
      static_cast<int32_t>(millis() - attDeadline) >= 0) {
    disconnectClient();
    if (!emergencyStop()) {
      attRecoveryConnected = connectSelected(false);
      disconnectClient();
    }
    setMessage(attRecoveryConnected ? "Bounded read probes; target recovered" : "Recovery reconnect failed/inconclusive");
    activeStage = ActiveStage::Complete;
    return;
  }
  CharacteristicRecord& record = characteristics[attIndex++];
  if (!record.readable) return;
  ++attRequests;
  if (probeRead(record.characteristic->getHandle(), true)) ++attSuccesses;
  if (client == nullptr || !client->isConnected()) {
    if (activeStage != ActiveStage::Aborted) {
      activeStage = ActiveStage::Aborted;
      setMessage("ATT probe timed out/disconnected");
    }
  }
}

void startConnectionTest() {
  disconnectClient();
  operationFailed = false;
  attRequests = attSuccesses = 0;
  attRecoveryConnected = false;
  connectionAttempts = connectionSuccesses = 0;
  activeStage = ActiveStage::Running;
  waitForTouchRelease();
  nextConnectionAt = millis();
  setMessage("Bounded connect/disconnect test running");
}

void processConnectionTest() {
  if (activeStage != ActiveStage::Running) return;
  if (emergencyStop()) return;
  if (connectionAttempts >= MAX_CONNECTION_ATTEMPTS) {
    activeStage = ActiveStage::Complete;
    setMessage(connectionSuccesses ? "Connection test complete; reconnect observed" :
                                     "Connection test complete; no reconnect observed");
    return;
  }
  if (static_cast<int32_t>(millis() - nextConnectionAt) < 0) return;
  ++connectionAttempts;
  if (connectSelected(false)) {
    ++connectionSuccesses;
    disconnectClient();
  }
  nextConnectionAt = millis() + CONNECT_INTERVAL_MS;
}

void runMeshAuditor() {
  resetOperation();
  scanInventory(5000);
  size_t provisioning = 0, proxy = 0, beacons = 0, messages = 0;
  for (size_t i = 0; i < deviceCount; ++i) {
    provisioning += devices[i].meshProvisioning ? 1 : 0;
    proxy += devices[i].meshProxy ? 1 : 0;
    beacons += devices[i].meshBeacon ? 1 : 0;
    messages += devices[i].meshMessage ? 1 : 0;
  }
  std::snprintf(operationMessage, sizeof(operationMessage), "Prov:%u proxy:%u beacon:%u msg:%u",
                static_cast<unsigned>(provisioning), static_cast<unsigned>(proxy),
                static_cast<unsigned>(beacons), static_cast<unsigned>(messages));
}

void captureReplayValue(bool advance = false) {
  replayCaptured = replaySent = false;
  replayLength = 0;
  replayCharacteristic = nullptr;
  replayUuid[0] = '\0';
  if (!advance || client == nullptr || !client->isConnected() || characteristicCount == 0) {
    disconnectClient();
    if (!connectSelected(true)) return;
    collectCharacteristics(false);
    replayCandidateIndex = SIZE_MAX;
  }
  const size_t start = replayCandidateIndex == SIZE_MAX ? 0 : (replayCandidateIndex + 1) % characteristicCount;
  bool oversizeRejected = false;
  for (size_t offset = 0; offset < characteristicCount; ++offset) {
    const size_t i = (start + offset) % characteristicCount;
    CharacteristicRecord& record = characteristics[i];
    if (!record.readable || (!record.writable && !record.writeNoResponse)) continue;
    const bool completeRead = probeReadLong(record.characteristic->getHandle());
    if (rawRead.overflow) {
      oversizeRejected = true;
      continue;
    }
    if (!completeRead) continue;
    if (rawRead.length == 0) continue;
    replayLength = BleAssessmentLogic::boundedCopy(
        replayData, sizeof(replayData), rawRead.data, rawRead.length);
    replayCharacteristic = record.characteristic;
    replayPeerAddress = devices[selectedDevice].address;
    replayPeerValid = true;
    replayCandidateIndex = i;
    std::snprintf(replayUuid, sizeof(replayUuid), "%s", record.uuid);
    replayCaptured = replayLength > 0;
    setMessage(replayCaptured ? "Candidate selected; inspect UUID, then confirm" : "Capture empty");
    return;
  }
  setMessage(oversizeRejected ? "Candidate exceeds 64 bytes; replay rejected" :
                                "No other readable+writable candidate found");
}

void sendReplayOnce() {
  activeStage = ActiveStage::Running;
  waitForTouchRelease();
  if (!replayCaptured || !replayPeerValid || !targetSelected ||
      devices[selectedDevice].address != replayPeerAddress || replayCharacteristic == nullptr ||
      client == nullptr || !client->isConnected()) {
    setMessage("Replay target/value no longer valid");
    activeStage = ActiveStage::Aborted;
    return;
  }
  replaySent = replayCharacteristic->writeValue(replayData, replayLength, true);
  setMessage(replaySent ? "One replay sent to selected target" : "Replay rejected or disconnected");
  activeStage = ActiveStage::Complete;
}

void drawHeader(const char* title) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(UI_CYAN, TFT_BLACK);
  tft.setCursor(6, 8); tft.print(title);
  tft.setTextColor(UI_AMBER, TFT_BLACK);
  tft.setCursor(185, 8); tft.print("< BACK");
  tft.drawFastHLine(0, 22, 240, UI_CYAN);
}

void drawSuite() {
  drawHeader("BLE Assessment Suite");
  if (suiteBlocked) {
    tft.setTextColor(UI_AMBER, TFT_BLACK);
    tft.drawString("BLE HID peer is connected.", 10, 72);
    tft.drawString("Disconnect it before assessment", 10, 90);
    tft.drawString("to protect shared BLE state.", 10, 108);
    return;
  }
  tft.setTextColor(UI_GUNMETAL, TFT_BLACK);
  tft.setCursor(5, 30);
  tft.printf("Target: %s", targetSelected ? devices[selectedDevice].name : "tap to scan/select");
  const size_t start = toolPage * TOOLS_PER_PAGE;
  const size_t end = std::min(TOOL_COUNT, start + TOOLS_PER_PAGE);
  for (size_t i = start; i < end; ++i) {
    const int y = 58 + static_cast<int>(i - start) * 30;
    tft.setTextColor(i == selectedTool ? UI_AMBER : UI_CYAN, TFT_BLACK);
    tft.setCursor(10, y); tft.printf("%u. %s", static_cast<unsigned>(i + 1), TOOL_NAMES[i]);
  }
  tft.setTextColor(UI_GUNMETAL, TFT_BLACK);
  tft.setCursor(5, 278); tft.printf("Page %u/%u  tap bottom to page",
      static_cast<unsigned>(toolPage + 1), static_cast<unsigned>((TOOL_COUNT + TOOLS_PER_PAGE - 1) / TOOLS_PER_PAGE));
  tft.setCursor(5, 298); tft.print(AUTHORIZATION_REQUIRED);
}

void drawDevices() {
  drawHeader("Select BLE Target");
  const size_t start = devicePage * 10;
  const size_t end = std::min(deviceCount, start + 10);
  for (size_t i = start; i < end; ++i) {
    const int y = 34 + static_cast<int>(i - start) * 24;
    const bool selected = targetSelected && i == selectedDevice;
    tft.setTextColor(selected ? UI_AMBER : UI_CYAN, TFT_BLACK);
    tft.setCursor(5, y); tft.printf("%c %-16.16s %d", selected ? '>' : ' ', devices[i].name, devices[i].rssi);
  }
  tft.setTextColor(UI_GUNMETAL, TFT_BLACK);
  tft.setCursor(5, 282); tft.printf("Retained %u/%u; bottom=rescan/page",
      static_cast<unsigned>(deviceCount), static_cast<unsigned>(MAX_DEVICES));
}

void drawActivePrompt(const char* action) {
  tft.setTextColor(UI_AMBER, TFT_BLACK);
  if (targetSelected) {
    tft.setCursor(5, 207);
    tft.printf("Peer: %s", devices[selectedDevice].address.toString().c_str());
  }
  tft.setCursor(5, 225); tft.print(AUTHORIZATION_REQUIRED);
  tft.setCursor(5, 243);
  if (!authorized) tft.printf("Tap ACTION: authorize %s", action);
  else if (!confirmed) tft.print("Tap ACTION again: CONFIRM");
  else if (activeStage == ActiveStage::Running) tft.print("RUNNING - bottom tap is emergency STOP");
}

void drawTool() {
  drawHeader(TOOL_NAMES[static_cast<size_t>(view) - 2]);
  const DeviceRecord* target = targetSelected && selectedDevice < deviceCount ? &devices[selectedDevice] : nullptr;
  tft.setTextColor(UI_CYAN, TFT_BLACK);
  tft.setCursor(5, 30); tft.printf("Target: %s", target ? target->name : "none");
  tft.setCursor(5, 47); tft.printf("Status: %.66s", operationMessage);
  tft.setTextColor(UI_GUNMETAL, TFT_BLACK);

  switch (view) {
    case View::Security:
      if (target) {
        tft.setCursor(5, 70); tft.printf("Address: %s", addressPrivacy(target->address));
        tft.setCursor(5, 88); tft.printf("Conn:%s enc:%s auth:%s bond:%s key:%u",
            target->connectable ? "Y" : "N", connectionEncrypted ? "Y" : "N",
            connectionAuthenticated ? "Y" : "N", connectionBonded ? "Y" : "N", connectionKeySize);
        tft.setCursor(5, 106); tft.printf("Svc:%u Char:%u open reads:%u",
            serviceCount, static_cast<unsigned>(characteristicCount), openReadCount);
        tft.setCursor(5, 124); tft.printf("Write props:%u notify props:%u", writableCount, notifyPropertyCount);
      }
      break;
    case View::Gatt:
      tft.setCursor(5, 70); tft.printf("Services:%u characteristics:%u", serviceCount, static_cast<unsigned>(characteristicCount));
      tft.setCursor(5, 88); tft.printf("Pre-security reads accepted:%u", openReadCount);
      tft.setCursor(5, 106); tft.printf("Denied:%u security-trigger:%u", deniedReadCount, securityTriggeredReadCount);
      tft.setCursor(5, 124); tft.printf("Writable:%u notify:%u", writableCount, notifyPropertyCount);
      tft.setCursor(5, 142); tft.print("Safe mode: no write probes sent");
      break;
    case View::Privacy:
      if (target) {
        tft.setCursor(5, 70); tft.printf("Type: %s", addressPrivacy(target->address));
        tft.setCursor(5, 88); tft.printf("Same address seen:%s", privacySameAddress ? "YES" : "NO");
        tft.setCursor(5, 106); tft.printf("Fingerprint at new addr:%s", privacyRotatedMatch ? "YES" : "NO");
        tft.setCursor(5, 124); tft.printf("Stable adv fingerprint:%08lx", static_cast<unsigned long>(privacyOriginalFingerprint));
      }
      break;
    case View::Pairing:
      tft.setCursor(5, 70); tft.printf("Encrypted:%s authenticated:%s", connectionEncrypted ? "YES" : "NO", connectionAuthenticated ? "YES" : "NO");
      tft.setCursor(5, 88); tft.printf("Bonded:%s key size:%u", connectionBonded ? "YES" : "NO", connectionKeySize);
      tft.setCursor(5, 106); tft.print("No PIN guessing or bond deletion");
      drawActivePrompt("single pairing check");
      break;
    case View::Rogue:
      tft.setCursor(5, 70); tft.printf("Baseline:%s", rogueBaselineLoaded ? "loaded" : "not enrolled");
      tft.setCursor(5, 88); tft.printf("Seen:%s fingerprint changed:%s", rogueMatchSeen ? "YES" : "NO", rogueFingerprintChanged ? "YES" : "NO");
      tft.setCursor(5, 225); tft.print("ACTION: enroll selected; bottom: compare");
      break;
    case View::Notifications: {
      uint32_t count, hash, preSecurityCount, securedCount; uint16_t length;
      portENTER_CRITICAL(&notificationMux);
      count = notificationCount; hash = notificationPayloadHash; length = notificationLastLength;
      preSecurityCount = unencryptedNotificationCount;
      securedCount = encryptedNotificationCount;
      portEXIT_CRITICAL(&notificationMux);
      tft.setCursor(5, 70); tft.printf("Subscribed:%u events:%lu", static_cast<unsigned>(subscriptionCount), static_cast<unsigned long>(count));
      tft.setCursor(5, 88); tft.printf("Last length:%u hash:%08lx", length, static_cast<unsigned long>(hash));
      tft.setCursor(5, 106); tft.print("Payload not retained; metadata only");
      tft.setCursor(5, 124); tft.printf("Pre-sec:%lu secured:%lu", static_cast<unsigned long>(preSecurityCount), static_cast<unsigned long>(securedCount));
      tft.setCursor(5, 142); tft.printf("Security transitions:%u", subscriptionSecurityTransitions);
      tft.setCursor(5, 160); tft.printf("Connection encrypted:%s", connectionEncrypted ? "YES" : "NO");
      if (count) {
        tft.setCursor(5, 178); tft.printf("Last event: %.25s", lastNotificationUuid);
      }
      if (subscriptionSecurityTransitions) {
        tft.setCursor(5, 196); tft.printf("Last transition: %.22s", lastSecurityTransitionUuid);
      }
      drawActivePrompt("notification subscription");
      break;
    }
    case View::Att:
      tft.setCursor(5, 70); tft.printf("Requests:%u/%u success:%u", attRequests, MAX_ATT_REQUESTS, attSuccesses);
      tft.setCursor(5, 88); tft.printf("Recovery reconnect:%s", attRecoveryConnected ? "YES" : "NO/inconclusive");
      tft.setCursor(5, 106); tft.print("Read-only bounded probes; no mutation");
      drawActivePrompt("ATT read resilience");
      break;
    case View::Connections:
      tft.setCursor(5, 70); tft.printf("Attempts:%u/%u successes:%u", connectionAttempts, MAX_CONNECTION_ATTEMPTS, connectionSuccesses);
      tft.setCursor(5, 88); tft.print("700ms minimum interval; STOP available");
      drawActivePrompt("connect resilience");
      break;
    case View::Mesh:
      tft.setCursor(5, 70); tft.print("Passive UUID exposure scan only");
      tft.setCursor(5, 88); tft.printf("Provisioning UUID: 0x%04x", MESH_PROVISIONING_UUID);
      break;
    case View::Replay:
      tft.setCursor(5, 70); tft.printf("Captured:%s bytes:%u", replayCaptured ? "YES" : "NO", static_cast<unsigned>(replayLength));
      tft.setCursor(5, 88); tft.printf("Replay sent:%s", replaySent ? "YES" : "NO");
      tft.setCursor(5, 106); tft.printf("UUID:%.32s", replayUuid[0] ? replayUuid : "none");
      tft.setCursor(5, 124); tft.print("Same target+characteristic; one write max");
      tft.setCursor(150, 268); tft.print("NEXT CHAR");
      drawActivePrompt("single replay");
      break;
    default: break;
  }
  tft.setTextColor(UI_AMBER, TFT_BLACK);
  tft.setCursor(5, 286); tft.print("ACTION");
  tft.setCursor(160, 286); tft.print("RESCAN/RUN");
}

void enterTool(size_t index) {
  if (index >= TOOL_COUNT) return;
  if (!targetSelected) {
    setMessage("Select a target before opening a tool");
    drawSuite();
    return;
  }
  resetOperation();
  selectedTool = index;
  view = static_cast<View>(index + 2);
  switch (view) {
    case View::Security: runSecurityAuditor(); break;
    case View::Gatt: runGattValidator(); break;
    case View::Privacy: runPrivacyAnalyzer(); break;
    case View::Pairing: activeStage = ActiveStage::AwaitAuthorization; setMessage("Awaiting explicit authorization"); break;
    case View::Rogue: runRogueComparison(); break;
    case View::Notifications: activeStage = ActiveStage::AwaitAuthorization; setMessage("Awaiting explicit authorization"); break;
    case View::Att: activeStage = ActiveStage::AwaitAuthorization; setMessage("Awaiting explicit authorization"); break;
    case View::Connections: activeStage = ActiveStage::AwaitAuthorization; setMessage("Awaiting explicit authorization"); break;
    case View::Mesh: runMeshAuditor(); break;
    case View::Replay: activeStage = ActiveStage::AwaitAuthorization; setMessage("Authorize before capture/read"); break;
    default: break;
  }
  drawTool();
}

void handleActiveAction() {
  if (!authorized) {
    if (!targetSelected) {
      setMessage("Target selection is no longer valid");
      drawTool();
      return;
    }
    authorized = true;
    authorizedTarget = devices[selectedDevice].address;
    authorizedTargetValid = true;
    if (view == View::Replay) {
      activeStage = ActiveStage::Running;
      setMessage("Authorization recorded; capturing selected peer");
      drawTool();
      waitForTouchRelease();
      captureReplayValue(false);
      if (replayCaptured) {
        activeStage = ActiveStage::AwaitConfirmation;
      } else {
        authorized = false;
        authorizedTargetValid = false;
        activeStage = ActiveStage::AwaitAuthorization;
      }
      drawTool();
      return;
    }
    activeStage = ActiveStage::AwaitConfirmation;
    setMessage("Authorization recorded; confirm separately");
    drawTool();
    return;
  }
  if (!confirmed) {
    if (!targetSelected || !authorizedTargetValid ||
        devices[selectedDevice].address != authorizedTarget) {
      authorized = confirmed = false;
      authorizedTargetValid = false;
      activeStage = ActiveStage::AwaitAuthorization;
      setMessage("Target changed; authorization cleared");
      drawTool();
      return;
    }
    confirmed = true;
    if (view == View::Pairing) runPairingTest();
    else if (view == View::Att) startAttRobustness();
    else if (view == View::Connections) startConnectionTest();
    else if (view == View::Notifications) runNotificationMonitor();
    else if (view == View::Replay) sendReplayOnce();
    drawTool();
  }
}

void handleTap(int x, int y) {
  if (y <= 25 && x >= 165) {
    if (view == View::Suite) feature_exit_requested = true;
    else {
      disconnectClient();
      view = View::Suite;
      drawSuite();
    }
    return;
  }
  if (suiteBlocked) return;
  if (view == View::Suite) {
    if (y >= 25 && y < 53) {
      view = View::Devices;
      drawDevices();
      return;
    }
    if (y >= 52 && y < 270) {
      const size_t index = toolPage * TOOLS_PER_PAGE + static_cast<size_t>((y - 52) / 30);
      if (index < TOOL_COUNT) enterTool(index);
      return;
    }
    if (y >= 270) {
      toolPage = (toolPage + 1) % ((TOOL_COUNT + TOOLS_PER_PAGE - 1) / TOOLS_PER_PAGE);
      drawSuite();
    }
    return;
  }
  if (view == View::Devices) {
    if (y >= 28 && y < 270) {
      const size_t index = devicePage * 10 + static_cast<size_t>((y - 28) / 24);
      if (index < deviceCount) {
        selectedDevice = index;
        targetSelected = true;
        view = View::Suite;
        drawSuite();
      }
    } else if (y >= 270) {
      if (deviceCount > 10 && devicePage == 0) devicePage = 1;
      else { devicePage = 0; scanInventory(); }
      drawDevices();
    }
    return;
  }

  if (y >= 270 && x < 120) {
    if (view == View::Pairing || view == View::Notifications || view == View::Att ||
        view == View::Connections || view == View::Replay) {
      handleActiveAction();
    } else if (view == View::Rogue) {
      rogueBaselineLoaded = saveRogueBaseline();
      setMessage(rogueBaselineLoaded ? "Selected target enrolled" : "Baseline save failed");
      drawTool();
    }
  } else if (y >= 270) {
    if (view == View::Rogue) runRogueComparison();
    else if (view == View::Replay && authorized && !confirmed && authorizedTargetValid &&
             targetSelected && devices[selectedDevice].address == authorizedTarget) captureReplayValue(true);
    else if (view == View::Mesh) runMeshAuditor();
    else if (view == View::Privacy) runPrivacyAnalyzer();
    else if (view == View::Security) runSecurityAuditor();
    else if (view == View::Gatt) runGattValidator();
    drawTool();
  }
}

void setup() {
  tft.setRotation(0);
  setupTouchscreen();
  resetOperation();
  deviceCount = selectedDevice = devicePage = toolPage = selectedTool = 0;
  targetSelected = false;
  view = View::Suite;
  touchWasDown = false;
  callbacksEnabled = false;
  suiteBlocked = BleHidInject::isConnected();
  if (suiteBlocked) {
    setMessage("Disconnect BLE HID peer before assessment");
    drawSuite();
    return;
  }
  if (!BLEDevice::isInitialized()) BLEDevice::init("Quetzal Assessment");
  stopScan();
  if (BLEDevice::getAdvertising() != nullptr) BLEDevice::getAdvertising()->stop();
  scanInventory();
  drawSuite();
}

void loop() {
  if (view == View::Pairing) {
    const ActiveStage beforeStage = activeStage;
    processPairingTest();
    if (activeStage != beforeStage) drawTool();
  }
  if (view == View::Att) {
    const uint8_t before = attRequests;
    const ActiveStage beforeStage = activeStage;
    processAttRobustness();
    if (attRequests != before || activeStage != beforeStage) drawTool();
  }
  if (view == View::Connections) {
    const uint8_t before = connectionAttempts;
    const ActiveStage beforeStage = activeStage;
    processConnectionTest();
    if (connectionAttempts != before || activeStage != beforeStage) drawTool();
  }
  if (view == View::Notifications && notificationUntil != 0) {
    if (emergencyStop()) {
      notificationUntil = 0;
      drawTool();
    } else {
      const bool encryptedNow = client != nullptr && client->isConnected() &&
                                client->getConnInfo().isEncrypted();
      portENTER_CRITICAL(&notificationMux);
      notificationSecuritySnapshot = encryptedNow;
      portEXIT_CRITICAL(&notificationMux);
      connectionEncrypted = encryptedNow;
      if (static_cast<int32_t>(millis() - notificationUntil) >= 0) {
        unsubscribeAll();
        notificationUntil = 0;
        activeStage = ActiveStage::Complete;
        setMessage("Monitoring window complete");
        drawTool();
      }
    }
  }

  const bool down = ts.touched();
  if (down && !touchWasDown) {
    TS_Point p = ts.getPoint();
    const int x = ::map(p.x, TS_MINX, TS_MAXX, 0, 239);
    const int y = ::map(p.y, TS_MAXY, TS_MINY, 0, 319);
    handleTap(x, y);
  }
  touchWasDown = down;
  delay(1);
}

void cleanup() {
  callbacksEnabled = false;
  stopScan();
  destroyClient();
  deviceCount = 0;
  selectedDevice = 0;
  targetSelected = false;
  suiteBlocked = false;
  authorized = confirmed = false;
  activeStage = ActiveStage::Idle;
  view = View::Suite;
}

}  // namespace BleAssessment
