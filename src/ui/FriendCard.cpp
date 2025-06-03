#include "ui/FriendCard.h"
#include "Style.h"
#include "sprites.h"
#include "hardware/Input.h"

AnimationCard::AnimationCard(lv_obj_t* parent)
    : _card(nullptr)
    , _background(nullptr)
    , _anim_img(nullptr)
    , _label(nullptr)
    , _label_shadow(nullptr)
    , _animation_running(false)
    , _current_message_index(0) {
    
    // Create main card with black background
    _card = lv_obj_create(parent);
    if (!_card) return;
    
    // Set card size and style - black background
    lv_obj_set_width(_card, lv_pct(100));
    lv_obj_set_height(_card, lv_pct(100));
    lv_obj_set_style_bg_color(_card, lv_color_black(), 0);
    lv_obj_set_style_border_width(_card, 0, 0);
    lv_obj_set_style_pad_all(_card, 5, 0);
    lv_obj_set_style_margin_all(_card, 0, 0);
    
    // Create green container with rounded corners
    _background = lv_obj_create(_card);
    if (!_background) return;
    
    // Make green container fill parent completely
    lv_obj_set_style_radius(_background, 8, LV_PART_MAIN);
    lv_obj_set_style_bg_color(_background, lv_color_hex(0x8B0000), 0);
    lv_obj_set_style_border_width(_background, 0, 0);
    lv_obj_set_style_pad_all(_background, 5, 0);
    
    lv_obj_set_width(_background, lv_pct(100));
    lv_obj_set_height(_background, lv_pct(100));
    
    // Create animation image object inside green container
    _anim_img = lv_animimg_create(_background);
    if (!_anim_img) return;
    
    // Set the sprite array
    lv_animimg_set_src(_anim_img, (const void**)walking_sprites, walking_sprites_count);
    
    // Set animation duration and make it repeat infinitely
    lv_animimg_set_duration(_anim_img, ANIMATION_DURATION_MS);
    lv_animimg_set_repeat_count(_anim_img, LV_ANIM_REPEAT_INFINITE);
    
    // Double the size using zoom (must be called AFTER setting the source)
    lv_img_set_zoom(_anim_img, 512); // 256 = 100%, 512 = 200%
    
    // Position at left-center
    lv_obj_align(_anim_img, LV_ALIGN_LEFT_MID, -10, 0);
    
    // Create shadow label (black, 1px offset)
    _label_shadow = createStyledLabel(_background, lv_color_black(), 0, 1);
    
    // Create main label (white, no shadow offset)
    _label = createStyledLabel(_background, lv_color_white(), -1, 0);
    
    // Add default message
    addMessage("YOUR CHOICES ARE ADEQUATE");
    addMessage("I APPROVE OF YOU AS A PERSON");
    addMessage("YOU MEET MY EXPECTATIONS");
    addMessage("YOUR PRODUCT IS GOOD");
    addMessage("I ACCEPT YOUR LIMITATIONS");
    addMessage("YOUR DREAM IS ATTAINABLE");
    addMessage("YOU WILL DO THINGS");
    addMessage("YOU SHOULD NOT BE AFRAID");

    // Display the first message
    if (!_messages.empty()) {
        setText(_messages[0].c_str());
    }
    
    // Start animation automatically
    startAnimation();
}

lv_obj_t* AnimationCard::createStyledLabel(lv_obj_t* parent, lv_color_t color, int16_t x_offset, int16_t y_offset) {
    lv_obj_t* label = lv_label_create(parent);
    if (!label) return nullptr;
    
    // Set font and text alignment
    lv_obj_set_style_text_font(label, Style::loudNoisesFont(), 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    
    // Allow label to wrap to multiple lines
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    
    // Set the label's size and position
    lv_obj_set_width(label, lv_pct(70));
    lv_obj_align(label, LV_ALIGN_RIGHT_MID, x_offset, y_offset);
    
    return label;
}

AnimationCard::~AnimationCard() {
    // Clean up UI elements safely
    if (isValidObject(_card)) {
        lv_obj_add_flag(_card, LV_OBJ_FLAG_HIDDEN);
        lv_obj_del_async(_card);
        
        // When card is deleted, all children are automatically deleted
        _card = nullptr;
        _background = nullptr;
        _anim_img = nullptr;
        _label = nullptr;
        _label_shadow = nullptr;
    }
}

lv_obj_t* AnimationCard::getCardObject() const {
    return _card;
}

void AnimationCard::startAnimation() {
    // Don't start if animation is already running
    if (_animation_running) return;
    
    // Start animation using LVGL's built-in animation image functionality
    if (isValidObject(_anim_img)) {
        lv_animimg_start(_anim_img);
        _animation_running = true;
    }
}

void AnimationCard::stopAnimation() {
    // Do nothing if animation is not running
    if (!_animation_running) return;
    
    // We could pause the animation here if lv_animimg had a pause function
    // For now, just mark it as not running
    _animation_running = false;
}

void AnimationCard::setText(const char* text) {
    // Update both labels with the same text
    if (isValidObject(_label)) {
        lv_label_set_text(_label, text);
    }
    
    if (isValidObject(_label_shadow)) {
        lv_label_set_text(_label_shadow, text);
    }
}

void AnimationCard::addMessage(const char* message) {
    // Add message to the cycling list
    _messages.push_back(std::string(message));
}

void AnimationCard::cycleNextMessage() {
    // If we have no messages, do nothing
    if (_messages.empty()) return;
    
    // Cycle to next message
    _current_message_index = (_current_message_index + 1) % _messages.size();
    
    // Display the message
    setText(_messages[_current_message_index].c_str());
}

bool AnimationCard::handleButtonPress(uint8_t button_index) {
    // Handle center button (Button 1) press to cycle messages
    if (button_index == Input::BUTTON_CENTER) {
        cycleNextMessage();
        return true;  // We handled this button press
    }
    
    // We didn't handle the button press
    return false;
}

// Add empty implementation for update() from InputHandler
void AnimationCard::update() {
    // AnimationCard does not require continuous updates beyond LVGL animations
}

bool AnimationCard::isValidObject(lv_obj_t* obj) const {
    return obj && lv_obj_is_valid(obj);
}