#pragma once

#include <Arduino.h>

#include <vector>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "ApiClient.h"
#include "Model.h"
#include "Store.h"

// The Lineup API owns the clock: POST /api/sessions start|stop, then read
// hours.done from GET /api/sessions/current (it already includes elapsed).
//
// Offline, start/stop are queued in NVS and flushed when the network returns.
// A reboot restores the last known project until the next pull from the server.
class Tracker {
public:
    void begin(Store *store, ApiClient *api);

    void setPerson(const String &personId);
    const String &personId() const { return _personId; }

    bool active() const { return _projectId.length() > 0; }
    const String &projectId() const { return _projectId; }
    const String &projectName() const { return _projectName; }

    uint32_t elapsedSeconds() const;
    float projectedDone() const;
    String elapsedLabel() const;

    void start(const Project &project);
    void stop();
    void toggle(const Project &project);

    // Retries queued start/stop and re-reads the running session when due.
    void loop(bool online);
    void setOnline(bool online);
    void flushNow();

    // Pulls GET /sessions/current. No-op when offline.
    bool pull();

    size_t queuedActions() const { return _pending.size(); }
    const String &lastError() const { return _lastError; }

    bool takeDirty();

    // Copy the last pulled hours onto the matching project row.
    void applyHours(std::vector<Project> &projects) const;

    static String formatHoursMinutes(float hours);

private:
    struct PendingAction {
        String projectId;
        bool start = true;
        uint32_t epoch = 0;
    };

    void applySnapshot(const SessionSnapshot &snap, const String &fallbackName);
    void persistSession();
    void queueAction(const String &projectId, bool start);
    void flushQueue();
    void loadQueue();
    void storeQueue();

    Store *_store = nullptr;
    ApiClient *_api = nullptr;
    SemaphoreHandle_t _lock = nullptr;
    uint32_t _epoch = 0;

    String _personId;
    String _projectId;
    String _projectName;
    String _startedAt;

    float _doneAtPull = 0.0f;
    float _total = 0.0f;
    bool _hasTotal = false;
    uint32_t _pulledMs = 0;
    uint32_t _anchorMs = 0;

    std::vector<PendingAction> _pending;

    uint32_t _lastPullMs = 0;
    uint32_t _lastQueueAttemptMs = 0;
    String _lastError;
    bool _dirty = false;
    bool _online = false;
};
