#include <esp_adc_cal.h>
#include <driver/adc.h>
#include "battery.h"
#include "face.h"
#include <DFRobotDFPlayerMini.h>

extern DFRobotDFPlayerMini dfPlayer;

#define BAT_ADC_PIN ADC1_CHANNEL_7  // GPIO35 — 1 MΩ + 1 MΩ voltage divider

static esp_adc_cal_characteristics_t adcChars;
static bool warnedLow = false;

void battery_setup() {
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(BAT_ADC_PIN, ADC_ATTEN_DB_11);
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &adcChars);
}

int battery_percent() {
    // Average 16 readings to reduce ADC noise
    uint32_t sum = 0;
    for (int i = 0; i < 16; i++) {
        sum += adc1_get_raw(BAT_ADC_PIN);
    }
    uint32_t raw     = sum / 16;
    uint32_t vPin_mV = esp_adc_cal_raw_to_voltage(raw, &adcChars);
    float    vBat    = (vPin_mV / 1000.0f) * 2.0f;  // divider restores full voltage
    int      pct     = (int)((vBat - 3.2f) / (4.2f - 3.2f) * 100.0f);
    return constrain(pct, 0, 100);
}

void battery_auto_warn() {
    int pct = battery_percent();
    if (pct <= 20 && !warnedLow) {
        dfPlayer.play(60);
        warnedLow = true;
        drawFace(THINKING);
    }
    if (pct > 25) {
        warnedLow = false;
    }
}

void battery_respond_to_query() {
    int  pct = battery_percent();
    char line[20];
    sprintf(line, "Battery: %d%%", pct);
    showText("Am I hungry?", line, NULL);
    if      (pct >= 80) dfPlayer.play(61);
    else if (pct >= 50) dfPlayer.play(62);
    else if (pct >= 30) dfPlayer.play(63);
    else if (pct >  20) dfPlayer.play(64);
    else                dfPlayer.play(65);
    drawFace(SPEAKING);
}
