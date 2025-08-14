// PokeAPIClient.cpp
#include "network/PokeAPIClient.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <cstring>

PokeAPIClient::PokeAPIClient(EventQueue& eventQueue) : _eventQueue(eventQueue) {
    // Configure SSL client to skip certificate verification for now
    _secureClient.setInsecure();
}

PokeAPIClient::~PokeAPIClient() {
    // Destructor
}

void PokeAPIClient::processFetchRequest(int pokemonId) {
    Serial.printf("[PokeAPI] Starting fetch for Pokemon #%d\n", pokemonId);
    
    // First fetch species data for name and description
    String name = "";
    String description = "";
    
    if (fetchSpeciesData(pokemonId, name, description)) {
        Serial.printf("[PokeAPI] Sending event: name='%s', desc='%s'\n", name.c_str(), description.c_str());
        // Send data ready event
        Event event;
        event.type = EventType::POKEAPI_DATA_READY;
        event.intData = pokemonId;
        strncpy(event.stringData, name.c_str(), sizeof(event.stringData) - 1);
        event.stringData[sizeof(event.stringData) - 1] = '\0';
        strncpy(event.stringData2, description.c_str(), sizeof(event.stringData2) - 1);
        event.stringData2[sizeof(event.stringData2) - 1] = '\0';
        _eventQueue.publishEvent(event);
    } else {
        // Send error event
        Event event;
        event.type = EventType::POKEAPI_ERROR;
        strncpy(event.stringData, "Failed to fetch Pokemon data", sizeof(event.stringData) - 1);
        event.stringData[sizeof(event.stringData) - 1] = '\0';
        _eventQueue.publishEvent(event);
    }
}

void PokeAPIClient::processSpriteRequest(int pokemonId) {
    Serial.printf("[PokeAPI] Fetching sprite for Pokemon #%d\n", pokemonId);
    
    size_t size = 0;
    uint8_t* pngData = fetchSpriteData(pokemonId, size);
    
    if (pngData && size > 0) {
        Serial.printf("[PokeAPI] Got sprite: %d bytes\n", size);
        
        // Send sprite ready event
        Event event;
        event.type = EventType::POKEAPI_SPRITE_READY;
        event.intData = pokemonId;
        event.byteData = pngData;
        event.byteDataSize = size;
        _eventQueue.publishEvent(event);
    } else {
        // Send error event
        Event event;
        event.type = EventType::POKEAPI_ERROR;
        strncpy(event.stringData, "Failed to fetch sprite", sizeof(event.stringData) - 1);
        event.stringData[sizeof(event.stringData) - 1] = '\0';
        _eventQueue.publishEvent(event);
    }
}

