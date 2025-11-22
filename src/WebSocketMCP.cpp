#include "WebSocketMCP.h"
#include <Arduino.h>
#include <Client.h>
#include <Preferences.h>
#include <ArduinoJson.h>       // ✅ Essential for StaticJsonDocument
#include <WiFiClientSecure.h>  // ✅ Essential for dynamic_cast<WiFiClientSecure*>
#include <HTTPClient.h>
#include "esp_log.h"
#include "mbedtls/sha1.h"
#include "mbedtls/base64.h"
#include <algorithm>
#include <cstring>
#include <cstdlib>

// Log tag for ESP-IDF logging system
static const char* TAG = "MCP_WSS";

// === Static constants ===
const int WebSocketMCP::INITIAL_BACKOFF = 1000;      // 1 second
const int WebSocketMCP::MAX_BACKOFF = 60000;         // 60 seconds
const int WebSocketMCP::PING_INTERVAL = 10000;       // 10 seconds
const int WebSocketMCP::DISCONNECT_TIMEOUT = 60000;  // 60 seconds

// Embedded XiaoZhi Root CA (AAA Certificate Services)

const char XIAOZHI_ROOT_CA[] PROGMEM = \
"-----BEGIN CERTIFICATE-----\n" \
"MIIEMjCCAxqgAwIBAgIBATANBgkqhkiG9w0BAQUFADB7MQswCQYDVQQGEwJHQjEb\n" \
"MBkGA1UECAwSR3JlYXRlciBNYW5jaGVzdGVyMRAwDgYDVQQHDAdTYWxmb3JkMRow\n" \
"GAYDVQQKDBFDb21vZG8gQ0EgTGltaXRlZDEhMB8GA1UEAwwYQUFBIENlcnRpZmlj\n" \
"YXRlIFNlcnZpY2VzMB4XDTA0MDEwMTAwMDAwMFoXDTI4MTIzMTIzNTk1OVowezEL\n" \
"MAkGA1UEBhMCR0IxGzAZBgNVBAgMEkdyZWF0ZXIgTWFuY2hlc3RlcjEQMA4GA1UE\n" \
"BwwHU2FsZm9yZDEaMBgGA1UECgwRQ29tb2RvIENBIExpbWl0ZWQxITAfBgNVBAMM\n" \
"GEFBQSBDZXJ0aWZpY2F0ZSBTZXJ2aWNlczCCASIwDQYJKoZIhvcNAQEBBQADggEP\n" \
"ADCCAQoCggEBAL5AnfRu4ep2hxxNRUSOvkbIgwadwSr+GB+O5AL686tdUIoWMQua\n" \
"BtDFcCLNSS1UY8y2bmhGC1Pqy0wkwLxyTurxFa70VJoSCsN6sjNg4tqJVfMiWPPe\n" \
"3M/vg4aijJRPn2jymJBGhCfHdr/jzDUsi14HZGWCwEiwqJH5YZ92IFCokcdmtet4\n" \
"YgNW8IoaE+oxox6gmf049vYnMlhvB/VruPsUK6+3qszWY19zjNoFmag4qMsXeDZR\n" \
"rOme9Hg6jc8P2ULimAyrL58OAd7vn5lJ8S3frHRNG5i1R8XlKdH5kBjHYpy+g8cm\n" \
"ez6KJcfA3Z3mNWgQIJ2P2N7Sw4ScDV7oL8kCAwEAAaOBwDCBvTAdBgNVHQ4EFgQU\n" \
"oBEKIz6W8Qfs4q8p74Klf9AwpLQwDgYDVR0PAQH/BAQDAgEGMA8GA1UdEwEB/wQF\n" \
"MAMBAf8wewYDVR0fBHQwcjA4oDagNIYyaHR0cDovL2NybC5jb21vZG9jYS5jb20v\n" \
"QUFBQ2VydGlmaWNhdGVTZXJ2aWNlcy5jcmwwNqA0oDKGMGh0dHA6Ly9jcmwuY29t\n" \
"b2RvLm5ldC9BQUFDZXJ0aWZpY2F0ZVNlcnZpY2VzLmNybDANBgkqhkiG9w0BAQUF\n" \
"AAOCAQEACFb8AvCb6P+k+tZ7xkSAzk/ExfYAWMymtrwUSWgEdujm7l3sAg9g1o1Q\n" \
"GE8mTgHj5rCl7r+8dFRBv/38ErjHT1r0iWAFf2C3BUrz9vHCv8S5dIa2LX1rzNLz\n" \
"Rt0vxuBqw8M0Ayx9lt1awg6nCpnBBYurDC/zXDrPbDdVCYfeU0BsWO/8tqtlbgT2\n" \
"G9w84FoVxp7Z8VlIMCFlA2zs6SFz7JsDoeA3raAVGI/6ugLOpyypEBMs1OUIJqsi\n" \
"l2D4kF501KKaU73yqWjgom7C12yxow+ev+to51byrvLjKzg6CYG1a4XXvi3tPxq3\n" \
"smPi9WIsgtRqAEFQ8TmDn5XpNpaYbg==\n" \
"-----END CERTIFICATE-----";

