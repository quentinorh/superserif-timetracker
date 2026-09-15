#include "TouchInput.h"

#include <TouchDrvGT911.hpp>
#include <Wire.h>

#include "Config.h"
#include "epd_driver.h"
#include "utilities.h"

namespace {
TouchDrvGT911 panel;

// The panel answers on one of two addresses depending on the batch.
uint8_t probeAddress()
{
    for (uint8_t address : {0x14, 0x5D}) {
        Wire.beginTransmission(address);
        if (Wire.endTransmission() == 0) {
            return address;
        }
    }
    return 0;
}
}  // namespace

bool TouchInput::begin()
{
    Wire.begin(BOARD_SDA, BOARD_SCL);

    // If we came back from deep sleep the controller may still be asleep;
    // toggling INT high wakes it.
    pinMode(TOUCH_INT, OUTPUT);
    digitalWrite(TOUCH_INT, HIGH);
    delay(10);

    uint8_t address = probeAddress();
    if (address == 0) {
        log_e("GT911 not found on I2C");
        return false;
    }

    panel.setPins(-1, TOUCH_INT);
    if (!panel.begin(Wire, address, BOARD_SDA, BOARD_SCL)) {
        log_e("GT911 init failed at 0x%02X", address);
        return false;
    }

    // The panel is mounted rotated with respect to the display.
    panel.setMaxCoordinates(EPD_WIDTH, EPD_HEIGHT);
    panel.setSwapXY(true);
    panel.setMirrorXY(false, true);

    _ready = true;
    const BaseType_t ok =
        xTaskCreatePinnedToCore(taskTrampoline, "touch", 4096, this, 4, nullptr, 0);
    _taskRunning = (ok == pdPASS);
    if (!_taskRunning) {
        log_e("touch task failed; polling from the main loop");
    }
    log_i("GT911 ready at 0x%02X", address);
    return true;
}

void TouchInput::taskTrampoline(void *arg)
{
    static_cast<TouchInput *>(arg)->taskLoop();
}

void TouchInput::taskLoop()
{
    vTaskDelay(pdMS_TO_TICKS(300));
    for (;;) {
        if (_ready) {
            service();
        }
        vTaskDelay(pdMS_TO_TICKS(TOUCH_POLL_MS));
    }
}

void TouchInput::service()
{
    uint32_t now = millis();
    int16_t px = 0;
    int16_t py = 0;
    const bool down = panel.getPoint(&px, &py, 1) != 0;

    if (!down) {
        _downCount = 0;
        if (_held) {
            _held = false;
            _releasedMs = now;
        }
        return;
    }

    if (_downCount < 250) {
        _downCount++;
    }

    // Two samples (~40 ms) filters waveform spikes without eating a real tap.
    constexpr uint8_t DEBOUNCE = 2;
    constexpr uint32_t SETTLE_MS = 250;
    if (_held || _downCount < DEBOUNCE) {
        return;
    }
    if (_releasedMs != 0 && now - _releasedMs < SETTLE_MS) {
        _held = true;
        return;
    }

    _held = true;
    _tapX = px;
    _tapY = py;
    _tapReady = true;
}

bool TouchInput::tapped(int16_t &x, int16_t &y)
{
    if (!_ready) {
        return false;
    }
    if (!_taskRunning) {
        service();
    }
    if (!_tapReady) {
        return false;
    }
    x = _tapX;
    y = _tapY;
    _tapReady = false;
    return true;
}

void TouchInput::sleep()
{
    if (_ready) {
        panel.sleep();
    }
}
