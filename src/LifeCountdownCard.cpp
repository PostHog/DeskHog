#include "LifeCountdownCard.h"
#include <Arduino.h>
#include <ctime>
#include <cmath>

// Define the static holiday array
const LifeCountdownCard::Holiday LifeCountdownCard::US_HOLIDAYS[] = {
    {1, 1, "New Year's Day"},
    {1, 20, "Martin Luther King Jr. Day"},
    {2, 17, "Presidents Day"},
    {5, 26, "Memorial Day"},
    {6, 19, "Juneteenth"},
    {7, 4, "Independence Day"},
    {9, 1, "Labor Day"},
    {10, 13, "Columbus Day"},
    {11, 11, "Veterans Day"},
    {11, 27, "Thanksgiving"},
    {12, 25, "Christmas"}
};

const int LifeCountdownCard::HOLIDAY_COUNT = sizeof(US_HOLIDAYS) / sizeof(Holiday);

LifeCountdownCard::LifeCountdownCard() 
    : main_container(nullptr),
      display_label(nullptr),
      currentMode(MODE_END_OF_WORKDAY),
      birthdayMonth(6),
      birthdayDay(15),
      navaStartYear(2024),
      navaStartMonth(1),
      navaStartDay(15),
      gender(0),
      customLifeExpectancy(80),
      workStartHour(9),
      workEndHour(17) {
}

LifeCountdownCard::~LifeCountdownCard() {
    cleanup();
}

