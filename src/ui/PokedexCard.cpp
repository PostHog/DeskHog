#include "ui/PokedexCard.h"
#include "Style.h"
#include "hardware/Input.h"
#include "EventQueue.h"
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include "../../lib/lodepng/lodepng.h"
#include "sprites/sprites.h"  // For default Pokemon sprites

extern EventQueue* eventQueue;  // Global event queue

// Offline Pokemon data (from old PokedexCard)
struct OfflinePokemon {
    int id;
    const char* name;
    const char* description;
    const lv_img_dsc_t* sprite;
};

static const OfflinePokemon offlinePokemon[] = {
    {1, "Bulbasaur", "A strange seed was planted on its back at birth.", &sprite_bulbasaur},
    {4, "Charmander", "The flame at the tip of its tail makes a sound as it burns.", &sprite_charmander},
    {7, "Squirtle", "After birth, its back swells and hardens into a shell.", &sprite_squirtle},
    {25, "Pikachu", "When several gather, their electricity could build and cause storms.", &sprite_pikachu}
};
static const int OFFLINE_POKEMON_COUNT = sizeof(offlinePokemon) / sizeof(offlinePokemon[0]);
static int currentOfflineIndex = -1;  // Will be initialized randomly in constructor

// Helper for text scrolling animation
static void scroll_y_anim_cb(void* obj, int32_t v) {
    lv_obj_set_y((lv_obj_t*)obj, -v);
}