/*
const char XIAOZHI_ROOT_CA[] PROGMEM = \
// 1. WoTrus DV Server CA (intermediário)
"-----BEGIN CERTIFICATE-----\n" \
"MIIF4jCCA8qgAwIBAgIRANVuJGyU7WOrsUbvwZa2T7AwDQYJKoZIhvcNAQEMBQAw\n" \
"gYgxCzAJBgNVBAYTAlVTMRMwEQYDVQQIEwpOZXcgSmVyc2V5MRQwEgYDVQQHEwtK\n" \
"ZXJzZXkgQ2l0eTEeMBwGA1UEChMVVGhlIFVTRVJUUlVTVCBOZXR3b3JrMS4wLAYD\n" \
"VQQDEyVVU0VSVHJ1c3QgUlNBIENlcnRpZmljYXRpb24gQXV0aG9yaXR5MB4XDTIw\n" \
"MDEwODAwMDAwMFoXDTMwMDEwNzIzNTk1OVowXDELMAkGA1UEBhMCQ04xGjAYBgNV\n" \
"BAoTEVdvVHJ1cyBDQSBMaW1pdGVkMTEwLwYDVQQDDChXb1RydXMgRFYgU2VydmVy\n" \
"IENBICBbUnVuIGJ5IHRoZSBJc3N1ZXJdMIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8A\n" \
"MIIBCgKCAQEA7IE0rmVTVdRdNOzK1jsR/VppyukZ/XbQgakJHOhg6XGDsiHe/l5B\n" \
"3PxyXw18jEdN+7YxP0qsGz+HlQbsQh6XlwIyjpz/2gFMiqa7y1v+dHOgj6xNOF5a\n" \
"oaPm/Qhb0N+JYQidgaC+1Zp6W+YeC736rzCMr9vL1Usa3QLzRoQEo0DzbG4sPeP1\n" \
"US0Ia/i8o6szArH+DAcvrzCZ2kkpTScQ9QfOsvkMBP1W2otICdKUyZHaBc+ztTAd\n" \
"ovSlOR+GPf29dYfGQkZAp0tffIRw/na3WB86WGZPpNFfo2QxxsHYoL3oSWKfSWTY\n" \
"FgW22J8eA03TFHYowm/NqYuJ7GW553HppQIDAQABo4IBcDCCAWwwHwYDVR0jBBgw\n" \
"FoAUU3m/WqorSs9UgOHYm8Cd8rIDZsswHQYDVR0OBBYEFJmbLfaL8KPbidSe++V0\n" \
"L2jSkE/kMA4GA1UdDwEB/wQEAwIBhjASBgNVHRMBAf8ECDAGAQH/AgEAMB0GA1Ud\n" \
"JQQWMBQGCCsGAQUFBwMBBggrBgEFBQcDAjAiBgNVHSAEGzAZMA0GCysGAQQBsjEB\n" \
"AgIWMAgGBmeBDAECATBQBgNVHR8ESTBHMEWgQ6BBhj9odHRwOi8vY3JsLnVzZXJ0\n" \
"cnVzdC5jb20vVVNFUlRydXN0UlNBQ2VydGlmaWNhdGlvbkF1dGhvcml0eS5jcmww\n" \
"cQYIKwYBBQUHAQEEZTBjMDoGCCsGAQUFBzAChi5odHRwOi8vY3J0LnVzZXJ0cnVz\n" \
"dC5jb20vVVNFUlRydXN0UlNBQUFBQ0EuY3J0MCUGCCsGAQUFBzABhhlodHRwOi8v\n" \
"b2NzcC51c2VydHJ1c3QuY29tMA0GCSqGSIb3DQEBDAUAA4ICAQB5t8v3uYzHa4EL\n" \
"0rOb9g/YAmptUbILcBMKk1x188ucsGVPaG1DG9bpVamxbmCtFA1MlrA7iUC8SGop\n" \
"KBnuWFsNKiC7jCbRoahT1/FSwFsSuDlDmOjr1MqDXE+or08UkXsJB57XxXxdVOPl\n" \
"DcZHII4qHi1XKK4iurMqb+kbdpAWadyfidRRCGPopYCVYLLYhRJgpFGtfr6Gk8N0\n" \
"j81jq/7QbN0dRSDzMNdadKTc7c3+i9fIrXj79lV5Wvva+OL7nh8MxQhG1Ekek7Rv\n" \
"en++jSZvaEhCrMsSedFTA/aIy7oJg85tfglF2ybK61HsobjYzdDNICKJlIm4chlA\n" \
"XIDDqw2mw0Kz2snrkp9dpvMBqahF/Uy1kHzPcrq1/w5OqZWAuDKxZ68PuZ/ME2hI\n" \
"YbIDG9dWT6Y7eqtjQ2TmAQbOqdAG2LeikPMl2DMrPEa4lcKJzsFbHfHAW3hVgPSQ\n" \
"hRfS4TtbNnxijbsp8GguMHxP2R7dpAAYybwfZdXP7WYAnwEr1mzIf0Y3J0m7GDyX\n" \
"JhaflN3G2wIm2HzRd39NvnDRmFEraqui/YYO9ym0pwq1d0S+bGG6876QCto0u3Cg\n" \
"ItFh3Za2ZeIY+g5mWrejSaDs9LT7eu44iCyebfgekdMRqFeCuGAsJdsun3LOHHJo\n" \
"tCVPRjyFg9NDeJeMa4Z8QuXAXLd9cw==\n" \
"-----END CERTIFICATE-----\n" \
// 2. AAA Certificate Services (raiz)
"-----BEGIN CERTIFICATE-----\n" \
"MIIEMjCCAxqgAwIBAgIBATANBgkqhkiG9w0BAQUFADB7MQswCQYDVQQGEwJHQjEb\n" \
"MAkGA1UECAwSR3JlYXRlciBNYW5jaGVzdGVyMRAwDgYDVQQHDAdTYWxmb3JkMRow\n" \
"GAYDVQQKDBFDb21vZG8gQ0EgTGltaXRlZDEhMB8GA1UEAwwYQUFBIENlcnRpZmlj\n" \
"YXRlIFNlcnZpY2VzMB4XDTA0MDEwMTAwMDAwMFoXDTI4MTIzMTIzNTk1OVowezEL\n" \
"MAkGA1UEBhMCR0IxGzAZBgNVBAgMEkdyZWF0ZXIgTWFuY2hlc3RlcjEQMA4GA1UE\n" \
"BwwHU2FsZm9yZDEaMBgGA1UECgwRQ29tb2RvIENBIExpbWl0ZWQxITAfBgNVBAMM\n" \
"GEFBQSBDZXJ0aWZpY2F0ZSBTZXJ2aWNlczCCASIwDQYJKoZIhvcNAQEBBQADggEP\n" \
"ADCCAQoCggEBAL5AnfRu4ep2hxxNRUSOvkbIgwadwSr+GB+O5AL686tdUIoWMQua\n" \
"BtDFcCLNSS1UY8y2bmhGC1Pqy0wkwLxyTurxFa70VJoSCsN6sjNg4tqJVfMiWPPe\n" \
"3M/vg4aijJRPn2jymJBGhCfHdr/jzDUsi14HZGWCwEiwqJH5YZ92IFCokcdmtet4\n" \
"YgNW8IoaE+oxox6gmf049vYnMlhvB/VruPsUK6+3qszWY19zjNoFmag4qMsXeDZR\n" \
"rOme9Hg6jc8P2ULimAyrL58OAd7vn5lJ8S3frHRNG5i1R8XlKdH5kBjHYpy+g8cm\n" \
"ez6KJcfA3Z3mNWgQIJ2P2N7Sw4ScDV7oL8kCAwEAAaOBwDCBvTAdBgNVHQ4EFgQU\n" \
"oBEKIz6W8Qfs4q8p74Klf9AwpLQwDgYDVR0PAQH/BAQDAgEGMA8GA1UdEwEB/wQF\n" \
"MAMBAf8wewYDVR0fBHQwcjA4oDagNIYyaHR0cDovL2NybC5jb21vZG9jYS5jb20v\n" \
"QUFBQ2VydGlmaWNhdGVTZXJ2aWNlcy5jcmwwNqA0oDKGMGh0dHA6Ly9jcmwuY29t\n" \
"b2RvLm5ldC9BQUFDZXJ0aWZpY2F0ZVNlcnZpY2VzLmNybDANBgkqhkiG9w0BAQUF\n" \
"AAOCAQEACFb8AvCb6P+k+tZ7xkSAzk/ExfYAWMymtrwUSWgEdujm7l3sAg9g1o1Q\n" \
"GE8mTgHj5rCl7r+8dFRBv/38ErjHT1r0iWAFf2C3BUrz9vHCv8S5dIa2LX1rzNLz\n" \
"Rt0vxuBqw8M0Ayx9lt1awg6nCpnBBYurDC/zXDrPbDdVCYfeU0BsWO/8tqtlbgT2\n" \
"G9w84FoVxp7Z8VlIMCFlA2zs6SFz7JsDoeA3raAVGI/6ugLOpyypEBMs1OUIJqsi\n" \
"l2D4kF501KKaU73yqWjgom7C12yxow+ev+to51byrvLjKzg6CYG1a4XXvi3tPxq3\n" \
"smPi9WIsgtRqAEFQ8TmDn5XpNpaYbg==\n" \
"-----END CERTIFICATE-----";
*/

