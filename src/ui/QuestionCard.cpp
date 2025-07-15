#include "ui/QuestionCard.h"
#include "Style.h"

/*--------------------------------------------------------------
 *  Helper – animation callback that moves a scrollable object
 *------------------------------------------------------------*/
static void scroll_y_anim_cb(void * obj, int32_t v)
{
    lv_obj_scroll_to_y(static_cast<lv_obj_t *>(obj), v, LV_ANIM_OFF);
}

/*--------------------------------------------------------------
 *  Constructor
 *------------------------------------------------------------*/
QuestionCard::QuestionCard(lv_obj_t* parent)
    : _card(nullptr)
    , _background(nullptr)
    , _label(nullptr)
    , _label_shadow(nullptr)
    , _current_question_index(0)
    , _questions()
    , _cont(nullptr)
    , _shadow_cont(nullptr)
{
    /* ---------- main card ----------- */
    _card = lv_obj_create(parent);
    if (!_card) return;

    lv_obj_set_width (_card, lv_pct(100));
    lv_obj_set_height(_card, lv_pct(100));
    lv_obj_set_style_bg_color  (_card, lv_color_black(), 0);
    lv_obj_set_style_border_width(_card, 0, 0);
    lv_obj_set_style_pad_all     (_card, 5, 0);
    lv_obj_set_style_margin_all  (_card, 0, 0);

    /* ---------- background panel ---- */
    _background = lv_obj_create(_card);
    if (!_background) return;

    lv_obj_set_style_radius      (_background, 8, LV_PART_MAIN);
    lv_obj_set_style_bg_color    (_background, lv_color_hex(0x4A4A4A), 0);
    lv_obj_set_style_border_width(_background, 0, 0);
    lv_obj_set_style_pad_all     (_background, 5, 0);

    lv_obj_set_width (_background, lv_pct(100));
    lv_obj_set_height(_background, lv_pct(100));

    /* ---------- 2 labels (shadow + main) ---------- */
    _label_shadow = createStyledLabel(_background, lv_color_black(),  0,  1);
    _label        = createStyledLabel(_background, lv_color_white(), -1,  0);

    /* ---------- sample questions ------------------ */
    addQuestion("If you had to describe your last weekend using only 3 emojis, what would they be?");
    addQuestion("If your name were the acronym for your next job title (e.g., ANNIKA = Astronaut Navigating New Interstellar Knowledge Adventures), what would it stand for?");
    addQuestion("What\'s the last photo you took on your phone that you'd be willing to share and explain?");
    addQuestion("What song title best describes your current mood or week?");
    addQuestion("What\'s a random topic you could give a 10-minute presentation on with no prep?");
    addQuestion("What\'s a trend you secretly (or not so secretly) loved?");
    addQuestion("Pineapple on pizza: yes or no?");

    if (!_questions.empty())
        setText(_questions[0].c_str());
}

/*--------------------------------------------------------------
 *  Label creator – wraps text & adds symmetric padding
 *------------------------------------------------------------*/
