#ifndef WEBSOCKET_MCP_H
#define WEBSOCKET_MCP_H

/**
 * @file WebSocketMCP.h
 * @brief WebSocket client for Model Context Protocol (MCP), optimized for XiaoZhi.ai.
 * 
 * Features:
 * - Full RFC 6455 WebSocket framing (client-side masking, ping/pong, close).
 * - JSON-RPC 2.0 message handling (tool_call, tool_response, initialize).
 * - Built-in TLS support with XiaoZhi root CA certificate (no external secrets needed).
 * - Automatic token persistence via NVS (non-volatile storage).
 * - Agent-based activation flow: pair device using 6-digit agent code.
 * 
 * Designed for ESP32 with minimal heap usage:
 * - No String class in hot paths.
 * - All buffers are static (stack or member arrays).
 * - Zero dynamic allocation after setup.
 */

#include <Arduino.h>
#include <Client.h>
#include <functional>
#include <vector>

// Buffer size limits (adjust if needed, but keep conservative for ESP32-S3)
#define MAX_URL_LENGTH      128   // Max hostname length
#define MAX_PATH_LENGTH     512    // Max endpoint path length
#define MAX_MESSAGE_LENGTH  1024  // Max incoming JSON/tool response size


/**
 * @brief Lightweight response wrapper for tool callbacks.
 * 
 * Stores only success/failure flag and a pointer to a static JSON string.
 * Avoids heap allocation and copy overhead.
 */
class ToolResponse {
public:
    /**
     * @param err True if an error occurred (tool should return error response).
     * @param msg Pointer to static JSON string (e.g., "{\"success\":true}").
     *            Must remain valid after return (use literals or static storage).
     */
    ToolResponse(bool err = false, const char* msg = nullptr);

    bool isValid() const { return true; }   // Always valid — caller handles parsing
    bool isError() const { return _error; }
    const char* c_str() const { return _message; }

private:
    bool _error;
    const char* _message; // Points to string literal or static buffer
};

// Callback type for tool invocation: receives JSON args as C-string
typedef std::function<ToolResponse(const char*)> ToolCallback;
typedef void (*ConnectionCallback)(bool connected);

// External declaration of XiaoZhi root CA (defined in .cpp to avoid header bloat)
extern const char XIAOZHI_ROOT_CA[];

/**
 * @brief WebSocket client for Model Context Protocol (MCP).
 * 
 * Supports two usage modes:
 * 
 * 1. Standard mode (manual token):
 *      mcp.begin("wss://api.xiaozhi.me/mcp/?token=...");
 * 
 * 2. XiaoZhi activation mode (recommended):
 *      mcp.beginWithAgentCode("Fx5L4pDZqw"); // 6-digit agent code from device
 *    → Device connects to /mcp, sends `initialize`, receives JWT token,
 *      saves it to NVS, and reconnects automatically.
 * 
 * Thread-safe for FreeRTOS: call loop() from a dedicated task.
 */
class WebSocketMCP {
public:
    /**
     * @brief Constructor — binds to a pre-allocated Client (e.g., WiFiClientSecure).
     * @param client Reference to network client. Must outlive this instance.
     */
    explicit WebSocketMCP(Client& client);

    // === Standard MCP Interface (backward-compatible) ===
    
    /**
     * @brief Initialize connection using a full WebSocket endpoint.
     * @param mcpEndpoint WebSocket URL (e.g., "wss://api.xiaozhi.me/mcp/?token=...")
     *                    If nullptr, loads token from NVS and uses default XiaoZhi endpoint.
     * @param connCb Optional callback for connection state changes.
     * @return true if URL parsed successfully (does not guarantee network connect).
     */
    bool begin(const char* mcpEndpoint, ConnectionCallback connCb = nullptr);


    // === XiaoZhi Activation Flow (recommended for new deployments) ===
    
