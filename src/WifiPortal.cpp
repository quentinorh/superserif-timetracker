#include "WifiPortal.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <algorithm>
#include <time.h>

#include "Config.h"

namespace {

constexpr uint32_t JOIN_VERDICT_GRACE_MS = 4000;
constexpr uint32_t JOIN_FAILURE_LINGER_MS = 1500;

void ensureNtp()
{
    static bool started = false;
    if (started) {
        return;
    }
    configTime(0, 0, "pool.ntp.org", "time.google.com");
    started = true;
}

}  // namespace

void WifiPortal::begin(Store *store)
{
    _store = store;

    WiFi.persistent(false);
    WiFi.setHostname(DEVICE_HOSTNAME);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
    WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);

    _offlineSinceMs = millis();
    _lastRetryMs = 0;
    _changed = true;
}

bool WifiPortal::takeChanged()
{
    bool c = _changed;
    _changed = false;
    return c;
}

void WifiPortal::loop()
{
    pumpScan();
    pumpJoin();

    bool connected = (WiFi.status() == WL_CONNECTED);
    if (connected != _connected) {
        _connected = connected;
        _changed = true;
        if (!connected) {
            _internet = false;
            if (_offlineSinceMs == 0) {
                _offlineSinceMs = millis();
            }
        }
    }

    if (_join != Join::Idle) {
        return;
    }

    if (online()) {
        ensureNtp();
        _offlineSinceMs = 0;
        _retryRound = 0;
        if (millis() - _lastInternetCheckMs > INTERNET_RECHECK_MS) {
            bool before = _internet;
            _internet = probeInternet();
            _lastInternetCheckMs = millis();
            if (before != _internet) {
                _changed = true;
                _offlineSinceMs = _internet ? 0 : millis();
            }
        }
        return;
    }

    if (_offlineSinceMs == 0) {
        _offlineSinceMs = millis();
    }

    if (millis() - _lastRetryMs > retryDelay()) {
        _lastRetryMs = millis();
        if (_connected) {
            _internet = probeInternet();
            _lastInternetCheckMs = millis();
            _changed = true;
            if (_internet) {
                ensureNtp();
                return;
            }
        } else {
            tryNextSavedNetwork();
        }
    }
}

uint32_t WifiPortal::retryDelay() const
{
    uint32_t d = 5000UL << (_retryRound > 3 ? 3 : _retryRound);
    return d > 40000UL ? 40000UL : d;
}

void WifiPortal::tryNextSavedNetwork()
{
    uint8_t count = _store ? _store->networkCount() : 0;
    if (count == 0) {
        if (_retryRound < 4) {
            _retryRound++;
        }
        return;
    }

    if (_retryIndex >= count) {
        _retryIndex = 0;
        if (_retryRound < 4) {
            _retryRound++;
        }
    }

    String ssid, password;
    if (_store->network(_retryIndex, ssid, password)) {
        _retryIndex++;
        beginJoin(ssid, password);
    }
}

void WifiPortal::retryNow()
{
    _retryIndex = 0;
    _retryRound = 0;
    _lastRetryMs = 0;
    _failReason = "";
    _changed = true;
}

void WifiPortal::join(const String &ssid, const String &password)
{
    beginJoin(ssid, password);
}

void WifiPortal::beginJoin(const String &ssid, const String &password)
{
    _joinSsid = ssid;
    _joinPassword = password;
    _join = Join::Pending;
    _failReason = "";
    _changed = true;
}

void WifiPortal::failJoin(const __FlashStringHelper *reason)
{
    _failReason = reason;
    _join = Join::Failed;
    _joinSettledMs = millis();
    _changed = true;
    log_w("join %s failed: %s", _joinSsid.c_str(), _failReason.c_str());
}

void WifiPortal::pumpJoin()
{
    switch (_join) {
        case Join::Idle:
            break;

        case Join::Pending:
            log_i("joining %s", _joinSsid.c_str());
            if (WiFi.status() == WL_CONNECTED) {
                WiFi.disconnect(false, false);
            }
            WiFi.begin(_joinSsid.c_str(),
                       _joinPassword.length() ? _joinPassword.c_str() : nullptr);
            _joinStartedMs = millis();
            _join = Join::Connecting;
            _changed = true;
            break;

        case Join::Connecting: {
            wl_status_t status = WiFi.status();
            if (status == WL_CONNECTED) {
                _join = Join::Verifying;
                _changed = true;
                break;
            }
            uint32_t elapsed = millis() - _joinStartedMs;
            if (elapsed > JOIN_VERDICT_GRACE_MS) {
                if (status == WL_NO_SSID_AVAIL) {
                    failJoin(F("Réseau introuvable"));
                    break;
                }
                if (status == WL_CONNECT_FAILED) {
                    failJoin(F("Mot de passe refusé"));
                    break;
                }
            }
            if (elapsed > STA_CONNECT_TIMEOUT_MS) {
                WiFi.disconnect(false, false);
                failJoin(F("Délai de connexion dépassé"));
            }
            break;
        }

        case Join::Verifying:
            _internet = probeInternet();
            _lastInternetCheckMs = millis();
            if (_internet) {
                _store->rememberNetwork(_joinSsid, _joinPassword);
                _retryIndex = 0;
                _retryRound = 0;
                _offlineSinceMs = 0;
                _join = Join::Success;
                _joinSettledMs = millis();
                _changed = true;
                ensureNtp();
                log_i("online via %s (%s)", _joinSsid.c_str(), WiFi.localIP().toString().c_str());
            } else {
                failJoin(F("Réseau joignable mais pas Internet"));
            }
            break;

        case Join::Success:
            _join = Join::Idle;
            _changed = true;
            break;

        case Join::Failed:
            if (millis() - _joinSettledMs > JOIN_FAILURE_LINGER_MS) {
                _join = Join::Idle;
                _lastRetryMs = millis();
                _changed = true;
            }
            break;
    }
}