lv_obj_t* QuestionCard::createStyledLabel(lv_obj_t* parent,
                                          lv_color_t  color,
                                          int16_t     x_offset,
                                          int16_t     y_offset)
{
    /* scrollable container */
    lv_obj_t* cont = lv_obj_create(parent);
    if (x_offset == -1)  _cont        = cont;
    else                 _shadow_cont = cont;

    lv_obj_set_size(cont, lv_pct(95), lv_pct(100));
    lv_obj_set_style_bg_opa   (cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_scroll_dir     (cont, LV_DIR_VER);
    lv_obj_set_scrollbar_mode (cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_align              (cont, LV_ALIGN_CENTER, x_offset, y_offset);

    /* symmetric 4 px vertical padding */
    lv_obj_set_style_pad_top   (cont, 4, 0);
    lv_obj_set_style_pad_bottom(cont, 4, 0);

    /* the label itself */
    lv_obj_t* label = lv_label_create(cont);
    if (!label) return nullptr;

    lv_obj_set_style_text_font (label, Style::loudNoisesFont(), 0);
    lv_obj_set_style_text_color(label, color, 0);

    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);   // word‑wrap
    lv_obj_set_width        (label, lv_pct(100));        // keep inside cont

    return label;
}

/*--------------------------------------------------------------*/
QuestionCard::~QuestionCard()
{
    if (isValidObject(_card)) {
        lv_obj_add_flag(_card, LV_OBJ_FLAG_HIDDEN);
        lv_obj_del_async(_card);
    }
}

/*--------------------------------------------------------------*/
lv_obj_t* QuestionCard::getCard()            { return _card; }

void QuestionCard::addQuestion(const char* q){ _questions.push_back(q); }

/*--------------------------------------------------------------*/
void QuestionCard::setText(const char* txt)
{
    if (_label)        lv_label_set_text(_label, txt);
    if (_label_shadow) lv_label_set_text(_label_shadow, txt);
    startScrolling();
}

/*--------------------------------------------------------------*/
void QuestionCard::cycleNextQuestion()
{
    if (_questions.empty()) return;

    _current_question_index =
        (_current_question_index + 1) % _questions.size();
    setText(_questions[_current_question_index].c_str());

    /* rotate background colour */
    static const uint32_t colors[] = {
        0x4A4A4A, 0x2C3E50, 0x27AE60, 0x8E44AD,
        0xE67E22, 0x2980B9, 0xC0392B
    };
    lv_obj_set_style_bg_color(
        _background,
        lv_color_hex(colors[_current_question_index %
                           (sizeof(colors) / sizeof(colors[0]))]),
        0);
}

/*--------------------------------------------------------------*/
bool QuestionCard::handleButtonPress(uint8_t button)
{
    if (button == 1) { cycleNextQuestion(); return true; }
    return false;
}

/*--------------------------------------------------------------
 *  Scroll animation – moves the *containers* up & down
 *  at a constant pixel‑per‑second speed
 *------------------------------------------------------------*/
void QuestionCard::startScrolling()
{
    if (!_cont || !_label) return;

    /* ensure layout is up‑to‑date */
    lv_obj_update_layout(_cont);

    lv_coord_t label_h = lv_obj_get_height(_label);
    lv_coord_t cont_h  = lv_obj_get_height(_cont);

    /* include vertical padding */
    lv_coord_t pad_top    = lv_obj_get_style_pad_top   (_cont, 0);
    lv_coord_t pad_bottom = lv_obj_get_style_pad_bottom(_cont, 0);

    lv_coord_t distance = label_h + pad_top + pad_bottom - cont_h;
    if (distance <= 0) return;           // no scrolling needed

    /* --- constant speed calculation --------------------------- */
    constexpr uint32_t kPixelsPerSecond = 40;           // adjust to taste
    uint32_t duration_ms = static_cast<uint32_t>(
        (distance * 1000) / kPixelsPerSecond);

    /* reset to top */
    lv_obj_scroll_to_y(_cont, 0, LV_ANIM_OFF);
    if (_shadow_cont)
        lv_obj_scroll_to_y(_shadow_cont, 0, LV_ANIM_OFF);

    /* template animation */
    lv_anim_t base;
    lv_anim_init(&base);
    lv_anim_set_exec_cb   (&base, scroll_y_anim_cb);
    lv_anim_set_values    (&base, 0, distance);
    lv_anim_set_time      (&base, duration_ms);        // down
    lv_anim_set_playback_time(&base, duration_ms);     // up
    lv_anim_set_repeat_count(&base, LV_ANIM_REPEAT_INFINITE);
    /* optional pauses: lv_anim_set_delay(&base, 1000);
                        lv_anim_set_playback_delay(&base, 1000); */

    /* start for main text */
    lv_anim_t a = base;
    lv_anim_set_var(&a, _cont);
    lv_anim_start(&a);

    /* shadow follows */
    if (_shadow_cont) {
        lv_anim_t b = base;
        lv_anim_set_var(&b, _shadow_cont);
        lv_anim_start(&b);
    }
}

/*--------------------------------------------------------------*/
bool QuestionCard::isValidObject(lv_obj_t* obj) const
{
    return obj && lv_obj_is_valid(obj);
}