    /**
     * @brief Activate device using 6-digit agent code (e.g., "Fx5L4pDZqw").
     *        This initiates the XiaoZhi pairing flow:
     *        1. Connects to wss://api.xiaozhi.me/mcp (no token)
     *        2. Sends JSON-RPC `initialize` with agent_code
     *        3. Waits for `accessToken` in response
     *        4. Saves token to NVS under namespace "xiaozhi"
     *        5. Reconnects automatically using the new token
     * @param agentCode 6-digit alphanumeric code from XiaoZhi device dashboard.
     * @param connCb Callback for connection events (called on initial + reconnection).
     * @return true if activation flow started successfully.
     */
    bool beginWithAgentCode(const char* agentCode, ConnectionCallback connCb = nullptr);
	bool activateWithAgentCode(const char* agentCode);
    /**
     * @brief Check if device is already activated (token exists in NVS).
     * @return true if a valid JWT token is stored and ready for use.
     */
    bool isActivated();

    /**
     * @brief Erase stored token from NVS (e.g., for factory reset or re-pairing).
     */
    void clearActivation();

    // === Core MCP Methods ===
    
    /**
     * @brief Send a JSON-RPC message (tool_call, notification, etc.).
     * @param message C-string containing JSON.
     * @param length Length of message (avoids strlen).
     * @return true if frame was queued for sending (does not guarantee delivery).
     */
    bool sendMessage(const char* message, size_t length);

    /**
     * @brief Process network events, handle reconnection, keep-alive.
     *        Must be called repeatedly (e.g., in a FreeRTOS task loop).
     */
    void loop();

    /**
     * @return true if WebSocket handshake completed and connection is active.
     */
    bool isConnected();

    /**
     * @brief Gracefully close connection (sends CLOSE frame, stops client).
     */
    void disconnect();

    // === Tool Management (MCP Protocol) ===
    
    /**
     * @brief Register a tool that the LLM can invoke.
     * @param name Tool name (e.g., "report_result").
     * @param description Human-readable description (for LLM prompt).
     * @param inputSchema JSON Schema string for input validation.
     * @param callback Function to execute when tool is called.
     * @return true on success.
     */
    bool registerTool(const char* name, const char* description,
                      const char* inputSchema, ToolCallback callback);

    bool unregisterTool(const char* name);
	bool saveTokenToNVS(const char* token);
    size_t getToolCount();
    void clearTools();

private:
    // Connection state machine
    enum WsState {
        WS_DISCONNECTED,
        WS_HANDSHAKING,
        WS_CONNECTED
    };

    Client* _injectedClient;
    WsState _currentState;

    // Parsed connection parameters
    char _host[MAX_URL_LENGTH];
    uint16_t _port;
    char _path[MAX_PATH_LENGTH];
    bool _isSecure;
	char _sessionId[64] = {0}; 

    // Reconnection backoff strategy
    ConnectionCallback _connectionCallback;
    bool _connected;
    unsigned long _lastReconnectAttempt;
    static const int INITIAL_BACKOFF;   // 1000 ms
    static const int MAX_BACKOFF;       // 60000 ms
    static const int PING_INTERVAL;     // 10000 ms
    static const int DISCONNECT_TIMEOUT; // 60000 ms
    int _currentBackoff;
    int _reconnectAttempt;
    unsigned long _lastPingTime;

    // Incoming message buffer (reused for each frame)
    char _receiveBuffer[MAX_MESSAGE_LENGTH];

    // Registered tools (MCP protocol)
    struct Tool {
        const char* name;
        const char* description;
        const char* inputSchema;
        ToolCallback callback;
    };
    std::vector<Tool> _tools;

    // Internal protocol methods
    bool performHandshake();
    bool sendWebSocketFrame(const char* data, size_t length, bool isText);
    size_t receiveWebSocketFrame();
    void processReceivedData();
    void handleReconnect();
    void resetReconnectParams();
    void handleJsonRpcMessage(const char* message, size_t length);
	bool parseActivationResponse(const String& jsonStr);

    // NVS helpers (private implementation — no Preferences.h in header)
    String loadTokenFromNVS();
    

    // Activation state during beginWithAgentCode()
    bool _awaitingActivation;
    char _pendingAgentCode[16]; // Holds agent code during activation handshake
	int _activationRetryCount; 
	static const int MAX_ACTIVATION_RETRIES = 3;
};

#endif // WEBSOCKET_MCP_H
