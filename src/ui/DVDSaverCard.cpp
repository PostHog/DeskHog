#include "ui/DVDSaverCard.h"
#include <Arduino.h> // For random()
#include "misc/lv_timer.h" // For lv_timer_get_user_data, if not pulled in by lvgl.h fully

// Define animation properties
static constexpr int16_t INITIAL_VELOCITY_MAX = 2; // Max initial speed (pixels per tick)
static constexpr uint32_t ANIMATION_TIMER_PERIOD_MS = 33; // roughly 30 FPS
static constexpr uint16_t LOGO_ZOOM_PERCENT_50 = 128; // LVGL zoom: 256 is 100%, so 128 is 50%
// Estimate: 10% transparent padding on each side of the visual content
static constexpr float VISUAL_PADDING_RATIO = 0.1f;

DVDSaverCard::DVDSaverCard(lv_obj_t* parent, uint16_t screenWidth, uint16_t screenHeight)
    : _card(nullptr),
      _logo_img(nullptr),
      _pos_x(0),
      _pos_y(0),
      _vel_x(0),
      _vel_y(0),
      _screenWidth(screenWidth),
      _screenHeight(screenHeight),
      _img_width(0),
      _img_height(0),
      _animation_timer(nullptr),
      _current_color_index(0) { // Initialize color index

    _card = lv_obj_create(parent);
    if (!_card) {
        // LV_LOG_ERROR("Failed to create DVDSaverCard card object");
        return;
    }
    lv_obj_set_size(_card, _screenWidth, _screenHeight);
    lv_obj_set_style_bg_color(_card, lv_color_black(), 0);
    lv_obj_set_style_border_width(_card, 0, 0);
    lv_obj_set_style_pad_all(_card, 0, 0);
    lv_obj_clear_flag(_card, LV_OBJ_FLAG_SCROLLABLE);

    // Create the logo image
    _logo_img = lv_img_create(_card);
    if (!_logo_img) {
        // LV_LOG_ERROR("Failed to create logo image for DVDSaverCard");
        return;
    }
    lv_img_set_src(_logo_img, &sprite_posthog_logo_white);
    lv_img_set_zoom(_logo_img, LOGO_ZOOM_PERCENT_50); // Make logo 50% smaller

    // Get image dimensions *after* zoom has been applied
    _img_width = lv_obj_get_width(_logo_img);
    _img_height = lv_obj_get_height(_logo_img);

    // Center the image data; visual centering is implicitly handled if padding is symmetrical
    _pos_x = (_screenWidth - _img_width) / 2;
    _pos_y = (_screenHeight - _img_height) / 2;

    // Random initial velocity
    _vel_x = random(1, INITIAL_VELOCITY_MAX + 1) * (random(0, 2) == 0 ? 1 : -1); // 1 to N, random direction
    _vel_y = random(1, INITIAL_VELOCITY_MAX + 1) * (random(0, 2) == 0 ? 1 : -1);
    if (_vel_x == 0) _vel_x = 1; // Ensure it moves
    if (_vel_y == 0) _vel_y = 1;

    lv_obj_set_pos(_logo_img, _pos_x, _pos_y);

    // Initialize colors
    _logo_colors.push_back(lv_palette_main(LV_PALETTE_RED));
    _logo_colors.push_back(lv_palette_main(LV_PALETTE_GREEN));
    _logo_colors.push_back(lv_palette_main(LV_PALETTE_BLUE));
    _logo_colors.push_back(lv_palette_main(LV_PALETTE_YELLOW));
    _logo_colors.push_back(lv_palette_main(LV_PALETTE_CYAN));
    _logo_colors.push_back(lv_palette_main(LV_PALETTE_PURPLE));
    _logo_colors.push_back(lv_color_white()); // Add white as an option too

    change_logo_color(); // Set initial color

    // Create the animation timer
    _animation_timer = lv_timer_create(animation_timer_cb, ANIMATION_TIMER_PERIOD_MS, this);
    if (!_animation_timer) {
        // LV_LOG_ERROR("Failed to create animation timer for DVDSaverCard");
    }
}

DVDSaverCard::~DVDSaverCard() {
    if (_animation_timer) {
        lv_timer_del(_animation_timer);
        _animation_timer = nullptr;
    }
    if (_card) {
        lv_obj_del_async(_card); // Use async delete for LVGL objects
        _card = nullptr;
        _logo_img = nullptr; // Child of _card, will be deleted with it
    }
}

lv_obj_t* DVDSaverCard::getCard() const {
    return _card;
}

void DVDSaverCard::change_logo_color() {
    if (!_logo_img || _logo_colors.empty()) return;
    lv_color_t new_color = _logo_colors[_current_color_index];
    lv_obj_set_style_img_recolor(_logo_img, new_color, 0);
    lv_obj_set_style_img_recolor_opa(_logo_img, LV_OPA_COVER, 0); 
}

void DVDSaverCard::animation_timer_cb(lv_timer_t* timer) {
    DVDSaverCard* instance = (DVDSaverCard*)lv_timer_get_user_data(timer);
    if (instance) {
        instance->update_animation();
    }
}

void DVDSaverCard::update_animation() {
    if (!_logo_img || !_card) return;

    _pos_x += _vel_x;
    _pos_y += _vel_y;

    // Calculate inset based on padding guess for collision detection
    float inset_x = _img_width * VISUAL_PADDING_RATIO;
    float inset_y = _img_height * VISUAL_PADDING_RATIO;

    // Effective visual boundaries of the logo content
    float visual_left_edge = _pos_x + inset_x;
    float visual_right_edge = _pos_x + _img_width - inset_x;
    float visual_top_edge = _pos_y + inset_y;
    float visual_bottom_edge = _pos_y + _img_height - inset_y;

    bool bounced = false;

    // Left wall collision
    if (visual_left_edge <= 0) {
        _pos_x = -inset_x; // Align visual left with screen left
        _vel_x = -_vel_x;
        bounced = true;
    }
    // Right wall collision 
    else if (visual_right_edge >= _screenWidth) {
        _pos_x = _screenWidth - (_img_width - inset_x); // Align visual right with screen right
        _vel_x = -_vel_x;
        bounced = true;
    }

    // Top wall collision
    if (visual_top_edge <= 0) {
        _pos_y = -inset_y; // Align visual top with screen top
        _vel_y = -_vel_y;
        bounced = true;
    }
    // Bottom wall collision
    else if (visual_bottom_edge >= _screenHeight) {
        _pos_y = _screenHeight - (_img_height - inset_y); // Align visual bottom with screen bottom
        _vel_y = -_vel_y;
        bounced = true;
    }

    if (bounced && !_logo_colors.empty()) {
        _current_color_index = (_current_color_index + 1) % _logo_colors.size();
        change_logo_color();
    }

    lv_obj_set_pos(_logo_img, (lv_coord_t)_pos_x, (lv_coord_t)_pos_y);
} 