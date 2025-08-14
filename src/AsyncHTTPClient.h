#pragma once

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <functional>
#include <map>
#include <memory>
#include "EventQueue.h"

/**
 * @class AsyncHTTPClient
 * @brief True async HTTP client that doesn't block any tasks
 * 
 * Features:
 * - Non-blocking HTTP requests
 * - SSL/TLS support with connection reuse
 * - Automatic retry with exponential backoff
 * - Request timeout handling
 * - Memory efficient with PSRAM support
 * - Thread-safe callbacks via EventQueue
 */
class AsyncHTTPClient {
public:
    /**
     * @brief HTTP request methods
     */
    enum class Method {
        GET,
        POST,
        PUT,
        DELETE
    };

    /**
     * @brief Request callback function types
     */
    using SuccessCallback = std::function<void(const String& response, int statusCode)>;
    using ErrorCallback = std::function<void(const String& error, int statusCode)>;
    using ProgressCallback = std::function<void(size_t current, size_t total)>;

    /**
     * @brief Request configuration
     */
    struct RequestConfig {
        String url;
        Method method = Method::GET;
        String headers;
        String body;
        unsigned long timeout = 30000;  // 30 seconds
        uint8_t maxRetries = 3;
        bool useSSL = true;
        SuccessCallback onSuccess;
        ErrorCallback onError;
        ProgressCallback onProgress;
    };

private:
    /**
     * @brief Internal request state
     */
    enum class RequestState {
        IDLE,
        DNS_LOOKUP,
        CONNECTING,
        SENDING_REQUEST,
        RECEIVING_HEADERS,
        RECEIVING_BODY,
        COMPLETE,
        ERROR,
        TIMEOUT
    };

    /**
     * @brief Active request tracking
     */
    struct ActiveRequest {
        String requestId;
        RequestConfig config;
        RequestState state = RequestState::IDLE;
        WiFiClientSecure* client = nullptr;
        String host;
        uint16_t port = 443;
        String path;
        unsigned long startTime = 0;
        unsigned long lastActivity = 0;
        uint8_t retryCount = 0;
        
        // Response handling
        String responseHeaders;
        String responseBody;
        int statusCode = 0;
        size_t contentLength = 0;
        size_t receivedBytes = 0;
        bool headersParsed = false;
        bool expectingBody = false;
    };

public:
    /**
     * @brief Constructor
     * @param eventQueue Reference to event queue for thread-safe callbacks
     */
    explicit AsyncHTTPClient(EventQueue& eventQueue);
    
    /**
     * @brief Destructor
     */
    ~AsyncHTTPClient();
    
    /**
     * @brief Make an async HTTP request
     * @param config Request configuration including URL, callbacks, etc.
     * @return Request ID for tracking, empty string if failed to queue
     */
    String request(const RequestConfig& config);
    
    /**
     * @brief Cancel a pending request
     * @param requestId ID returned from request() call
     * @return true if request was found and cancelled
     */
    bool cancelRequest(const String& requestId);
    
    /**
     * @brief Cancel all pending requests
     */
    void cancelAllRequests();
    
    /**
     * @brief Process async operations (call regularly from main loop)
     * Must be called from the network task (Core 0)
     */
    void process();
    
    /**
     * @brief Get number of active requests
     * @return Number of requests currently being processed
     */
    size_t getActiveRequestCount() const;
    
    /**
     * @brief Set global request timeout
     * @param timeoutMs Timeout in milliseconds
     */
    void setDefaultTimeout(unsigned long timeoutMs) { _defaultTimeout = timeoutMs; }
    
    /**
     * @brief Set global max retries
     * @param maxRetries Maximum retry attempts
     */
    void setDefaultMaxRetries(uint8_t maxRetries) { _defaultMaxRetries = maxRetries; }

private:
    EventQueue& _eventQueue;                                ///< Event queue for thread-safe callbacks
    std::map<String, std::shared_ptr<ActiveRequest>> _activeRequests; ///< Active requests
    unsigned long _defaultTimeout = 30000;                 ///< Default timeout in ms
    uint8_t _defaultMaxRetries = 3;                        ///< Default max retries
    uint32_t _nextRequestId = 1;                           ///< Counter for generating request IDs
    
    /**
     * @brief Generate unique request ID
     * @return Unique request ID string
     */
    String generateRequestId();
    
    /**
     * @brief Parse URL into components
     * @param url Full URL to parse
     * @param host Output host
     * @param port Output port
     * @param path Output path
     * @param useSSL Output SSL flag
     * @return true if parsing successful
     */
    bool parseUrl(const String& url, String& host, uint16_t& port, String& path, bool& useSSL);
    
    /**
     * @brief Start a request
     * @param request Shared pointer to request object
     */
    void startRequest(std::shared_ptr<ActiveRequest> request);
    
    /**
     * @brief Process a single request
     * @param request Shared pointer to request object
     */
    void processRequest(std::shared_ptr<ActiveRequest> request);
    
    /**
     * @brief Handle DNS lookup for request
     * @param request Shared pointer to request object
     */
    void handleDnsLookup(std::shared_ptr<ActiveRequest> request);
    
    /**
     * @brief Handle connection establishment
     * @param request Shared pointer to request object
     */
    void handleConnection(std::shared_ptr<ActiveRequest> request);
    
    /**
     * @brief Send HTTP request
     * @param request Shared pointer to request object
     */
    void sendHttpRequest(std::shared_ptr<ActiveRequest> request);
    
    /**
     * @brief Receive and parse HTTP response
     * @param request Shared pointer to request object
     */
    void receiveResponse(std::shared_ptr<ActiveRequest> request);
    
    /**
     * @brief Parse HTTP response headers
     * @param request Shared pointer to request object
     * @param data Received data
     */
    void parseResponseHeaders(std::shared_ptr<ActiveRequest> request, const String& data);
    
    /**
     * @brief Complete request successfully
     * @param request Shared pointer to request object
     */
    void completeRequest(std::shared_ptr<ActiveRequest> request);
    
    /**
     * @brief Fail request with error
     * @param request Shared pointer to request object
     * @param error Error message
     */
    void failRequest(std::shared_ptr<ActiveRequest> request, const String& error);
    
    /**
     * @brief Retry request if retries remaining
     * @param request Shared pointer to request object
     * @param error Error that caused retry
     */
    void retryRequest(std::shared_ptr<ActiveRequest> request, const String& error);
    
    /**
     * @brief Clean up request resources
     * @param request Shared pointer to request object
     */
    void cleanupRequest(std::shared_ptr<ActiveRequest> request);
    
    /**
     * @brief Check for request timeouts
     */
    void checkTimeouts();
    
    /**
     * @brief Dispatch callback to UI thread safely
     * @param callback Function to execute on UI thread
     */
    void dispatchCallback(std::function<void()> callback);
};