#pragma once

#include <Arduino.h>

#include <vector>

struct Person {
    String id;
    String name;
};

struct Project {
    String id;
    String name;
    String status;
    String startDate;
    String endDate;
    bool moonmoon = false;

    float done = 0.0f;
    float total = 0.0f;
    bool hasTotal = false;  // `hours.total` is null on unquoted projects

    std::vector<Person> assignees;

    bool hasAssignee(const String &personId) const
    {
        for (const Person &p : assignees) {
            if (p.id == personId) {
                return true;
            }
        }
        return false;
    }

    // -1 means "unknown" (no sold hours).
    int progressPct(float doneOverride = -1.0f) const
    {
        if (!hasTotal || total <= 0.0f) {
            return -1;
        }
        float d = doneOverride >= 0.0f ? doneOverride : done;
        return (int)lroundf(d * 100.0f / total);
    }
};

// GET /api/sessions/current — `hours` already includes the running session.
struct SessionSnapshot {
    String projectId;
    String startedAt;
    float done = 0.0f;
    float total = 0.0f;
    bool hasTotal = false;
    bool hasHours = false;

    bool active() const { return projectId.length() > 0; }
};

struct WifiNetwork {
    String ssid;
    int32_t rssi = 0;
    bool secure = true;
};

// Snapshot of the network layer, handed to the UI so it does not need to know
// about WifiPortal.
struct NetInfo {
    bool connected = false;
    bool internet = false;

    String ssid;
    String ip;
    int rssi = 0;

    String note;
};
