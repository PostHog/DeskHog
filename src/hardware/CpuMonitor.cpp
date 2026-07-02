#include "hardware/CpuMonitor.h"

CpuMonitor::CpuMonitor()
    : _lastSampleMs(0)
    , _smoothed(0.0f)
    , _started(false)
    , _hostLoad(0)
    , _hostLoadMs(0) {
    for (int i = 0; i < kCores; i++) {
        _counters[i] = 0;
        _lastCounts[i] = 0;
        _maxRate[i] = 1.0f;  // Avoid divide-by-zero before first calibration
    }
}

void CpuMonitor::observerTask(void* arg) {
    // arg encodes the core index for this observer.
    volatile uint32_t* counter = static_cast<volatile uint32_t*>(arg);
    for (;;) {
        (*counter)++;
        // Yield so we never starve the real idle task (needed for WDT feeding,
        // power management, etc). At idle priority this still measures how much
        // slack the core has.
        taskYIELD();
    }
}

void CpuMonitor::begin() {
    if (_started) return;
    _started = true;

    for (int core = 0; core < kCores; core++) {
        char name[16];
        snprintf(name, sizeof(name), "cpuobs%d", core);
        xTaskCreatePinnedToCore(
            observerTask,
            name,
            1024,
            (void*)&_counters[core],
            tskIDLE_PRIORITY,   // Runs only in the core's spare time
            nullptr,
            core
        );
    }

    _lastSampleMs = millis();
}

void CpuMonitor::setHostLoad(uint8_t percent) {
    if (percent > 100) percent = 100;
    _hostLoad = percent;
    _hostLoadMs = millis();
    if (_hostLoadMs == 0) _hostLoadMs = 1;  // 0 is the "never" sentinel
}

bool CpuMonitor::hasFreshHostLoad() const {
    if (_hostLoadMs == 0) return false;
    return (millis() - _hostLoadMs) < kHostFreshMs;
}

uint8_t CpuMonitor::getLoadPercent() {
    uint32_t now = millis();

    // Prefer a live host feed (e.g. a Mac streaming its CPU over serial).
    if (hasFreshHostLoad()) {
        float instant = (float)_hostLoad;
        _smoothed += (instant - _smoothed) * 0.4f;
        // Keep device sampling coherent for a clean handover when the feed drops.
        _lastSampleMs = now;
        for (int core = 0; core < kCores; core++) {
            _lastCounts[core] = _counters[core];
        }
        return (uint8_t)(_smoothed + 0.5f);
    }

    uint32_t elapsed = now - _lastSampleMs;
    if (elapsed < 50) {
        // Too soon to get a meaningful delta; return the last smoothed value.
        return (uint8_t)(_smoothed + 0.5f);
    }

    float loadSum = 0.0f;
    for (int core = 0; core < kCores; core++) {
        uint32_t count = _counters[core];
        uint32_t delta = count - _lastCounts[core];
        _lastCounts[core] = count;

        float rate = (float)delta / (float)elapsed;  // counts per ms
        if (rate > _maxRate[core]) {
            _maxRate[core] = rate;  // Self-calibrate to the idlest run seen
        }

        float idleFraction = rate / _maxRate[core];      // 1.0 == fully idle
        float load = (1.0f - idleFraction) * 100.0f;
        if (load < 0.0f) load = 0.0f;
        if (load > 100.0f) load = 100.0f;
        loadSum += load;
    }

    _lastSampleMs = now;

    float instant = loadSum / (float)kCores;
    // Light exponential smoothing so the hog's pace glides rather than jitters.
    _smoothed += (instant - _smoothed) * 0.4f;
    return (uint8_t)(_smoothed + 0.5f);
}
