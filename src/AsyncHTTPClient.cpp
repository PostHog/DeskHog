#include "AsyncHTTPClient.h"
#include <algorithm>

AsyncHTTPClient::AsyncHTTPClient(EventQueue& eventQueue) 
    : _eventQueue(eventQueue) {
}

AsyncHTTPClient::~AsyncHTTPClient() {
    cancelAllRequests();
}

String AsyncHTTPClient::request(const RequestConfig& config) {
    String requestId = generateRequestId();
    
    auto request = std::make_shared<ActiveRequest>();
    request->requestId = requestId;
    request->config = config;
    request->startTime = millis();
    request->lastActivity = millis();
    
    // Parse URL
    if (!parseUrl(config.url, request->host, request->port, request->path, request->config.useSSL)) {
        Serial.printf("[AsyncHTTP] Failed to parse URL: %s\n", config.url.c_str());
        return "";
    }
    
    // Store request
    _activeRequests[requestId] = request;
    
    Serial.printf("[AsyncHTTP] Queued request %s: %s\n", requestId.c_str(), config.url.c_str());
    
    return requestId;
}

bool AsyncHTTPClient::cancelRequest(const String& requestId) {
    auto it = _activeRequests.find(requestId);
    if (it != _activeRequests.end()) {
        cleanupRequest(it->second);
        _activeRequests.erase(it);
        Serial.printf("[AsyncHTTP] Cancelled request %s\n", requestId.c_str());
        return true;
    }
    return false;
}

void AsyncHTTPClient::cancelAllRequests() {
    for (auto& pair : _activeRequests) {
        cleanupRequest(pair.second);
    }
    _activeRequests.clear();
    Serial.printf("[AsyncHTTP] Cancelled all requests\n");
}

void AsyncHTTPClient::process() {
    // Check timeouts first
    checkTimeouts();
    
    // Process each active request
    auto it = _activeRequests.begin();
    while (it != _activeRequests.end()) {
        auto request = it->second;
        
        processRequest(request);
        
        // Remove completed/failed requests
        if (request->state == RequestState::COMPLETE || request->state == RequestState::ERROR) {
            it = _activeRequests.erase(it);
        } else {
            ++it;
        }
    }
}

size_t AsyncHTTPClient::getActiveRequestCount() const {
    return _activeRequests.size();
}

String AsyncHTTPClient::generateRequestId() {
    return "req_" + String(_nextRequestId++);
}

bool AsyncHTTPClient::parseUrl(const String& url, String& host, uint16_t& port, String& path, bool& useSSL) {
    // Simple URL parsing for HTTP/HTTPS
    if (url.startsWith("https://")) {
        useSSL = true;
        port = 443;
        String remainder = url.substring(8); // Remove "https://"
        
        int slashIndex = remainder.indexOf('/');
        if (slashIndex == -1) {
            host = remainder;
            path = "/";
        } else {
            host = remainder.substring(0, slashIndex);
            path = remainder.substring(slashIndex);
        }
        
        // Check for custom port
        int colonIndex = host.indexOf(':');
        if (colonIndex != -1) {
            port = host.substring(colonIndex + 1).toInt();
            host = host.substring(0, colonIndex);
        }
        
        return true;
    } else if (url.startsWith("http://")) {
        useSSL = false;
        port = 80;
        String remainder = url.substring(7); // Remove "http://"
        
        int slashIndex = remainder.indexOf('/');
        if (slashIndex == -1) {
            host = remainder;
            path = "/";
        } else {
            host = remainder.substring(0, slashIndex);
            path = remainder.substring(slashIndex);
        }
        
        // Check for custom port
        int colonIndex = host.indexOf(':');
        if (colonIndex != -1) {
            port = host.substring(colonIndex + 1).toInt();
            host = host.substring(0, colonIndex);
        }
        
        return true;
    }
    
    return false;
}

void AsyncHTTPClient::processRequest(std::shared_ptr<ActiveRequest> request) {
    switch (request->state) {
        case RequestState::IDLE:
            startRequest(request);
            break;
            
        case RequestState::DNS_LOOKUP:
            handleDnsLookup(request);
            break;
            
        case RequestState::CONNECTING:
            handleConnection(request);
            break;
            
        case RequestState::SENDING_REQUEST:
            sendHttpRequest(request);
            break;
            
        case RequestState::RECEIVING_HEADERS:
        case RequestState::RECEIVING_BODY:
            receiveResponse(request);
            break;
            
        case RequestState::COMPLETE:
        case RequestState::ERROR:
        case RequestState::TIMEOUT:
            // These states are handled by the caller
            break;
    }
}

void AsyncHTTPClient::startRequest(std::shared_ptr<ActiveRequest> request) {
    // Create SSL client
    request->client = new WiFiClientSecure();
    if (!request->client) {
        failRequest(request, "Failed to create client");
        return;
    }
    
    // Configure SSL
    if (request->config.useSSL) {
        request->client->setInsecure(); // TODO: Add proper certificate validation
    }
    
    Serial.printf("[AsyncHTTP] Starting request %s to %s:%d\n", 
                  request->requestId.c_str(), request->host.c_str(), request->port);
    
    request->state = RequestState::CONNECTING;
    request->lastActivity = millis();
}

