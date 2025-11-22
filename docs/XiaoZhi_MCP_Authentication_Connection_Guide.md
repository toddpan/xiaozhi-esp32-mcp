# 🔐 XiaoZhi MCP Authentication & Connection Guide

This document explains the **complete authentication and connection flow** for the XiaoZhi Model Context Protocol (MCP) WebSocket endpoint, as implemented in the `xiaozhi-esp32-mcp` library.

> ⚠️ **Important**: The official XiaoZhi documentation does **not** cover several critical details (e.g., WAF challenges, TLS requirements, endpoint structure). This guide is based on reverse-engineering, production deployment, and direct protocol testing.

---

## 🔑 Overview

The XiaoZhi MCP service uses a **two-phase authentication** system:

1. **Device Activation** (one-time or periodic):  
   Exchange a short-lived `agent_code` for a long-lived JWT `access_token` via HTTPS.

2. **WebSocket Connection**:  
   Use the `access_token` to establish a persistent, bidirectional MCP channel.

Due to security layers (Alibaba Cloud WAF), both phases require specific headers, TLS settings, and endpoint formats.

---

## 📦 Prerequisites

- ESP32 (tested on ESP32-S3, ESP32-C3)
- Arduino Core for ESP32 ≥ 2.0.14
- Libraries:
  - `ArduinoJson` ≥ 6.21.0
  - `xiaozhi-esp32-mcp` (this library)

---

## 🌐 1. Network Setup

The device must have internet access. Two common modes:

### A. Station Mode (Connect to Existing Wi-Fi)

```cpp
#include <WiFi.h>

void connectToWiFi(const char* ssid, const char* password) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  constexpr uint32_t timeout_ms = 15000;
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeout_ms) {
    delay(500);
  }

  if (WiFi.status() != WL_CONNECTED) {
    // Handle failure (e.g., restart, fallback to AP)
    ESP.restart();
  }
}
```

### B. SoftAP + Configuration Portal (Recommended for Production)

See [`Access_Point_Configuration_Instructions.md`](./Access_Point_Configuration_Instructions.md) for full implementation.

---

## 🪪 2. Obtaining an `agent_code`

The `agent_code` is a 6–8 character alphanumeric identifier used to **activate** a device.

### How to get it:

