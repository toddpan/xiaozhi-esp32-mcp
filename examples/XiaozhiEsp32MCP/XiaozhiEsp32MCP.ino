/**
 * @file XiaoZhi_MCP_Example.ino
 * @brief Minimal example for XiaoZhi MCP client on ESP32.
 * 
 * Features:
 * - One-step activation using 6-digit agent code (e.g., "Fx5L4pDZqw")
 * - Automatic token persistence via NVS (no re-activation needed)
 * - LED control tool (on/off/blink)
 * - FreeRTOS-safe: dedicated network task
 * - Zero dynamic allocation after setup
 * 
 * Steps to use:
 * 1. Replace WIFI_SSID and WIFI_PASS
 * 2. Replace AGENT_CODE with your 6-digit code from XiaoZhi dashboard
 * 3. Upload and open Serial Monitor (115200 baud)
 * 4. Go to https://xiaozhi.me/activate?code=Fx5L4pDZqw and activate device
 * 5. Watch logs — device reconnects automatically with JWT token
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "WebSocketMCP.h"

// === Configuration ===
const char* WIFI_SSID = "Your_WiFi_SSID";
const char* WIFI_PASS = "Your_WiFi_Password";
const char* AGENT_CODE = "Fx5L4pDZqw"; // ← Get this from XiaoZhi device dashboard

#define LED_PIN 2  // ESP32-S3 onboard LED (active-low on some boards — adjust if needed)

// === Global objects ===
WiFiClientSecure secureClient;
WebSocketMCP mcpClient(secureClient);

// FreeRTOS task handle
TaskHandle_t networkTaskHandle = nullptr;

// === Callbacks ===

/**
 * @brief Called when MCP connection state changes.
 *        Registers tools *after* successful connection.
 */
void onMcpConnection(bool connected) {
  Serial.printf("[MCP] Connection state: %s\n", connected ? "CONNECTED" : "DISCONNECTED");

  if (connected) {
    // Register tools only after connection (MCP spec requirement)
    Serial.println("[MCP] Registering tools...");

    // Tool: control onboard LED
    mcpClient.registerTool(
      "led_control",                                      // tool name
      "Control the ESP32 onboard LED state",             // description (for LLM)
      R"({"type":"object","properties":{"state":{"type":"string","enum":["on","off","blink"]}},"required":["state"]})", // JSON schema
      [](const char* args) -> ToolResponse {
        // Parse JSON args (lightweight — no heap allocation)
        if (strstr(args, "\"state\":\"on\"")) {
          digitalWrite(LED_PIN, HIGH);
          Serial.println("[TOOL] LED turned ON");
          return ToolResponse(false, "{\"success\":true,\"action\":\"led_on\"}");
        } else if (strstr(args, "\"state\":\"off\"")) {
          digitalWrite(LED_PIN, LOW);
          Serial.println("[TOOL] LED turned OFF");
          return ToolResponse(false, "{\"success\":true,\"action\":\"led_off\"}");
        } else if (strstr(args, "\"state\":\"blink\"")) {
          // Simple blink (blocking — for demo only; use non-blocking in production)
          for (int i = 0; i < 3; i++) {
            digitalWrite(LED_PIN, HIGH);
            delay(200);
            digitalWrite(LED_PIN, LOW);
            delay(200);
          }
          Serial.println("[TOOL] LED blinked 3 times");
          return ToolResponse(false, "{\"success\":true,\"action\":\"led_blink\"}");
        }
        return ToolResponse(true, "{\"error\":\"invalid_state\"}");
      }
    );

    Serial.println("[MCP] Tools registered successfully");
  }
}

/**
 * @brief FreeRTOS task for MCP network loop.
 *        Pinned to Core 0 (networking core on ESP32).
 */
void networkTask(void* param) {
  Serial.println("[TASK] MCP network task started (Core 0)");
  while (1) {
    mcpClient.loop();  // Handles reconnect, ping, message processing
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

// === Setup ===

void setup() {
  Serial.begin(115200);
  while (!Serial); // Wait for USB serial (if needed)
  Serial.println("\n[SETUP] XiaoZhi MCP Example");

  // Initialize LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Connect to WiFi
  Serial.printf("[WIFI] Connecting to '%s'...\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.printf("\n[WIFI] Connected! IP: %s\n", WiFi.localIP().toString().c_str());

  // Initialize MCP client
  Serial.printf("[MCP] Starting activation with agent code: %s\n", AGENT_CODE);
  if (!mcpClient.beginWithAgentCode(AGENT_CODE, onMcpConnection)) {
    Serial.println("[ERROR] Failed to start activation flow");
    return;
  }

  // Create network task (Core 0)
  xTaskCreatePinnedToCore(
    networkTask,
    "MCP_Network",
    8192,           // Increased stack (was 4096 → overflow)
    nullptr,
    5,              // Priority
    &networkTaskHandle,
    0               // Core 0
  );
}

// === Loop ===

void loop() {
  // Keepalive — no work needed here (network task handles everything)
  vTaskDelay(pdMS_TO_TICKS(1000));

  // Optional: send test message every 10s (e.g., for debugging)
  static unsigned long lastTest = 0;
  if (mcpClient.isConnected() && millis() - lastTest > 10000) {
    lastTest = millis();
    const char* testMsg = R"({"jsonrpc":"2.0","method":"ping","id":999})";
    mcpClient.sendMessage(testMsg, strlen(testMsg));
    Serial.println("[DEBUG] Sent ping message");
  }
}
