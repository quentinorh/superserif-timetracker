#include "ApiClient.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <algorithm>

#include "Config.h"

namespace {

struct PsramAllocator : ArduinoJson::Allocator {
    void *allocate(size_t size) override
    {
        void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
        return p ? p : malloc(size);
    }
    void deallocate(void *pointer) override { heap_caps_free(pointer); }
    void *reallocate(void *pointer, size_t size) override
    {
        void *p = heap_caps_realloc(pointer, size, MALLOC_CAP_SPIRAM);
        return p ? p : realloc(pointer, size);
    }
};

PsramAllocator psramAllocator;

Project projectFrom(JsonObjectConst src)
{
    Project p;
    p.id = src["id"].as<const char *>() ? src["id"].as<const char *>() : "";
    p.name = src["name"].as<const char *>() ? src["name"].as<const char *>() : "";
    p.status = src["status"].as<const char *>() ? src["status"].as<const char *>() : "";
    p.startDate = src["start_date"].as<const char *>() ? src["start_date"].as<const char *>() : "";
    p.endDate = src["end_date"].as<const char *>() ? src["end_date"].as<const char *>() : "";
    p.moonmoon = src["moonmoon"] | false;

    JsonVariantConst hours = src["hours"];
    p.done = hours["done"] | 0.0f;
    p.hasTotal = !hours["total"].isNull();
    p.total = p.hasTotal ? (hours["total"] | 0.0f) : 0.0f;

    JsonVariantConst assignees = src["assignees"];
    if (assignees.is<JsonArrayConst>()) {
        for (JsonObjectConst a : assignees.as<JsonArrayConst>()) {
            Person person;
            person.id = a["id"].as<const char *>() ? a["id"].as<const char *>() : "";
            person.name = a["name"].as<const char *>() ? a["name"].as<const char *>() : "";
            if (person.id.length() > 0) {
                p.assignees.push_back(person);
            }
        }
    }
    return p;
}

}  // namespace

void ApiClient::begin()
{
    _tls.setInsecure();
    _tls.setHandshakeTimeout(15);
}