// === ToolResponse implementation ===
ToolResponse::ToolResponse(bool err, const char* msg)
    : _error(err), _message(msg ? msg : "") {}

// === WebSocketMCP implementation ===

WebSocketMCP::WebSocketMCP(Client& client)
    : _injectedClient(&client),
      _currentState(WS_DISCONNECTED),
      _port(0),
      _isSecure(false),
      _connectionCallback(nullptr),
      _connected(false),
      _lastReconnectAttempt(0),
      _currentBackoff(INITIAL_BACKOFF),
      _reconnectAttempt(0),
      _lastPingTime(0),
      _awaitingActivation(false),
	  _activationRetryCount(0)
{
    _host[0] = '\0';
    _path[0] = '\0';
    _receiveBuffer[0] = '\0';
    _pendingAgentCode[0] = '\0';
    ESP_LOGI(TAG, "WebSocketMCP initialized");
}

// === NVS Helpers ===

String WebSocketMCP::loadTokenFromNVS() {
    Preferences prefs;
    prefs.begin("xiaozhi", true);
    String token = prefs.getString("mcp_token", "");
    prefs.end();
    if (!token.isEmpty()) {
        ESP_LOGI(TAG, "Loaded token from NVS (len: %u)", token.length());
    }
    return token;
}

bool WebSocketMCP::saveTokenToNVS(const char* token) {
    if (!token || strlen(token) < 50) return false;
    Preferences prefs;
    prefs.begin("xiaozhi", false);
    bool ok = prefs.putString("mcp_token", token);
    prefs.end();
    if (ok) ESP_LOGI(TAG, "Saved token to NVS (len: %u)", strlen(token));
    return ok;
}

