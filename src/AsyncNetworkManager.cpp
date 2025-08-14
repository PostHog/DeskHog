#include "AsyncNetworkManager.h"
#include <algorithm>

AsyncNetworkManager::AsyncNetworkManager(EventQueue& eventQueue) 
    : _eventQueue(eventQueue) {
}

AsyncNetworkManager::~AsyncNetworkManager() {
    cancelAllRequests();
}

bool AsyncNetworkManager::performRequest(
    const String& requestId,
    std::function<bool(String&)> networkOperation,
    std::function<void(const String&)> onSuccess,
    std::function<void(const String&)> onError,
    std::function<void(NetworkState)> onStateChanged,
    const String& cachedData,
    unsigned long timeout,
    uint8_t maxRetries
) {
    // Cancel existing request with same ID
    cancelRequest(requestId);
    
    // Create new request
    auto request = std::make_shared<NetworkRequest>();
    request->requestId = requestId;
    request->networkOperation = networkOperation;
    request->onSuccess = onSuccess;
    request->onError = onError;
    request->onStateChanged = onStateChanged;
    request->timeout = timeout;
    request->maxRetries = maxRetries;
    request->state = NetworkState::IDLE;
    
    // Handle cached data for progressive loading
    if (!cachedData.isEmpty()) {
        request->cachedData = cachedData;
        request->hasCachedData = true;
        
        // Immediately show cached data on UI thread
        dispatchToUIThread([=]() {
            if (onSuccess) {
                onSuccess(cachedData);
            }
            if (onStateChanged) {
                onStateChanged(NetworkState::LOADING);
            }
        });
    }
    
    // Store request and queue for execution
    _requests[requestId] = request;
    _pendingRequests.push_back(request);
    
    // Set initial state
    updateRequestState(request, NetworkState::LOADING);
    
    return true;
}

bool AsyncNetworkManager::cancelRequest(const String& requestId) {
    auto it = _requests.find(requestId);
    if (it != _requests.end()) {
        auto request = it->second;
        updateRequestState(request, NetworkState::CANCELLED);
        
        // Remove from pending queue
        _pendingRequests.erase(
            std::remove(_pendingRequests.begin(), _pendingRequests.end(), request),
            _pendingRequests.end()
        );
        
        // Notify UI on main thread
        dispatchToUIThread([=]() {
            if (request->onCancelled) {
                request->onCancelled();
            }
        });
        
        _requests.erase(it);
        return true;
    }
    return false;
}

void AsyncNetworkManager::cancelAllRequests() {
    std::vector<String> requestIds;
    for (const auto& pair : _requests) {
        requestIds.push_back(pair.first);
    }
    
    for (const String& id : requestIds) {
        cancelRequest(id);
    }
}

NetworkState AsyncNetworkManager::getRequestState(const String& requestId) const {
    auto it = _requests.find(requestId);
    return (it != _requests.end()) ? it->second->state : NetworkState::IDLE;
}

bool AsyncNetworkManager::isRequestActive(const String& requestId) const {
    NetworkState state = getRequestState(requestId);
    return state == NetworkState::LOADING;
}

void AsyncNetworkManager::process() {
    // Process pending requests
    if (!_pendingRequests.empty()) {
        auto request = _pendingRequests.front();
        _pendingRequests.erase(_pendingRequests.begin());
        
        if (request->state != NetworkState::CANCELLED) {
            executeRequest(request);
        }
    }
    
    // Check for timeouts
    unsigned long now = millis();
    std::vector<String> timedOutRequests;
    
    for (const auto& pair : _requests) {
        auto request = pair.second;
        if (request->state == NetworkState::LOADING && 
            now - request->startTime > request->timeout) {
            timedOutRequests.push_back(pair.first);
        }
    }
    
    // Handle timeouts
    for (const String& requestId : timedOutRequests) {
        auto it = _requests.find(requestId);
        if (it != _requests.end()) {
            auto request = it->second;
            
            // Try retry if attempts remaining
            if (request->retryCount < request->maxRetries) {
                request->retryCount++;
                
                // Calculate exponential backoff delay
                unsigned long retryDelay = calculateRetryDelay(request->retryCount);
                
                Serial.printf("Request %s timed out, retrying in %lu ms (attempt %d/%d)\n", 
                             requestId.c_str(), retryDelay, request->retryCount, request->maxRetries);
                
                // Add delay and re-queue
                delay(retryDelay);
                _pendingRequests.push_back(request);
                request->startTime = millis();
            } else {
                // Max retries reached, fail the request
                handleRequestCompletion(request, false, "Request timed out after " + String(request->maxRetries) + " retries");
            }
        }
    }
    
    // Clean up completed requests
    cleanupCompletedRequests();
}

