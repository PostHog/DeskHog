#include "TimeZone.h"

#include <stdio.h>
#include <string.h>

namespace deskhog {

namespace {

bool isAlpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool isDigit(char c) { return c >= '0' && c <= '9'; }

// "CET", or "<+0330>" for names with digits and signs.
bool readName(const char*& p, char* out, size_t outSize) {
    size_t length = 0;
    if (*p == '<') {
        p++;
        while (*p && *p != '>') {
            if (length + 1 < outSize) out[length++] = *p;
            p++;
        }
        if (*p != '>') return false;
        p++;
    } else {
        while (isAlpha(*p)) {
            if (length + 1 < outSize) out[length++] = *p;
            p++;
        }
    }
    out[length] = '\0';
    return length >= 3;
}

bool readNumber(const char*& p, int& out, int most) {
    if (!isDigit(*p)) return false;
    int value = 0;
    while (isDigit(*p)) {
        value = value * 10 + (*p - '0');
        if (value > most) return false;
        p++;
    }
    out = value;
    return true;
}

// [+-]hh[:mm[:ss]] as seconds. Rule times may run to a week either way.
bool readTime(const char*& p, int32_t& out, int mostHours) {
    int sign = 1;
    if (*p == '+') {
        p++;
    } else if (*p == '-') {
        sign = -1;
        p++;
    }
    int hours = 0, minutes = 0, seconds = 0;
    if (!readNumber(p, hours, mostHours)) return false;
    if (*p == ':') {
        p++;
        if (!readNumber(p, minutes, 59)) return false;
        if (*p == ':') {
            p++;
            if (!readNumber(p, seconds, 59)) return false;
        }
    }
    out = sign * (hours * 3600 + minutes * 60 + seconds);
    return true;
}

bool isLeap(int year) { return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0); }

const char* const kDayNames[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
const char* const kMonthNames[] = {"January", "February", "March",     "April",   "May",      "June",
                                   "July",    "August",   "September", "October", "November", "December"};

}  // namespace

int64_t TimeZone::daysFromCivil(int64_t year, int month, int day) {
    year -= month <= 2;
    const int64_t era = (year >= 0 ? year : year - 399) / 400;
    const int64_t yearOfEra = year - era * 400;
    const int64_t dayOfYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const int64_t dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
    return era * 146097 + dayOfEra - 719468;
}

void TimeZone::civilFromDays(int64_t days, int& year, int& month, int& day) {
    days += 719468;
    const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const int64_t dayOfEra = days - era * 146097;
    const int64_t yearOfEra = (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
    const int64_t dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
    const int64_t shifted = (5 * dayOfYear + 2) / 153;
    day = (int)(dayOfYear - (153 * shifted + 2) / 5 + 1);
    month = (int)(shifted < 10 ? shifted + 3 : shifted - 9);
    year = (int)(yearOfEra + era * 400 + (month <= 2 ? 1 : 0));
}

bool TimeZone::parse(const char* text) {
    *this = TimeZone();
    if (!text) return false;
    TimeZone zone;
    const char* p = text;
    while (*p == ' ') p++;
    if (*p == ':') p++;  // some systems write ":name"; only the POSIX form after it is understood

    int32_t value = 0;
    if (!readName(p, zone.stdName_, sizeof(zone.stdName_))) return false;
    if (!readTime(p, value, 24)) return false;
    zone.stdOffset_ = -value;  // POSIX counts hours *behind* UTC

    if (*p && *p != ',') {
        if (!readName(p, zone.dstName_, sizeof(zone.dstName_))) return false;
        zone.hasDst_ = true;
        zone.dstOffset_ = zone.stdOffset_ + 3600;
        if (*p && *p != ',') {
            if (!readTime(p, value, 24)) return false;
            zone.dstOffset_ = -value;
        }
        // Without rules: the ones the United States uses, as most systems assume.
        zone.start_ = Rule{Rule::MonthWeekDay, 3, 2, 0, 7200};
        zone.end_ = Rule{Rule::MonthWeekDay, 11, 1, 0, 7200};
        if (*p == ',') {
            Rule* const rules[] = {&zone.start_, &zone.end_};
            for (int i = 0; i < 2; i++) {
                Rule& rule = *rules[i];
                if (*p != ',') return false;
                p++;
                rule.time = 7200;
                if (*p == 'M') {
                    p++;
                    rule.kind = Rule::MonthWeekDay;
                    if (!readNumber(p, rule.month, 12) || rule.month < 1 || *p++ != '.') return false;
                    if (!readNumber(p, rule.week, 5) || rule.week < 1 || *p++ != '.') return false;
                    if (!readNumber(p, rule.day, 6)) return false;
                } else if (*p == 'J') {
                    p++;
                    rule.kind = Rule::JulianNoLeap;
                    if (!readNumber(p, rule.day, 365) || rule.day < 1) return false;
                } else {
                    rule.kind = Rule::DayOfYear;
                    if (!readNumber(p, rule.day, 365)) return false;
                }
                if (*p == '/') {
                    p++;
                    if (!readTime(p, rule.time, 167)) return false;
                }
            }
        }
    }
    while (*p == ' ') p++;
    if (*p) return false;
    *this = zone;
    return true;
}

int64_t TimeZone::ruleSeconds(const Rule& rule, int year) {
    int64_t days = 0;
    if (rule.kind == Rule::MonthWeekDay) {
        const int64_t first = daysFromCivil(year, rule.month, 1);
        const int firstWeekday = (int)(((first % 7) + 11) % 7);  // 1 January 1970 was a Thursday (4)
        int day = 1 + (rule.day - firstWeekday + 7) % 7 + (rule.week - 1) * 7;
        static const int lengths[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        const int length = lengths[rule.month - 1] + (rule.month == 2 && isLeap(year) ? 1 : 0);
        while (day > length) day -= 7;  // week 5 means "the last one"
        days = first + day - 1;
    } else if (rule.kind == Rule::JulianNoLeap) {
        // Day 1-365; 29 February is never counted.
        int day = rule.day;
        if (isLeap(year) && day >= 60) day++;
        days = daysFromCivil(year, 1, 1) + day - 1;
    } else {
        days = daysFromCivil(year, 1, 1) + rule.day;
    }
    return days * 86400 + rule.time;
}

int32_t TimeZone::offsetAt(int64_t utc, bool* dst) const {
    if (dst) *dst = false;
    if (!hasDst_) return stdOffset_;

    int year, month, day;
    const int64_t local = utc + stdOffset_;
    civilFromDays((local >= 0 ? local : local - 86399) / 86400, year, month, day);
    // The switch to summer time is given in standard time, the switch back in summer time.
    const int64_t start = ruleSeconds(start_, year) - stdOffset_;
    const int64_t end = ruleSeconds(end_, year) - dstOffset_;
    // South of the equator summer runs over new year: it starts after it ends.
    const bool summer = start < end ? (utc >= start && utc < end) : !(utc >= end && utc < start);
    if (dst) *dst = summer;
    return summer ? dstOffset_ : stdOffset_;
}

int64_t TimeZone::fromCivil(int64_t year, int month, int day, int64_t hour, int64_t minute, int64_t second,
                            int dst) const {
    // Month 13 is January of the next year, month 0 December of the one before.
    int64_t months = (int64_t)month - 1;
    year += months >= 0 ? months / 12 : -((11 - months) / 12);
    months = ((months % 12) + 12) % 12;
    const int64_t wall = (daysFromCivil(year, (int)months + 1, 1) + day - 1) * 86400 + hour * 3600 + minute * 60 + second;
    if (!hasDst_) return wall - stdOffset_;

    // The clock reads `wall` at most twice: once counted in each of the two offsets.
    const int64_t asStd = wall - stdOffset_;
    const int64_t asDst = wall - dstOffset_;
    const bool stdFits = offsetAt(asStd) == stdOffset_;
    const bool dstFits = offsetAt(asDst) == dstOffset_;
    if (stdFits && dstFits) {
        // It happens twice. The first time is the one further ahead of UTC.
        if (dst == 0) return asStd;
        if (dst > 0) return asDst;
        return asStd < asDst ? asStd : asDst;
    }
    if (stdFits) return asStd;
    if (dstFits) return asDst;
    // It never happens: the clocks skipped it. Counting on in the offset from
    // before the change lands as far after the change as the time was into the gap.
    return asStd > asDst ? asStd : asDst;
}

CivilTime TimeZone::toCivil(int64_t utc) const {
    CivilTime out;
    out.offset = offsetAt(utc, &out.dst);
    const int64_t local = utc + out.offset;
    const int64_t days = (local >= 0 ? local : local - 86399) / 86400;
    const int64_t rest = local - days * 86400;
    civilFromDays(days, out.year, out.month, out.day);
    out.hour = (int)(rest / 3600);
    out.minute = (int)(rest % 3600 / 60);
    out.second = (int)(rest % 60);
    out.weekday = (int)(((days % 7) + 11) % 7);
    out.yearDay = (int)(days - daysFromCivil(out.year, 1, 1));
    snprintf(out.zone, sizeof(out.zone), "%s", out.dst ? dstName_ : stdName_);
    return out;
}

std::string formatTime(const char* format, const CivilTime& t, size_t most) {
    std::string out;
    char buf[40];
    for (const char* p = format; *p && out.size() < most; p++) {
        if (*p != '%') {
            out += *p;
            continue;
        }
        p++;
        bool pad = true;
        if (*p == '-') {
            pad = false;
            p++;
        }
        const char* two = pad ? "%02d" : "%d";
        buf[0] = '\0';
        switch (*p) {
            case 'Y': snprintf(buf, sizeof(buf), "%d", t.year); break;
            case 'y': snprintf(buf, sizeof(buf), two, ((t.year % 100) + 100) % 100); break;
            case 'm': snprintf(buf, sizeof(buf), two, t.month); break;
            case 'd': snprintf(buf, sizeof(buf), two, t.day); break;
            case 'e': snprintf(buf, sizeof(buf), pad ? "%2d" : "%d", t.day); break;
            case 'H': snprintf(buf, sizeof(buf), two, t.hour); break;
            case 'I': snprintf(buf, sizeof(buf), two, (t.hour + 11) % 12 + 1); break;
            case 'M': snprintf(buf, sizeof(buf), two, t.minute); break;
            case 'S': snprintf(buf, sizeof(buf), two, t.second); break;
            case 'p': snprintf(buf, sizeof(buf), "%s", t.hour < 12 ? "AM" : "PM"); break;
            case 'a': snprintf(buf, sizeof(buf), "%.3s", kDayNames[t.weekday]); break;
            case 'A': snprintf(buf, sizeof(buf), "%s", kDayNames[t.weekday]); break;
            case 'b': snprintf(buf, sizeof(buf), "%.3s", kMonthNames[t.month - 1]); break;
            case 'B': snprintf(buf, sizeof(buf), "%s", kMonthNames[t.month - 1]); break;
            case 'j': snprintf(buf, sizeof(buf), pad ? "%03d" : "%d", t.yearDay + 1); break;
            case 'Z': snprintf(buf, sizeof(buf), "%s", t.zone); break;
            case 'z': {
                const int32_t minutes = (t.offset < 0 ? -t.offset : t.offset) / 60;
                snprintf(buf, sizeof(buf), "%c%02d%02d", t.offset < 0 ? '-' : '+', (int)(minutes / 60), (int)(minutes % 60));
                break;
            }
            case 'F': snprintf(buf, sizeof(buf), "%d-%02d-%02d", t.year, t.month, t.day); break;
            case 'T': snprintf(buf, sizeof(buf), "%02d:%02d:%02d", t.hour, t.minute, t.second); break;
            case 'R': snprintf(buf, sizeof(buf), "%02d:%02d", t.hour, t.minute); break;
            case '%': snprintf(buf, sizeof(buf), "%%"); break;
            case '\0':
                out += '%';
                p--;  // the loop steps onto the terminator
                break;
            default:
                // Not one of ours: leave it readable.
                out += '%';
                if (!pad) out += '-';
                out += *p;
                break;
        }
        out += buf;
    }
    if (out.size() > most) out.resize(most);
    return out;
}

}  // namespace deskhog