bool WebSocketMCP::isActivated() {
    return !loadTokenFromNVS().isEmpty();
}

void WebSocketMCP::clearActivation() {
    Preferences prefs;
    prefs.begin("xiaozhi", false);
    prefs.clear();
    prefs.end();
    ESP_LOGI(TAG, "Cleared NVS activation");
}

// === Activation Flow ===
bool WebSocketMCP::activateWithAgentCode(const char* agentCode) {
    if (!agentCode || strlen(agentCode) == 0) {
        ESP_LOGE(TAG, "activateWithAgentCode: invalid agent code");
        return false;
    }

    ESP_LOGI(TAG, "🔑 Activating agent code: ***%s", 
             agentCode + std::max(0, (int)strlen(agentCode) - 4));

    // ✅ Use /device/activate — endpoint correto
    const char* URL = "https://api.xiaozhi.me/xiaozhi/v1/device/activate";

    // === Etapa 1: Obter acw_tc (pode ser necessário) ===
    WiFiClientSecure client1;
    client1.setCACert(XIAOZHI_ROOT_CA);
    client1.setTimeout(8000);

    HTTPClient http1;
    if (!http1.begin(client1, URL)) {
        ESP_LOGE(TAG, "HTTP1 begin failed");
        return false;
    }

    // ✅ Headers exatos do dispositivo oficial
    http1.addHeader("User-Agent", "XiaoZhi-Device/1.0 ESP32");
    http1.addHeader("Content-Type", "application/json;charset=utf-8"); // ← charset=utf-8 é crítico
    http1.addHeader("Accept", "application/json");

    // ✅ Corpo UTF-8 válido
    String body = "{\"agent_code\":\"";
    body += agentCode;
    body += "\"}";

    int code1 = http1.POST(body);
    String setCookie = http1.header("set-cookie");
    String resp1 = http1.getString();
    http1.end();

    ESP_LOGD(TAG, "Step 1 → HTTP %d, Set-Cookie: %.60s", code1, setCookie.c_str());

    String acw_tc;
    if (code1 == 426 && setCookie.indexOf("acw_tc=") != -1) {
        // Extrair acw_tc
        int start = setCookie.indexOf("acw_tc=") + 7;
        int end = setCookie.indexOf(';', start);
        if (end == -1) end = setCookie.length();
        acw_tc = setCookie.substring(start, end);
        ESP_LOGI(TAG, "🍪 acw_tc extracted (len=%u)", acw_tc.length());
    } else if (code1 == 200) {
        return parseActivationResponse(resp1);
    } else {
        ESP_LOGE(TAG, "Step 1 failed: HTTP %d", code1);
        ESP_LOGE(TAG, "Response: %s", resp1.c_str());
        return false;
    }

    // === Etapa 2: Reenviar com cookie ===
    WiFiClientSecure client2;
    client2.setCACert(XIAOZHI_ROOT_CA);
    client2.setTimeout(8000);

    HTTPClient http2;
    if (!http2.begin(client2, URL)) {
        ESP_LOGE(TAG, "HTTP2 begin failed");
        return false;
    }

    http2.addHeader("User-Agent", "XiaoZhi-Device/1.0 ESP32");
    http2.addHeader("Content-Type", "application/json;charset=utf-8");
    http2.addHeader("Accept", "application/json");
    http2.addHeader("Cookie", "acw_tc=" + acw_tc);

    int code2 = http2.POST(body);
    String resp2 = http2.getString();
    http2.end();

    ESP_LOGD(TAG, "Step 2 → HTTP %d", code2);
    ESP_LOGD(TAG, "Response: %s", resp2.c_str());

    if (code2 == 200) {
        return parseActivationResponse(resp2);
    } else {
        ESP_LOGE(TAG, "Activation failed after cookie: HTTP %d", code2);
        return false;
    }
}

// Função auxiliar para evitar duplicação
bool WebSocketMCP::parseActivationResponse(const String& jsonStr) {
    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, jsonStr);
    if (err) {
        ESP_LOGE(TAG, "JSON parse error: %s", err.c_str());
        return false;
    }

    if (doc["code"].as<int>() != 0) {
        const char* msg = doc["message"] | "(no message)";
        ESP_LOGE(TAG, "Server error: %s", msg);
        return false;
    }

    const char* token = doc["data"]["access_token"] | doc["data"]["accessToken"];
    if (!token || strlen(token) < 50) {
        ESP_LOGE(TAG, "No valid access_token in response");
        return false;
    }

    if (saveTokenToNVS(token)) {
        ESP_LOGI(TAG, "✅ Token activated and saved (len=%u)", strlen(token));
        return true;
    }
    return false;
}

