#include "host_scanner.h"
#include "shared.h"
#include "icon.h"
#include "Touchscreen.h"
#include "utils.h"
#include "wificonfig.h"
#include <WiFi.h>

// ═══════════════════════════════════════════════════════════════════════════
// Host Scanner - connects out to a WiFi network (reusing FirmwareUpdate's
// existing STA connect flow), then sweeps the local /24 subnet for live
// hosts and checks a curated list of ~20 common ports on each one found.
//
// Two-phase design keeps this sane on a single-core microcontroller: a
// liveness pass first (one quick connection attempt per candidate IP, timed
// rather than judged purely on success/failure - a fast RST from a closed
// port still proves the host is alive), then the full port sweep only
// against hosts that actually responded. A naive single-phase 254-host x
// 20-port sweep would be ~21 minutes worst case; two-phase keeps a typical
// home/office subnet under a couple of minutes.
// ═══════════════════════════════════════════════════════════════════════════

extern TFT_eSPI tft;

namespace HostScanner {

#define SCREEN_WIDTH 240
#define STATUS_BAR_Y_OFFSET 20
#define STATUS_BAR_HEIGHT 16
#define ICON_SIZE 16
#define ICON_NUM 2

static bool uiDrawn = false;
static const int yshift = 30;

enum State { STATE_IDLE, STATE_RESULTS };
static State state = STATE_IDLE;

static const uint16_t COMMON_PORTS[] = {
  21, 22, 23, 25, 53, 80, 110, 111, 135, 139,
  143, 443, 445, 993, 995, 1723, 3306, 3389, 5900, 8080
};
#define NUM_COMMON_PORTS 20
#define CONNECT_TIMEOUT_MS 250

struct HostResult {
  uint8_t lastOctet;
  uint16_t openPorts[NUM_COMMON_PORTS];
  uint8_t openPortCount;
};
#define MAX_HOSTS 64
static HostResult results[MAX_HOSTS];
static int resultCount = 0;
static int listStartIndex = 0;

static int iconX[ICON_NUM] = {188, 210};
static const unsigned char* icons[ICON_NUM] = {
  bitmap_icon_undo,     // rescan
  bitmap_icon_go_back   // exit feature
};

int stopX = 60, stopY = 280, stopW = 120, stopH = 28;

bool checkAbortTouch() {
  if (!ts.touched()) return false;
  TS_Point p = ts.getPoint();
  int x = ::map(p.x, TS_MINX, TS_MAXX, 0, SCREEN_WIDTH - 1);
  int y = ::map(p.y, TS_MAXY, TS_MINY, 0, 319);
  return (x >= stopX && x <= stopX + stopW && y >= stopY && y <= stopY + stopH);
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
  tft.print("Connects to WiFi, then scans");
  tft.setCursor(10, 62 + yshift);
  tft.print("the local subnet for live hosts");
  tft.setCursor(10, 74 + yshift);
  tft.print("and ~20 common ports on each.");

  tft.drawRoundRect(60, 110 + yshift, 120, 34, 4, UI_AMBER);
  tft.setTextColor(UI_AMBER, TFT_BLACK);
  tft.setCursor(85, 122 + yshift);
  tft.print("SCAN");
}

void drawResults() {
  tft.fillRect(0, STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT, SCREEN_WIDTH, 320, TFT_BLACK);
  tft.setTextFont(1);
  tft.setTextColor(UI_CYAN, TFT_BLACK);
  tft.setCursor(10, 24 + yshift);
  if (resultCount == 0) {
    tft.print("No live hosts found.");
    return;
  }
  tft.printf("%d live host(s):", resultCount);

  int yPos = 40 + yshift;
  for (int i = 0; i < 10; i++) {
    int idx = i + listStartIndex;
    if (idx >= resultCount) break;

    tft.setCursor(10, yPos);
    tft.setTextColor(UI_CYAN, TFT_BLACK);
    tft.printf(".%-3d ", results[idx].lastOctet);

    if (results[idx].openPortCount == 0) {
      tft.setTextColor(UI_GUNMETAL, TFT_BLACK);
      tft.print("(no common ports open)");
    } else {
      String line = "";
      for (int j = 0; j < results[idx].openPortCount; j++) {
        if (j > 0) line += ",";
        line += String(results[idx].openPorts[j]);
      }
      if (line.length() > 28) line = line.substring(0, 25) + "...";
      tft.setTextColor(GREEN, TFT_BLACK);
      tft.print(line);
    }
    yPos += 22;
  }
}

// Blocking: connects to WiFi, sweeps the subnet, and shows progress. Polls
// the on-screen STOP button between probes so a scan can be aborted early.
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

