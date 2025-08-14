#pragma once

#include <Arduino.h>
#include <functional>
#include <memory>
#include <vector>
#include <map>
#include "EventQueue.h"
#include "ui/UICallback.h"

/**
 * @enum NetworkState
 * @brief Represents the current state of a network request
 */
enum class NetworkState {
    IDLE,           ///< No request in progress
    LOADING,        ///< Request in progress
    SUCCESS,        ///< Request completed successfully
    ERROR,          ///< Request failed
    CANCELLED       ///< Request was cancelled
};

/**
 * @struct NetworkRequest
 * @brief Represents a network request with callbacks and state management
 */
struct NetworkRequest {
    String requestId;                                           ///< Unique identifier for the request
    std::function<bool(String&)> networkOperation;             ///< The actual network operation to perform
    std::function<void(const String&)> onSuccess;              ///< Callback for successful completion
    std::function<void(const String&)> onError;                ///< Callback for error handling
    std::function<void()> onCancelled;                         ///< Callback for cancellation
    std::function<void(NetworkState)> onStateChanged;          ///< Callback for state changes
    NetworkState state = NetworkState::IDLE;                   ///< Current state of the request
    String cachedData;                                          ///< Cached data for progressive loading
    bool hasCachedData = false;                                ///< Whether cached data is available
    unsigned long startTime = 0;                               ///< Request start timestamp
    unsigned long timeout = 30000;                             ///< Request timeout in milliseconds
    uint8_t retryCount = 0;                                     ///< Current retry attempt
    uint8_t maxRetries = 3;                                     ///< Maximum retry attempts
};

/**
 * @class AsyncNetworkManager
 * @brief Apple-like async network manager for smooth UI updates
 * 
 * Features:
 * - Non-blocking network operations
 * - Progressive loading (show cached data, update with fresh)
 * - Smooth state transitions
 * - Automatic retry with exponential backoff
 * - Thread-safe UI updates via EventQueue
 * - Request cancellation support
 */
class AsyncNetworkManager {
public:
    /**
     * @brief Constructor
     * @param eventQueue Reference to event queue for UI updates
     */
    explicit AsyncNetworkManager(EventQueue& eventQueue);
    
    /**
     * @brief Destructor - cancels all pending requests
     */
    ~AsyncNetworkManager();
    
    /**
     * @brief Perform an async network request with Apple-like UX
     * 
     * @param requestId Unique identifier for the request
     * @param networkOperation Function that performs the actual network call
     * @param onSuccess Callback for successful completion (called on UI thread)
     * @param onError Callback for error handling (called on UI thread)
     * @param onStateChanged Callback for state changes (called on UI thread)
     * @param cachedData Optional cached data to show immediately
     * @param timeout Request timeout in milliseconds (default: 30s)
     * @param maxRetries Maximum retry attempts (default: 3)
     * @return true if request was queued successfully
     */
    bool performRequest(
        const String& requestId,
        std::function<bool(String&)> networkOperation,
        std::function<void(const String&)> onSuccess,
        std::function<void(const String&)> onError = nullptr,
        std::function<void(NetworkState)> onStateChanged = nullptr,
        const String& cachedData = "",
        unsigned long timeout = 30000,
        uint8_t maxRetries = 3
    );
    
    /**
     * @brief Cancel a pending request
     * @param requestId ID of request to cancel
     * @return true if request was found and cancelled
     */
    bool cancelRequest(const String& requestId);
    
    /**
     * @brief Cancel all pending requests
     */
    void cancelAllRequests();
    
    /**
     * @brief Get current state of a request
     * @param requestId ID of request to check
     * @return Current state of the request
     */
    NetworkState getRequestState(const String& requestId) const;
    
    /**
     * @brief Check if a request is currently active
     * @param requestId ID of request to check
     * @return true if request is in progress
     */
    bool isRequestActive(const String& requestId) const;
    
    /**
     * @brief Process pending requests and handle timeouts
     * Should be called regularly from the network task
     */
    void process();
    
    /**
     * @brief Get number of active requests
     * @return Number of requests currently in progress
     */
    size_t getActiveRequestCount() const;

private:
    EventQueue& _eventQueue;                                    ///< Event queue for UI updates
    std::map<String, std::shared_ptr<NetworkRequest>> _requests; ///< Active requests
    std::vector<std::shared_ptr<NetworkRequest>> _pendingRequests; ///< Requests waiting to be processed
    
    static const unsigned long RETRY_BASE_DELAY = 1000;        ///< Base delay for exponential backoff
    static const unsigned long MAX_RETRY_DELAY = 8000;         ///< Maximum retry delay
    
    /**
     * @brief Calculate retry delay with exponential backoff
     * @param retryCount Current retry attempt
     * @return Delay in milliseconds
     */
    unsigned long calculateRetryDelay(uint8_t retryCount) const;
    
    /**
     * @brief Execute a network request
     * @param request Shared pointer to the request
     */
    void executeRequest(std::shared_ptr<NetworkRequest> request);
    
    /**
     * @brief Handle request completion
     * @param request Shared pointer to the request
     * @param success Whether the request succeeded
     * @param data Response data or error message
     */
    void handleRequestCompletion(std::shared_ptr<NetworkRequest> request, bool success, const String& data);
    
    /**
     * @brief Update request state and notify UI
     * @param request Shared pointer to the request
     * @param newState New state to set
     */
    void updateRequestState(std::shared_ptr<NetworkRequest> request, NetworkState newState);
    
    /**
     * @brief Dispatch UI callback to main thread
     * @param callback Function to execute on UI thread
     */
    void dispatchToUIThread(std::function<void()> callback);
    
    /**
     * @brief Clean up completed requests
     */
    void cleanupCompletedRequests();
};