bool WebSocketMCP::beginWithAgentCode(const char* agentCode, ConnectionCallback connCb) {
    if (!agentCode || strlen(agentCode) == 0) {
        ESP_LOGE(TAG, "Invalid agent code");
        return false;
    }

    // Copiar o agentCode para uso futuro (ex: retry)
    size_t len = std::min(strlen(agentCode), sizeof(_pendingAgentCode) - 1);
    strncpy(_pendingAgentCode, agentCode, len);
    _pendingAgentCode[len] = '\0';

    _connectionCallback = connCb;
    _awaitingActivation = true;
    _activationRetryCount = 0;

    // 🔑 Construir endpoint COM agent_code na query string
    char endpointBuf[256];
    int written = snprintf(endpointBuf, sizeof(endpointBuf),
        "wss://api.xiaozhi.me/xiaozhi/v1/?agent_code=%s", agentCode);
    
    if (written < 0 || written >= (int)sizeof(endpointBuf)) {
        ESP_LOGE(TAG, "Agent code too long for endpoint buffer");
        return false;
    }

    ESP_LOGI(TAG, "🔑 Starting activation with agent code: ***%s", 
             agentCode + std::max(0, (int)strlen(agentCode) - 4)); // mostra só últimos 4 chars

    return begin(endpointBuf, connCb);
}

// === Core: begin(endpoint) ===

bool WebSocketMCP::begin(const char* mcpEndpoint, ConnectionCallback connCb) {
    _connectionCallback = connCb;

    // Case 1: nullptr → load token from NVS and use default endpoint
    if (mcpEndpoint == nullptr) {
        String token = loadTokenFromNVS();
        if (token.isEmpty()) {
            ESP_LOGE(TAG, "No token found in NVS. Use beginWithAgentCode() first.");
            return false;
        }
        char endpointBuf[512];
        snprintf(endpointBuf, sizeof(endpointBuf),
                 "wss://api.xiaozhi.me/mcp/?token=%s", token.c_str());
        mcpEndpoint = endpointBuf;
        ESP_LOGI(TAG, "Using saved token from NVS");
    }

    // Parse protocol
    const char* protocol_end = strstr(mcpEndpoint, "://");
    if (!protocol_end) {
        ESP_LOGE(TAG, "Invalid endpoint URL: missing '://'");
        return false;
    }

    if (strncmp(mcpEndpoint, "wss", 3) == 0) {
        _isSecure = true;
        // ✅ SAFE CAST (no RTTI): sketch guarantees _injectedClient is WiFiClientSecure
        if (_injectedClient && strstr(mcpEndpoint, "xiaozhi.me")) {
            // Cast to WiFiClientSecure and inject embedded CA
            ((WiFiClientSecure*)_injectedClient)->setCACert(XIAOZHI_ROOT_CA);
            ESP_LOGI(TAG, "Injected XiaoZhi root CA certificate");
        }
    } else if (strncmp(mcpEndpoint, "ws", 2) == 0) {
        _isSecure = false;
    } else {
        ESP_LOGE(TAG, "Unsupported protocol (must be 'ws' or 'wss')");
        return false;
    }

    if (_isSecure && !_injectedClient) {
        ESP_LOGE(TAG, "WSS requested but no Client object injected.");
        return false;
    }

    // Parse host, port, path
    const char* host_start = protocol_end + 3;
    const char* path_start = strchr(host_start, '/');

    if (!path_start) {
        strncpy(_path, "/", sizeof(_path) - 1);
        _path[sizeof(_path) - 1] = '\0';
    } else {
        size_t path_len = strlen(path_start);
        size_t copy_len = std::min(path_len, sizeof(_path) - 1);
        strncpy(_path, path_start, copy_len);
        _path[copy_len] = '\0';
    }

    size_t host_port_len = path_start ? (path_start - host_start) : strlen(host_start);
    char host_port_temp[MAX_URL_LENGTH];
    if (host_port_len >= sizeof(host_port_temp)) {
        ESP_LOGE(TAG, "Host/port too long (max %u)", MAX_URL_LENGTH - 1);
        return false;
    }
    strncpy(host_port_temp, host_start, host_port_len);
    host_port_temp[host_port_len] = '\0';

    const char* port_sep = strchr(host_port_temp, ':');
    if (!port_sep) {
        _port = _isSecure ? 443 : 80;
        strncpy(_host, host_port_temp, sizeof(_host) - 1);
        _host[sizeof(_host) - 1] = '\0';
    } else {
        size_t host_len = port_sep - host_port_temp;
        if (host_len >= sizeof(_host)) {
            ESP_LOGE(TAG, "Host name too long (max %u)", MAX_URL_LENGTH - 1);
            return false;
        }
        strncpy(_host, host_port_temp, host_len);
        _host[host_len] = '\0';
        _port = atoi(port_sep + 1);
    }

    ESP_LOGI(TAG, "Parsed endpoint: host='%s', port=%u, path='%s'", _host, _port, _path);

    _lastReconnectAttempt = 0;
    _currentBackoff = INITIAL_BACKOFF;

    return true;
}
/**
 * @brief Perform WebSocket handshake (RFC 6455).
 * 
 * Steps:
 * 1. Connect TCP/TLS (if not already connected)
 * 2. Generate Sec-WebSocket-Key (16 random bytes, Base64, no padding)
 * 3. Send HTTP Upgrade request
 * 4. Read and validate server response (must be HTTP/1.1 101)
 * 
 * Note: Enables insecure mode temporarily to work around missing SNI
 * in ESP32 Arduino Core 3.3.3.
 * 
 * @return true if handshake succeeds, false otherwise.
 */
