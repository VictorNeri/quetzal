#include "wifi_assessment.h"

#include "Touchscreen.h"
#include "icon.h"
#include "shared.h"
#include "utils.h"
#include "wifi_80211.h"
#include "wifi_capture.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <algorithm>
#include <cstring>

extern TFT_eSPI tft;

namespace WifiAssessment {
namespace {
constexpr int SCREEN_W = 240;
constexpr int SCREEN_H = 320;
constexpr int TOP_Y = 20;
constexpr int BACK_X = 210;
constexpr size_t MAX_APS = 32;
constexpr size_t MAX_CLIENTS = 32;
constexpr uint32_t SURVEY_DWELL_MS = 600;
constexpr uint8_t MAX_DEAUTH_FRAMES = 10;
constexpr uint32_t DEAUTH_INTERVAL_MS = 500;
constexpr size_t MAX_FRAMES_PER_LOOP = 16;
constexpr char AUTHORIZATION_REQUIRED[] = "I'M AUTHORIZED";
constexpr uint32_t DLT_IEEE802_11_RADIO = 127;

const char* const TOOL_NAMES[] = {
    "Config Auditor", "EAPOL Capture", "Mgmt Analyzer",
    "Rogue AP Detector", "AP/Client Mapper", "WPS Scanner",
    "Channel Survey", "Deauth Resilience"};
constexpr size_t TOOL_COUNT = sizeof(TOOL_NAMES) / sizeof(TOOL_NAMES[0]);

enum class View : uint8_t { Menu, Audit, Eapol, Mgmt, Rogue, Mapper, Wps, Survey, Resilience };

struct ApEntry {
  wifi_ap_record_t ap{};
  Wifi80211::SecurityInfo security{};
  bool securitySeen{false};
};

struct ClientEntry {
  Wifi80211::MacAddress client;
  Wifi80211::MacAddress bssid;
  uint8_t channel{0};
  int8_t rssi{0};
  uint32_t lastSeen{0};
  uint16_t frames{0};
};

struct RogueBaseline {
  uint32_t magic{0};
  uint8_t bssid[6]{};
  uint8_t ssid[33]{};
  uint8_t channel{0};
  uint8_t auth{0};
  uint8_t pairwise{0};
  uint8_t group{0};
  uint8_t pmfFlags{0};
};

struct __attribute__((packed)) PcapHeader {
  uint32_t magic{0xa1b2c3d4};
  uint16_t major{2};
  uint16_t minor{4};
  int32_t timezone{0};
  uint32_t accuracy{0};
  uint32_t snaplen{512};
  uint32_t linktype{DLT_IEEE802_11_RADIO};
};

struct __attribute__((packed)) PcapRecordHeader {
  uint32_t seconds;
  uint32_t microseconds;
  uint32_t capturedLength;
  uint32_t originalLength;
};

ApEntry aps[MAX_APS];
size_t apCount = 0;
uint16_t apTotalCount = 0;
ClientEntry clients[MAX_CLIENTS];
size_t clientCount = 0;
View view = View::Menu;
bool uiDrawn = false;
bool touchWasDown = false;
size_t selectedAp = 0;
size_t auditOffset = 0;
uint32_t lastUiUpdate = 0;
File pcapFile;
String pcapPath;
uint32_t pcapPackets = 0;
bool pcapWriteFailed = false;
bool toolCaptureError = false;
uint8_t handshakeMask = 0;
Wifi80211::MacAddress handshakeClient;
uint64_t handshakeReplayM1 = 0;
uint64_t handshakeReplayM3 = 0;
uint32_t handshakeUpdatedAt = 0;
uint32_t mgmtCounts[16]{};
uint16_t lastReason = 0;
uint16_t lastStatus = 0;
RogueBaseline baseline{};
uint16_t rogueAlerts = 0;
bool rogueComparisonRun = false;
bool rogueBaselineSeen = false;
uint32_t surveyPackets[15]{};
uint32_t surveyBytes[15]{};
uint8_t surveyFirstChannel = 1;
uint8_t surveyLastChannel = 11;
uint8_t surveyChannel = 1;
uint32_t surveyStartedAt = 0;
bool surveyComplete = false;
bool surveyError = false;
bool authorized = false;
bool deauthConfirmed = false;
bool deauthRunning = false;
bool deauthChannelError = false;
bool observationCaptureError = false;
uint8_t deauthSent = 0;
uint8_t deauthAccepted = 0;
uint32_t lastDeauthAt = 0;
uint32_t observeUntil = 0;
uint32_t clientActivityAfterTest = 0;

uint16_t readLe16(const uint8_t* data) {
  return static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
}
uint16_t readBe16(const uint8_t* data) {
  return (static_cast<uint16_t>(data[0]) << 8) | data[1];
}
uint64_t readBe64(const uint8_t* data) {
  uint64_t value = 0;
  for (size_t i = 0; i < 8; ++i) value = (value << 8) | data[i];
  return value;
}
bool sameMac(const uint8_t* left, const Wifi80211::MacAddress& right) {
  return std::memcmp(left, right.bytes, 6) == 0;
}
bool sameMac(const uint8_t* left, const uint8_t* right) {
  return std::memcmp(left, right, 6) == 0;
}
bool multicast(const Wifi80211::MacAddress& mac) { return (mac.bytes[0] & 1) != 0; }

const char* authName(wifi_auth_mode_t mode) {
  switch (mode) {
    case WIFI_AUTH_OPEN: return "OPEN";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA";
    case WIFI_AUTH_WPA2_PSK: return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-E";
    case WIFI_AUTH_WPA3_PSK: return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/3";
    case WIFI_AUTH_WAPI_PSK: return "WAPI";
    case WIFI_AUTH_OWE: return "OWE";
    default: return "OTHER";
  }
}

const char* cipherName(wifi_cipher_type_t cipher) {
  switch (cipher) {
    case WIFI_CIPHER_TYPE_NONE: return "NONE";
    case WIFI_CIPHER_TYPE_WEP40: return "WEP40";
    case WIFI_CIPHER_TYPE_WEP104: return "WEP104";
    case WIFI_CIPHER_TYPE_TKIP: return "TKIP";
    case WIFI_CIPHER_TYPE_CCMP: return "CCMP";
    case WIFI_CIPHER_TYPE_TKIP_CCMP: return "TKIP+CCMP";
    case WIFI_CIPHER_TYPE_GCMP: return "GCMP";
    case WIFI_CIPHER_TYPE_GCMP256: return "GCMP256";
    default: return "OTHER";
  }
}

void drawChrome(const char* title) {
  tft.fillScreen(TFT_BLACK);
  tft.drawLine(0, 19, SCREEN_W, 19, UI_CYAN);
  tft.fillRect(190, TOP_Y, 50, 16, DARK_GRAY);
  tft.drawBitmap(BACK_X, TOP_Y, bitmap_icon_go_back, 16, 16, UI_CYAN);
  tft.drawLine(0, 36, SCREEN_W, 36, UI_AMBER);
  tft.setTextFont(2);
  tft.setTextColor(UI_CYAN, TFT_BLACK);
  tft.setCursor(6, 42);
  tft.print(title);
  tft.setTextFont(1);
}

void printMac(const uint8_t* mac) {
  tft.printf("%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

bool getTouch(int& x, int& y) {
  bool down = ts.touched();
  if (!down) {
    touchWasDown = false;
    return false;
  }
  if (touchWasDown) return false;
  touchWasDown = true;
  TS_Point point = ts.getPoint();
  x = ::map(point.x, TS_MINX, TS_MAXX, 0, SCREEN_W - 1);
  y = ::map(point.y, TS_MAXY, TS_MINY, 0, SCREEN_H - 1);
  return true;
}

bool backTouched(int x, int y) { return x >= 195 && y >= 16 && y <= 45; }

int securityScore(const ApEntry& entry) {
  int score = 100;
  switch (entry.ap.authmode) {
    case WIFI_AUTH_OPEN: score -= 70; break;
    case WIFI_AUTH_WEP: score -= 65; break;
    case WIFI_AUTH_WPA_PSK: score -= 35; break;
    case WIFI_AUTH_WPA_WPA2_PSK: score -= 25; break;
    case WIFI_AUTH_WPA2_WPA3_PSK: score -= 8; break;
    default: break;
  }
  if (entry.ap.pairwise_cipher == WIFI_CIPHER_TYPE_TKIP ||
      entry.ap.pairwise_cipher == WIFI_CIPHER_TYPE_TKIP_CCMP) score -= 15;
  if (entry.ap.wps || entry.security.hasWps) score -= 10;
  if (entry.securitySeen && !entry.security.pmfCapable) score -= 10;
  return std::max(0, score);
}

bool hasInconsistentDuplicate(size_t index) {
  if (index >= apCount || aps[index].ap.ssid[0] == '\0') return false;
  for (size_t i = 0; i < apCount; ++i) {
    if (i == index ||
        std::strncmp(reinterpret_cast<const char*>(aps[index].ap.ssid),
                     reinterpret_cast<const char*>(aps[i].ap.ssid), 32) != 0) continue;
    if (aps[index].ap.authmode != aps[i].ap.authmode ||
        aps[index].ap.pairwise_cipher != aps[i].ap.pairwise_cipher ||
        aps[index].ap.group_cipher != aps[i].ap.group_cipher) return true;
  }
  return false;
}

bool scanInventory() {
  WifiCapture::end();
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, false);
  delay(30);
  wifi_scan_config_t config{};
  config.show_hidden = true;
  config.scan_type = WIFI_SCAN_TYPE_PASSIVE;
  config.scan_time.passive = 120;
  if (esp_wifi_scan_start(&config, true) != ESP_OK) {
    apCount = 0;
    apTotalCount = 0;
    return false;
  }
  uint16_t total = 0;
  esp_wifi_scan_get_ap_num(&total);
  if (total == 0) {
    esp_wifi_clear_ap_list();
    apCount = 0;
    apTotalCount = 0;
    return true;
  }
  uint16_t count = std::min<uint16_t>(total, MAX_APS);
  wifi_ap_record_t records[MAX_APS]{};
  esp_err_t result = esp_wifi_scan_get_ap_records(&count, records);
  esp_wifi_clear_ap_list();
  if (result != ESP_OK) {
    apCount = 0;
    apTotalCount = 0;
    return false;
  }
  apTotalCount = total;
  apCount = std::min<size_t>(count, MAX_APS);
  for (size_t i = 0; i < apCount; ++i) {
    aps[i] = ApEntry{};
    aps[i].ap = records[i];
  }
  selectedAp = 0;
  return true;
}

void enrichInventory() {
  if (apCount == 0) return;
  uint8_t channels[MAX_APS]{};
  size_t channelCount = 0;
  for (size_t i = 0; i < apCount; ++i) {
    const uint8_t candidate = aps[i].ap.primary;
    bool known = false;
    for (size_t j = 0; j < channelCount; ++j) known |= channels[j] == candidate;
    if (!known && candidate != 0 && channelCount < MAX_APS) channels[channelCount++] = candidate;
  }
  for (size_t channelIndex = 0; channelIndex < channelCount; ++channelIndex) {
    const uint8_t channel = channels[channelIndex];
    if (!WifiCapture::begin({channel, WIFI_PROMIS_FILTER_MASK_MGMT})) continue;
    uint32_t until = millis() + 180;
    while (static_cast<int32_t>(until - millis()) > 0) {
      WifiCapture::FrameRecord record;
      size_t processed = 0;
      while (processed++ < MAX_FRAMES_PER_LOOP && WifiCapture::poll(record)) {
        Wifi80211::ParsedFrame parsed;
        if (!Wifi80211::parseFrame(record.payload, record.capturedLength, parsed) ||
            parsed.type != Wifi80211::FrameType::Management ||
            (parsed.subtype != 8 && parsed.subtype != 5) || parsed.ieOffset == 0) continue;
        for (size_t i = 0; i < apCount; ++i) {
          if (!sameMac(aps[i].ap.bssid, parsed.bssid)) continue;
          Wifi80211::SecurityInfo security;
          if (Wifi80211::parseSecurityIes(record.payload + parsed.ieOffset,
                                          record.capturedLength - parsed.ieOffset, security)) {
            aps[i].security = security;
            aps[i].securitySeen = true;
          }
        }
      }
      delay(1);
    }
    WifiCapture::end();
  }
}

void ensureInventory(bool enrich) {
  if (apCount == 0) scanInventory();
  if (enrich) enrichInventory();
}

void drawMenu() {
  drawChrome("Wi-Fi Assessment Suite");
  for (size_t i = 0; i < TOOL_COUNT; ++i) {
    int y = 67 + static_cast<int>(i) * 27;
    tft.drawRoundRect(6, y - 4, 228, 24, 3, UI_GUNMETAL);
    tft.setTextColor(UI_CYAN, TFT_BLACK);
    tft.setCursor(12, y + 3);
    tft.printf("%u. %s", static_cast<unsigned>(i + 1), TOOL_NAMES[i]);
  }
  tft.setTextColor(UI_GUNMETAL, TFT_BLACK);
  tft.setCursor(8, 292);
  tft.print("Authorized networks only");
  uiDrawn = true;
}

void drawAudit() {
  drawChrome("Config Auditor");
  if (apCount == 0) {
    tft.setCursor(8, 72); tft.setTextColor(UI_AMBER, TFT_BLACK); tft.print("No access points found");
    return;
  }
  tft.setTextColor(UI_GUNMETAL, TFT_BLACK);
  tft.setCursor(8, 64); tft.printf("Found %u, kept %u", apTotalCount, static_cast<unsigned>(apCount));
  const size_t pageEnd = std::min<size_t>(apCount, auditOffset + 7);
  for (size_t i = auditOffset; i < pageEnd; ++i) {
    int y = 82 + static_cast<int>(i - auditOffset) * 25;
    tft.setTextColor(i == selectedAp ? TFT_WHITE : securityScore(aps[i]) < 50 ? UI_AMBER : UI_CYAN, TFT_BLACK);
    tft.setCursor(8, y);
    tft.printf("%c%d %-11.11s", hasInconsistentDuplicate(i) ? '!' : ' ', securityScore(aps[i]),
               reinterpret_cast<const char*>(aps[i].ap.ssid));
    tft.setCursor(150, y);
    tft.printf("%s C%u", authName(aps[i].ap.authmode), aps[i].ap.primary);
  }
  tft.setTextColor(UI_CYAN, TFT_BLACK);
  tft.setCursor(8, 263); tft.printf("< page %u/%u >", static_cast<unsigned>(auditOffset / 7 + 1),
    static_cast<unsigned>((apCount + 6) / 7));
  const ApEntry& ap = aps[selectedAp];
  tft.setTextColor(UI_GUNMETAL, TFT_BLACK);
  tft.setCursor(8, 286);
  tft.printf("PMF:%s WPS:%s %s Dup:%s",
             ap.securitySeen ? (ap.security.pmfRequired ? "REQ" : ap.security.pmfCapable ? "CAP" : "OFF") : "?",
             (ap.ap.wps || ap.security.hasWps) ? "YES" : "NO", cipherName(ap.ap.pairwise_cipher),
             hasInconsistentDuplicate(selectedAp) ? "YES" : "no");
}

void writePcapPacket(const WifiCapture::FrameRecord& record) {
  if (!pcapFile) return;
  uint8_t radiotap[15] = {0, 0, 15, 0, 0x2a, 0, 0, 0, 0, 0};
  uint16_t frequency = record.channel == 14 ? 2484 :
      record.channel > 14 ? static_cast<uint16_t>(5000 + record.channel * 5) :
                            static_cast<uint16_t>(2407 + record.channel * 5);
  uint16_t channelFlags = record.channel > 14 ? 0x0100 : 0x0080;
  radiotap[10] = frequency & 0xff; radiotap[11] = frequency >> 8;
  radiotap[12] = channelFlags & 0xff; radiotap[13] = channelFlags >> 8;
  radiotap[14] = static_cast<uint8_t>(record.rssi);
  uint64_t timestamp = record.timestampUs;
  PcapRecordHeader header{static_cast<uint32_t>(timestamp / 1000000ULL),
                          static_cast<uint32_t>(timestamp % 1000000ULL),
                          static_cast<uint32_t>(sizeof(radiotap) + record.capturedLength),
                          static_cast<uint32_t>(sizeof(radiotap) + record.originalLength)};
  bool written =
      pcapFile.write(reinterpret_cast<const uint8_t*>(&header), sizeof(header)) == sizeof(header) &&
      pcapFile.write(radiotap, sizeof(radiotap)) == sizeof(radiotap) &&
      pcapFile.write(record.payload, record.capturedLength) == record.capturedLength;
  if (!written) {
    pcapWriteFailed = true;
    pcapFile.close();
    return;
  }
  pcapPackets++;
  if ((pcapPackets & 7) == 0) pcapFile.flush();
}

bool selectedBssid(const Wifi80211::ParsedFrame& parsed) {
  return apCount > 0 && sameMac(aps[selectedAp].ap.bssid, parsed.bssid);
}

void beginEapol() {
  ensureInventory(false);
  pcapPackets = 0;
  pcapWriteFailed = false;
  handshakeMask = 0;
  handshakeClient = Wifi80211::MacAddress{};
  handshakeReplayM1 = handshakeReplayM3 = 0;
  handshakeUpdatedAt = 0;
  toolCaptureError = false;
  pcapPath = "/eapol-" + String(millis()) + ".pcap";
  if (LittleFS.begin(false)) {
    pcapFile = LittleFS.open(pcapPath, FILE_WRITE);
    if (pcapFile) {
      PcapHeader header;
      if (pcapFile.write(reinterpret_cast<const uint8_t*>(&header), sizeof(header)) != sizeof(header)) {
        pcapWriteFailed = true;
        pcapFile.close();
      }
    } else {
      pcapWriteFailed = true;
    }
  } else pcapWriteFailed = true;
  toolCaptureError = apCount == 0 ||
      !WifiCapture::begin({aps[selectedAp].ap.primary,
                           WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA});
}

void classifyHandshake(const uint8_t* eapol, size_t length,
                       const Wifi80211::MacAddress& client) {
  if (length < 17 || eapol[1] != 3) return;
  const uint16_t bodyLength = readBe16(eapol + 2);
  if (bodyLength < 13 || static_cast<size_t>(bodyLength) + 4 > length) return;
  uint16_t keyInfo = readBe16(eapol + 5);
  if ((keyInfo & 0x0008) == 0) return;  // Ignore group-key handshakes.
  bool ack = (keyInfo & 0x0080) != 0;
  bool mic = (keyInfo & 0x0100) != 0;
  bool secure = (keyInfo & 0x0200) != 0;
  bool install = (keyInfo & 0x0040) != 0;
  const uint64_t replay = readBe64(eapol + 9);
  if (ack && !mic) {
    handshakeMask = 1;
    handshakeClient = client;
    handshakeReplayM1 = replay;
    handshakeReplayM3 = 0;
    handshakeUpdatedAt = millis();
  } else if (client == handshakeClient && millis() - handshakeUpdatedAt <= 15000) {
    if (!ack && mic && !secure && replay == handshakeReplayM1 && (handshakeMask & 1)) {
      handshakeMask |= 2;
    } else if (ack && mic && install && replay >= handshakeReplayM1 && (handshakeMask & 2)) {
      handshakeMask |= 4;
      handshakeReplayM3 = replay;
    } else if (!ack && mic && secure && replay == handshakeReplayM3 && (handshakeMask & 4)) {
      handshakeMask |= 8;
    }
    handshakeUpdatedAt = millis();
  }
}

void drawEapol() {
  drawChrome("WPA/EAPOL Capture");
  tft.setTextColor(UI_CYAN, TFT_BLACK);
  tft.setCursor(8, 70);
  if (apCount) tft.printf("%-20.20s Ch %u", aps[selectedAp].ap.ssid, aps[selectedAp].ap.primary);
  else tft.print("No target AP");
  tft.setCursor(8, 94); tft.printf("PCAP packets: %lu", static_cast<unsigned long>(pcapPackets));
  tft.setCursor(8, 116); tft.printf("Handshake: M1%c M2%c M3%c M4%c", handshakeMask & 1 ? '+' : '-', handshakeMask & 2 ? '+' : '-', handshakeMask & 4 ? '+' : '-', handshakeMask & 8 ? '+' : '-');
  tft.setTextColor(UI_GUNMETAL, TFT_BLACK);
  tft.setCursor(8, 145); tft.print("Passive fixed-channel capture");
  tft.setCursor(8, 160); tft.print("No password cracking on device");
  tft.setCursor(8, 190); tft.printf("Saved: %-28.28s", pcapPath.c_str());
  if (pcapWriteFailed) {
    tft.setTextColor(UI_AMBER, TFT_BLACK);
    tft.setCursor(8, 215); tft.print("PCAP write failed / filesystem full");
  }
  if (toolCaptureError) {
    tft.setTextColor(UI_AMBER, TFT_BLACK);
    tft.setCursor(8, 230); tft.print("Capture unavailable / no target AP");
  }
}

void processEapol() {
  WifiCapture::FrameRecord record;
  size_t processed = 0;
  while (processed++ < MAX_FRAMES_PER_LOOP && WifiCapture::poll(record)) {
    Wifi80211::ParsedFrame parsed;
    if (!Wifi80211::parseFrame(record.payload, record.capturedLength, parsed)) continue;
    bool save = parsed.type == Wifi80211::FrameType::Management && selectedBssid(parsed) && (parsed.subtype == 8 || parsed.subtype == 5);
    size_t offset = 0;
    if (Wifi80211::findEapol(record.payload, record.capturedLength, parsed, offset) &&
        (sameMac(aps[selectedAp].ap.bssid, parsed.transmitter) || sameMac(aps[selectedAp].ap.bssid, parsed.receiver))) {
      save = true;
      Wifi80211::MacAddress client = sameMac(aps[selectedAp].ap.bssid, parsed.transmitter)
          ? parsed.receiver : parsed.transmitter;
      classifyHandshake(record.payload + offset, record.capturedLength - offset, client);
    }
    if (save) writePcapPacket(record);
  }
}

void drawMgmt() {
  drawChrome("Management Analyzer");
  tft.setTextColor(UI_CYAN, TFT_BLACK);
  tft.setCursor(8, 66); tft.printf("Beacon %lu Probe %lu/%lu", static_cast<unsigned long>(mgmtCounts[8]), static_cast<unsigned long>(mgmtCounts[4]), static_cast<unsigned long>(mgmtCounts[5]));
  tft.setCursor(8, 88); tft.printf("Auth %lu Assoc %lu/%lu", static_cast<unsigned long>(mgmtCounts[11]), static_cast<unsigned long>(mgmtCounts[0]), static_cast<unsigned long>(mgmtCounts[1]));
  tft.setCursor(8, 110); tft.printf("Deauth %lu Disassoc %lu", static_cast<unsigned long>(mgmtCounts[12]), static_cast<unsigned long>(mgmtCounts[10]));
  tft.setCursor(8, 132); tft.printf("Action %lu", static_cast<unsigned long>(mgmtCounts[13]));
  tft.setTextColor(UI_GUNMETAL, TFT_BLACK);
  tft.setCursor(8, 165); tft.printf("Last status: %u", lastStatus);
  tft.setCursor(8, 182); tft.printf("Last reason: %u", lastReason);
  tft.setCursor(8, 215); tft.print("Counts are fixed-channel only");
  if (toolCaptureError) {
    tft.setTextColor(UI_AMBER, TFT_BLACK);
    tft.setCursor(8, 235); tft.print("Capture unavailable / no target AP");
  }
}

void processMgmt() {
  WifiCapture::FrameRecord record;
  size_t processed = 0;
  while (processed++ < MAX_FRAMES_PER_LOOP && WifiCapture::poll(record)) {
    Wifi80211::ParsedFrame parsed;
    if (!Wifi80211::parseFrame(record.payload, record.capturedLength, parsed) || parsed.type != Wifi80211::FrameType::Management) continue;
    mgmtCounts[parsed.subtype]++;
    if ((parsed.subtype == 1 || parsed.subtype == 3) && record.capturedLength >= 30) lastStatus = readLe16(record.payload + 26);
    if (parsed.subtype == 11 && record.capturedLength >= 30) lastStatus = readLe16(record.payload + 28);
    if ((parsed.subtype == 10 || parsed.subtype == 12) && record.capturedLength >= 26) lastReason = readLe16(record.payload + 24);
  }
}

void saveBaseline(const ApEntry& entry) {
  rogueAlerts = 0;
  rogueComparisonRun = false;
  rogueBaselineSeen = false;
  baseline = RogueBaseline{};
  baseline.magic = 0x51545a4c;
  std::memcpy(baseline.bssid, entry.ap.bssid, 6);
  std::memcpy(baseline.ssid, entry.ap.ssid, 33);
  baseline.channel = entry.ap.primary;
  baseline.auth = entry.ap.authmode;
  baseline.pairwise = entry.ap.pairwise_cipher;
  baseline.group = entry.ap.group_cipher;
  baseline.pmfFlags = entry.securitySeen ?
      0x80 | (entry.security.pmfCapable ? 1 : 0) | (entry.security.pmfRequired ? 2 : 0) : 0;
  Preferences prefs;
  prefs.begin("wifi-audit", false);
  prefs.putBytes("baseline", &baseline, sizeof(baseline));
  prefs.end();
}

void loadBaseline() {
  Preferences prefs;
  prefs.begin("wifi-audit", true);
  if (prefs.getBytesLength("baseline") == sizeof(baseline)) prefs.getBytes("baseline", &baseline, sizeof(baseline));
  prefs.end();
  if (baseline.magic != 0x51545a4c) baseline = RogueBaseline{};
}

void compareBaseline() {
  rogueAlerts = 0;
  rogueComparisonRun = true;
  rogueBaselineSeen = false;
  if (baseline.magic == 0) return;
  scanInventory();
  enrichInventory();
  for (size_t i = 0; i < apCount; ++i) {
    if (std::strncmp(reinterpret_cast<const char*>(aps[i].ap.ssid), reinterpret_cast<const char*>(baseline.ssid), 32) != 0) continue;
    if (sameMac(aps[i].ap.bssid, baseline.bssid)) rogueBaselineSeen = true;
    uint8_t pmfFlags = aps[i].securitySeen ?
        (aps[i].security.pmfCapable ? 1 : 0) | (aps[i].security.pmfRequired ? 2 : 0) : 0;
    bool fingerprintMismatch = aps[i].ap.authmode != baseline.auth ||
        aps[i].ap.pairwise_cipher != baseline.pairwise ||
        aps[i].ap.group_cipher != baseline.group ||
        ((baseline.pmfFlags & 0x80) && aps[i].securitySeen &&
         pmfFlags != (baseline.pmfFlags & 0x03));
    if (fingerprintMismatch) rogueAlerts++;
  }
}

void drawRogue() {
  drawChrome("Rogue AP Detector");
  tft.setTextColor(UI_CYAN, TFT_BLACK);
  tft.setCursor(8, 70);
  if (baseline.magic) tft.printf("Baseline: %-20.20s", baseline.ssid); else tft.print("No baseline enrolled");
  tft.setCursor(8, 96); tft.printf("Actionable alerts: %u", rogueAlerts);
  if (rogueComparisonRun && !rogueBaselineSeen) {
    tft.setTextColor(UI_AMBER, TFT_BLACK);
    tft.setCursor(8, 112); tft.print("Baseline not seen: inconclusive");
  }
  tft.setTextColor(UI_GUNMETAL, TFT_BLACK);
  tft.setCursor(8, 135); tft.print("Different BSSID alone is normal");
  tft.setCursor(8, 150); tft.print("Alerts require fingerprint change");
  tft.drawRoundRect(8, 245, 105, 42, 4, UI_CYAN);
  tft.drawRoundRect(127, 245, 105, 42, 4, UI_AMBER);
  tft.setTextColor(UI_CYAN, TFT_BLACK); tft.setCursor(28, 262); tft.print("ENROLL");
  tft.setTextColor(UI_AMBER, TFT_BLACK); tft.setCursor(146, 262); tft.print("COMPARE");
}

void updateClient(const Wifi80211::MacAddress& client, const Wifi80211::MacAddress& bssid, uint8_t channel, int8_t rssi) {
  if (client.isZero() || multicast(client) || client == bssid) return;
  for (size_t i = 0; i < clientCount; ++i) {
    if (clients[i].client == client && clients[i].bssid == bssid) {
      clients[i].lastSeen = millis(); clients[i].rssi = rssi; clients[i].frames++; return;
    }
  }
  if (clientCount < MAX_CLIENTS) clients[clientCount++] = {client, bssid, channel, rssi, millis(), 1};
}

void processMapper() {
  WifiCapture::FrameRecord record;
  size_t processed = 0;
  while (processed++ < MAX_FRAMES_PER_LOOP && WifiCapture::poll(record)) {
    Wifi80211::ParsedFrame parsed;
    if (!Wifi80211::parseFrame(record.payload, record.capturedLength, parsed)) continue;
    if (parsed.type == Wifi80211::FrameType::Data && selectedBssid(parsed)) {
      Wifi80211::MacAddress client = parsed.toDs ? parsed.source : parsed.destination;
      updateClient(client, parsed.bssid, record.channel, record.rssi);
    } else if (parsed.type == Wifi80211::FrameType::Management &&
               (parsed.subtype == 0 || parsed.subtype == 2) && selectedBssid(parsed)) {
      updateClient(parsed.source, parsed.bssid, record.channel, record.rssi);
    }
  }
}

void drawMapper() {
  drawChrome("AP/Client Mapper");
  tft.setTextColor(UI_CYAN, TFT_BLACK);
  tft.setCursor(8, 66); tft.printf("Clients observed: %u", static_cast<unsigned>(clientCount));
  for (size_t i = 0; i < std::min<size_t>(clientCount, 8); ++i) {
    tft.setCursor(8, 88 + i * 22); printMac(clients[i].client.bytes);
    tft.setCursor(155, 88 + i * 22); tft.printf("%ddBm %u", clients[i].rssi, clients[i].frames);
  }
  tft.setTextColor(UI_GUNMETAL, TFT_BLACK);
  tft.setCursor(8, 278); tft.print("Relationships inferred from DS bits");
  if (toolCaptureError) {
    tft.setTextColor(UI_AMBER, TFT_BLACK);
    tft.setCursor(8, 256); tft.print("Capture unavailable / no target AP");
  }
}

void drawWps() {
  drawChrome("Passive WPS Scanner");
  size_t exposed = 0;
  for (size_t i = 0; i < apCount; ++i) if (aps[i].ap.wps || aps[i].security.hasWps) exposed++;
  tft.setTextColor(UI_AMBER, TFT_BLACK); tft.setCursor(8, 66); tft.printf("WPS: %u/%u kept (%u seen)", static_cast<unsigned>(exposed), static_cast<unsigned>(apCount), apTotalCount);
  size_t row = 0;
  for (size_t i = 0; i < apCount && row < 9; ++i) {
    if (!aps[i].ap.wps && !aps[i].security.hasWps) continue;
    tft.setTextColor(UI_CYAN, TFT_BLACK); tft.setCursor(8, 90 + row * 21);
    tft.printf("%-20.20s C%u %s", aps[i].ap.ssid, aps[i].ap.primary,
               aps[i].security.wpsConfigured ? "CFG" : "WPS");
    row++;
  }
  tft.setTextColor(UI_GUNMETAL, TFT_BLACK); tft.setCursor(8, 286); tft.print("Passive exposure reporting only");
}

void beginSurvey() {
  std::memset(surveyPackets, 0, sizeof(surveyPackets));
  std::memset(surveyBytes, 0, sizeof(surveyBytes));
  wifi_country_t country{};
  if (esp_wifi_get_country(&country) == ESP_OK && country.nchan > 0) {
    surveyFirstChannel = std::max<uint8_t>(1, country.schan);
    surveyLastChannel = std::min<uint8_t>(14, country.schan + country.nchan - 1);
  } else {
    surveyFirstChannel = 1;
    surveyLastChannel = 11;
  }
  surveyChannel = surveyFirstChannel;
  surveyComplete = false;
  surveyError = !WifiCapture::begin({surveyChannel, WIFI_PROMIS_FILTER_MASK_ALL});
  if (surveyError) surveyComplete = true;
  surveyStartedAt = millis();
}

void processSurvey() {
  WifiCapture::FrameRecord record;
  size_t processed = 0;
  while (processed++ < MAX_FRAMES_PER_LOOP && WifiCapture::poll(record)) {
    if (record.channel >= surveyFirstChannel && record.channel <= surveyLastChannel) {
      surveyPackets[record.channel]++;
      surveyBytes[record.channel] += record.originalLength;
    }
  }
  if (!surveyComplete && millis() - surveyStartedAt >= SURVEY_DWELL_MS) {
    if (surveyChannel < surveyLastChannel) {
      const uint8_t nextChannel = surveyChannel + 1;
      if (WifiCapture::setChannel(nextChannel)) {
        surveyChannel = nextChannel;
        surveyStartedAt = millis();
      } else {
        surveyError = true;
        surveyComplete = true;
        WifiCapture::end();
      }
    } else {
      surveyComplete = true;
      WifiCapture::end();
    }
  }
}

void drawSurvey() {
  drawChrome("Channel Survey");
  tft.setTextColor(UI_GUNMETAL, TFT_BLACK); tft.setCursor(8, 64);
  tft.print(surveyError ? "Channel transition failed" : surveyComplete ? "Observed traffic complete" : "Surveying 2.4 GHz...");
  for (uint8_t channel = surveyFirstChannel; channel <= surveyLastChannel; ++channel) {
    int y = 82 + (channel - surveyFirstChannel) * 15;
    tft.setTextColor(channel == surveyChannel && !surveyComplete ? UI_AMBER : UI_CYAN, TFT_BLACK);
    tft.setCursor(8, y); tft.printf("C%02u %5lu pkts %7lu B", channel,
      static_cast<unsigned long>(surveyPackets[channel]), static_cast<unsigned long>(surveyBytes[channel]));
  }
  tft.setTextColor(UI_GUNMETAL, TFT_BLACK); tft.setCursor(8, 286); tft.print("Observed packets; not CCA airtime");
}

void resetResilience() {
  authorized = deauthConfirmed = deauthRunning = false;
  deauthChannelError = false;
  observationCaptureError = false;
  deauthSent = deauthAccepted = 0;
  observeUntil = 0;
  clientActivityAfterTest = 0;
}

void startDeauthTest() {
  if (clientCount == 0) return;
  deauthRunning = false;
  deauthChannelError = false;
  observationCaptureError = false;
  deauthSent = deauthAccepted = 0;
  lastDeauthAt = 0;
  clientActivityAfterTest = 0;
  WifiCapture::end();
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, false);
  if (esp_wifi_set_channel(clients[0].channel, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
    deauthChannelError = true;
    return;
  }
  deauthRunning = true;
}

void sendBoundedDeauth() {
  if (!deauthRunning || clientCount == 0 || millis() - lastDeauthAt < DEAUTH_INTERVAL_MS) return;
  lastDeauthAt = millis();
  uint8_t frame[26] = {0xc0, 0x00, 0x00, 0x00};
  std::memcpy(frame + 4, clients[0].client.bytes, 6);
  std::memcpy(frame + 10, clients[0].bssid.bytes, 6);
  std::memcpy(frame + 16, clients[0].bssid.bytes, 6);
  frame[24] = 7; frame[25] = 0;
  if (esp_wifi_80211_tx(WIFI_IF_STA, frame, sizeof(frame), true) == ESP_OK) deauthAccepted++;
  deauthSent++;
  if (deauthSent >= MAX_DEAUTH_FRAMES) {
    deauthRunning = false;
    if (WifiCapture::begin({clients[0].channel,
                            WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA})) {
      observeUntil = millis() + 5000;
    } else {
      observationCaptureError = true;
      observeUntil = 0;
    }
  }
}

void observeClient() {
  if (observeUntil == 0) return;
  WifiCapture::FrameRecord record;
  size_t processed = 0;
  while (processed++ < MAX_FRAMES_PER_LOOP && WifiCapture::poll(record)) {
    Wifi80211::ParsedFrame parsed;
    if (Wifi80211::parseFrame(record.payload, record.capturedLength, parsed) &&
        (parsed.transmitter == clients[0].client || parsed.receiver == clients[0].client)) clientActivityAfterTest++;
  }
  if (static_cast<int32_t>(millis() - observeUntil) >= 0) {
    observeUntil = 0;
    WifiCapture::end();
  }
}

void drawResilience() {
  drawChrome("Deauth Resilience");
  tft.setTextColor(UI_AMBER, TFT_BLACK); tft.setCursor(8, 66); tft.print("AUTHORIZED LAB TEST ONLY");
  tft.setTextColor(UI_GUNMETAL, TFT_BLACK); tft.setCursor(8, 88);
  if (clientCount == 0) {
    tft.print("Run AP/Client Mapper first");
    return;
  }
  tft.print("Target client: "); printMac(clients[0].client.bytes);
  tft.setCursor(8, 110); tft.printf("Limit: %u unicast frames / 5 sec", MAX_DEAUTH_FRAMES);
  tft.setCursor(8, 132); tft.print("TX accepted is not delivery proof");
  tft.setTextColor(UI_CYAN, TFT_BLACK); tft.setCursor(8, 166);
  if (!authorized) tft.print("Step 1: confirm authorization");
  else if (!deauthConfirmed) tft.print("Step 2: confirm bounded test");
  else if (deauthChannelError) tft.print("ABORTED: target channel unavailable");
  else if (observationCaptureError) tft.print("Observation unavailable: inconclusive");
  else if (deauthRunning) tft.printf("Sending: %u/%u accepted %u", deauthSent, MAX_DEAUTH_FRAMES, deauthAccepted);
  else if (observeUntil) tft.print("Observing client activity...");
  else if (deauthSent) tft.printf("Post-test client frames: %lu", static_cast<unsigned long>(clientActivityAfterTest));
  tft.drawRoundRect(15, 235, 210, 46, 4, authorized ? UI_AMBER : UI_CYAN);
  const char* actionLabel = AUTHORIZATION_REQUIRED;
  if (authorized && !deauthConfirmed) actionLabel = "SEND BOUNDED TEST";
  else if (deauthChannelError) actionLabel = "ABORTED";
  else if (observationCaptureError) actionLabel = "INCONCLUSIVE";
  else if (deauthRunning || observeUntil) actionLabel = "TEST IN PROGRESS";
  else if (deauthSent) actionLabel = "TEST COMPLETE";
  tft.setCursor(authorized ? 63 : 62, 253);
  tft.print(actionLabel);
}

void stopTool() {
  WifiCapture::end();
  if (pcapFile) { pcapFile.flush(); pcapFile.close(); }
  resetResilience();
  view = View::Menu;
  uiDrawn = false;
}

void enterTool(size_t index) {
  WifiCapture::end();
  if (pcapFile) pcapFile.close();
  view = static_cast<View>(index + 1);
  uiDrawn = false;
  toolCaptureError = false;
  switch (view) {
    case View::Audit: auditOffset = 0; scanInventory(); enrichInventory(); selectedAp = 0; drawAudit(); break;
    case View::Eapol: beginEapol(); drawEapol(); break;
    case View::Mgmt:
      ensureInventory(false); std::memset(mgmtCounts, 0, sizeof(mgmtCounts)); lastReason = lastStatus = 0;
      toolCaptureError = apCount == 0 ||
          !WifiCapture::begin({aps[selectedAp].ap.primary, WIFI_PROMIS_FILTER_MASK_MGMT});
      drawMgmt(); break;
    case View::Rogue:
      rogueAlerts = 0; rogueComparisonRun = false; rogueBaselineSeen = false;
      loadBaseline(); drawRogue(); break;
    case View::Mapper:
      ensureInventory(false); clientCount = 0;
      toolCaptureError = apCount == 0 ||
          !WifiCapture::begin({aps[selectedAp].ap.primary,
                               WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA});
      drawMapper(); break;
    case View::Wps: scanInventory(); enrichInventory(); drawWps(); break;
    case View::Survey: beginSurvey(); drawSurvey(); break;
    case View::Resilience: resetResilience(); drawResilience(); break;
    default: break;
  }
  uiDrawn = true;
}

void handleMenuTouch(int, int y) {
  for (size_t i = 0; i < TOOL_COUNT; ++i) {
    int rowY = 63 + static_cast<int>(i) * 27;
    if (y >= rowY && y <= rowY + 26) { enterTool(i); return; }
  }
}

}  // namespace

void setup() {
  clientCount = 0;
  std::memset(clients, 0, sizeof(clients));
  view = View::Menu;
  uiDrawn = false;
  touchWasDown = ts.touched();
  drawMenu();
}

void loop() {
  updateStatusBar();
  if (!uiDrawn && view == View::Menu) drawMenu();

  switch (view) {
    case View::Eapol: processEapol(); break;
    case View::Mgmt: processMgmt(); break;
    case View::Mapper: processMapper(); break;
    case View::Survey: processSurvey(); break;
    case View::Resilience: sendBoundedDeauth(); observeClient(); break;
    default: break;
  }

  if (millis() - lastUiUpdate >= 500) {
    lastUiUpdate = millis();
    switch (view) {
      case View::Eapol: drawEapol(); break;
      case View::Mgmt: drawMgmt(); break;
      case View::Mapper: drawMapper(); break;
      case View::Survey: drawSurvey(); break;
      case View::Resilience: drawResilience(); break;
      default: break;
    }
  }

  int x, y;
  if (!getTouch(x, y)) return;
  if (backTouched(x, y)) {
    if (view == View::Menu) feature_exit_requested = true;
    else stopTool();
    return;
  }
  if (view == View::Menu) {
    handleMenuTouch(x, y);
  } else if (view == View::Audit && y >= 78 && y < 257) {
    size_t index = auditOffset + static_cast<size_t>((y - 78) / 25);
    if (index < std::min<size_t>(apCount, auditOffset + 7)) { selectedAp = index; drawAudit(); }
  } else if (view == View::Audit && y >= 257) {
    if (x < 120 && auditOffset >= 7) auditOffset -= 7;
    else if (x >= 120 && auditOffset + 7 < apCount) auditOffset += 7;
    selectedAp = auditOffset;
    drawAudit();
  } else if (view == View::Rogue && y >= 235) {
    ensureInventory(true);
    if (x < 120 && apCount) saveBaseline(aps[selectedAp]);
    else compareBaseline();
    drawRogue();
  } else if (view == View::Resilience && y >= 225 && clientCount > 0) {
    if (!authorized) authorized = true;
    else if (!deauthConfirmed) { deauthConfirmed = true; startDeauthTest(); }
    drawResilience();
  }
}

void cleanup() {
  WifiCapture::end();
  if (pcapFile) { pcapFile.flush(); pcapFile.close(); }
  resetResilience();
  clientCount = 0;
  std::memset(clients, 0, sizeof(clients));
  view = View::Menu;
  uiDrawn = false;
}

}  // namespace WifiAssessment
