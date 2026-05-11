#include "face.h"

U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

FaceState     currentFace = IDLE;
unsigned long lastBlink   = 0;
bool          eyeOpen     = true;
int           mouthPhase  = 0;

void face_setup() {
    u8g2.begin();
    u8g2.setFont(u8g2_font_6x10_tf);
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
        case IDLE:
            if (eyeOpen) {
                u8g2.drawRFrame(28, 18, 20, 20, 4);
                u8g2.drawRFrame(80, 18, 20, 20, 4);
            } else {
                u8g2.drawHLine(28, 28, 20);
                u8g2.drawHLine(80, 28, 20);
            }
            u8g2.drawHLine(48, 52, 32);
            break;

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
            u8g2.drawStr(10, 30, "RONNIE v2.0");
            u8g2.drawStr(10, 44, "Booting...");
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
