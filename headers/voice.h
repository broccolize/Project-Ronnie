#pragma once
#include <Arduino.h>

void voice_setup();  // install I2S driver, alloc audio buffer
void voice_loop();   // sample mic, run Edge Impulse classifier, dispatch commands