1. Log in to the [XiaoZhi Dashboard](https://console.xiaozhi.me).
2. Navigate to **Devices** → **+ Add Device**.
3. Select your device type and click **Generate Code**.
4. A code like `Fx5L4pDZ` will appear. It expires in **5 minutes**.

> 🔒 Never hardcode `agent_code` in firmware. Use only for initial provisioning.

---

## 🔁 3. Activation: `agent_code` → `access_token`

The device exchanges the `agent_code` for a JWT `access_token` via HTTPS POST.

### ✅ Correct Endpoint & Headers

| Item | Value |
|------|-------|
| **URL** | `POST https://api.xiaozhi.me/xiaozhi/v1/device/activate` |
| **Headers** | `Content-Type: application/json;charset=utf-8`<br>`User-Agent: XiaoZhi-Device/1.0 ESP32`<br>`Accept: application/json` |
| **Body** | `{"agent_code":"Fx5L4pDZ"}` |

### ⚠️ WAF Challenge (`426 Upgrade Required`)

The server may respond with:
```http
HTTP/1.1 426 Upgrade Required
Set-Cookie: acw_tc=0a061a8017638256846382239e32b97ca...
```

**Solution**: Extract `acw_tc` and **retry the POST with**:
```
Cookie: acw_tc=0a061a8017638256846382239e32b97ca...
```

The `xiaozhi-esp32-mcp` library handles this automatically via:
```cpp
mcp.activateWithAgentCode("Fx5L4pDZ");
// → Returns `true` on success, saves token to NVS
```

### ✅ Successful Response

```json
{
  "code": 0,
  "message": "ok",
  "data": {
    "access_token": "eyJhbGciOiJFUzI1NiIsInR5cCI6IkpXVCJ9.xxxxx"
  }
}
```

The token is:
- ~280–350 characters (JWT ES256)
- Valid for **1 year** (check `exp` claim)
- Must be persisted (e.g., NVS)

---

## 📡 4. WebSocket Connection

Once activated, connect to the MCP endpoint:

### ✅ Correct WebSocket URL

```
wss://api.xiaozhi.me/xiaozhi/v1/mcp?token=<access_token>
```

> ❗ Critical details:
> - Use `/xiaozhi/v1/mcp` (**not** `/mcp` or `/xiaozhi/mcp`)
> - **No trailing slash** after `mcp`
> - Token must be **URL-encoded** (the library handles this)

### ✅ Required Handshake Headers

The library automatically sends:
```
GET /xiaozhi/v1/mcp?token=... HTTP/1.1
Host: api.xiaozhi.me
Connection: Upgrade
Upgrade: websocket
Sec-WebSocket-Version: 13
Sec-WebSocket-Key: [base64]
User-Agent: XiaoZhi-Device/1.0 ESP32
```

> 📌 `User-Agent` is **mandatory** — omission causes `426`.

### ✅ TLS Requirements

- **CA Certificate**: XiaoZhi uses a WoTrus DV chain.  
  The library embeds the root CA (`XIAOZHI_ROOT_CA`) — no external certs needed.
- **SNI**: Must be enabled (automatic when using `httpsClient.begin("https://...")`).
- **Protocol**: TLS 1.2 preferred (ESP32 negotiates correctly when CA is set).

On success:
```http
HTTP/1.1 101 Switching Protocols
Upgrade: websocket
Connection: upgrade
Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=
```

---

## 💾 5. Token Persistence

The library uses **NVS (Non-Volatile Storage)**:

- Namespace: `"xiaozhi"`
- Key: `"mcp_token"`

### Public API

| Method | Description |
|--------|-------------|
| `mcp.isActivated()` | `true` if token exists in NVS |
| `mcp.clearActivation()` | Erase token (factory reset) |
| `mcp.begin(nullptr, callback)` | Auto-load token from NVS and connect |

---

## 🛠️ 6. Usage Examples

### A. Full Activation Flow (First Boot)

```cpp
#include "WebSocketMCP.h"
#include <WiFiClientSecure.h>

WiFiClientSecure client;
WebSocketMCP mcp(client);

void setup() {
  connectToWiFi("MySSID", "MyPass");

  // One-time activation
  if (!mcp.isActivated()) {
    if (!mcp.activateWithAgentCode("Fx5L4pDZ")) {
      ESP.restart(); // Retry or enter config mode
    }
  }

  // Connect using saved token
  mcp.begin(nullptr, [](bool connected) {
    Serial.printf("MCP %s\n", connected ? "connected" : "disconnected");
  });

  // Start task
  xTaskCreate(networkLoop, "MCP", 8192, &mcp, 1, nullptr);
}

void networkLoop(void* param) {
  WebSocketMCP* mcp = (WebSocketMCP*)param;
  while (1) {
    mcp->loop();
    vTaskDelay(1);
  }
}
```

### B. Pre-Provisioned Token (Manufacturing)

```cpp
const char* TOKEN = "eyJhbGci..."; // From dashboard or API

if (!mcp.isActivated()) {
  mcp.saveTokenToNVS(TOKEN); // Public method
}
mcp.begin(nullptr, onConnect);
```

---

## 🚨 7. Common Errors & Fixes

| Symptom | Cause | Solution |
|--------|-------|----------|
| `HTTP 426` | Missing `User-Agent` or `acw_tc` | Use library’s `activateWithAgentCode()`; ensure `User-Agent` in handshake |
| `HTTP 400` | Truncated token in URL | Increase `MAX_PATH_LENGTH` to `512` in `WebSocketMCP.h` |
| `HTTP 404` | Wrong endpoint (`/mcp` vs `/xiaozhi/v1/mcp`) | Use `/xiaozhi/v1/mcp?token=...` |
| `TLS handshake failed` | Missing CA cert | Ensure `XIAOZHI_ROOT_CA` is injected via `setCACert()` |
| `JSON serialization failed` | Small buffer | Increase `StaticJsonDocument` and `char[]` sizes (≥256) |

---

## 🔐 Security Notes

- **Never store tokens in code** (use NVS or secure element).
- The embedded CA is safe — it only accepts certificates issued by XiaoZhi’s chain.
- Tokens are bound to `agentId` — revocation is possible via dashboard.

---

## 📚 References

- [XiaoZhi Dashboard](https://console.xiaozhi.me)
- [Access Point Configuration](./Access_Point_Configuration_Instructions.md)
- [JWT Debugger (for token inspection)](https://jwt.io)

> ✅ This guide reflects real-world behavior as of **November 2025**. XiaoZhi may update endpoints or security policies — monitor logs for `4xx` errors.


