#include "tw/time_utils.h"

namespace tw {

int64_t daysFromCivil(int64_t y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

CivilTime civilFromUnix(int64_t unixSeconds) {
    const int64_t days = floorDiv(unixSeconds, 86400);
    const int64_t secs = unixSeconds - days * 86400;

    const int64_t z = days + 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t y = static_cast<int64_t>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m = mp < 10 ? mp + 3 : mp - 9;
    y += (m <= 2);

    CivilTime t;
    t.year = static_cast<int>(y);
    t.month = static_cast<int>(m);
    t.day = static_cast<int>(d);
    t.hour = static_cast<int>(secs / 3600);
    t.minute = static_cast<int>((secs % 3600) / 60);
    t.second = static_cast<int>(secs % 60);
    t.weekday = static_cast<int>(floorMod(days + 4, 7));  // 1970-01-01 was a Thursday
    return t;
}

int64_t unixFromCivil(int year, int month, int day, int hour, int minute, int second) {
    return daysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day)) * 86400 +
           hour * 3600 + minute * 60 + second;
}

const char* weekdayAbbrev(int weekday) {
    static const char* const kNames[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    return (weekday >= 0 && weekday < 7) ? kNames[weekday] : "???";
}

const char* monthAbbrev(int month) {
    static const char* const kNames[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                         "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    return (month >= 1 && month <= 12) ? kNames[month - 1] : "???";
}

}  // namespace tw
