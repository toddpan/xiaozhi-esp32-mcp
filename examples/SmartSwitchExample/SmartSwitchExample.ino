/**
 * @file SmartSwitch.ino
 * @brief 6-channel relay controller with XiaoZhi MCP integration.
 * 
 * Features:
 * - Physical switch control (active-low, with debouncing)
 * - Remote control via XiaoZhi LLM (tool_call: relay_control, relay_status)
 * - One-step activation using 6-digit agent code
 * - Automatic token persistence (NVS)
 * - Non-blocking switch polling
 * 
 * Wiring:
 *   Switches: pins 1–6 (internal pull-up, active LOW)
 *   Relays:   pins 21, 45, 46, 38, 39, 40 (active HIGH)
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "WebSocketMCP.h"

// === Configuration ===
const char* WIFI_SSID = "your-ssid";
const char* WIFI_PASS = "your-password";
const char* AGENT_CODE = "Fx5L4pDZqw";  // ← Get from XiaoZhi dashboard

// Relay & switch pins (adjust for your board)
constexpr int SWITCH_PINS[] = {1, 2, 3, 4, 5, 6};
constexpr int RELAY_PINS[] = {21, 45, 46, 38, 39, 40};
constexpr size_t NUM_RELAYS = 6;

// State tracking
bool relayStates[NUM_RELAYS] = {false};
unsigned long lastDebounceTime[NUM_RELAYS] = {0};
constexpr unsigned long DEBOUNCE_DELAY_MS = 50;

// === Global objects ===
WiFiClientSecure client;
WebSocketMCP mcp(client);

// === Helper: control relay safely ===
void setRelay(size_t index, bool on) {
  if (index >= NUM_RELAYS) return;
  relayStates[index] = on;
  digitalWrite(RELAY_PINS[index], on ? HIGH : LOW);
  Serial.printf("[RELAY] #%zu → %s\n", index + 1, on ? "ON" : "OFF");
}

// === Callback: MCP connection & tool registration ===
void onMcpConnect(bool connected) {
  if (connected) {
    Serial.println("[MCP] ✅ Connected — registering tools...");

    // Tool 1: control single relay
    mcp.registerTool(
      "relay_control",
      "Control a specific relay (1-6)",
      R"({"type":"object","properties":{"relayIndex":{"type":"integer","minimum":1,"maximum":6},"state":{"type":"boolean"}},"required":["relayIndex","state"]})",
      [](const char* args) -> ToolResponse {
        // Lightweight parsing — no heap allocation
        int idx = -1, state = -1;
        if (sscanf(args, R"({"relayIndex":%d,"state":%d)", &idx, &state) == 2 && idx >= 1 && idx <= 6) {
          setRelay(idx - 1, state == 1);
          return ToolResponse(false, R"({"success":true})");
        }
        return ToolResponse(true, R"({"error":"invalid_params"})");
      }
    );

    // Tool 2: get all relay states
    mcp.registerTool(
      "relay_status",
      "Get current state of all 6 relays",
      R"({"type":"object","properties":{}})",
      [](const char* args) -> ToolResponse {
        // Build JSON response on stack
        char response[256];
        int len = snprintf(response, sizeof(response),
          R"({"success":true,"relays":[)");
        for (size_t i = 0; i < NUM_RELAYS && len < 240; i++) {
          if (i > 0) response[len++] = ',';
          len += snprintf(response + len, sizeof(response) - len,
            R"({"index":%u,"state":%s})", i + 1, relayStates[i] ? "true" : "false");
        }
        if (len < 250) strcpy(response + len, "]}");
        return ToolResponse(false, response);
      }
    );

    Serial.println("[MCP] ✅ Tools registered");
  }
}

// === Setup ===
void setup() {
  Serial.begin(115200);

  // Initialize switches (active-low, pull-up)
  for (size_t i = 0; i < NUM_RELAYS; i++) {
    pinMode(SWITCH_PINS[i], INPUT_PULLUP);
  }

  // Initialize relays (off by default)
  for (size_t i = 0; i < NUM_RELAYS; i++) {
    pinMode(RELAY_PINS[i], OUTPUT);
    digitalWrite(RELAY_PINS[i], LOW);
  }

  // Connect to WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("[WIFI] Connecting to '%s'...\n", WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\n[WIFI] ✅ Connected. IP: %s\n", WiFi.localIP().toString().c_str());

  // Start XiaoZhi activation
  Serial.printf("[MCP] 🔑 Starting activation with agent code: %s\n", AGENT_CODE);
  mcp.beginWithAgentCode(AGENT_CODE, onMcpConnect);
}

// === Non-blocking switch debouncer ===
void checkSwitches() {
  unsigned long now = millis();
  for (size_t i = 0; i < NUM_RELAYS; i++) {
    int reading = digitalRead(SWITCH_PINS[i]);  // Active LOW
    if (reading == LOW && (now - lastDebounceTime[i]) > DEBOUNCE_DELAY_MS) {
      lastDebounceTime[i] = now;
      setRelay(i, !relayStates[i]);  // Toggle
    }
  }
}

// === Loop ===
void loop() {
  mcp.loop();       // Handles MCP protocol, reconnection, activation
  checkSwitches();  // Poll switches non-blocking
  delay(10);        // Gentle yield
}
