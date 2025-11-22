/**
 * @file Basic_Example.ino
 * @brief Minimal XiaoZhi MCP client — activation + LED control in under 40 lines.
 * 
 * How it works:
 * 1. Connects to WiFi
 * 2. Starts activation with 6-digit agent code (e.g., "Fx5L4pDZqw")
 * 3. After activation on https://xiaozhi.me, token is saved to NVS
 * 4. Device reconnects automatically — no manual token handling
 * 5. Registers `led_control` tool for LLM interaction
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "WebSocketMCP.h"

// === Configuration ===
const char* WIFI_SSID = "your-ssid";
const char* WIFI_PASS = "your-password";
const char* AGENT_CODE = "Fx5L4pDZqw";  // ← Get from XiaoZhi device dashboard

#define LED_PIN 2  // ESP32 onboard LED

// === Global objects ===
WiFiClientSecure client;
WebSocketMCP mcp(client);

// === Callback: connection state & tool registration ===
void onMcpConnect(bool connected) {
  if (connected) {
    Serial.println("[MCP] ✅ Connected — registering tools...");

    mcp.registerTool(
      "led_control",
      "Toggle ESP32 onboard LED",
      R"({"type":"object","properties":{"state":{"type":"string","enum":["on","off"]}},"required":["state"]})",
      [](const char* args) -> ToolResponse {
        if (strstr(args, "\"state\":\"on\"")) {
          digitalWrite(LED_PIN, HIGH);
          return ToolResponse(false, "{\"success\":true}");
        } else if (strstr(args, "\"state\":\"off\"")) {
          digitalWrite(LED_PIN, LOW);
          return ToolResponse(false, "{\"success\":true}");
        }
        return ToolResponse(true, "{\"error\":\"invalid_state\"}");
      }
    );
  }
}

// === Setup ===
void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n[WiFi] ✅ Connected");

  // XiaoZhi activation (one-time; token saved to NVS)
  Serial.printf("[MCP] 🔑 Starting activation with code: %s\n", AGENT_CODE);
  mcp.beginWithAgentCode(AGENT_CODE, onMcpConnect);
}

// === Loop ===
void loop() {
  mcp.loop();  // Handles reconnect, ping, messages, activation flow
  delay(10);
}
