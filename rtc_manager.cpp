#include <RTClib.h>
#include "rtc_manager.h"

extern RTC_DS3231 rtc;

String getFormattedTime() {
    DateTime now  = rtc.now();
    int      h    = now.hour();
    int      m    = now.minute();
    String   ampm = (h >= 12) ? "PM" : "AM";
    if (h > 12) h -= 12;
    if (h == 0) h  = 12;
    char buf[12];
    sprintf(buf, "%d:%02d %s", h, m, ampm.c_str());
    return String(buf);
}

String getFormattedDate() {
    DateTime now = rtc.now();
    if (now.month() < 1 || now.month() > 12) return "Date Error";
    const char* months[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    char buf[20];
    sprintf(buf, "%d %s %d", now.day(), months[now.month() - 1], now.year());
    return String(buf);
}

int getCurrentHour()   { return rtc.now().hour(); }
int getCurrentMinute() { return rtc.now().minute(); }