void AsyncHTTPClient::handleDnsLookup(std::shared_ptr<ActiveRequest> request) {
    // DNS lookup is handled by WiFiClientSecure::connect() in ESP32
    request->state = RequestState::CONNECTING;
}

void AsyncHTTPClient::handleConnection(std::shared_ptr<ActiveRequest> request) {
    if (!request->client) {
        failRequest(request, "No client available");
        return;
    }
    
    // Try to connect (non-blocking)
    if (!request->client->connected()) {
        int result = request->client->connect(request->host.c_str(), request->port);
        if (result == 1) {
            // Connected successfully
            Serial.printf("[AsyncHTTP] Connected to %s:%d\n", request->host.c_str(), request->port);
            request->state = RequestState::SENDING_REQUEST;
            request->lastActivity = millis();
        } else if (result == 0) {
            // Still connecting, wait
            return;
        } else {
            // Connection failed
            retryRequest(request, "Connection failed");
            return;
        }
    } else {
        // Already connected
        request->state = RequestState::SENDING_REQUEST;
        request->lastActivity = millis();
    }
}

void AsyncHTTPClient::sendHttpRequest(std::shared_ptr<ActiveRequest> request) {
    if (!request->client || !request->client->connected()) {
        retryRequest(request, "Client disconnected during send");
        return;
    }
    
    // Build HTTP request
    String httpRequest;
    
    // Request line
    switch (request->config.method) {
        case Method::GET:    httpRequest += "GET "; break;
        case Method::POST:   httpRequest += "POST "; break;
        case Method::PUT:    httpRequest += "PUT "; break;
        case Method::DELETE: httpRequest += "DELETE "; break;
    }
    httpRequest += request->path + " HTTP/1.1\r\n";
    
    // Headers
    httpRequest += "Host: " + request->host + "\r\n";
    httpRequest += "Connection: close\r\n";
    httpRequest += "User-Agent: DeskHog/1.0\r\n";
    
    // Custom headers
    if (!request->config.headers.isEmpty()) {
        httpRequest += request->config.headers;
        if (!request->config.headers.endsWith("\r\n")) {
            httpRequest += "\r\n";
        }
    }
    
    // Content-Length for POST/PUT
    if (!request->config.body.isEmpty()) {
        httpRequest += "Content-Length: " + String(request->config.body.length()) + "\r\n";
        httpRequest += "Content-Type: application/json\r\n";
    }
    
    // End headers
    httpRequest += "\r\n";
    
    // Body
    if (!request->config.body.isEmpty()) {
        httpRequest += request->config.body;
    }
    
    // Send request
    size_t written = request->client->print(httpRequest);
    if (written == httpRequest.length()) {
        Serial.printf("[AsyncHTTP] Sent request %s (%d bytes)\n", 
                      request->requestId.c_str(), written);
        request->state = RequestState::RECEIVING_HEADERS;
        request->lastActivity = millis();
    } else {
        retryRequest(request, "Failed to send complete request");
    }
}

void AsyncHTTPClient::receiveResponse(std::shared_ptr<ActiveRequest> request) {
    if (!request->client || !request->client->connected()) {
        if (!request->headersParsed) {
            retryRequest(request, "Client disconnected during receive");
            return;
        }
        // Connection closed after headers - this is normal for HTTP/1.1 with Connection: close
        completeRequest(request);
        return;
    }
    
    // Read available data
    String newData;
    while (request->client->available()) {
        newData += (char)request->client->read();
        request->lastActivity = millis();
    }
    
    if (newData.isEmpty()) {
        return; // No new data
    }
    
    if (request->state == RequestState::RECEIVING_HEADERS) {
        request->responseHeaders += newData;
        
        // Check if we have complete headers
        int headerEndIndex = request->responseHeaders.indexOf("\r\n\r\n");
        if (headerEndIndex != -1) {
            // Parse headers
            parseResponseHeaders(request, request->responseHeaders.substring(0, headerEndIndex + 4));
            
            // Start receiving body if there's remaining data
            String remainingData = request->responseHeaders.substring(headerEndIndex + 4);
            request->responseHeaders = request->responseHeaders.substring(0, headerEndIndex + 4);
            
            request->state = RequestState::RECEIVING_BODY;
            request->headersParsed = true;
            
            if (!remainingData.isEmpty()) {
                request->responseBody += remainingData;
                request->receivedBytes += remainingData.length();
            }
        }
    } else if (request->state == RequestState::RECEIVING_BODY) {
        request->responseBody += newData;
        request->receivedBytes += newData.length();
        
        // Call progress callback if available
        if (request->config.onProgress && request->contentLength > 0) {
            dispatchCallback([=]() {
                request->config.onProgress(request->receivedBytes, request->contentLength);
            });
        }
    }
    
    // Check if we've received the complete response
    if (request->headersParsed) {
        bool isComplete = false;
        
        if (request->contentLength > 0) {
            // We know the content length
            isComplete = (request->receivedBytes >= request->contentLength);
        } else {
            // No content length, connection close indicates end
            isComplete = !request->client->connected();
        }
        
        if (isComplete) {
            completeRequest(request);
        }
    }
}