bool WebSocketMCP::performHandshake() {
    ESP_LOGI(TAG, "Starting WebSocket handshake with %s:%u", _host, _port);

    Client* netClient = _injectedClient;
    if (!netClient) {
        ESP_LOGE(TAG, "No network client available");
        return false;
    }

    // Ensure secure client is properly configured
    if (WiFiClientSecure* secure = (WiFiClientSecure*)_injectedClient) {
        secure->setCACert(XIAOZHI_ROOT_CA);
        // ✅ Critical: Use TLS 1.2 only (Alibaba Cloud requirement)
        // This is done via low-level config — but Arduino doesn't expose it directly.
        // Instead, we rely on the fact that setting CA cert + SNI forces correct negotiation.
    }

    if (!netClient->connected()) {
        if (!netClient->connect(_host, _port)) {
            ESP_LOGE(TAG, "Connection failed");
            return false;
        }
    }

    // Generate key
    uint8_t keyBytes[16];
    for (int i = 0; i < 16; i++) keyBytes[i] = random(0, 256);
    char keyBase64[25];
    size_t olen = 0;
    mbedtls_base64_encode((uint8_t*)keyBase64, 25, &olen, keyBytes, 16);
    keyBase64[std::min(olen, (size_t)24)] = '\0';

    // ✅ EXACT request used by XiaoZhi official firmware
    String request = 
        "GET " + String(_path) + " HTTP/1.1\r\n"
        "Host: " + String(_host) + "\r\n"
        "Connection: Upgrade\r\n"
        "Upgrade: websocket\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "Sec-WebSocket-Key: " + String(keyBase64) + "\r\n"
        "User-Agent: XiaoZhi-Device/1.0 ESP32\r\n"
        "\r\n";

    if (netClient->write((const uint8_t*)request.c_str(), request.length()) != request.length()) {
        ESP_LOGE(TAG, "Write failed");
        netClient->stop();
        return false;
    }

    // Read response
    char response[1024] = {0};
    unsigned long start = millis();
    int len = 0;
    while (millis() - start < 8000 && len < 1023) {
        if (netClient->available()) {
            response[len++] = netClient->read();
            if (len >= 4 && strcmp(&response[len-4], "\r\n\r\n") == 0) break;
        }
        delay(1);
    }

    ESP_LOGW(TAG, "Response: %.*s", std::min(len, 512), response);

    if (strstr(response, "101 Switching Protocols")) {
        ESP_LOGI(TAG, "✅ WebSocket handshake successful");
        _currentState = WS_CONNECTED;
        return true;
    }

    netClient->stop();
    return false;
}

bool WebSocketMCP::sendWebSocketFrame(const char* data, size_t len, bool isText) {
    if (!_connected || !_injectedClient) return false;
    Client* netClient = _injectedClient;

    uint8_t header[14];
    int hlen = 0;
    uint8_t mask[4];
    for (int i = 0; i < 4; i++) mask[i] = random(0, 256);

    uint8_t opcode = isText ? 0x01 : 0x0A;
    if (len == 0 && !isText) opcode = _currentState == WS_DISCONNECTED ? 0x08 : 0x0A;
    header[hlen++] = 0x80 | opcode;

    if (len <= 125) {
        header[hlen++] = 0x80 | (uint8_t)len;
    } else if (len <= 65535) {
        header[hlen++] = 0x80 | 126;
        header[hlen++] = (len >> 8) & 0xFF;
        header[hlen++] = len & 0xFF;
    } else {
        return false;
    }

    memcpy(&header[hlen], mask, 4);
    hlen += 4;
    netClient->write(header, hlen);

    for (size_t i = 0; i < len; i++) {
        uint8_t masked = data[i] ^ mask[i % 4];
        netClient->write(&masked, 1);
    }
    netClient->flush();
    return true;
}

size_t WebSocketMCP::receiveWebSocketFrame() {
    if (!_injectedClient || !_injectedClient->connected()) return 0;
    Client* netClient = _injectedClient;

    unsigned long timeout = millis() + 5000;
    while (netClient->available() < 2 && millis() < timeout) delay(1);
    if (netClient->available() < 2) return 0;

    uint8_t h1 = netClient->read();
    uint8_t h2 = netClient->read();
    uint8_t opcode = h1 & 0x0F;
    bool mask = h2 & 0x80;
    size_t plen = h2 & 0x7F;

    if (opcode == 0x08) { disconnect(); return 0; }
    if (opcode == 0x0A || opcode == 0x09) {
        if (opcode == 0x09) sendWebSocketFrame("", 0, false);
        _lastPingTime = millis();
        return 0;
    }
    if (opcode != 0x01 || !(h1 & 0x80)) return 0;

    if (plen == 126) {
        if (netClient->available() < 2) return 0;
        plen = (netClient->read() << 8) | netClient->read();
    } else if (plen == 127) {
        for (int i = 0; i < 8; i++) netClient->read();
        return 0;
    }
    if (plen >= MAX_MESSAGE_LENGTH) {
        while (netClient->available() > 0) netClient->read();
        return 0;
    }

    uint8_t maskingKey[4] = {0};
    if (mask) {
        for (int i = 0; i < 4; i++) maskingKey[i] = netClient->read();
    }

    timeout = millis() + 5000;
    while (netClient->available() < plen && millis() < timeout) delay(1);
    if (netClient->available() < plen) return 0;

    for (size_t i = 0; i < plen; i++) {
        uint8_t b = netClient->read();
        if (mask) b ^= maskingKey[i % 4];
        _receiveBuffer[i] = (char)b;
    }
    _receiveBuffer[plen] = '\0';
    return plen;
}

