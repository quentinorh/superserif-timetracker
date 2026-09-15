#pragma once

#include <Arduino.h>

// GT911 capacitive panel on the T5 4.7" v2.3. The reset line is hard pulled
// up, so the I2C address cannot be forced and has to be discovered.
//
// Polling runs on a FreeRTOS task so a blocking e-paper refresh (pause
// spinner, partial bands) cannot swallow taps.
class TouchInput {
public:
    bool begin();
    bool available() const { return _ready; }

    // True once per press. Coordinates are in display space (960x540).
    bool tapped(int16_t &x, int16_t &y);

    // Finger down or a tap waiting to be consumed — skip the spinner frame.
    bool busy() const { return _held || _tapReady; }

    void sleep();

private:
    static void taskTrampoline(void *arg);
    void taskLoop();
    void service();

    bool _ready = false;
    bool _taskRunning = false;
    volatile bool _held = false;
    volatile bool _tapReady = false;
    volatile int16_t _tapX = 0;
    volatile int16_t _tapY = 0;
    uint32_t _releasedMs = 0;
    uint8_t _downCount = 0;
};