  IPAddress localIP = WiFi.localIP();

  tft.fillRect(0, STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT, SCREEN_WIDTH, 320, TFT_BLACK);
  tft.setTextColor(UI_CYAN, TFT_BLACK);
  tft.setCursor(10, 30 + yshift);
  tft.print("Connected: " + localIP.toString());
  drawStopButton();

  uint8_t liveHosts[254];
  int liveCount = 0;
  bool aborted = false;

  for (int host = 1; host <= 254 && !aborted; host++) {
    if (host == localIP[3]) continue;  // skip ourselves

    tft.fillRect(10, 55 + yshift, 220, 12, TFT_BLACK);
    tft.setCursor(10, 55 + yshift);
    tft.setTextColor(UI_CYAN, TFT_BLACK);
    tft.printf("Phase 1/2: .%d (%d found)", host, liveCount);

    IPAddress target(localIP[0], localIP[1], localIP[2], (uint8_t)host);
    WiFiClient client;
    unsigned long t0 = millis();
    bool ok = client.connect(target, 80, CONNECT_TIMEOUT_MS);
    unsigned long elapsed = millis() - t0;
    client.stop();

    // A fast response - open (ok==true) or a quick refusal/RST - proves the
    // host is alive even if port 80 itself is closed; only a full timeout
    // with no reply at all means nothing answered.
    bool alive = ok || (elapsed < (unsigned long)(CONNECT_TIMEOUT_MS - 30));
    if (alive && liveCount < 254) liveHosts[liveCount++] = (uint8_t)host;

    aborted = checkAbortTouch();
  }

  resultCount = 0;
  for (int i = 0; i < liveCount && !aborted && resultCount < MAX_HOSTS; i++) {
    uint8_t host = liveHosts[i];
    HostResult& r = results[resultCount];
    r.lastOctet = host;
    r.openPortCount = 0;

    for (int p = 0; p < NUM_COMMON_PORTS && !aborted; p++) {
      tft.fillRect(10, 55 + yshift, 220, 12, TFT_BLACK);
      tft.setCursor(10, 55 + yshift);
      tft.setTextColor(UI_CYAN, TFT_BLACK);
      tft.printf("Phase 2/2: .%d port %d", host, COMMON_PORTS[p]);

      IPAddress target(localIP[0], localIP[1], localIP[2], host);
      WiFiClient client;
      if (client.connect(target, COMMON_PORTS[p], CONNECT_TIMEOUT_MS)) {
        if (r.openPortCount < NUM_COMMON_PORTS) {
          r.openPorts[r.openPortCount++] = COMMON_PORTS[p];
        }
      }
      client.stop();

      aborted = checkAbortTouch();
    }
    resultCount++;
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
      if (icons[i] != NULL) tft.drawBitmap(iconX[i], iconY, icons[i], ICON_SIZE, ICON_SIZE, UI_CYAN);
    }
    tft.drawLine(0, STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT, SCREEN_WIDTH, STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT, UI_AMBER);
    uiDrawn = true;
  }

  static unsigned long lastTouchCheck = 0;
  if (millis() - lastTouchCheck < 50) return;
  lastTouchCheck = millis();

  if (!ts.touched() || !feature_active) return;

  TS_Point p = ts.getPoint();
  int x = ::map(p.x, TS_MINX, TS_MAXX, 0, SCREEN_WIDTH - 1);
  int y = ::map(p.y, TS_MAXY, TS_MINY, 0, 319);

  if (y >= STATUS_BAR_Y_OFFSET && y <= STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT + 5) {
    if (x >= iconX[0] - 5 && x <= iconX[0] + ICON_SIZE + 5) {
      state = STATE_IDLE;
      drawIdle();
      return;
    }
    if (x >= iconX[1] - 5 && x <= iconX[1] + ICON_SIZE + 5) {
      feature_exit_requested = true;
      return;
    }
    return;
  }

  if (state == STATE_IDLE) {
    if (x >= 60 && x <= 180 && y >= 110 + yshift && y <= 144 + yshift) {
      delay(150);  // debounce so the same tap doesn't get read again mid-scan
      runScan();
    }
  } else if (state == STATE_RESULTS) {
    // No per-row detail view in this pass; up/down scrolling not needed for
    // typical small subnets (list shows the first 10 live hosts).
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
  listStartIndex = 0;

  float currentBatteryVoltage = readBatteryVoltage();
  drawStatusBar(currentBatteryVoltage, false);

  runUI();
  drawIdle();
}

void hostScannerLoop() {
  tft.drawLine(0, 19, 240, 19, UI_CYAN);
  updateStatusBar();
  runUI();
}

}  // namespace HostScanner
