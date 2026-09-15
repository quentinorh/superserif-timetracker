#include "Tracker.h"

#include <math.h>
#include <time.h>

#include "Config.h"

namespace {
constexpr uint32_t QUEUE_RETRY_MS = 60UL * 1000UL;

struct Guard {
    SemaphoreHandle_t handle;
    explicit Guard(SemaphoreHandle_t h) : handle(h)
    {
        if (handle) {
            xSemaphoreTakeRecursive(handle, portMAX_DELAY);
        }
    }
    ~Guard()
    {
        if (handle) {
            xSemaphoreGiveRecursive(handle);
        }
    }
    Guard(const Guard &) = delete;
    Guard &operator=(const Guard &) = delete;
};

bool timeIsSynced()
{
    return time(nullptr) > 1700000000;
}

time_t parseIsoUtc(const String &iso)
{
    if (iso.length() < 19) {
        return 0;
    }
    struct tm t = {};
    t.tm_year = iso.substring(0, 4).toInt() - 1900;
    t.tm_mon = iso.substring(5, 7).toInt() - 1;
    t.tm_mday = iso.substring(8, 10).toInt();
    t.tm_hour = iso.substring(11, 13).toInt();
    t.tm_min = iso.substring(14, 16).toInt();
    t.tm_sec = iso.substring(17, 19).toInt();
    t.tm_isdst = 0;
    time_t seconds = mktime(&t);
    return seconds < 0 ? 0 : seconds;
}

String formatMinutes(uint32_t minutes)
{
    unsigned h = minutes / 60;
    unsigned m = minutes % 60;
    char buf[20];
    if (h > 0 && m > 0) {
        snprintf(buf, sizeof(buf), "%uH%uMIN", h, m);
    } else if (h > 0) {
        snprintf(buf, sizeof(buf), "%uH", h);
    } else {
        snprintf(buf, sizeof(buf), "%uMIN", m);
    }
    return buf;
}
}  // namespace

void Tracker::begin(Store *store, ApiClient *api)
{
    _store = store;
    _api = api;
    if (!_lock) {
        _lock = xSemaphoreCreateRecursiveMutex();
    }

    loadQueue();

    if (_store->hasSession()) {
        _projectId = _store->sessionProjectId();
        _projectName = _store->sessionProjectName();
        _anchorMs = millis();
        _pulledMs = millis();
        log_i("cached session on %s (will confirm with the API)", _projectName.c_str());
    }
}

bool Tracker::takeDirty()
{
    Guard g(_lock);
    bool d = _dirty;
    _dirty = false;
    return d;
}

void Tracker::setPerson(const String &personId)
{
    Guard g(_lock);
    if (_personId == personId) {
        return;
    }
    bool firstAssign = _personId.length() == 0;
    _personId = personId;
    if (firstAssign) {
        return;
    }
    _epoch++;
    _projectId = "";
    _projectName = "";
    _startedAt = "";
    _doneAtPull = 0.0f;
    _anchorMs = 0;
    _pulledMs = 0;
    if (_store) {
        _store->clearSession();
    }
}

// ---------------------------------------------------------------------------
// Running session
// ---------------------------------------------------------------------------

uint32_t Tracker::elapsedSeconds() const
{
    if (!active()) {
        return 0;
    }
    if (_startedAt.length() > 0 && timeIsSynced()) {
        time_t started = parseIsoUtc(_startedAt);
        time_t now = time(nullptr);
        if (started > 0 && now > started) {
            return (uint32_t)(now - started);
        }
    }
    if (_anchorMs == 0) {
        return 0;
    }
    return (millis() - _anchorMs) / 1000UL;
}

float Tracker::projectedDone() const
{
    if (!active()) {
        return 0.0f;
    }
    uint32_t extraMs = _pulledMs ? (millis() - _pulledMs) : 0;
    return _doneAtPull + (float)extraMs / 3600000.0f;
}