void WebSocketMCP::processReceivedData() {
    while (_injectedClient && _injectedClient->available()) {
        size_t len = receiveWebSocketFrame();
        if (len > 0) handleJsonRpcMessage(_receiveBuffer, len);
    }
}

void WebSocketMCP::handleJsonRpcMessage(const char* msg, size_t len) {
    StaticJsonDocument<512> doc;  // aumentado para capturar erros maiores
    DeserializationError err = deserializeJson(doc, msg, len);
    if (err) {
        ESP_LOGW(TAG, "JSON parse error in message (len=%u): %s", len, err.c_str());
        ESP_LOGW(TAG, "Raw message: %.200s", msg);  // primeiros 200 chars para debug
        return;
    }
	    // 🔑 Caso especial: ativação automática via agent_code na URL
    if (doc.containsKey("method") && strcmp(doc["method"], "agent.activated") == 0) {
        if (doc.containsKey("params") && doc["params"].containsKey("accessToken")) {
            const char* token = doc["params"]["accessToken"].as<const char*>();
            if (token && strlen(token) > 50) {
                if (saveTokenToNVS(token)) {
                    ESP_LOGI(TAG, "Auto-activated! Token saved (len=%u)", strlen(token));
                    _awaitingActivation = false;
                    _activationRetryCount = 0;

                    // ✅ Opcional: confirmar ao servidor que está pronto
                    const char* readyMsg = R"({"jsonrpc":"2.0","method":"agent.ready","id":2})";
                    sendWebSocketFrame(readyMsg, strlen(readyMsg), true);

                    if (_connectionCallback) _connectionCallback(true);
                    return;
                }
            }
        }
        ESP_LOGE(TAG, "agent.activated received but no valid accessToken");
        return;
    }

    // 🔴 Caso 1: resposta para initialize (id:1) — sucesso ou falha
    if (doc.containsKey("id") && doc["id"] == 1) {
        // ✅ Sucesso: accessToken presente
        if (doc.containsKey("result") && doc["result"].containsKey("accessToken")) {
            const char* token = doc["result"]["accessToken"].as<const char*>();
            if (token && strlen(token) > 50) {
                if (saveTokenToNVS(token)) {
                    ESP_LOGI(TAG, "New token acquired and saved to NVS (len=%u)", strlen(token));
                    _awaitingActivation = false;
                    _activationRetryCount = 0;  // reset contador
                    disconnect();
                    delay(100);
                    begin(nullptr, _connectionCallback);  // reconnect com novo token
                    return;
                } else {
                    ESP_LOGE(TAG, "Failed to save new token to NVS");
                }
            } else {
                ESP_LOGE(TAG, "Invalid accessToken (null or too short)");
            }
            _awaitingActivation = false;
            return;
        }

        // ❌ Falha: error presente (ex: "invalid token", "agent not found")
        if (doc.containsKey("error")) {
            const char* errmsg = doc["error"]["message"] | "(no error message)";
            int errcode = doc["error"]["code"] | -1;
            ESP_LOGE(TAG, "Initialize failed (id=1, code=%d): %s", errcode, errmsg);

            // Se estivermos aguardando ativação (via agent_code), tentar novamente
            if (_awaitingActivation && _pendingAgentCode[0] != '\0') {
                _activationRetryCount++;
                if (_activationRetryCount > MAX_ACTIVATION_RETRIES) {
                    ESP_LOGE(TAG, "Max activation retries (%d) exceeded. Clearing token and aborting.",
                             MAX_ACTIVATION_RETRIES);
                    clearActivation();
                    _awaitingActivation = false;
                    _activationRetryCount = 0;
                    if (_connectionCallback) _connectionCallback(false);
                    return;
                }

                ESP_LOGI(TAG, "Retrying activation (attempt %d/%d) with agent code...",
                         _activationRetryCount, MAX_ACTIVATION_RETRIES);
                disconnect();
                delay(200);
                // Forçar reconexão ao endpoint base (sem token) para reenviar initialize
                begin("wss://api.xiaozhi.me/xiaozhi/v1/", _connectionCallback);
            }
            return;
        }
    }

    // 🔴 Caso 2: notificação assíncrona de erro (ex: token expirou após conexão)
    if (doc.containsKey("method") && strcmp(doc["method"], "agent.error") == 0) {
        const char* reason = doc["params"]["reason"] | "(unknown reason)";
        const char* token_hint = doc["params"]["token"] | "";
        ESP_LOGE(TAG, "agent.error received: '%s'", reason);

        // Verificação adicional: se o token esperado difere do salvo
        String currentToken = loadTokenFromNVS();
        if (!token_hint[0] && !currentToken.isEmpty()) {
            // servidor não enviou token, mas temos um — provavelmente inválido
            ESP_LOGW(TAG, "Token likely invalid/expired (NVS has token, server reports error)");
        } else if (token_hint[0] && currentToken != token_hint) {
            ESP_LOGW(TAG, "Token mismatch: NVS='%s...', server='%s...'",
                     currentToken.substring(0, 8).c_str(),
                     String(token_hint).substring(0, 8).c_str());
        }

        // Decisão: se o motivo envolve autenticação/token, reativar
        const char* keywords[] = {"token", "auth", "expired", "invalid", "revoked"};
        bool shouldReactivate = false;
        for (const char* kw : keywords) {
            if (strstr(reason, kw) != nullptr) {
                shouldReactivate = true;
                break;
            }
        }

        if (shouldReactivate && _pendingAgentCode[0] != '\0') {
            ESP_LOGI(TAG, "Token error detected. Triggering reactivation with agent code...");
            _awaitingActivation = true;
            _activationRetryCount = 0;  // reset aqui porque é nova tentativa explícita
            disconnect();
            delay(200);
            begin("wss://api.xiaozhi.me/xiaozhi/v1/", _connectionCallback);
            return;
        }
    }

    // 🟢 Caso 3: ToolInvocation (seu fluxo existente)
    if (doc.containsKey("method")) {
        const char* method = doc["method"].as<const char*>();
        if (method && strcmp(method, "ToolInvocation") == 0) {
            if (doc.containsKey("params") && doc["params"].containsKey("tool_name")) {
                const char* name = doc["params"]["tool_name"].as<const char*>();
                const char* args = doc["params"]["parameters"].as<const char*>();
                if (!name || !args) {
                    ESP_LOGW(TAG, "ToolInvocation missing name or args");
                    return;
                }

                for (auto& tool : _tools) {
                    if (strcmp(tool.name, name) == 0) {
                        tool.callback(args);
                        return;
                    }
                }
                ESP_LOGW(TAG, "No registered tool found for name: %s", name);
            }
        }
    }
}