bool WifiPortal::probeInternet()
{
    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }

    {
        WiFiClient client;
        HTTPClient http;
        http.setConnectTimeout(4000);
        http.setTimeout(4000);
        http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
        if (http.begin(client, CONNECTIVITY_URL)) {
            int code = http.GET();
            int length = http.getSize();
            http.end();
            if (code == 204 || (code == 200 && length == 0)) {
                return true;
            }
        }
    }

    WiFiClientSecure tls;
    tls.setInsecure();
    tls.setTimeout(8);
    bool reachable = tls.connect(API_HOST, 443);
    tls.stop();
    return reachable;
}

void WifiPortal::forgetCurrentNetwork()
{
    if (!_store) {
        return;
    }
    String current = WiFi.SSID();
    if (current.length() > 0) {
        _store->forgetNetwork(current);
    }
    WiFi.disconnect(false, false);
    _connected = false;
    _internet = false;
    _offlineSinceMs = millis();
    _lastRetryMs = millis();
    _changed = true;
}

void WifiPortal::startScan()
{
    _scanning = true;
    _changed = true;

    WiFi.mode(WIFI_STA);
    WiFi.scanDelete();

    // Synchronous on purpose: the Arduino helper treats a scan as failed after
    // max_ms_per_chan*20 (~6 s). A full e-paper refresh lasts that long, so an
    // async scan started just before painting the picker would be discarded
    // and the list would stay empty.
    int16_t found = runScan();
    if (found <= 0) {
        log_w("wifi scan returned %d, retrying", (int)found);
        delay(80);
        WiFi.scanDelete();
        found = runScan();
    }

    collectScanResults(found);
    _scanning = false;
    _changed = true;
}

int16_t WifiPortal::runScan()
{
    return WiFi.scanNetworks(/*async=*/false, /*show_hidden=*/false, /*passive=*/false,
                             /*max_ms_per_chan=*/120, /*channel=*/0);
}

void WifiPortal::collectScanResults(int16_t found)
{
    _scan.clear();
    if (found <= 0) {
        log_w("wifi scan: no AP (status %d)", (int)found);
        if (WiFi.status() == WL_CONNECTED) {
            String current = WiFi.SSID();
            if (current.length() > 0) {
                _scan.push_back({current, WiFi.RSSI(), true});
            }
        }
        return;
    }

    for (int16_t i = 0; i < found; i++) {
        String ssid = WiFi.SSID(i);
        if (ssid.length() == 0) {
            continue;
        }
        auto it = std::find_if(_scan.begin(), _scan.end(),
                               [&](const WifiNetwork &e) { return e.ssid == ssid; });
        if (it != _scan.end()) {
            if (WiFi.RSSI(i) > it->rssi) {
                it->rssi = WiFi.RSSI(i);
            }
            continue;
        }
        _scan.push_back({ssid, WiFi.RSSI(i), WiFi.encryptionType(i) != WIFI_AUTH_OPEN});
    }
    std::sort(_scan.begin(), _scan.end(),
              [](const WifiNetwork &a, const WifiNetwork &b) { return a.rssi > b.rssi; });

    log_i("wifi scan: %u réseau(x)", (unsigned)_scan.size());
    WiFi.scanDelete();
}

void WifiPortal::pumpScan()
{
    if (_scanning) {
        return;
    }
    int16_t found = WiFi.scanComplete();
    if (found > 0 && _scan.empty()) {
        collectScanResults(found);
        _changed = true;
    }
}

NetInfo WifiPortal::info() const
{
    NetInfo out;
    out.connected = _connected;
    out.internet = _internet;
    out.ssid = WiFi.SSID();
    out.rssi = _connected ? WiFi.RSSI() : 0;
    out.ip = _connected ? WiFi.localIP().toString() : String();

    switch (_join) {
        case Join::Pending:
        case Join::Connecting:
            out.note = String(F("Connexion à « ")) + _joinSsid + F(" »...");
            break;
        case Join::Verifying:
            out.note = F("Vérification de l'accès Internet...");
            break;
        case Join::Success:
            out.note = String(F("Connecté à « ")) + _joinSsid + F(" »");
            break;
        case Join::Failed:
            out.note = _failReason;
            break;
        case Join::Idle:
            if (_failReason.length() > 0 && !online()) {
                out.note = _failReason;
            } else if (_scanning) {
                out.note = F("Recherche des réseaux...");
            }
            break;
    }
    return out;
}