String Tracker::elapsedLabel() const
{
    return formatMinutes(elapsedSeconds() / 60);
}

String Tracker::formatHoursMinutes(float hours)
{
    if (hours < 0.0f) {
        hours = 0.0f;
    }
    return formatMinutes((uint32_t)lroundf(hours * 60.0f));
}

void Tracker::applyHours(std::vector<Project> &projects) const
{
    if (!active()) {
        return;
    }
    float done = projectedDone();
    for (Project &project : projects) {
        if (project.id == _projectId) {
            project.done = done;
            if (_hasTotal || _total > 0.0f) {
                project.hasTotal = _hasTotal;
                project.total = _total;
            }
            return;
        }
    }
}

void Tracker::applySnapshot(const SessionSnapshot &snap, const String &fallbackName)
{
    _projectId = snap.projectId;
    if (fallbackName.length() > 0) {
        _projectName = fallbackName;
    }
    _startedAt = snap.startedAt;
    if (snap.hasHours) {
        _doneAtPull = snap.done;
        _hasTotal = snap.hasTotal;
        _total = snap.total;
    }
    _pulledMs = millis();
    if (_anchorMs == 0) {
        _anchorMs = millis();
    }
    persistSession();
}

void Tracker::start(const Project &project)
{
    Guard g(_lock);
    if (_personId.length() == 0) {
        _lastError = F("Écran non attribué");
        return;
    }

    _lastError = "";
    _epoch++;
    _projectId = project.id;
    _projectName = project.name;
    _doneAtPull = project.done;
    _hasTotal = project.hasTotal;
    _total = project.total;
    _startedAt = "";
    _anchorMs = millis();
    _pulledMs = millis();
    persistSession();
    queueAction(project.id, /*start=*/true);
    _dirty = true;
    log_i("timer start on %s (sync in background)", _projectName.c_str());
}

void Tracker::stop()
{
    Guard g(_lock);
    if (!active()) {
        return;
    }

    String stoppedId = _projectId;
    String stoppedName = _projectName;
    _lastError = "";
    _epoch++;
    queueAction(stoppedId, /*start=*/false);
    _projectId = "";
    _projectName = "";
    _startedAt = "";
    _store->clearSession();
    _dirty = true;
    log_i("timer stop on %s (sync in background)", stoppedName.c_str());
}

void Tracker::toggle(const Project &project)
{
    Guard g(_lock);
    if (active() && _projectId == project.id) {
        stop();
        return;
    }
    // Tapping another row stops the current timer and starts this one.
    // Locally we just replace the session; the API stop is implied by start.
    start(project);
}

void Tracker::loop(bool online)
{
    bool shouldFlush = false;
    bool shouldPull = false;
    {
        Guard g(_lock);
        _online = online;
        if (online && !_pending.empty() && millis() - _lastQueueAttemptMs > QUEUE_RETRY_MS) {
            _lastQueueAttemptMs = millis();
            shouldFlush = true;
        }
        if (active() && online && _personId.length() > 0 &&
            (_lastPullMs == 0 || millis() - _lastPullMs >= SESSION_PULL_MS)) {
            shouldPull = true;
        }
    }
    if (shouldFlush) {
        flushQueue();
    }
    if (shouldPull) {
        pull();
    }
}

void Tracker::setOnline(bool online)
{
    Guard g(_lock);
    _online = online;
}

void Tracker::flushNow()
{
    bool online = false;
    {
        Guard g(_lock);
        online = _online;
    }
    if (online) {
        flushQueue();
    }
}

