#pragma once

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "EventQueue.h"

class PokeAPIClient {
public:
    PokeAPIClient(EventQueue& eventQueue);
    ~PokeAPIClient();
    
    // Process a fetch request from the event queue
    void processFetchRequest(int pokemonId);
    
    // Process sprite fetch request
    void processSpriteRequest(int pokemonId);
    
private:
    EventQueue& _eventQueue;
    HTTPClient _http;
    WiFiClientSecure _secureClient;
    
    // Fetch species data (name and description)
    bool fetchSpeciesData(int id, String& name, String& description);
    
    // Fetch sprite PNG data
    uint8_t* fetchSpriteData(int id, size_t& size);
    
    // Parse species JSON stream to extract name and flavor text
    void parseSpeciesStream(Stream* stream, String& name, String& description);
    
    // Helper to clean up flavor text (remove newlines, etc)
    String cleanFlavorText(const String& text);
};