bool PokeAPIClient::fetchSpeciesData(int id, String& name, String& description) {
    String url = "https://pokeapi.co/api/v2/pokemon-species/" + String(id);
    Serial.printf("[PokeAPI] Fetching species from: %s\n", url.c_str());
    
    // Retry logic with exponential backoff
    const int maxRetries = 3;
    int retryDelay = 1000; // Start with 1 second
    bool foundName = false;
    bool foundDescription = false;
    
    for (int attempt = 0; attempt < maxRetries; attempt++) {
        if (attempt > 0) {
            Serial.printf("[PokeAPI] Retry attempt %d/%d after %dms delay\n", 
                         attempt + 1, maxRetries, retryDelay);
            delay(retryDelay);
            retryDelay *= 2; // Exponential backoff
        }
        
        _http.begin(_secureClient, url);
        _http.setTimeout(15000); // 15 second timeout
        _http.addHeader("User-Agent", "DeskHog/1.0");
        
        int httpCode = _http.GET();
        
        if (httpCode == 200) {
            // Parse JSON stream to extract name and description
            Stream* stream = _http.getStreamPtr();
            String buffer = "";
            
            while (stream->available() && (!foundName || !foundDescription)) {
                char c = stream->read();
                buffer += c;
                
                // Keep buffer size limited
                if (buffer.length() > 2000) {
                    buffer = buffer.substring(1000);
                }
                
                // Look for the root-level name field (not nested in other objects)
                // The root name appears after "is_mythical" field in the JSON structure
                if (!foundName && buffer.indexOf("\"is_mythical\":") != -1) {
                    // We found is_mythical, now look for the next "name" field
                    buffer = "";
                    while (stream->available() && !foundName) {
                        c = stream->read();
                        buffer += c;
                        
                        // Look for the name field that comes after is_mythical
                        int nameIndex = buffer.indexOf("\"name\":\"");
                        if (nameIndex != -1) {
                            // Extract Pokemon name
                            String tempName = "";
                            int startIdx = nameIndex + 8; // Skip past "name":"
                            
                            // Read until we find the closing quote
                            for (int i = startIdx; i < buffer.length(); i++) {
                                if (buffer[i] == '"' && (i == startIdx || buffer[i-1] != '\\')) {
                                    foundName = true;
                                    break;
                                }
                                tempName += buffer[i];
                            }
                            
                            // Continue reading if we haven't found the closing quote
                            while (stream->available() && !foundName) {
                                c = stream->read();
                                if (c == '"') {
                                    foundName = true;
                                    break;
                                }
                                tempName += c;
                            }
                            
                            if (foundName) {
                                name = tempName;
                                Serial.printf("[PokeAPI] Found Pokemon name: %s\n", name.c_str());
                            }
                        }
                    }
                }
                
                // Look for English flavor text
                if (!foundDescription && buffer.indexOf("\"flavor_text_entries\"") != -1) {
                    Serial.println("[PokeAPI] Found flavor_text_entries array");
                    buffer = "";
                    
                    // Keep reading and looking for English entries
                    while (stream->available() && !foundDescription) {
                        c = stream->read();
                        buffer += c;
                        
                        if (buffer.length() > 2000) {
                            // Keep the last 1000 chars to avoid missing patterns at boundaries
                            buffer = buffer.substring(1000);
                        }
                        
                        // Look for pattern: "language":{"name":"en" first, then backtrack to find flavor_text
                        int langIndex = buffer.indexOf("\"language\":{\"name\":\"en\"");
                        if (langIndex == -1) {
                            langIndex = buffer.indexOf("\"language\": {\"name\": \"en\"");
                        }
                        
                        if (langIndex != -1) {
                            // Found English language tag! Now find the associated flavor_text
                            // It could be before or after the language tag within the same object
                            
                            // First, find the boundaries of this flavor_text_entry object
                            // Look backwards for the opening brace
                            int objectStart = -1;
                            int braceCount = 0;
                            for (int i = langIndex - 1; i >= 0; i--) {
                                if (buffer[i] == '}') braceCount++;
                                else if (buffer[i] == '{') {
                                    if (braceCount == 0) {
                                        objectStart = i;
                                        break;
                                    }
                                    braceCount--;
                                }
                            }
                            
                            // Now look for flavor_text within this object's boundaries
                            if (objectStart != -1) {
                                // Extract the object substring to search within
                                String objectStr = buffer.substring(objectStart);
                                
                                // Find the end of this object
                                int objectEnd = -1;
                                braceCount = 1;
                                for (int i = 1; i < objectStr.length(); i++) {
                                    if (objectStr[i] == '{') braceCount++;
                                    else if (objectStr[i] == '}') {
                                        braceCount--;
                                        if (braceCount == 0) {
                                            objectEnd = i;
                                            break;
                                        }
                                    }
                                }
                                
                                // If we need more data to complete the object, read it
                                while (stream->available() && objectEnd == -1) {
                                    char ch = stream->read();
                                    objectStr += ch;
                                    buffer += ch;
                                    
                                    if (ch == '{') braceCount++;
                                    else if (ch == '}') {
                                        braceCount--;
                                        if (braceCount == 0) {
                                            objectEnd = objectStr.length() - 1;
                                            break;
                                        }
                                    }
                                }
                                
                                // Now find flavor_text within this object
                                int flavorIndex = objectStr.indexOf("\"flavor_text\":\"");
                                if (flavorIndex != -1 && (objectEnd == -1 || flavorIndex < objectEnd)) {
                                    // Extract the flavor text
                                    int startIdx = flavorIndex + 15;
                                    String tempDesc = "";
                                    
                                    for (int i = startIdx; i < objectStr.length(); i++) {
                                        if (objectStr[i] == '"' && (i == startIdx || objectStr[i-1] != '\\')) {
                                            break;
                                        }
                                        if (objectStr[i] == '\\' && i + 1 < objectStr.length()) {
                                            i++; // Skip the backslash
                                            if (objectStr[i] == 'n' || objectStr[i] == 'f') {
                                                tempDesc += ' ';
                                            } else if (objectStr[i] == '"') {
                                                tempDesc += '"';
                                            } else if (objectStr[i] == '\\') {
                                                tempDesc += '\\';
                                            } else {
                                                tempDesc += objectStr[i];
                                            }
                                        } else if (objectStr[i] != '\\') {
                                            tempDesc += objectStr[i];
                                        }
                                    }
                                    
                                    description = cleanFlavorText(tempDesc);
                                    foundDescription = true;
                                    Serial.printf("[PokeAPI] Found English description: %s\n", description.c_str());
                                    
                                    // Clear buffer after the found entry to avoid re-processing
                                    buffer = buffer.substring(objectStart + objectEnd + 1);
                                }
                            }
                            
                            // If we found English but no flavor_text in the same object, 
                            // clear this entry from buffer and continue searching
                            if (!foundDescription) {
                                buffer = buffer.substring(langIndex + 20);
                            }
                        }
                    }
                }
            }
            
            _http.end();
            
            if (foundName && foundDescription) {
                Serial.printf("[PokeAPI] fetchSpeciesData returning: name='%s', desc='%s'\n", 
                              name.c_str(), description.c_str());
                return true; // Success
            }
        } else if (httpCode > 0) {
            Serial.printf("[PokeAPI] HTTP error: %d\n", httpCode);
            _http.end();
            
            // Don't retry on 404 or other client errors
            if (httpCode >= 400 && httpCode < 500) {
                return false;
            }
        } else {
            Serial.printf("[PokeAPI] Connection error: %s\n", _http.errorToString(httpCode).c_str());
            _http.end();
        }
    }
    
    return false; // Failed after all retries
}

