#pragma once

#include <lvgl.h>
#include "InputHandler.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <vector>
#include <string>

class PokedexCard : public InputHandler {
public:
    PokedexCard(lv_obj_t* parent);
    ~PokedexCard();
    
    lv_obj_t* getCard() const { return _card; }
    
    bool handleButtonPress(uint8_t button_index) override;
    void prepareForRemoval() override { _card = nullptr; }
    
    // Called from network task when data is ready
    void onDataReceived(int id, const String& name, const String& description);
    void onSpriteReceived(uint8_t* pngData, size_t pngSize);
    void onFetchError(const String& error);

private:
    // Current Pokemon (displayed)
    struct {
        int id;
        String name;
        String description;
        lv_img_dsc_t* sprite;
    } _current;
    
    // Pending Pokemon (being fetched)
    struct {
        int id;
        String name;
        String description;
        uint8_t* pngData;
        size_t pngSize;
        lv_img_dsc_t* sprite;
    } _pending;
    
    // Cache for fetched Pokemon (keep up to 5)
    struct CachedPokemon {
        int id;
        String name;
        String description;
        lv_img_dsc_t* sprite;
    };
    static const int MAX_CACHE_SIZE = 5;
    std::vector<CachedPokemon> _cache;
    
    // Network state
    enum State {
        IDLE,
        FETCHING_SPECIES,
        FETCHING_SPRITE,
        DECODING,
        TRANSITIONING
    } _state = IDLE;
    
    // UI elements
    lv_obj_t* _card;
    lv_obj_t* _nameLabel;
    lv_obj_t* _numberLabel;
    lv_obj_t* _descLabel;
    lv_obj_t* _descContainer;  // Container for scrollable text
    lv_obj_t* _spriteImg;
    
    // Battle transition animation
    lv_obj_t* _transitionOverlay;
    lv_obj_t* _gridCells[32];  // 8x4 grid = 32 cells
    bool _isTransitioning;
    
    // Methods
    void setupUI();
    void displayPokemon();
    void transitionToNewPokemon();
    void startTextScrolling();
    lv_img_dsc_t* decodePngToLvgl(uint8_t* pngData, size_t pngSize);
    lv_img_dsc_t* createPlaceholderSprite();
    
    // Memory management
    void freeSprite(lv_img_dsc_t*& sprite);
    
    // Cache management
    bool checkCache(int id);
    void addToCache(int id, const String& name, const String& description, lv_img_dsc_t* sprite);
    void clearCache();
    
    // Battle transition methods
    void startBattleTransition();
    void startSpiralAnimation();
    void createTransitionOverlay();
    static void spiralAnimCallback(void* var, int32_t value);
    static void spiralCompleteCallback(lv_anim_t* anim);
    static void fadeInCallback(lv_anim_t* anim);
    void cleanupTransition();
    void fadeOutOverlay();
    void updatePokemonDisplay();
    
    // Network fetching
    void startFetchSequence(int id);
    
    // For random selection
    unsigned long _lastRequestTime = 0;
    const unsigned long REQUEST_COOLDOWN = 1000;  // 1 second
    bool canMakeRequest();
    bool _dataReady = false;
};