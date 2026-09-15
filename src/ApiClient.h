#pragma once

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <functional>
#include <vector>

#include "Model.h"

// Thin wrapper around the Lineup REST API:
//   GET  /api/people
//   GET  /api/projects?status=demarre[&person_id=]
//   POST /api/sessions                  { person_id, project_id, action }
//   GET  /api/sessions/current?person_id=
class ApiClient {
public:
    void begin();

    bool fetchPeople(std::vector<Person> &out);
    bool fetchProjects(std::vector<Project> &out, const String &personId);
    bool postSession(const String &personId, const String &projectId, bool start);
    bool fetchCurrentSession(const String &personId, SessionSnapshot &out);

    int lastStatus() const { return _lastStatus; }
    const String &lastError() const { return _lastError; }

private:
    // `body` empty means GET. Returns true on 2xx.
    bool request(const char *method, const String &url, const String &body,
                 std::function<bool(const String &payload)> parse);

    WiFiClientSecure _tls;
    int _lastStatus = 0;
    String _lastError;
};