bool ApiClient::request(const char *method, const String &url, const String &body,
                        std::function<bool(const String &)> parse)
{
    _lastStatus = 0;
    _lastError = "";

    HTTPClient http;
    http.setReuse(false);
    http.setConnectTimeout(8000);
    http.setTimeout(15000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    // Chunked transfer (Vercel default) + TLS is unreliable on this Arduino
    // HTTPClient: the JSON stream is often truncated. HTTP/1.0 forces a
    // Content-Length body we can buffer in one go.
    http.useHTTP10(true);

    if (!http.begin(_tls, url)) {
        _lastError = F("URL invalide");
        return false;
    }

    if (strlen(API_TOKEN) > 0) {
        http.addHeader(F("Authorization"), String(F("Bearer ")) + API_TOKEN);
    }

    int code;
    if (body.length() > 0) {
        http.addHeader(F("Content-Type"), F("application/json"));
        code = http.sendRequest(method, (uint8_t *)body.c_str(), body.length());
    } else {
        code = http.sendRequest(method);
    }
    _lastStatus = code;

    String payload = http.getString();
    http.end();

    if (code < 200 || code >= 300) {
        if (code < 0) {
            _lastError = HTTPClient::errorToString(code);
        } else {
            JsonDocument doc(&psramAllocator);
            if (deserializeJson(doc, payload) == DeserializationError::Ok &&
                doc["error"].is<const char *>()) {
                _lastError = doc["error"].as<const char *>();
            } else {
                _lastError = String(F("HTTP ")) + code;
            }
        }
        log_w("%s %s -> %d (%s)", method, url.c_str(), code, _lastError.c_str());
        return false;
    }

    bool ok = parse(payload);
    if (!ok && _lastError.length() == 0) {
        _lastError = F("Reponse illisible");
    }
    return ok;
}

bool ApiClient::fetchPeople(std::vector<Person> &out)
{
    String url = String(API_BASE) + F("/api/people");

    return request("GET", url, "", [&](const String &payload) {
        JsonDocument doc(&psramAllocator);
        DeserializationError err = deserializeJson(doc, payload);
        if (err) {
            _lastError = String(F("JSON: ")) + err.c_str();
            log_w("people JSON (%u bytes): %s", (unsigned)payload.length(), err.c_str());
            return false;
        }

        out.clear();
        JsonVariantConst list = doc["people"];
        if (!list.is<JsonArrayConst>()) {
            _lastError = F("Pas de tableau people");
            return false;
        }

        for (JsonObjectConst item : list.as<JsonArrayConst>()) {
            Person person;
            person.id = item["id"].as<const char *>() ? item["id"].as<const char *>() : "";
            person.name = item["name"].as<const char *>() ? item["name"].as<const char *>() : "";
            if (person.id.length() > 0) {
                out.push_back(person);
            }
        }
        std::sort(out.begin(), out.end(),
                  [](const Person &a, const Person &b) { return a.name < b.name; });
        log_i("API: %u membre(s)", (unsigned)out.size());
        return true;
    });
}

bool ApiClient::fetchProjects(std::vector<Project> &out, const String &personId)
{
    String url = String(API_BASE) + F("/api/projects?status=") + API_STATUS_FILTER;
    if (personId.length() > 0) {
        url += F("&person_id=");
        url += personId;
    }

    return request("GET", url, "", [&](const String &payload) {
        JsonDocument doc(&psramAllocator);
        DeserializationError err = deserializeJson(doc, payload);
        if (err) {
            _lastError = String(F("JSON: ")) + err.c_str();
            log_w("projects JSON (%u bytes): %s", (unsigned)payload.length(), err.c_str());
            return false;
        }

        out.clear();
        JsonVariantConst list = doc["projects"];
        if (!list.is<JsonArrayConst>()) {
            _lastError = F("Pas de tableau projects");
            return false;
        }

        for (JsonObjectConst item : list.as<JsonArrayConst>()) {
            Project p = projectFrom(item);
            if (p.id.length() > 0) {
                out.push_back(p);
            }
        }
        log_i("API: %u projet(s)", (unsigned)out.size());
        return true;
    });
}

bool ApiClient::postSession(const String &personId, const String &projectId, bool start)
{
    String url = String(API_BASE) + F("/api/sessions");
    String body = F("{\"person_id\":\"");
    body += personId;
    body += F("\",\"project_id\":\"");
    body += projectId;
    body += F("\",\"action\":\"");
    body += start ? F("start") : F("stop");
    body += F("\"}");

    return request("POST", url, body, [&](const String &payload) {
        (void)payload;
        return true;
    });
}

bool ApiClient::fetchCurrentSession(const String &personId, SessionSnapshot &out)
{
    String url = String(API_BASE) + F("/api/sessions/current?person_id=") + personId;
    out = SessionSnapshot();

    return request("GET", url, "", [&](const String &payload) {
        JsonDocument doc(&psramAllocator);
        DeserializationError err = deserializeJson(doc, payload);
        if (err) {
            _lastError = String(F("JSON: ")) + err.c_str();
            return false;
        }

        if (!doc["project_id"].isNull()) {
            const char *id = doc["project_id"].as<const char *>();
            if (id) {
                out.projectId = id;
            }
        }
        if (!doc["started_at"].isNull()) {
            const char *started = doc["started_at"].as<const char *>();
            if (started) {
                out.startedAt = started;
            }
        }

        JsonVariantConst hours = doc["hours"];
        if (!hours.isNull() && hours.is<JsonObjectConst>()) {
            out.hasHours = true;
            out.done = hours["done"] | 0.0f;
            out.hasTotal = !hours["total"].isNull();
            out.total = out.hasTotal ? (hours["total"] | 0.0f) : 0.0f;
        }
        return true;
    });
}
