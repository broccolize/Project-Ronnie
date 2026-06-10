#include "face.h"

U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

FaceState     currentFace = IDLE;
unsigned long lastBlink   = 0;
bool          eyeOpen     = true;
int           mouthPhase  = 0;

// Smart idle status bar values — updated by face_set_status() every 10 s
static int  s_hour    = 0;
static int  s_minute  = 0;
static bool s_isPM    = false;
static int  s_battPct = 0;

// Text shown during REMINDER_SHOW state — set by face_set_reminder_text()
static char s_reminderText[64] = "";

void face_setup() {
    u8g2.begin();
    u8g2.setFont(u8g2_font_6x10_tf);
}

void face_set_status(int hour, int minute, bool isPM, int battPct) {
    s_hour    = hour;
    s_minute  = minute;
    s_isPM    = isPM;
    s_battPct = battPct;
}

void face_set_reminder_text(const char* text) {
    strlcpy(s_reminderText, text ? text : "", sizeof(s_reminderText));
}

void face_idle_tick() {
    unsigned long now = millis();
    if (eyeOpen && now - lastBlink > 5000) {
        eyeOpen   = false;
        lastBlink = now;
        drawFace(currentFace);
    } else if (!eyeOpen && now - lastBlink > 80) {
        eyeOpen = true;
        drawFace(currentFace);
    }
}

void drawFace(FaceState state) {
    currentFace = state;
    u8g2.clearBuffer();
    int eyeR = eyeOpen ? 10 : 2;

    switch (state) {
        case IDLE: {
            // Smart idle: face centre + clock top-left + battery icon top-right
            u8g2.setFont(u8g2_font_6x10_tf);
            char timeBuf[10];
            snprintf(timeBuf, sizeof(timeBuf), "%d:%02d%s",
                     s_hour, s_minute, s_isPM ? "P" : "A");
            u8g2.drawStr(0, 10, timeBuf);

            // Battery icon (top-right): small outline + fill
            int bx = 108, by = 2, bw = 16, bh = 8;
            u8g2.drawFrame(bx, by, bw, bh);
            u8g2.drawBox(bx + bw, by + 2, 2, 4);  // battery nub
            int fill = (s_battPct * (bw - 2)) / 100;
            if (fill > 0) u8g2.drawBox(bx + 1, by + 1, fill, bh - 2);

            // Face centred
            if (eyeOpen) {
                u8g2.drawRFrame(28, 22, 20, 20, 4);
                u8g2.drawRFrame(80, 22, 20, 20, 4);
            } else {
                u8g2.drawHLine(28, 32, 20);
                u8g2.drawHLine(80, 32, 20);
            }
            u8g2.drawHLine(48, 54, 32);
            break;
        }

        case HAPPY:
            u8g2.drawDisc(40, 28, eyeR);
            u8g2.drawDisc(88, 28, eyeR);
            for (int i = -20; i < 20; i++) {
                int y = (i * i) / 40;
                u8g2.drawPixel(64 + i, 50 + y);
            }
            break;

        case THINKING:
            u8g2.drawDisc(40, 28, eyeR);
            u8g2.drawBox(80, 24, 16, 8);
            u8g2.drawStr(50, 54, "...");
            break;

        case SPEAKING:
            u8g2.drawDisc(40, 28, eyeR);
            u8g2.drawDisc(88, 28, eyeR);
            u8g2.drawDisc(64, 50, (mouthPhase % 2 == 0) ? 8 : 4);
            mouthPhase = (mouthPhase + 1) % 100;
            break;

        case SLEEPING:
            u8g2.drawHLine(28, 28, 20);
            u8g2.drawHLine(80, 28, 20);
            u8g2.drawStr(72, 20, "zzz");
            u8g2.drawHLine(48, 52, 32);
            break;

        case BOOTING:
            u8g2.drawStr(10, 30, "RONNIE v7.0");
            u8g2.drawStr(10, 44, "Booting...");
            break;

        case ALARM_RING:
            u8g2.setFont(u8g2_font_7x13B_tf);
            u8g2.drawStr(28, 20, "ALARM!");
            u8g2.setFont(u8g2_font_6x10_tf);
            u8g2.drawStr(10, 38, "Double-tap head");
            u8g2.drawStr(22, 52, "to dismiss");
            break;

        case REMINDER_SHOW:
            u8g2.setFont(u8g2_font_7x13B_tf);
            u8g2.drawStr(14, 18, "REMINDER");
            u8g2.setFont(u8g2_font_6x10_tf);
            u8g2.drawStr(4, 38, s_reminderText);
            break;

        case TIMER_FACE:
        case STOPWATCH_FACE:
            // Large-digit display is handled by showTimerText(); drawFace() just
            // clears the buffer so a subsequent showTimerText() call can paint it.
            break;
    }
    u8g2.sendBuffer();
}

void showText(const char* line1, const char* line2, const char* line3) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_7x13B_tf);
    if (line1) u8g2.drawStr(4, 16, line1);
    u8g2.setFont(u8g2_font_6x10_tf);
    if (line2) u8g2.drawStr(4, 34, line2);
    if (line3) u8g2.drawStr(4, 50, line3);
    u8g2.sendBuffer();
}

void showTimerText(const char* label, const char* digits) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr(4, 12, label);
    // Large number font — only digits and colon (reduces flash vs full font)
    u8g2.setFont(u8g2_font_logisoso28_tn);
    // Centre the digit string horizontally
    int strW = u8g2.getStrWidth(digits);
    int x    = (128 - strW) / 2;
    u8g2.drawStr(x, 58, digits);
    u8g2.sendBuffer();
}
