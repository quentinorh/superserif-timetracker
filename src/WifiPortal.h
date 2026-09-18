#pragma once

#include <Arduino.h>
#include <WiFi.h>

#include <vector>

#include "Model.h"
#include "Store.h"

// Station-only Wi-Fi: retries saved networks, proves there is internet, and
// exposes a scan + join API for the on-screen picker. No access point.
class WifiPortal {
public:
    enum class Join : uint8_t { Idle, Pending, Connecting, Verifying, Success, Failed };

    void begin(Store *store);
    void loop();

    bool online() const { return _connected && _internet; }
    Join joinState() const { return _join; }

    void join(const String &ssid, const String &password);
    void forgetCurrentNetwork();
    void retryNow();
    void connectSavedNow();
    void setAutoJoin(bool enabled) { _autoJoin = enabled; }

    bool hasVisibleSavedNetwork() const;
    bool triedAllSaved() const { return _allSavedTried; }

    void startScan();
    bool scanning() const { return _scanning; }
    const std::vector<WifiNetwork> &networks() const { return _scan; }

    NetInfo info() const;
    bool takeChanged();

private:
    void beginJoin(const String &ssid, const String &password);
    void pumpJoin();
    void failJoin(const __FlashStringHelper *reason);

    void pumpScan();
    int16_t runScan();
    void collectScanResults(int16_t found);
    bool probeInternet();
    void tryNextSavedNetwork();
    uint32_t retryDelay() const;
    bool ssidInScan(const String &ssid) const;

    Store *_store = nullptr;

    bool _connected = false;
    bool _internet = false;

    Join _join = Join::Idle;
    String _joinSsid;
    String _joinPassword;
    uint32_t _joinStartedMs = 0;
    uint32_t _joinSettledMs = 0;
    String _failReason;

    uint8_t _retryIndex = 0;
    uint32_t _offlineSinceMs = 0;
    uint32_t _lastRetryMs = 0;
    uint8_t _retryRound = 0;
    uint32_t _lastInternetCheckMs = 0;
    bool _autoJoin = true;
    bool _allSavedTried = false;

    std::vector<WifiNetwork> _scan;
    bool _scanning = false;

    bool _changed = false;
};
