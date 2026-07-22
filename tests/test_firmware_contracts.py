import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class FirmwareContractTests(unittest.TestCase):
    def test_espnow_replaces_ir_remote_menu(self):
        sketch = (ROOT / "quetzal.ino").read_text(encoding="utf-8")
        self.assertNotIn('"IR Remote"', sketch)
        self.assertIn('"ESP-NOW"', sketch)
        self.assertIn('#include "espnow_test.h"', sketch)
        self.assertTrue((ROOT / "espnow_test.cpp").is_file())
        self.assertTrue((ROOT / "espnow_test.h").is_file())
        self.assertIn("EspNowTest::espNowTestSetup", sketch)
        self.assertIn("EspNowTest::espNowTestCleanup", sketch)

    def test_host_scanner_uses_real_subnet_and_bounded_icmp_probe(self):
        source = (ROOT / "host_scanner.cpp").read_text(encoding="utf-8")
        self.assertIn("WiFi.subnetMask()", source)
        self.assertIn("SOCK_RAW", source)
        self.assertIn("IPPROTO_ICMP", source)
        self.assertIn("close(pingSocket)", source)
        self.assertNotIn("esp_ping_new_session", source)
        self.assertNotIn("client.connect(target, 80, CONNECT_TIMEOUT_MS)", source)
        self.assertIn("networkAddress", source)
        self.assertIn("broadcastAddress", source)

    def test_host_scanner_results_support_paging_and_detail(self):
        source = (ROOT / "host_scanner.cpp").read_text(encoding="utf-8")
        self.assertIn("RESULTS_PER_PAGE", source)
        self.assertIn("listStartIndex", source)
        self.assertIn("scrollResults", source)
        self.assertIn("drawResultDetail", source)
        self.assertIn("resultsTruncated", source)
        self.assertIn("if (!aborted) resultCount++", source)

    def test_espnow_reports_full_v2_frame_length(self):
        source = (ROOT / "espnow_test.cpp").read_text(encoding="utf-8")
        self.assertIn("volatile uint16_t lastLength", source)
        self.assertNotIn("min(length, 255)", source)
    def test_wifi_assessment_suite_contract(self):
        sketch = (ROOT / "quetzal.ino").read_text(encoding="utf-8")
        parser = (ROOT / "wifi_80211.cpp").read_text(encoding="utf-8")
        capture = (ROOT / "wifi_capture.cpp").read_text(encoding="utf-8")
        suite = (ROOT / "wifi_assessment.cpp").read_text(encoding="utf-8")
        self.assertIn('"Assessment Suite"', sketch)
        for name in (
            "Config Auditor", "EAPOL Capture", "Mgmt Analyzer",
            "Rogue AP Detector", "AP/Client Mapper", "WPS Scanner",
            "Channel Survey", "Deauth Resilience",
        ):
            self.assertIn(name, suite)
        self.assertIn("parseSecurityIes", parser)
        self.assertIn("findEapol", parser)
        self.assertIn("DLT_IEEE802_11_RADIO", suite)
        self.assertIn("esp_wifi_set_promiscuous_rx_cb", capture)
        self.assertIn("hasInconsistentDuplicate", suite)
        self.assertIn("MAX_DEAUTH_FRAMES", suite)
        self.assertIn("AUTHORIZATION_REQUIRED", suite)

    def test_wifi_assessment_safety_regressions(self):
        suite = (ROOT / "wifi_assessment.cpp").read_text(encoding="utf-8")
        capture = (ROOT / "wifi_capture.cpp").read_text(encoding="utf-8")
        parser = (ROOT / "wifi_80211.cpp").read_text(encoding="utf-8")
        self.assertIn("esp_wifi_get_country", suite)
        self.assertIn("ABORTED: target channel unavailable", suite)
        self.assertIn("Baseline not seen: inconclusive", suite)
        self.assertIn("Ignore group-key handshakes", suite)
        self.assertIn("handshakeClient", suite)
        self.assertIn("bodyLength", suite)
        self.assertGreaterEqual(suite.count("clientCount = 0"), 3)
        self.assertIn("if (!captureActive)", capture)
        self.assertIn("record.timestampUs", suite)
        self.assertIn("surveyPackets[record.channel]", suite)
        self.assertIn("toolCaptureError", suite)
        self.assertIn("observationCaptureError", suite)
        self.assertIn("if ((out.subtype & 0x8) != 0)", parser)


if __name__ == "__main__":
    unittest.main()