void LifeCountdownCard::setup(lv_obj_t* parent_screen) {
    // Create main container
    main_container = lv_obj_create(parent_screen);
    lv_obj_set_size(main_container, LV_PCT(100), LV_PCT(100));
    lv_obj_clear_flag(main_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(main_container, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(main_container, 0, 0);
    lv_obj_set_style_pad_all(main_container, 0, 0);
    
    // Create display label for countdown
    display_label = lv_label_create(main_container);
    lv_obj_set_style_text_color(display_label, lv_color_hex(0xFFFFFF), 0);
    //lv_obj_set_style_text_font(display_label, &lv_font_montserrat_24, 0);
    lv_obj_center(display_label);
    lv_label_set_text(display_label, "Loading...");
    
    // Load default settings
    loadPreferences();
    
    // Initial draw
    drawModeDisplay();
}

void LifeCountdownCard::loop() {
    // Update display regularly
    drawModeDisplay();
}

void LifeCountdownCard::cleanup() {
    if (main_container != nullptr) {
        lv_obj_del(main_container);
        main_container = nullptr;
        display_label = nullptr;
    }
}

lv_obj_t* LifeCountdownCard::get_main_container() {
    return main_container;
}

void LifeCountdownCard::loadPreferences() {
    // For now, just use hardcoded defaults
    // TODO: Add persistent storage later
    currentMode = MODE_END_OF_WORKDAY;
    
    // Default user settings (you can customize these)
    birthdayMonth = 6;        // June
    birthdayDay = 15;         // 15th
    navaStartYear = 2024;
    navaStartMonth = 1;
    navaStartDay = 15;
    gender = 0;
    customLifeExpectancy = 80;
    workStartHour = 9;
    workEndHour = 17;
}

void LifeCountdownCard::cycleMode() {
    currentMode = (CountdownMode)((currentMode + 1) % MODE_LAST_DAY);
    drawModeDisplay();
}

void LifeCountdownCard::drawModeDisplay() {
    if (display_label == nullptr) return;
    
    char buffer[128];
    const char* modeLabel = getModeLabel();
    const char* unitLabel = getUnitLabel();
    
    switch (currentMode) {
        case MODE_END_OF_WORKDAY: {
            float hours = calculateHoursUntilEndOfDay();
            snprintf(buffer, sizeof(buffer), "%s\n%.1f %s", modeLabel, hours, unitLabel);
            break;
        }
        case MODE_END_OF_WEEK: {
            float days = calculateDaysUntilEndOfWeek();
            snprintf(buffer, sizeof(buffer), "%s\n%.1f %s", modeLabel, days, unitLabel);
            break;
        }
        case MODE_NEXT_HOLIDAY: {
            const char* holidayName = "";
            int days = calculateDaysUntilNextHoliday(&holidayName);
            snprintf(buffer, sizeof(buffer), "%s\n%d %s\n(%s)", modeLabel, days, unitLabel, holidayName);
            break;
        }
        case MODE_NEXT_BIRTHDAY: {
            int days = calculateDaysUntilBirthday();
            snprintf(buffer, sizeof(buffer), "%s\n%d %s", modeLabel, days, unitLabel);
            break;
        }
        case MODE_DAYS_AT_NAVA: {
            int days = calculateDaysSinceNavaStart();
            snprintf(buffer, sizeof(buffer), "%s\n%d %s", modeLabel, days, unitLabel);
            break;
        }
        case MODE_END_OF_YEAR: {
            int days = calculateDaysUntilEndOfYear();
            snprintf(buffer, sizeof(buffer), "%s\n%d %s", modeLabel, days, unitLabel);
            break;
        }
        default:
            snprintf(buffer, sizeof(buffer), "Unknown Mode");
    }
    
    lv_label_set_text(display_label, buffer);
    lv_obj_center(display_label);
}

const char* LifeCountdownCard::getModeLabel() {
    switch (currentMode) {
        case MODE_END_OF_WORKDAY: return "Until End of Workday";
        case MODE_END_OF_WEEK: return "Until Weekend";
        case MODE_NEXT_HOLIDAY: return "Until Next Holiday";
        case MODE_NEXT_BIRTHDAY: return "Until Birthday";
        case MODE_DAYS_AT_NAVA: return "Days at Nava";
        case MODE_END_OF_YEAR: return "Until End of Year";
        default: return "Unknown";
    }
}

const char* LifeCountdownCard::getUnitLabel() {
    switch (currentMode) {
        case MODE_END_OF_WORKDAY: return "hours";
        case MODE_END_OF_WEEK: return "days";
        case MODE_NEXT_HOLIDAY: return "days";
        case MODE_NEXT_BIRTHDAY: return "days";
        case MODE_DAYS_AT_NAVA: return "days";
        case MODE_END_OF_YEAR: return "days";
        default: return "";
    }
}

// Calculation methods
struct tm LifeCountdownCard::getCurrentTime() {
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    return timeinfo;
}

float LifeCountdownCard::calculateHoursUntilEndOfDay() {
    struct tm now = getCurrentTime();
    int currentHour = now.tm_hour;
    int currentMin = now.tm_min;
    
    // If past work end time, show 0
    if (currentHour >= workEndHour) {
        return 0.0f;
    }
    
    float hoursLeft = (workEndHour - currentHour) - (currentMin / 60.0f);
    return hoursLeft > 0 ? hoursLeft : 0.0f;
}

float LifeCountdownCard::calculateDaysUntilEndOfWeek() {
    struct tm now = getCurrentTime();
    int currentDay = now.tm_wday; // 0=Sunday, 6=Saturday
    int currentHour = now.tm_hour;
    
    // Days until Friday 5pm
    int daysUntilFriday = (5 - currentDay + 7) % 7;
    if (daysUntilFriday == 0 && currentHour >= 17) {
        daysUntilFriday = 7; // Next Friday
    }
    
    float hoursIntoDay = currentHour + (now.tm_min / 60.0f);
    float daysLeft = daysUntilFriday + ((17.0f - hoursIntoDay) / 24.0f);
    
    return daysLeft > 0 ? daysLeft : 0.0f;
}

int LifeCountdownCard::calculateDaysUntilNextHoliday(const char** holidayName) {
    struct tm now = getCurrentTime();
    int currentYear = now.tm_year + 1900;
    int currentMonth = now.tm_mon + 1;
    int currentDay = now.tm_mday;
    
    int minDays = 999999;
    const char* nextHoliday = "None";
    
    for (int i = 0; i < HOLIDAY_COUNT; i++) {
        int holidayMonth = US_HOLIDAYS[i].month;
        int holidayDay = US_HOLIDAYS[i].day;
        
        // Calculate days to this holiday
        struct tm holidayTime = now;
        holidayTime.tm_year = currentYear - 1900;
        holidayTime.tm_mon = holidayMonth - 1;
        holidayTime.tm_mday = holidayDay;
        
        time_t holidayTimestamp = mktime(&holidayTime);
        time_t nowTimestamp = mktime(&now);
        
        int days = (int)difftime(holidayTimestamp, nowTimestamp) / 86400;
        
        // If holiday already passed this year, check next year
        if (days < 0) {
            holidayTime.tm_year = currentYear - 1900 + 1;
            holidayTimestamp = mktime(&holidayTime);
            days = (int)difftime(holidayTimestamp, nowTimestamp) / 86400;
        }
        
        if (days >= 0 && days < minDays) {
            minDays = days;
            nextHoliday = US_HOLIDAYS[i].name;
        }
    }
    
    *holidayName = nextHoliday;
    return minDays;
}

int LifeCountdownCard::calculateDaysUntilBirthday() {
    struct tm now = getCurrentTime();
    int currentYear = now.tm_year + 1900;
    
    struct tm birthday = now;
    birthday.tm_year = currentYear - 1900;
    birthday.tm_mon = birthdayMonth - 1;
    birthday.tm_mday = birthdayDay;
    
    time_t birthdayTimestamp = mktime(&birthday);
    time_t nowTimestamp = mktime(&now);
    
    int days = (int)difftime(birthdayTimestamp, nowTimestamp) / 86400;
    
    // If birthday already passed this year, calculate for next year
    if (days < 0) {
        birthday.tm_year = currentYear - 1900 + 1;
        birthdayTimestamp = mktime(&birthday);
        days = (int)difftime(birthdayTimestamp, nowTimestamp) / 86400;
    }
    
    return days;
}

int LifeCountdownCard::calculateDaysSinceNavaStart() {
    struct tm now = getCurrentTime();
    
    struct tm navaStart = {};
    navaStart.tm_year = navaStartYear - 1900;
    navaStart.tm_mon = navaStartMonth - 1;
    navaStart.tm_mday = navaStartDay;
    
    time_t navaTimestamp = mktime(&navaStart);
    time_t nowTimestamp = mktime(&now);
    
    int days = (int)difftime(nowTimestamp, navaTimestamp) / 86400;
    
    return days > 0 ? days : 0;
}

int LifeCountdownCard::calculateDaysUntilEndOfYear() {
    struct tm now = getCurrentTime();
    int currentYear = now.tm_year + 1900;
    
    struct tm endOfYear = now;
    endOfYear.tm_year = currentYear - 1900;
    endOfYear.tm_mon = 11; // December
    endOfYear.tm_mday = 31;
    endOfYear.tm_hour = 23;
    endOfYear.tm_min = 59;
    endOfYear.tm_sec = 59;
    
    time_t endTimestamp = mktime(&endOfYear);
    time_t nowTimestamp = mktime(&now);
    
    int days = (int)difftime(endTimestamp, nowTimestamp) / 86400;
    
    return days > 0 ? days : 0;
}

bool LifeCountdownCard::isLeapYear(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

int LifeCountdownCard::getDaysInMonth(int month, int year) {
    const int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && isLeapYear(year)) {
        return 29;
    }
    return daysInMonth[month - 1];
}

long LifeCountdownCard::calculateDaysUntilLastDay() {
    // This would calculate based on life expectancy
    // Placeholder for now
    return 30000; // ~82 years
}