void WebSocketMCP::handleReconnect() {
    unsigned long now = millis();
    if (!_connected && (now - _lastReconnectAttempt > _currentBackoff || _lastReconnectAttempt == 0)) {
        _reconnectAttempt++;
        _lastReconnectAttempt = now;
        _currentBackoff = std::min(_currentBackoff * 2, MAX_BACKOFF);

        Client* netClient = _injectedClient;
        if (!netClient || !netClient->connect(_host, _port)) return;

        _currentState = WS_HANDSHAKING;
        if (performHandshake()) {
            _connected = true;
            _lastPingTime = millis();
            _reconnectAttempt = 0;
            _currentBackoff = INITIAL_BACKOFF;

            if (_awaitingActivation && _pendingAgentCode[0]) {
                char init[256];
                snprintf(init, sizeof(init),
                    "{\"jsonrpc\":\"2.0\",\"method\":\"initialize\",\"params\":{\"agent_code\":\"%s\"},\"id\":1}",
                    _pendingAgentCode);
                sendMessage(init, strlen(init));
            }

            if (_connectionCallback) _connectionCallback(true);
        } else {
            netClient->stop();
            _currentState = WS_DISCONNECTED;
        }
    }
}

bool WebSocketMCP::sendMessage(const char* msg, size_t len) {
    return _connected ? sendWebSocketFrame(msg, len, true) : false;
}

void WebSocketMCP::loop() {
    if (!_connected || !_injectedClient || !_injectedClient->connected()) {
        handleReconnect();
        return;
    }

    if (_injectedClient->available()) processReceivedData();

    unsigned long now = millis();
    if (now - _lastPingTime > PING_INTERVAL) {
        sendWebSocketFrame("", 0, false);
        _lastPingTime = now;
    }

    if (_lastPingTime && now - _lastPingTime > DISCONNECT_TIMEOUT) {
        disconnect();
    }
}

bool WebSocketMCP::isConnected() {
    return _connected && _currentState == WS_CONNECTED;
}

void WebSocketMCP::disconnect() {
    if (_connected) {
        sendWebSocketFrame("", 0, false);
        if (_injectedClient) _injectedClient->stop();
        _connected = false;
        _currentState = WS_DISCONNECTED;
        _lastPingTime = 0;
		_activationRetryCount = 0; 
        if (_connectionCallback) _connectionCallback(false);
    }
}

bool WebSocketMCP::registerTool(const char* name, const char* desc,
                                const char* schema, ToolCallback cb) {
    if (!name || !desc || !schema || !cb) return false;
    _tools.push_back({name, desc, schema, cb});
    ESP_LOGI(TAG, "Registered tool: %s", name);
    return true;
}

bool WebSocketMCP::unregisterTool(const char* name) {
    for (auto it = _tools.begin(); it != _tools.end(); ++it) {
        if (strcmp(it->name, name) == 0) {
            _tools.erase(it);
            return true;
        }
    }
    return false;
}

size_t WebSocketMCP::getToolCount() { return _tools.size(); }
void WebSocketMCP::clearTools() { _tools.clear(); }


