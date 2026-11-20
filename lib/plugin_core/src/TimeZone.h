#pragma once

// Local time without a time zone database: POSIX TZ strings such as
// "CET-1CEST,M3.5.0,M10.5.0/3", parsed and applied here. Nothing global is
// touched (no setenv("TZ")), so every call can use its own zone, and the
// answers are the same on the device, in the simulator and in the tests.

#include <stdint.h>
#include <string>

namespace deskhog {

/** A moment as people write it. */
struct CivilTime {
    int year = 1970;
    int month = 1;   ///< 1-12
    int day = 1;     ///< 1-31
    int hour = 0;
    int minute = 0;
    int second = 0;
    int weekday = 0;  ///< 0 = Sunday
    int yearDay = 0;  ///< 0 = 1 January
    bool dst = false;
    int32_t offset = 0;    ///< Seconds east of UTC
    char zone[12] = "UTC"; ///< The abbreviation in use, e.g. "CEST"
};

class TimeZone {
public:
    /** UTC. */
    TimeZone() = default;

    /**
     * Read a POSIX TZ string. Understands a standard and a summer-time name
     * (letters, or anything between < and >), offsets as [+-]hh[:mm[:ss]], and
     * the rules Mm.w.d, Jn and n, each with an optional /time.
     * @return false if the text is not one; the zone is then UTC.
     */
    bool parse(const char* text);

    /** Seconds east of UTC at this moment. */
    int32_t offsetAt(int64_t utc, bool* dst = nullptr) const;
    CivilTime toCivil(int64_t utc) const;

    /**
     * The other way round: the moment a clock in this zone shows this date and
     * time. Fields outside their range carry over (day 32 is the 1st of the
     * next month, hour -1 is 23:00 the day before).
     *
     * Two kinds of time need a decision, both where the clocks change. A time
     * that is skipped (02:30 when 02:00 becomes 03:00) is read as that long
     * after the change: 03:30. A time that happens twice (02:30 when 03:00
     * becomes 02:00 again) is the first of the two, unless `dst` is 0, which
     * asks for the second, the one in standard time. `dst` -1 means "not said".
     */
    int64_t fromCivil(int64_t year, int month, int day, int64_t hour, int64_t minute, int64_t second, int dst = -1) const;

    /** Days since 1 January 1970 for a date, and back. */
    static int64_t daysFromCivil(int64_t year, int month, int day);
    static void civilFromDays(int64_t days, int& year, int& month, int& day);

private:
    struct Rule {
        enum Kind : uint8_t { MonthWeekDay, JulianNoLeap, DayOfYear } kind = MonthWeekDay;
        int month = 0, week = 0, day = 0;  ///< For MonthWeekDay; `day` alone for the other two
        int32_t time = 7200;               ///< Seconds after local midnight
    };

    /** When a rule falls in a year, in seconds since 1970 of local wall-clock time. */
    static int64_t ruleSeconds(const Rule& rule, int year);

    char stdName_[12] = "UTC";
    char dstName_[12] = "";
    int32_t stdOffset_ = 0;  ///< Seconds east of UTC
    int32_t dstOffset_ = 0;
    bool hasDst_ = false;
    Rule start_;
    Rule end_;
};

/**
 * A strftime look-alike: %Y %y %m %d %e %H %I %M %S %p %a %A %b %B %j %Z %z
 * %F %T %R and %%. A '-' after the % drops the padding ("%-d" gives "7").
 * English names. Anything else is copied as it is. The result is cut at
 * `most` bytes.
 */
std::string formatTime(const char* format, const CivilTime& time, size_t most = 256);

}  // namespace deskhog
