#ifndef LIFE_COUNTDOWN_CARD_H
#define LIFE_COUNTDOWN_CARD_H

#include <lvgl.h>
#include <time.h>

enum CountdownMode {
    MODE_END_OF_WORKDAY = 0,
    MODE_END_OF_WEEK,
    MODE_NEXT_HOLIDAY,
    MODE_NEXT_BIRTHDAY,
    MODE_DAYS_AT_NAVA,
    MODE_END_OF_YEAR,
    MODE_LAST_DAY
};

class LifeCountdownCard {
public:
    LifeCountdownCard();
    ~LifeCountdownCard();
    
    void setup(lv_obj_t* parent_screen);
    void loop();
    void cleanup();
    lv_obj_t* get_main_container();
    void cycleMode();
    
private:
    lv_obj_t* main_container;
    lv_obj_t* display_label;
    
    // Remove: Preferences prefs;
    CountdownMode currentMode;
    
    // User configuration (hardcoded for now)
    int birthdayMonth;
    int birthdayDay;
    int navaStartYear;
    int navaStartMonth;
    int navaStartDay;
    int gender;
    int customLifeExpectancy;
    int workStartHour;
    int workEndHour;
    
    struct Holiday {
        int month;
        int day;
        const char* name;
    };
    
    static const Holiday US_HOLIDAYS[];
    static const int HOLIDAY_COUNT;
    
    // Calculation methods
    float calculateHoursUntilEndOfDay();
    float calculateDaysUntilEndOfWeek();
    int calculateDaysUntilNextHoliday(const char** holidayName);
    int calculateDaysUntilBirthday();
    int calculateDaysSinceNavaStart();
    int calculateDaysUntilEndOfYear();
    long calculateDaysUntilLastDay();
    
    // Helper methods
    void loadPreferences();  // Will just set defaults
    void cycleToNextMode();
    void drawModeDisplay();
    const char* getModeLabel();
    const char* getUnitLabel();
    
    // Time helpers
    struct tm getCurrentTime();
    int getDaysInMonth(int month, int year);
    bool isLeapYear(int year);
};

#endif // LIFE_COUNTDOWN_CARD_H