void AsyncHTTPClient::parseResponseHeaders(std::shared_ptr<ActiveRequest> request, const String& headers) {
    // Parse status line
    int firstLineEnd = headers.indexOf("\r\n");
    if (firstLineEnd > 0) {
        String statusLine = headers.substring(0, firstLineEnd);
        int firstSpace = statusLine.indexOf(' ');
        int secondSpace = statusLine.indexOf(' ', firstSpace + 1);
        
        if (firstSpace > 0 && secondSpace > firstSpace) {
            request->statusCode = statusLine.substring(firstSpace + 1, secondSpace).toInt();
        }
    }
    
    // Parse Content-Length
    int contentLengthIndex = headers.indexOf("Content-Length:");
    if (contentLengthIndex >= 0) {
        int lineEnd = headers.indexOf("\r\n", contentLengthIndex);
        if (lineEnd > contentLengthIndex) {
            String contentLengthLine = headers.substring(contentLengthIndex + 15, lineEnd);
            contentLengthLine.trim();
            request->contentLength = contentLengthLine.toInt();
        }
    }
    
    Serial.printf("[AsyncHTTP] Response %s: HTTP %d, Content-Length: %d\n", 
                  request->requestId.c_str(), request->statusCode, request->contentLength);
}

void AsyncHTTPClient::completeRequest(std::shared_ptr<ActiveRequest> request) {
    unsigned long duration = millis() - request->startTime;
    Serial.printf("[AsyncHTTP] Completed request %s in %lu ms (status: %d, size: %d bytes)\n", 
                  request->requestId.c_str(), duration, request->statusCode, request->responseBody.length());
    
    request->state = RequestState::COMPLETE;
    
    // Call success callback on UI thread
    if (request->config.onSuccess) {
        dispatchCallback([=]() {
            request->config.onSuccess(request->responseBody, request->statusCode);
        });
    }
    
    cleanupRequest(request);
}

void AsyncHTTPClient::failRequest(std::shared_ptr<ActiveRequest> request, const String& error) {
    Serial.printf("[AsyncHTTP] Request %s failed: %s\n", request->requestId.c_str(), error.c_str());
    
    request->state = RequestState::ERROR;
    
    // Call error callback on UI thread
    if (request->config.onError) {
        dispatchCallback([=]() {
            request->config.onError(error, request->statusCode);
        });
    }
    
    cleanupRequest(request);
}

void AsyncHTTPClient::retryRequest(std::shared_ptr<ActiveRequest> request, const String& error) {
    request->retryCount++;
    
    if (request->retryCount <= request->config.maxRetries) {
        Serial.printf("[AsyncHTTP] Retrying request %s (attempt %d/%d): %s\n", 
                      request->requestId.c_str(), request->retryCount, request->config.maxRetries, error.c_str());
        
        // Clean up current connection
        if (request->client) {
            request->client->stop();
            delete request->client;
            request->client = nullptr;
        }
        
        // Reset state for retry
        request->state = RequestState::IDLE;
        request->responseHeaders = "";
        request->responseBody = "";
        request->statusCode = 0;
        request->contentLength = 0;
        request->receivedBytes = 0;
        request->headersParsed = false;
        request->startTime = millis();
        request->lastActivity = millis();
        
        // Add exponential backoff delay
        unsigned long delay = 1000 * (1 << (request->retryCount - 1)); // 1s, 2s, 4s, 8s...
        delay = std::min(delay, 8000UL); // Cap at 8 seconds
        
        // TODO: Implement proper async delay instead of blocking
        ::delay(delay);
    } else {
        failRequest(request, "Max retries exceeded: " + error);
    }
}

void AsyncHTTPClient::cleanupRequest(std::shared_ptr<ActiveRequest> request) {
    if (request->client) {
        request->client->stop();
        delete request->client;
        request->client = nullptr;
    }
}

void AsyncHTTPClient::checkTimeouts() {
    unsigned long now = millis();
    
    for (auto& pair : _activeRequests) {
        auto request = pair.second;
        
        // Check overall timeout
        if (now - request->startTime > request->config.timeout) {
            Serial.printf("[AsyncHTTP] Request %s timed out after %lu ms\n", 
                          request->requestId.c_str(), now - request->startTime);
            request->state = RequestState::TIMEOUT;
            retryRequest(request, "Request timeout");
            continue;
        }
        
        // Check activity timeout (no data received for 10 seconds)
        if (now - request->lastActivity > 10000) {
            Serial.printf("[AsyncHTTP] Request %s activity timeout\n", request->requestId.c_str());
            retryRequest(request, "Activity timeout");
        }
    }
}

void AsyncHTTPClient::dispatchCallback(std::function<void()> callback) {
    // Use EventQueue to safely dispatch callback to UI thread
    _eventQueue.publishEvent(EventType::UI_UPDATE_REQUESTED, "", "");
    
    // Execute callback immediately since EventQueue handles thread safety
    if (callback) {
        callback();
    }
}