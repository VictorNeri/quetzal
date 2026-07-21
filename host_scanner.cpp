#include "host_scanner.h"
#include "shared.h"
#include "icon.h"
#include "Touchscreen.h"
#include "utils.h"
#include "wificonfig.h"
#include <WiFi.h>
#include <lwip/inet_chksum.h>
#include <lwip/prot/icmp.h>
#include <lwip/sockets.h>

extern TFT_eSPI tft;

namespace HostScanner {

static constexpr int SCREEN_WIDTH = 240;
static constexpr int STATUS_BAR_Y_OFFSET = 20;
static constexpr int STATUS_BAR_HEIGHT = 16;
static constexpr int ICON_SIZE = 16;
static constexpr int ICON_NUM = 2;
static constexpr int RESULTS_PER_PAGE = 5;
static constexpr int MAX_SCAN_TARGETS = 254;
static constexpr int MAX_HOSTS = 64;
static constexpr uint32_t PING_TIMEOUT_MS = 180;
static constexpr uint32_t CONNECT_TIMEOUT_MS = 180;

static bool uiDrawn = false;
static const int yshift = 30;

enum State { STATE_IDLE, STATE_RESULTS };
static State state = STATE_IDLE;

static const uint16_t COMMON_PORTS[] = {
  21, 22, 23, 25, 53, 80, 110, 111, 135, 139,
  143, 443, 445, 993, 995, 1723, 3306, 3389, 5900, 8080
};
static constexpr int NUM_COMMON_PORTS = sizeof(COMMON_PORTS) / sizeof(COMMON_PORTS[0]);

struct HostResult {
  uint32_t address;
  uint16_t openPorts[NUM_COMMON_PORTS];
  uint8_t openPortCount;
};

static HostResult results[MAX_HOSTS];
static int resultCount = 0;
static int discoveredHostCount = 0;
static bool resultsTruncated = false;
static int listStartIndex = 0;

static int iconX[ICON_NUM] = {188, 210};
static const unsigned char* icons[ICON_NUM] = {
  bitmap_icon_undo,
  bitmap_icon_go_back
};

static int stopX = 60, stopY = 280, stopW = 120, stopH = 28;

void runUI();

uint32_t ipToUint(const IPAddress& ip) {
  return (static_cast<uint32_t>(ip[0]) << 24) |
         (static_cast<uint32_t>(ip[1]) << 16) |
         (static_cast<uint32_t>(ip[2]) << 8) |
         static_cast<uint32_t>(ip[3]);
}

IPAddress uintToIp(uint32_t value) {
  return IPAddress(static_cast<uint8_t>(value >> 24),
                   static_cast<uint8_t>(value >> 16),
                   static_cast<uint8_t>(value >> 8),
                   static_cast<uint8_t>(value));
}

int openPingSocket() {
  int socketFd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
  if (socketFd < 0) return -1;
  timeval timeout = {0, PING_TIMEOUT_MS * 1000};
  if (setsockopt(socketFd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0) {
    close(socketFd);
    return -1;
  }
  return socketFd;
}

bool pingHost(int socketFd, const IPAddress& target, uint16_t sequence) {
  icmp_echo_hdr request = {};
  request.type = ICMP_ECHO;
  request.code = 0;
  request.id = htons(0x515a);
  request.seqno = htons(sequence);
  request.chksum = inet_chksum(&request, sizeof(request));

  sockaddr_in destination = {};
  destination.sin_family = AF_INET;
  destination.sin_addr.s_addr = htonl(ipToUint(target));
  if (sendto(socketFd, &request, sizeof(request), 0,
             reinterpret_cast<sockaddr*>(&destination), sizeof(destination)) < 0) {
    return false;
  }

  uint8_t response[96];
  const unsigned long startedAt = millis();
  while (millis() - startedAt <= PING_TIMEOUT_MS) {
    sockaddr_in source = {};
    socklen_t sourceLength = sizeof(source);
    int length = recvfrom(socketFd, response, sizeof(response), 0,
                          reinterpret_cast<sockaddr*>(&source), &sourceLength);
    if (length < 0) return false;
    if (source.sin_addr.s_addr != destination.sin_addr.s_addr) continue;

    int ipHeaderLength = 0;
    if (response[0] != ICMP_ER) {
      ipHeaderLength = (response[0] & 0x0f) * 4;
      if (ipHeaderLength < 20) continue;
    }
    if (length < ipHeaderLength + static_cast<int>(sizeof(icmp_echo_hdr))) continue;
    const icmp_echo_hdr* reply =
        reinterpret_cast<const icmp_echo_hdr*>(response + ipHeaderLength);
    if (reply->type == ICMP_ER && reply->id == request.id && reply->seqno == request.seqno) {
      return true;
    }
  }
  return false;
}

bool checkAbortTouch() {
  if (!ts.touched()) return false;
  TS_Point p = ts.getPoint();
  int x = ::map(p.x, TS_MINX, TS_MAXX, 0, SCREEN_WIDTH - 1);
  int y = ::map(p.y, TS_MAXY, TS_MINY, 0, 319);
  return x >= stopX && x <= stopX + stopW && y >= stopY && y <= stopY + stopH;
}

void drawStopButton() {
  tft.drawRoundRect(stopX, stopY, stopW, stopH, 4, RED);
  tft.setTextColor(RED, TFT_BLACK);
  tft.setCursor(stopX + 40, stopY + 9);
  tft.print("STOP");
}

void drawIdle() {
  tft.fillRect(0, STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT, SCREEN_WIDTH, 320, TFT_BLACK);
  tft.setTextFont(2);
  tft.setTextColor(UI_CYAN, TFT_BLACK);
  tft.setCursor(10, 20 + yshift);
  tft.print("Host Scanner");

  tft.setTextFont(1);
  tft.setTextColor(UI_GUNMETAL, TFT_BLACK);
  tft.setCursor(10, 50 + yshift);
  tft.print("Uses ICMP to find live hosts,");
  tft.setCursor(10, 62 + yshift);
  tft.print("then checks 20 common ports.");
  tft.setCursor(10, 74 + yshift);
  tft.print("Scan range follows subnet mask.");

  tft.drawRoundRect(60, 110 + yshift, 120, 34, 4, UI_AMBER);
  tft.setTextColor(UI_AMBER, TFT_BLACK);
  tft.setCursor(85, 122 + yshift);
  tft.print("SCAN");
}

void drawResults() {
  tft.fillRect(0, STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT, SCREEN_WIDTH, 320, TFT_BLACK);
  tft.setTextFont(1);
  tft.setTextColor(UI_CYAN, TFT_BLACK);
  tft.setCursor(6, 24 + yshift);
  if (resultCount == 0) {
    tft.print("No ICMP-responsive hosts found.");
    return;
  }

  const int page = listStartIndex / RESULTS_PER_PAGE + 1;
  const int pages = (resultCount + RESULTS_PER_PAGE - 1) / RESULTS_PER_PAGE;
  if (resultsTruncated) {
    tft.printf("Showing %d/%d  page %d/%d", resultCount, discoveredHostCount, page, pages);
  } else {
    tft.printf("%d host(s)  page %d/%d", resultCount, page, pages);
  }

  for (int row = 0; row < RESULTS_PER_PAGE; row++) {
    const int index = listStartIndex + row;
    if (index >= resultCount) break;
    const int yPos = 70 + row * 40;
    const IPAddress address = uintToIp(results[index].address);
    tft.setCursor(6, yPos);
    tft.setTextColor(UI_CYAN, TFT_BLACK);
    tft.print(address.toString());
    tft.print(" ");

    if (results[index].openPortCount == 0) {
      tft.setTextColor(UI_GUNMETAL, TFT_BLACK);
      tft.print("alive");
    } else {
      tft.setTextColor(GREEN, TFT_BLACK);
      const int previewCount = min<int>(results[index].openPortCount, 4);
      for (int port = 0; port < previewCount; port++) {
        if (port > 0) tft.print(",");
        tft.print(results[index].openPorts[port]);
      }
      if (results[index].openPortCount > previewCount) {
        tft.printf(" +%d", results[index].openPortCount - previewCount);
      }
    }
    tft.setCursor(6, yPos + 14);
    tft.setTextColor(UI_GUNMETAL, TFT_BLACK);
    tft.print("Tap for all ports");
    tft.drawLine(0, yPos + 29, SCREEN_WIDTH, yPos + 29, UI_GUNMETAL);
  }

  tft.setTextColor(listStartIndex > 0 ? UI_AMBER : UI_GUNMETAL, TFT_BLACK);
  tft.setCursor(18, 286);
  tft.print("< PREV");
  tft.setTextColor(listStartIndex + RESULTS_PER_PAGE < resultCount ? UI_AMBER : UI_GUNMETAL,
                   TFT_BLACK);
  tft.setCursor(164, 286);
  tft.print("NEXT >");
}

void drawResultDetail(int index) {
  if (index < 0 || index >= resultCount) return;
  tft.fillScreen(TFT_BLACK);
  tft.setTextFont(1);
  tft.setTextColor(UI_CYAN, TFT_BLACK);
  tft.setCursor(5, 4);
  tft.print("Host details");
  tft.drawBitmap(210, 2, bitmap_icon_go_back, 16, 16, UI_CYAN);
  tft.drawLine(0, 20, SCREEN_WIDTH, 20, UI_CYAN);
  tft.setCursor(5, 32);
  tft.print(uintToIp(results[index].address).toString());
  tft.setCursor(5, 52);
  if (results[index].openPortCount == 0) {
    tft.print("No common open ports");
  } else {
    tft.print("Open ports:");
    for (int port = 0; port < results[index].openPortCount; port++) {
      tft.setCursor(5 + (port % 4) * 57, 72 + (port / 4) * 22);
      tft.print(results[index].openPorts[port]);
    }
  }

  while (true) {
    if (ts.touched()) {
      TS_Point point = ts.getPoint();
      int x = ::map(point.x, TS_MINX, TS_MAXX, 0, SCREEN_WIDTH - 1);
      int y = ::map(point.y, TS_MAXY, TS_MINY, 0, 319);
      if (x >= 195 && y <= 32) {
        while (ts.touched()) delay(10);
        uiDrawn = false;
        runUI();
        drawResults();
        return;
      }
    }
    delay(10);
  }
}

void scrollResults(int direction) {
  if (direction < 0 && listStartIndex > 0) {
    listStartIndex = max(0, listStartIndex - RESULTS_PER_PAGE);
    drawResults();
  } else if (direction > 0 && listStartIndex + RESULTS_PER_PAGE < resultCount) {
    listStartIndex += RESULTS_PER_PAGE;
    drawResults();
  }
}

void runScan() {
  tft.fillRect(0, STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT, SCREEN_WIDTH, 320, TFT_BLACK);
  tft.setTextFont(1);
  tft.setTextColor(UI_CYAN, TFT_BLACK);
  tft.setCursor(10, 40 + yshift);
  tft.print("Connecting to WiFi...");

  if (!FirmwareUpdate::connectToWiFiInteractive()) {
    tft.fillRect(0, STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT, SCREEN_WIDTH, 320, TFT_BLACK);
    tft.setTextColor(RED, TFT_BLACK);
    tft.setCursor(10, 40 + yshift);
    tft.print("WiFi connect failed/cancelled.");
    delay(1500);
    state = STATE_IDLE;
    drawIdle();
    return;
  }

  const IPAddress localIP = WiFi.localIP();
  const IPAddress subnetMask = WiFi.subnetMask();
  const uint32_t localAddress = ipToUint(localIP);
  const uint32_t mask = ipToUint(subnetMask);
  const uint32_t networkAddress = localAddress & mask;
  const uint32_t broadcastAddress = networkAddress | ~mask;

  if (mask == 0 || broadcastAddress <= networkAddress + 1) {
    tft.setTextColor(RED, TFT_BLACK);
    tft.setCursor(10, 60 + yshift);
    tft.print("Invalid subnet mask.");
    delay(1500);
    state = STATE_IDLE;
    drawIdle();
    return;
  }

  uint32_t firstTarget = networkAddress + 1;
  uint32_t lastTarget = broadcastAddress - 1;
  const uint32_t availableTargets = lastTarget - firstTarget + 1;
  bool rangeCapped = availableTargets > MAX_SCAN_TARGETS;
  if (rangeCapped) {
    const uint32_t halfWindow = MAX_SCAN_TARGETS / 2;
    firstTarget = localAddress > networkAddress + halfWindow
                    ? localAddress - halfWindow
                    : networkAddress + 1;
    if (firstTarget + MAX_SCAN_TARGETS - 1 > lastTarget) {
      firstTarget = lastTarget - MAX_SCAN_TARGETS + 1;
    }
    lastTarget = firstTarget + MAX_SCAN_TARGETS - 1;
  }

  tft.fillRect(0, STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT, SCREEN_WIDTH, 320, TFT_BLACK);
  tft.setTextColor(UI_CYAN, TFT_BLACK);
  tft.setCursor(10, 30 + yshift);
  tft.print("IP: " + localIP.toString());
  tft.setCursor(10, 42 + yshift);
  tft.print("Mask: " + subnetMask.toString());
  if (rangeCapped) {
    tft.setCursor(10, 54 + yshift);
    tft.setTextColor(UI_AMBER, TFT_BLACK);
    tft.print("Large subnet: nearest 254 hosts");
  }
  drawStopButton();

  uint32_t liveHosts[MAX_SCAN_TARGETS];
  int liveCount = 0;
  bool aborted = false;
  const int targetCount = static_cast<int>(lastTarget - firstTarget + 1);
  int targetIndex = 0;
  int pingSocket = openPingSocket();
  if (pingSocket < 0) {
    tft.setTextColor(RED, TFT_BLACK);
    tft.setCursor(10, 100);
    tft.print("Unable to open ICMP socket.");
    delay(1500);
    state = STATE_IDLE;
    drawIdle();
    return;
  }

  for (uint32_t address = firstTarget; address <= lastTarget && !aborted; address++) {
    targetIndex++;
    if (address == localAddress) continue;
    const IPAddress target = uintToIp(address);

    tft.fillRect(10, 100, 220, 14, TFT_BLACK);
    tft.setCursor(10, 100);
    tft.setTextColor(UI_CYAN, TFT_BLACK);
    tft.printf("Ping %d/%d (%d found)", targetIndex, targetCount, liveCount);

    if (pingHost(pingSocket, target, static_cast<uint16_t>(targetIndex)) &&
        liveCount < MAX_SCAN_TARGETS) {
      liveHosts[liveCount++] = address;
    }
    aborted = checkAbortTouch();
    yield();
  }
  close(pingSocket);

  resultCount = 0;
  discoveredHostCount = liveCount;
  resultsTruncated = liveCount > MAX_HOSTS;
  for (int i = 0; i < liveCount && !aborted && resultCount < MAX_HOSTS; i++) {
    HostResult& result = results[resultCount];
    result.address = liveHosts[i];
    result.openPortCount = 0;
    const IPAddress target = uintToIp(result.address);

    for (int portIndex = 0; portIndex < NUM_COMMON_PORTS && !aborted; portIndex++) {
      tft.fillRect(10, 100, 220, 14, TFT_BLACK);
      tft.setCursor(10, 100);
      tft.setTextColor(UI_CYAN, TFT_BLACK);
      tft.printf("Ports %d/%d: %s:%d", i + 1, liveCount,
                 target.toString().c_str(), COMMON_PORTS[portIndex]);

      WiFiClient client;
      if (client.connect(target, COMMON_PORTS[portIndex], CONNECT_TIMEOUT_MS) &&
          result.openPortCount < NUM_COMMON_PORTS) {
        result.openPorts[result.openPortCount++] = COMMON_PORTS[portIndex];
      }
      client.stop();
      aborted = checkAbortTouch();
      yield();
    }
    if (!aborted) resultCount++;
  }

  listStartIndex = 0;
  state = STATE_RESULTS;
  drawResults();
}

void runUI() {
  static int iconY = STATUS_BAR_Y_OFFSET;
  if (!uiDrawn) {
    tft.drawLine(0, 19, 240, 19, UI_CYAN);
    tft.fillRect(140, STATUS_BAR_Y_OFFSET, SCREEN_WIDTH - 140, STATUS_BAR_HEIGHT, DARK_GRAY);
    for (int i = 0; i < ICON_NUM; i++) {
      if (icons[i] != nullptr) tft.drawBitmap(iconX[i], iconY, icons[i], ICON_SIZE, ICON_SIZE, UI_CYAN);
    }
    tft.drawLine(0, STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                 STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT, UI_AMBER);
    uiDrawn = true;
  }

  static unsigned long lastTouchCheck = 0;
  if (millis() - lastTouchCheck < 80) return;
  lastTouchCheck = millis();
  if (!ts.touched() || !feature_active) return;

  TS_Point p = ts.getPoint();
  int x = ::map(p.x, TS_MINX, TS_MAXX, 0, SCREEN_WIDTH - 1);
  int y = ::map(p.y, TS_MAXY, TS_MINY, 0, 319);

  if (y >= STATUS_BAR_Y_OFFSET && y <= STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT + 5) {
    if (x >= iconX[0] - 5 && x <= iconX[0] + ICON_SIZE + 5) {
      state = STATE_IDLE;
      drawIdle();
    } else if (x >= iconX[1] - 5 && x <= iconX[1] + ICON_SIZE + 5) {
      feature_exit_requested = true;
    }
    return;
  }

  if (state == STATE_IDLE && x >= 60 && x <= 180 &&
      y >= 110 + yshift && y <= 144 + yshift) {
    unsigned long releaseStart = millis();
    while (ts.touched() && millis() - releaseStart < 1500) delay(10);
    runScan();
  } else if (state == STATE_RESULTS && y >= 65 && y < 265) {
    int row = max(0, (y - 70) / 40);
    int index = listStartIndex + row;
    unsigned long releaseStart = millis();
    while (ts.touched() && millis() - releaseStart < 1500) delay(10);
    drawResultDetail(index);
  } else if (state == STATE_RESULTS && y >= 275) {
    scrollResults(x < SCREEN_WIDTH / 2 ? -1 : 1);
    unsigned long releaseStart = millis();
    while (ts.touched() && millis() - releaseStart < 1500) delay(10);
  }
}

void hostScannerSetup() {
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(UI_CYAN, TFT_BLACK);
  tft.setTextSize(1);
  uiDrawn = false;
  state = STATE_IDLE;
  resultCount = 0;
  discoveredHostCount = 0;
  resultsTruncated = false;
  listStartIndex = 0;
  drawStatusBar(readBatteryVoltage(), false);
  runUI();
  drawIdle();
}

void hostScannerLoop() {
  tft.drawLine(0, 19, 240, 19, UI_CYAN);
  updateStatusBar();
  runUI();
}

}  // namespace HostScanner