bool Tracker::pull()
{
    String personId;
    uint32_t epochAtFetch = 0;
    {
        Guard g(_lock);
        if (_personId.length() == 0) {
            return true;
        }
        if (!_online) {
            _lastError = F("Hors ligne");
            return false;
        }
        personId = _personId;
        epochAtFetch = _epoch;
        _lastPullMs = millis();
    }

    SessionSnapshot snap;
    if (!_api->fetchCurrentSession(personId, snap)) {
        Guard g(_lock);
        if (epochAtFetch == _epoch) {
            _lastError = _api->lastError();
        }
        return false;
    }

    Guard g(_lock);
    if (epochAtFetch != _epoch) {
        // A tap changed the timer while this fetch was in flight.
        return true;
    }
    _lastError = "";
    if (!_pending.empty()) {
        // Local start/stop wins until the queued action is flushed.
        return true;
    }
    if (!snap.active()) {
        if (active()) {
            _projectId = "";
            _projectName = "";
            _startedAt = "";
            _store->clearSession();
            _dirty = true;
        }
        return true;
    }

    bool changed = _projectId != snap.projectId;
    String name = (_projectId == snap.projectId) ? _projectName : String();
    applySnapshot(snap, name);
    if (changed) {
        _dirty = true;
    }
    return true;
}

void Tracker::persistSession()
{
    if (!active()) {
        return;
    }
    _store->saveSession(_projectId, _projectName);
}

// ---------------------------------------------------------------------------
// Deferred start/stop
// ---------------------------------------------------------------------------

void Tracker::queueAction(const String &projectId, bool start)
{
    // A later action for the same person supersedes earlier ones.
    _pending.clear();
    _pending.push_back({projectId, start, _epoch});
    storeQueue();
    log_w("queued %s for %s", start ? "start" : "stop", projectId.c_str());
}

void Tracker::flushQueue()
{
    for (;;) {
        PendingAction action;
        String personId;
        {
            Guard g(_lock);
            if (_personId.length() == 0 || _pending.empty()) {
                return;
            }
            action = _pending.front();
            personId = _personId;
        }

        const bool ok = _api->postSession(personId, action.projectId, action.start);
        const int status = _api->lastStatus();
        const String err = _api->lastError();

        bool stillOurs = false;
        {
            Guard g(_lock);
            stillOurs = !_pending.empty() && _pending.front().epoch == action.epoch &&
                        _pending.front().projectId == action.projectId &&
                        _pending.front().start == action.start;
            if (ok) {
                log_i("flushed %s for %s", action.start ? "start" : "stop",
                      action.projectId.c_str());
                if (stillOurs) {
                    _pending.erase(_pending.begin());
                    storeQueue();
                }
            } else {
                _lastError = err;
                if (status == 409) {
                    log_w("dropping unpushable %s for %s: %s", action.start ? "start" : "stop",
                          action.projectId.c_str(), err.c_str());
                    if (stillOurs) {
                        _pending.erase(_pending.begin());
                        storeQueue();
                    }
                } else if (stillOurs) {
                    return;
                }
            }
        }

        if (ok) {
            pull();
        } else if (status != 409 && stillOurs) {
            return;
        }
    }
}

void Tracker::loadQueue()
{
    _pending.clear();
    String blob = _store->pendingActions();
    // Old firmware stored "<projectId>=<hours>"; that must not be replayed.
    if (blob.indexOf("s:") < 0 && blob.indexOf("t:") < 0) {
        if (blob.length() > 0) {
            log_w("dropping legacy hour queue");
            _store->setPendingActions("");
        }
        return;
    }

    int from = 0;
    while (from < (int)blob.length()) {
        int end = blob.indexOf(';', from);
        if (end < 0) {
            end = blob.length();
        }
        String entry = blob.substring(from, end);
        if (entry.startsWith("s:") || entry.startsWith("t:")) {
            _pending.push_back({entry.substring(2), entry.startsWith("s:")});
        }
        from = end + 1;
    }
    if (!_pending.empty()) {
        log_i("%u pending session action(s) restored", (unsigned)_pending.size());
    }
}

void Tracker::storeQueue()
{
    String blob;
    for (const PendingAction &action : _pending) {
        if (blob.length()) {
            blob += ';';
        }
        blob += action.start ? "s:" : "t:";
        blob += action.projectId;
    }
    _store->setPendingActions(blob);
}