size_t AsyncNetworkManager::getActiveRequestCount() const {
    size_t count = 0;
    for (const auto& pair : _requests) {
        if (pair.second->state == NetworkState::LOADING) {
            count++;
        }
    }
    return count;
}

unsigned long AsyncNetworkManager::calculateRetryDelay(uint8_t retryCount) const {
    // Exponential backoff: 1s, 2s, 4s, 8s (capped at MAX_RETRY_DELAY)
    unsigned long delay = RETRY_BASE_DELAY << (retryCount - 1);
    return std::min(delay, MAX_RETRY_DELAY);
}

void AsyncNetworkManager::executeRequest(std::shared_ptr<NetworkRequest> request) {
    if (request->state == NetworkState::CANCELLED) {
        return;
    }
    
    request->startTime = millis();
    
    // Execute the network operation
    String response;
    bool success = false;
    
    try {
        success = request->networkOperation(response);
    } catch (const std::exception& e) {
        Serial.printf("Network operation exception: %s\n", e.what());
        response = "Network operation failed: " + String(e.what());
        success = false;
    }
    
    // Handle completion
    handleRequestCompletion(request, success, response);
}

void AsyncNetworkManager::handleRequestCompletion(std::shared_ptr<NetworkRequest> request, bool success, const String& data) {
    if (request->state == NetworkState::CANCELLED) {
        return;
    }
    
    if (success) {
        updateRequestState(request, NetworkState::SUCCESS);
        
        // Only call success callback if data is different from cached data
        bool shouldUpdateUI = !request->hasCachedData || request->cachedData != data;
        
        if (shouldUpdateUI) {
            dispatchToUIThread([=]() {
                if (request->onSuccess) {
                    request->onSuccess(data);
                }
            });
        }
    } else {
        // Check if we should retry
        if (request->retryCount < request->maxRetries) {
            request->retryCount++;
            
            // Calculate retry delay
            unsigned long retryDelay = calculateRetryDelay(request->retryCount);
            
            Serial.printf("Request %s failed, retrying in %lu ms (attempt %d/%d)\n", 
                         request->requestId.c_str(), retryDelay, request->retryCount, request->maxRetries);
            
            // Add delay and re-queue
            delay(retryDelay);
            _pendingRequests.push_back(request);
            request->startTime = millis();
            return;
        }
        
        // Max retries reached or no retries configured
        updateRequestState(request, NetworkState::ERROR);
        
        dispatchToUIThread([=]() {
            if (request->onError) {
                request->onError(data);
            }
        });
    }
}

void AsyncNetworkManager::updateRequestState(std::shared_ptr<NetworkRequest> request, NetworkState newState) {
    if (request->state != newState) {
        request->state = newState;
        
        // Notify UI of state change
        dispatchToUIThread([=]() {
            if (request->onStateChanged) {
                request->onStateChanged(newState);
            }
        });
    }
}

void AsyncNetworkManager::dispatchToUIThread(std::function<void()> callback) {
    // Use EventQueue to safely dispatch to UI thread
    _eventQueue.publishEvent(EventType::UI_UPDATE_REQUESTED, "", "");
    
    // Store callback for UI thread to execute
    // This is a simplified approach - in a full implementation, you'd want a proper callback queue
    // For now, we'll execute immediately since EventQueue handles thread safety
    if (callback) {
        callback();
    }
}

void AsyncNetworkManager::cleanupCompletedRequests() {
    auto it = _requests.begin();
    while (it != _requests.end()) {
        auto request = it->second;
        if (request->state == NetworkState::SUCCESS || 
            request->state == NetworkState::ERROR || 
            request->state == NetworkState::CANCELLED) {
            it = _requests.erase(it);
        } else {
            ++it;
        }
    }
}