String PokeAPIClient::cleanFlavorText(const String& raw) {
    String clean = raw;
    
    // Remove any remaining escape sequences
    clean.replace("\\n", " ");
    clean.replace("\\f", " ");
    clean.replace("\\r", " ");
    clean.replace("\\t", " ");
    
    // Clean up multiple spaces
    while (clean.indexOf("  ") != -1) {
        clean.replace("  ", " ");
    }
    
    clean.trim();
    
    return clean;
}

uint8_t* PokeAPIClient::fetchSpriteData(int id, size_t& size) {
    // Use direct sprite URL to avoid extra API calls
    String url = "https://raw.githubusercontent.com/PokeAPI/sprites/master/sprites/pokemon/" 
                 + String(id) + ".png";
    
    Serial.printf("[PokeAPI] Fetching sprite from: %s\n", url.c_str());
    
    // Retry logic with exponential backoff
    const int maxRetries = 3;
    int retryDelay = 1000; // Start with 1 second
    
    for (int attempt = 0; attempt < maxRetries; attempt++) {
        if (attempt > 0) {
            Serial.printf("[PokeAPI] Retry attempt %d/%d after %dms delay\n", 
                         attempt + 1, maxRetries, retryDelay);
            delay(retryDelay);
            retryDelay *= 2; // Exponential backoff
        }
        
        _http.begin(_secureClient, url);
        _http.setTimeout(10000); // 10 second timeout for images
        _http.addHeader("User-Agent", "DeskHog/1.0");
        
        int httpCode = _http.GET();
        
        if (httpCode == 200) {
            int len = _http.getSize();
            
            if (len > 0 && len < 10000) { // Sanity check - sprites should be 1-5KB
                Serial.printf("[PokeAPI] Sprite size: %d bytes\n", len);
                
                // Allocate buffer for PNG
                uint8_t* pngData = new uint8_t[len];
                if (!pngData) {
                    Serial.println("[PokeAPI] Failed to allocate memory for sprite");
                    _http.end();
                    size = 0;
                    continue; // Try again on next iteration
                }
                
                // Read PNG data
                Stream* stream = _http.getStreamPtr();
                size_t bytesRead = stream->readBytes(pngData, len);
                
                _http.end();
                
                if (bytesRead == len) {
                    size = len;
                    return pngData; // Success!
                } else {
                    Serial.printf("[PokeAPI] Read mismatch: expected %d, got %d\n", len, bytesRead);
                    delete[] pngData;
                }
            } else {
                Serial.printf("[PokeAPI] Invalid sprite size: %d\n", len);
                _http.end();
                
                if (len <= 0) {
                    continue; // Retry on network issues
                } else {
                    break; // Don't retry on invalid size
                }
            }
        } else if (httpCode > 0) {
            Serial.printf("[PokeAPI] HTTP error fetching sprite: %d\n", httpCode);
            _http.end();
            
            // Don't retry on 404 or other client errors
            if (httpCode >= 400 && httpCode < 500) {
                break;
            }
        } else {
            Serial.printf("[PokeAPI] Connection error: %s\n", _http.errorToString(httpCode).c_str());
            _http.end();
        }
    }
    
    size = 0;
    return nullptr; // Failed after all retries
}