PokedexCard::PokedexCard(lv_obj_t* parent) : _card(nullptr), _state(IDLE), _isTransitioning(false), 
    _transitionOverlay(nullptr), _descContainer(nullptr), _lastRequestTime(0) {
    
    // Initialize grid cells array
    for (int i = 0; i < 32; i++) {
        _gridCells[i] = nullptr;
    }
    
    // Randomly select initial offline Pokemon if not already set
    if (currentOfflineIndex == -1) {
        currentOfflineIndex = random(0, OFFLINE_POKEMON_COUNT);
    }
    
    // Initialize current with random offline Pokemon
    _current.id = offlinePokemon[currentOfflineIndex].id;
    _current.name = offlinePokemon[currentOfflineIndex].name;
    _current.description = offlinePokemon[currentOfflineIndex].description;
    _current.sprite = (lv_img_dsc_t*)offlinePokemon[currentOfflineIndex].sprite;
    
    _pending.pngData = nullptr;
    _pending.pngSize = 0;
    _pending.sprite = nullptr;
    
    _card = lv_obj_create(parent);
    lv_obj_set_size(_card, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(_card, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_width(_card, 0, 0);
    lv_obj_set_style_pad_all(_card, 8, 0);
    
    setupUI();
    displayPokemon();
}

void PokedexCard::setupUI() {
    // Pokemon number label (top left)
    _numberLabel = lv_label_create(_card);
    lv_obj_set_style_text_color(_numberLabel, lv_color_hex(0xffcc00), 0);
    lv_obj_set_style_text_font(_numberLabel, Style::labelFont(), 0);
    lv_obj_align(_numberLabel, LV_ALIGN_TOP_LEFT, 0, 0);
    
    // Pokemon name label (top, next to number)
    _nameLabel = lv_label_create(_card);
    lv_obj_set_style_text_color(_nameLabel, lv_color_white(), 0);
    lv_obj_set_style_text_font(_nameLabel, Style::labelFont(), 0);  // Use smaller font
    lv_obj_align(_nameLabel, LV_ALIGN_TOP_LEFT, 45, 0);  // Positioned to the right of number
    
    // Sprite image (left side, lower position)
    _spriteImg = lv_img_create(_card);
    lv_obj_align(_spriteImg, LV_ALIGN_BOTTOM_LEFT, 5, -5);  // 5px from left and bottom
    // Set placeholder
    lv_obj_set_style_bg_color(_spriteImg, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_opa(_spriteImg, LV_OPA_COVER, 0);
    lv_obj_set_size(_spriteImg, 80, 80);  // Slightly smaller to fit layout
    
    // Container for scrollable description (right side, next to sprite)
    _descContainer = lv_obj_create(_card);
    lv_obj_set_size(_descContainer, 135, 110);  // More right padding, extend to bottom
    lv_obj_set_pos(_descContainer, 95, 25);
    lv_obj_set_style_bg_opa(_descContainer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_descContainer, 0, 0);
    lv_obj_set_style_pad_all(_descContainer, 0, 0);
    lv_obj_clear_flag(_descContainer, LV_OBJ_FLAG_SCROLLABLE);  // We'll control scrolling manually
    
    // Description label inside container
    _descLabel = lv_label_create(_descContainer);
    lv_obj_set_style_text_color(_descLabel, lv_color_hex(0xcccccc), 0);
    lv_obj_set_style_text_font(_descLabel, Style::labelFont(), 0);
    lv_label_set_long_mode(_descLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(_descLabel, 135);  // Match container width
    lv_obj_set_pos(_descLabel, 0, 0);
}

void PokedexCard::updatePokemonDisplay() {
    // Update Pokemon data
    _current.id = _pending.id;
    _current.name = _pending.name;
    _current.description = _pending.description;
    
    // Free old sprite if exists
    freeSprite(_current.sprite);
    
    // Use the already-decoded sprite
    _current.sprite = _pending.sprite;
    _pending.sprite = nullptr; // Transfer ownership
    
    // Update display
    displayPokemon();
}

void PokedexCard::displayPokemon() {
    // Update number
    char numStr[16];
    snprintf(numStr, sizeof(numStr), "#%03d", _current.id);
    lv_label_set_text(_numberLabel, numStr);
    
    // Update name (convert to uppercase)
    String upperName = _current.name;
    upperName.toUpperCase();
    lv_label_set_text(_nameLabel, upperName.c_str());
    
    // Update description
    lv_label_set_text(_descLabel, _current.description.c_str());
    
    // Start scrolling if text overflows
    startTextScrolling();
    
    // Update sprite if available
    if (_current.sprite != nullptr) {
        
        lv_img_set_src(_spriteImg, _current.sprite);
    }
}

bool PokedexCard::handleButtonPress(uint8_t button_index) {
    if (button_index == Input::BUTTON_CENTER) {
        // Ignore if already fetching or transitioning
        if (_state != IDLE || _isTransitioning) {
            return true;
        }
        
        // Check cooldown
        if (!canMakeRequest()) {
            return true;
        }
        
        // Generate random Pokemon ID (All generations 1-9)
        int randomId = random(1, 1026);  // Up to Gen 9 (Paldea region)
        
        // Start fetch sequence
        startFetchSequence(randomId);
        
        return true;
    }
    
    // Return false for UP/DOWN buttons so CardNavigationStack handles card navigation
    return false;
}

bool PokedexCard::canMakeRequest() {
    unsigned long now = millis();
    if ((now - _lastRequestTime) > REQUEST_COOLDOWN) {
        _lastRequestTime = now;
        return true;
    }
    return false;
}

void PokedexCard::startFetchSequence(int id) {
    // First check cache
    if (checkCache(id)) {
        // Found in cache! Start transition immediately
        startBattleTransition();
        return;
    }
    
    // Not in cache, proceed with network fetch
    _state = FETCHING_SPECIES;
    _pending.id = id;
    
    // Start the battle transition immediately
    startBattleTransition();
    
    // Queue network request via EventQueue
    Event event;
    event.type = EventType::POKEAPI_FETCH_REQUEST;
    event.intData = id;
    eventQueue->publishEvent(event);
}

void PokedexCard::onDataReceived(int id, const String& name, const String& description) {
    if (_state != FETCHING_SPECIES && _state != FETCHING_SPRITE) {
        return;  // Ignore if not expecting data
    }
    
    Serial.printf("[PokedexCard] onDataReceived: id=%d, name='%s', desc='%s'\n", 
                  id, name.c_str(), description.c_str());
    
    // Clean up the description text - handle common Pokemon characters
    String cleanDesc = "";
    for (size_t i = 0; i < description.length(); i++) {
        unsigned char c = (unsigned char)description.charAt(i);
        
        // Handle UTF-8 sequences
        if (c == 0xC3) {
            // This might be the start of an accented character (2-byte UTF-8)
            if (i + 1 < description.length()) {
                unsigned char next = (unsigned char)description.charAt(i+1);
                if (next == 0xA9) {
                    // é (lowercase e with acute)
                    cleanDesc += "e";
                    i += 1;
                    continue;
                } else if (next == 0x89) {
                    // É (uppercase E with acute)
                    cleanDesc += "E";
                    i += 1;
                    continue;
                }
            }
        } else if (c == 0xE2) {
            // This might be the start of a smart quote or em-dash (3-byte UTF-8)
            if (i + 2 < description.length()) {
                if (description.charAt(i+1) == 0x80) {
                    char third = description.charAt(i+2);
                    if (third == 0x98 || third == 0x99) {
                        // Smart single quotes
                        cleanDesc += "'";
                        i += 2;
                        continue;
                    } else if (third == 0x9C || third == 0x9D) {
                        // Smart double quotes
                        cleanDesc += "\"";
                        i += 2;
                        continue;
                    } else if (third == 0x94 || third == 0x93) {
                        // Em dash or en dash
                        cleanDesc += "-";
                        i += 2;
                        continue;
                    }
                }
            }
        }
        
        // Keep only printable ASCII characters
        if (c >= 32 && c <= 126) {
            cleanDesc += (char)c;
        } else if (c == '\n' || c == '\r') {
            cleanDesc += ' ';  // Replace newlines with spaces
        }
    }
    
    // Also clean the name (Pokemon names can have accented characters)
    String cleanName = "";
    for (size_t i = 0; i < name.length(); i++) {
        unsigned char c = (unsigned char)name.charAt(i);
        
        // Handle UTF-8 sequences
        if (c == 0xC3) {
            if (i + 1 < name.length()) {
                unsigned char next = (unsigned char)name.charAt(i+1);
                if (next == 0xA9) {
                    cleanName += "e";  // é -> e
                    i += 1;
                    continue;
                } else if (next == 0x89) {
                    cleanName += "E";  // É -> E
                    i += 1;
                    continue;
                }
            }
        }
        
        // Keep printable ASCII
        if (c >= 32 && c <= 126) {
            cleanName += (char)c;
        }
    }
    
    _pending.id = id;
    _pending.name = cleanName;
    _pending.description = cleanDesc;
    
    // Update state
    _state = FETCHING_SPRITE;
    
    // Now request the sprite
    Event event;
    event.type = EventType::POKEAPI_FETCH_SPRITE;
    event.intData = id;
    eventQueue->publishEvent(event);
}

void PokedexCard::onSpriteReceived(uint8_t* pngData, size_t pngSize) {
    if (_state != FETCHING_SPRITE) {
        delete[] pngData;  // Clean up if not expecting
        return;
    }
    
    Serial.printf("[PokedexCard] Received sprite PNG: %d bytes\n", pngSize);
    
    // Store the PNG data temporarily
    _pending.pngData = pngData;
    _pending.pngSize = pngSize;
    
    // Transition to new Pokemon
    transitionToNewPokemon();
}

void PokedexCard::onFetchError(const String& error) {
    Serial.printf("[PokedexCard] Fetch error: %s\n", error.c_str());
    
    // Cycle to next offline Pokemon on error
    currentOfflineIndex = (currentOfflineIndex + 1) % OFFLINE_POKEMON_COUNT;
    _current.id = offlinePokemon[currentOfflineIndex].id;
    _current.name = offlinePokemon[currentOfflineIndex].name;
    _current.description = "Network error. Showing offline Pokemon.";
    _current.sprite = (lv_img_dsc_t*)offlinePokemon[currentOfflineIndex].sprite;
    
    // Clear pending data
    if (_pending.pngData) {
        delete[] _pending.pngData;
        _pending.pngData = nullptr;
        _pending.pngSize = 0;
    }
    
    // Return to idle state
    _state = IDLE;
    
    // If transition is running, complete it to show offline Pokemon
    if (_isTransitioning && _transitionOverlay) {
        // Move offline Pokemon data to pending so transition can complete
        _pending.id = _current.id;
        _pending.name = _current.name;
        _pending.description = _current.description;
        _pending.sprite = _current.sprite;
        fadeOutOverlay();
    } else {
        // Just update display directly
        displayPokemon();
    }
}

void PokedexCard::transitionToNewPokemon() {
    _state = TRANSITIONING;
    
    // Decode PNG to LVGL format
    if (_pending.pngData && _pending.pngSize > 0) {
        _pending.sprite = decodePngToLvgl(_pending.pngData, _pending.pngSize);
        
        // Clean up PNG data after decoding
        delete[] _pending.pngData;
        _pending.pngData = nullptr;
        _pending.pngSize = 0;
    }
    
    // If transition is already running and waiting for data,
    // we should trigger the update now that data is ready
    if (_isTransitioning) {
        // Check if the transition already completed and is waiting
        // If so, update the display now
        if (_transitionOverlay) {
            // Stop any pulsing animations
            lv_anim_del(_transitionOverlay, nullptr);
            
            // Fade out overlay (which will update the display)
            fadeOutOverlay();
        }
        // Otherwise the transition is still running and will pick up the data
    } else {
        // No transition running, start one
        startBattleTransition();
    }
}

lv_img_dsc_t* PokedexCard::decodePngToLvgl(uint8_t* pngData, size_t pngSize) {
    Serial.printf("[PokedexCard] Decoding PNG to LVGL format (%d bytes)\n", pngSize);
    
    // Check PNG signature and print first bytes for debugging
    Serial.printf("[PokedexCard] First 8 bytes: %02X %02X %02X %02X %02X %02X %02X %02X\n",
                 pngData[0], pngData[1], pngData[2], pngData[3], 
                 pngData[4], pngData[5], pngData[6], pngData[7]);
    
    if (pngSize < 8 || pngData[0] != 0x89 || pngData[1] != 'P' || pngData[2] != 'N' || pngData[3] != 'G') {
        Serial.printf("[PokedexCard] Invalid PNG signature!\n");
        return createPlaceholderSprite();
    }
    
    // Decode PNG using lodepng
    unsigned char* decodedImage = nullptr;
    unsigned width, height;
    
    // Decode to RGBA format - use generic decode to handle indexed PNGs
    // LCT_RGBA = 6, bitdepth = 8 for RGBA8888
    unsigned error = lodepng_decode_memory(&decodedImage, &width, &height, 
                                           (const unsigned char*)pngData, (size_t)pngSize,
                                           6, 8);  // 6=LCT_RGBA, 8=8 bits per channel
    
    if (error) {
        Serial.printf("[PokedexCard] PNG decode error %u\n", error);
        // Fall back to colored placeholder
        return createPlaceholderSprite();
    }
    
    Serial.printf("[PokedexCard] Decoded PNG: %ux%u pixels\n", width, height);
    
    // Calculate size for LVGL format (ARGB8888)
    size_t dataSize = width * height * 4;
    
    // Allocate sprite data in PSRAM
    uint8_t* lvglData = (uint8_t*)heap_caps_malloc(dataSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!lvglData) {
        Serial.println("[PokedexCard] Failed to allocate memory for LVGL sprite");
        free(decodedImage);
        return createPlaceholderSprite();
    }
    
    // Convert from RGBA to ARGB (LVGL format)
    // lodepng gives us RGBA, but LVGL expects ARGB
    uint32_t* dstPixels = (uint32_t*)lvglData;
    
    for (size_t i = 0; i < width * height; i++) {
        // decodedImage is a byte array with RGBA format
        uint8_t* srcBytes = (uint8_t*)decodedImage;
        size_t srcIndex = i * 4;
        
        uint8_t r = srcBytes[srcIndex + 0];
        uint8_t g = srcBytes[srcIndex + 1]; 
        uint8_t b = srcBytes[srcIndex + 2];
        uint8_t a = srcBytes[srcIndex + 3];
        
        // Pack as ARGB8888 for LVGL
        dstPixels[i] = (a << 24) | (r << 16) | (g << 8) | b;
    }
    
    // Free the decoded image from lodepng
    free(decodedImage);
    
    // Create LVGL image descriptor  
    lv_img_dsc_t* sprite = (lv_img_dsc_t*)heap_caps_malloc(sizeof(lv_img_dsc_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!sprite) {
        heap_caps_free(lvglData);
        return createPlaceholderSprite();
    }
    
    sprite->header.magic = LV_IMAGE_HEADER_MAGIC;  // Must be 0x19 for LVGL 9
    sprite->header.cf = LV_COLOR_FORMAT_ARGB8888;
    sprite->header.flags = 0;  // No special flags needed
    sprite->header.w = width;
    sprite->header.h = height;
    sprite->header.stride = width * 4;  // 4 bytes per pixel for ARGB8888
    sprite->header.reserved_2 = 0;
    sprite->data_size = dataSize;
    sprite->data = lvglData;
    
    Serial.printf("[PokedexCard] Successfully decoded sprite for Pokemon #%d (%ux%u)\n", 
                 _pending.id, width, height);
    
    // Log PSRAM usage
    size_t freePSRAM = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    Serial.printf("[PokedexCard] Free PSRAM after allocation: %d KB\n", freePSRAM / 1024);
    
    return sprite;
}

lv_img_dsc_t* PokedexCard::createPlaceholderSprite() {
    const int width = 96;
    const int height = 96;
    const int bytesPerPixel = 4; // ARGB8888
    size_t dataSize = width * height * bytesPerPixel;
    
    // Allocate in PSRAM for placeholder sprites
    uint8_t* imgData = (uint8_t*)heap_caps_malloc(dataSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!imgData) {
        Serial.println("[PokedexCard] Failed to allocate memory for placeholder sprite");
        return nullptr;
    }
    
    // Create a colored pattern based on Pokemon ID
    uint32_t baseColor = 0xFF000000; // Alpha = 255
    baseColor |= ((_pending.id * 7) % 256) << 16;  // Red
    baseColor |= ((_pending.id * 13) % 256) << 8;  // Green  
    baseColor |= ((_pending.id * 17) % 256);       // Blue
    
    // Fill with gradient pattern
    uint32_t* pixels = (uint32_t*)imgData;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = y * width + x;
            
            // Add some pattern variation
            int brightness = 100 + ((x + y) % 50);
            uint32_t r = ((baseColor >> 16) & 0xFF) * brightness / 150;
            uint32_t g = ((baseColor >> 8) & 0xFF) * brightness / 150;
            uint32_t b = (baseColor & 0xFF) * brightness / 150;
            
            if (r > 255) r = 255;
            if (g > 255) g = 255;
            if (b > 255) b = 255;
            
            pixels[idx] = 0xFF000000 | (r << 16) | (g << 8) | b;
        }
    }
    
    // Create LVGL image descriptor  
    lv_img_dsc_t* sprite = (lv_img_dsc_t*)heap_caps_malloc(sizeof(lv_img_dsc_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!sprite) {
        heap_caps_free(imgData);
        return nullptr;
    }
    
    sprite->header.magic = LV_IMAGE_HEADER_MAGIC;  // Must be 0x19 for LVGL 9
    sprite->header.cf = LV_COLOR_FORMAT_ARGB8888;
    sprite->header.flags = 0;  // No special flags needed
    sprite->header.w = width;
    sprite->header.h = height;
    sprite->header.stride = width * 4;  // 4 bytes per pixel for ARGB8888
    sprite->header.reserved_2 = 0;
    sprite->data_size = dataSize;
    sprite->data = imgData;
    
    Serial.printf("[PokedexCard] Created placeholder sprite for Pokemon #%d\n", _pending.id);
    return sprite;
}

// Battle transition methods (similar to PokedexCard)
void PokedexCard::startBattleTransition() {
    _isTransitioning = true;
    
    // Create the overlay
    createTransitionOverlay();
    
    // Phase 1: First flash
    lv_anim_t flash1;
    lv_anim_init(&flash1);
    lv_anim_set_var(&flash1, _transitionOverlay);
    lv_anim_set_values(&flash1, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_time(&flash1, 40);
    lv_anim_set_exec_cb(&flash1, [](void* var, int32_t v) {
        lv_obj_t* overlay = (lv_obj_t*)var;
        lv_obj_set_style_bg_color(overlay, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(overlay, v, 0);
    });
    lv_anim_set_ready_cb(&flash1, [](lv_anim_t* a) {
        // Flash 1 fade out
        lv_anim_t flash1_out;
        lv_anim_init(&flash1_out);
        lv_anim_set_var(&flash1_out, a->var);
        lv_anim_set_values(&flash1_out, LV_OPA_COVER, LV_OPA_TRANSP);
        lv_anim_set_time(&flash1_out, 40);
        lv_anim_set_exec_cb(&flash1_out, [](void* var, int32_t v) {
            lv_obj_set_style_bg_opa((lv_obj_t*)var, v, 0);
        });
        lv_anim_set_ready_cb(&flash1_out, [](lv_anim_t* a2) {
            // Flash 2 fade in
            lv_anim_t flash2;
            lv_anim_init(&flash2);
            lv_anim_set_var(&flash2, a2->var);
            lv_anim_set_values(&flash2, LV_OPA_TRANSP, LV_OPA_COVER);
            lv_anim_set_time(&flash2, 40);
            lv_anim_set_delay(&flash2, 20);
            lv_anim_set_exec_cb(&flash2, [](void* var, int32_t v) {
                lv_obj_set_style_bg_opa((lv_obj_t*)var, v, 0);
            });
            lv_anim_set_ready_cb(&flash2, [](lv_anim_t* a3) {
                // Flash 2 fade out
                lv_anim_t flash2_out;
                lv_anim_init(&flash2_out);
                lv_anim_set_var(&flash2_out, a3->var);
                lv_anim_set_values(&flash2_out, LV_OPA_COVER, LV_OPA_TRANSP);
                lv_anim_set_time(&flash2_out, 40);
                lv_anim_set_exec_cb(&flash2_out, [](void* var, int32_t v) {
                    lv_obj_set_style_bg_opa((lv_obj_t*)var, v, 0);
                });
                lv_anim_set_user_data(&flash2_out, a3->user_data);
                lv_anim_set_ready_cb(&flash2_out, [](lv_anim_t* a4) {
                    // Start spiral after flashes
                    PokedexCard* card = (PokedexCard*)a4->user_data;
                    card->startSpiralAnimation();
                });
                lv_anim_start(&flash2_out);
            });
            lv_anim_set_user_data(&flash2, a2->user_data);
            lv_anim_start(&flash2);
        });
        lv_anim_set_user_data(&flash1_out, a->user_data);
        lv_anim_start(&flash1_out);
    });
    lv_anim_set_user_data(&flash1, this);
    lv_anim_start(&flash1);
}

void PokedexCard::startSpiralAnimation() {
    // Animate spiral grid cells
    lv_anim_t spiralAnim;
    lv_anim_init(&spiralAnim);
    lv_anim_set_var(&spiralAnim, this);
    lv_anim_set_values(&spiralAnim, 0, 110);  // Extend to 110 to give last cells time to finish
    lv_anim_set_time(&spiralAnim, 900);  // Slightly longer animation
    lv_anim_set_exec_cb(&spiralAnim, spiralAnimCallback);
    lv_anim_set_ready_cb(&spiralAnim, spiralCompleteCallback);
    lv_anim_set_user_data(&spiralAnim, this);
    lv_anim_start(&spiralAnim);
}

void PokedexCard::createTransitionOverlay() {
    // Create full-screen overlay container
    _transitionOverlay = lv_obj_create(lv_scr_act());
    lv_obj_set_size(_transitionOverlay, 240, 135);  // Full screen size
    lv_obj_set_pos(_transitionOverlay, 0, 0);
    lv_obj_clear_flag(_transitionOverlay, LV_OBJ_FLAG_SCROLLABLE);
    
    // Initially white for flash
    lv_obj_set_style_bg_color(_transitionOverlay, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(_transitionOverlay, LV_OPA_TRANSP, 0);  // Start transparent
    lv_obj_set_style_border_width(_transitionOverlay, 0, 0);
    lv_obj_set_style_pad_all(_transitionOverlay, 0, 0);
    
    // Create 8x4 grid of cells
    int cellWidth = 30;   // 240 / 8 = 30
    int cellHeight = 34;  // 135 / 4 ≈ 34 (slightly larger to cover)
    
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 8; col++) {
            int index = row * 8 + col;
            _gridCells[index] = lv_obj_create(_transitionOverlay);
            
            // Position and size each cell
            lv_obj_set_size(_gridCells[index], cellWidth, cellHeight);
            lv_obj_set_pos(_gridCells[index], col * cellWidth, row * cellHeight);
            
            // Black cells, initially transparent
            lv_obj_set_style_bg_color(_gridCells[index], lv_color_black(), 0);
            lv_obj_set_style_bg_opa(_gridCells[index], LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(_gridCells[index], 0, 0);
            lv_obj_set_style_pad_all(_gridCells[index], 0, 0);
            lv_obj_set_style_radius(_gridCells[index], 0, 0);  // Square corners
            lv_obj_clear_flag(_gridCells[index], LV_OBJ_FLAG_SCROLLABLE);
        }
    }
    
    // Move to front
    lv_obj_move_foreground(_transitionOverlay);
}

void PokedexCard::spiralAnimCallback(void* var, int32_t value) {
    PokedexCard* card = (PokedexCard*)var;
    
    // Define spiral order: clockwise from outside to inside
    static const int spiralOrder[32] = {
        // Outer ring (perimeter)
        0, 1, 2, 3, 4, 5, 6, 7,      // Top row (left to right)
        15, 23, 31,                   // Right column (down)
        30, 29, 28, 27, 26, 25, 24,   // Bottom row (right to left)
        16, 8,                        // Left column (up)
        // Inner ring
        9, 10, 11, 12, 13, 14,        // Second row (left to right)
        22,                           // Right column inner
        21, 20, 19, 18, 17            // Third row (right to left)
    };
    
    // Calculate how many cells should be visible (value 0-110)
    // Cap at 32 cells maximum
    int cellsToShow = (value * 32) / 100;
    if (cellsToShow > 32) cellsToShow = 32;
    
    // Animate cells in spiral order
    for (int i = 0; i < 32; i++) {
        int cellIndex = spiralOrder[i];
        if (!card->_gridCells[cellIndex]) continue;
        
        if (i < cellsToShow) {
            // This cell should be visible
            // Calculate local fade progress for this cell
            int localProgress = (value * 32 / 100) - i;
            if (localProgress > 3) localProgress = 3;  // Quick fade in over 3 steps
            
            int opa = (LV_OPA_COVER * localProgress) / 3;
            if (opa > LV_OPA_COVER) opa = LV_OPA_COVER;
            
            // Add pulsing effect
            if (i == cellsToShow - 1) {  // Leading edge cell
                int pulse = sin(value * 0.2) * 30;
                opa = opa + pulse;
                if (opa > LV_OPA_COVER) opa = LV_OPA_COVER;
                if (opa < LV_OPA_COVER/2) opa = LV_OPA_COVER/2;
            }
            
            lv_obj_set_style_bg_opa(card->_gridCells[cellIndex], opa, 0);
        } else {
            // This cell should be transparent
            lv_obj_set_style_bg_opa(card->_gridCells[cellIndex], LV_OPA_TRANSP, 0);
        }
    }
}

void PokedexCard::spiralCompleteCallback(lv_anim_t* anim) {
    PokedexCard* card = (PokedexCard*)lv_anim_get_user_data(anim);
    
    // Check if data is ready
    if (card->_state == TRANSITIONING && card->_pending.sprite != nullptr) {
        // Data is ready, but DON'T update display yet - wait for fade to start
        // This keeps the old sprite visible under the black overlay
        card->fadeOutOverlay();
    } else {
        // Data not ready yet, keep screen black with pulsing effect
        Serial.println("[PokedexCard] spiralCompleteCallback: Data not ready, starting pulse animation");
        
        // Create a pulsing animation on the black cells (pulse color, not opacity)
        lv_anim_t pulseAnim;
        lv_anim_init(&pulseAnim);
        lv_anim_set_var(&pulseAnim, card->_transitionOverlay);
        lv_anim_set_values(&pulseAnim, 0, 40);  // Pulse between black and dark gray
        lv_anim_set_time(&pulseAnim, 500);
        lv_anim_set_playback_time(&pulseAnim, 500);
        lv_anim_set_repeat_count(&pulseAnim, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_exec_cb(&pulseAnim, [](void* var, int32_t value) {
            lv_obj_t* overlay = (lv_obj_t*)var;
            // Pulse all grid cells by changing color brightness
            lv_color_t pulseColor = lv_color_make(value, value, value);  // Gray level
            for (int i = 0; i < 32; i++) {
                lv_obj_t* cell = lv_obj_get_child(overlay, i);
                if (cell) {
                    lv_obj_set_style_bg_color(cell, pulseColor, 0);
                    lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);  // Keep fully opaque
                }
            }
        });
        lv_anim_start(&pulseAnim);
        
        // Keep the transition state active
        return;  // Don't continue until data is ready
    }
}

void PokedexCard::fadeOutOverlay() {
    if (!_transitionOverlay) return;
    
    // Update the Pokemon display NOW (while screen is still black)
    // This way the new sprite is ready underneath the black overlay
    updatePokemonDisplay();
    
    // Create fade-out animation for all cells
    lv_anim_t fadeAnim;
    lv_anim_init(&fadeAnim);
    lv_anim_set_var(&fadeAnim, _transitionOverlay);
    lv_anim_set_values(&fadeAnim, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_time(&fadeAnim, 500);
    lv_anim_set_exec_cb(&fadeAnim, [](void* var, int32_t value) {
        lv_obj_t* overlay = (lv_obj_t*)var;
        // Fade all grid cells
        for (int i = 0; i < 32; i++) {
            lv_obj_t* cell = lv_obj_get_child(overlay, i);
            if (cell) {
                lv_obj_set_style_bg_opa(cell, value, 0);
            }
        }
    });
    lv_anim_set_ready_cb(&fadeAnim, [](lv_anim_t* anim) {
        PokedexCard* card = (PokedexCard*)lv_anim_get_user_data(anim);
        card->cleanupTransition();
    });
    lv_anim_set_user_data(&fadeAnim, this);
    lv_anim_start(&fadeAnim);
}

void PokedexCard::cleanupTransition() {
    if (_transitionOverlay) {
        lv_obj_del(_transitionOverlay);
        _transitionOverlay = nullptr;
    }
    _isTransitioning = false;
    
    // Add to cache if we just loaded new data (not from cache)
    if (_state == TRANSITIONING) {
        // Add to cache (create a copy of the sprite for cache)
        lv_img_dsc_t* cacheSprite = nullptr;
        if (_current.sprite) {
            cacheSprite = (lv_img_dsc_t*)heap_caps_malloc(sizeof(lv_img_dsc_t), MALLOC_CAP_SPIRAM);
            if (cacheSprite) {
                *cacheSprite = *_current.sprite;
                
                // Clone the pixel data
                size_t dataSize = _current.sprite->data_size;
                uint8_t* newData = (uint8_t*)heap_caps_malloc(dataSize, MALLOC_CAP_SPIRAM);
                if (newData) {
                    memcpy(newData, _current.sprite->data, dataSize);
                    cacheSprite->data = newData;
                    addToCache(_current.id, _current.name, _current.description, cacheSprite);
                } else {
                    heap_caps_free(cacheSprite);
                }
            }
        }
        
        // Return to idle state
        _state = IDLE;
        
        Serial.printf("[PokedexCard] Transition complete - Now showing %s (#%d)\n", 
                     _current.name.c_str(), _current.id);
    }
}

void PokedexCard::freeSprite(lv_img_dsc_t*& sprite) {
    if (sprite) {
        // Check if this is any static sprite - don't free them!
        for (int i = 0; i < OFFLINE_POKEMON_COUNT; i++) {
            if (sprite == (lv_img_dsc_t*)offlinePokemon[i].sprite) {
                sprite = nullptr;
                return;
            }
        }
        
        // Only free dynamically allocated sprites
        if (sprite->data) {
            Serial.printf("[PokedexCard] Freeing sprite data (%d bytes)\n", sprite->data_size);
            heap_caps_free((void*)sprite->data);
        }
        heap_caps_free(sprite);
        sprite = nullptr;
        
        // Log free PSRAM
        size_t freePSRAM = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        Serial.printf("[PokedexCard] Free PSRAM: %d KB\n", freePSRAM / 1024);
    }
}

PokedexCard::~PokedexCard() {
    cleanupTransition();  // Clean up any ongoing transition
    
    // Free sprites
    freeSprite(_current.sprite);
    freeSprite(_pending.sprite);
    
    // Free pending PNG data if exists
    if (_pending.pngData) {
        delete[] _pending.pngData;
        _pending.pngData = nullptr;
    }
    
    if (_card) {
        lv_obj_del_async(_card);
        _card = nullptr;
    }
    
    // Clear cache
    clearCache();
}

// Cache management methods
bool PokedexCard::checkCache(int id) {
    for (const auto& cached : _cache) {
        if (cached.id == id) {
            // Found in cache! Prepare pending data for transition
            _pending.id = cached.id;
            _pending.name = cached.name;
            _pending.description = cached.description;
            
            // Clone the sprite data (don't share the same pointer)
            if (cached.sprite) {
                _pending.sprite = (lv_img_dsc_t*)heap_caps_malloc(sizeof(lv_img_dsc_t), MALLOC_CAP_SPIRAM);
                if (_pending.sprite) {
                    *_pending.sprite = *cached.sprite;
                    
                    // Clone the pixel data
                    size_t dataSize = cached.sprite->data_size;
                    uint8_t* newData = (uint8_t*)heap_caps_malloc(dataSize, MALLOC_CAP_SPIRAM);
                    if (newData) {
                        memcpy(newData, cached.sprite->data, dataSize);
                        _pending.sprite->data = newData;
                    } else {
                        heap_caps_free(_pending.sprite);
                        _pending.sprite = nullptr;
                    }
                }
            }
            
            Serial.printf("[PokedexCard] Found Pokemon #%d in cache: %s\n", id, cached.name.c_str());
            _state = TRANSITIONING;  // Mark as transitioning for cached data
            return true;
        }
    }
    return false;
}

void PokedexCard::addToCache(int id, const String& name, const String& description, lv_img_dsc_t* sprite) {
    // Check if already in cache
    for (auto& cached : _cache) {
        if (cached.id == id) {
            // Update existing entry
            cached.name = name;
            cached.description = description;
            if (cached.sprite) {
                freeSprite(cached.sprite);
            }
            cached.sprite = sprite;
            Serial.printf("[PokedexCard] Updated Pokemon #%d in cache\n", id);
            return;
        }
    }
    
    // If cache is full, remove oldest entry
    if (_cache.size() >= MAX_CACHE_SIZE) {
        if (_cache[0].sprite) {
            freeSprite(_cache[0].sprite);
        }
        _cache.erase(_cache.begin());
        Serial.println("[PokedexCard] Cache full, removed oldest entry");
    }
    
    // Add new entry
    CachedPokemon newEntry;
    newEntry.id = id;
    newEntry.name = name;
    newEntry.description = description;
    newEntry.sprite = sprite;
    
    _cache.push_back(newEntry);
    Serial.printf("[PokedexCard] Added Pokemon #%d to cache (%d/%d)\n", id, (int)_cache.size(), MAX_CACHE_SIZE);
}

void PokedexCard::clearCache() {
    for (auto& cached : _cache) {
        if (cached.sprite) {
            freeSprite(cached.sprite);
        }
    }
    _cache.clear();
    Serial.println("[PokedexCard] Cache cleared");
}

void PokedexCard::startTextScrolling() {
    if (!_descContainer || !_descLabel) return;
    
    // Stop any existing animation on the label
    lv_anim_del(_descLabel, nullptr);
    
    // Reset position to top
    lv_obj_set_y(_descLabel, 0);
    
    // Update layout to get accurate dimensions
    lv_obj_update_layout(_descContainer);
    lv_obj_update_layout(_descLabel);
    
    lv_coord_t label_h = lv_obj_get_height(_descLabel);
    lv_coord_t cont_h = lv_obj_get_height(_descContainer);
    
    // Calculate how much we need to scroll (add extra pixels to ensure last line is fully visible)
    lv_coord_t distance = label_h - cont_h + 10;  // Add 10 pixels to ensure bottom text is fully visible
    if (distance <= 0) return;  // No scrolling needed
    
    // Calculate duration based on constant speed (20 pixels per second - slower)
    constexpr uint32_t kPixelsPerSecond = 20;
    uint32_t duration_ms = (distance * 1000) / kPixelsPerSecond;
    
    // Create scrolling animation with proper repeat delay
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, _descLabel);
    lv_anim_set_exec_cb(&anim, scroll_y_anim_cb);
    lv_anim_set_values(&anim, 0, distance);
    lv_anim_set_time(&anim, duration_ms);
    lv_anim_set_playback_time(&anim, duration_ms);
    lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_delay(&anim, 1000);  // Wait 1 second before starting first scroll
    lv_anim_set_playback_delay(&anim, 1000);  // Wait 1 second at bottom before scrolling back
    lv_anim_set_repeat_delay(&anim, 1000);  // Wait 1 second at top before repeating
    lv_anim_start(&anim);
}