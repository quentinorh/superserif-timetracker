#pragma once

#include <Arduino.h>
#include <Preferences.h>

#include "Config.h"

// Everything that must survive a reboot: known Wi-Fi networks, the selected
// team member and the timer session in progress.
class Store {
public:
    void begin();

    // -- Wi-Fi networks, most recently used first ---------------------------
    uint8_t networkCount();
    bool network(uint8_t index, String &ssid, String &password);
    void rememberNetwork(const String &ssid, const String &password);
    void forgetNetwork(const String &ssid);
    void forgetAllNetworks();

    // -- Assigned user (one screen = one team member) -----------------------
    String userId();
    String userName();
    void setUser(const String &id, const String &name);

    // -- Timer session (project id/name only; hours live on the server) -----
    bool hasSession();
    String sessionProjectId();
    String sessionProjectName();
    void saveSession(const String &projectId, const String &projectName);
    void clearSession();

    // -- start/stop the API has not accepted yet ----------------------------
    // Serialised as "s:<projectId>;t:<projectId>".
    String pendingActions();
    void setPendingActions(const String &blob);

    bool darkTheme();
    void setDarkTheme(bool dark);

private:
    void loadNetworks();
    void storeNetworks();

    struct Credential {
        String ssid;
        String password;
    };

    Preferences _prefs;
    Credential _networks[MAX_NETWORKS];
    uint8_t _count = 0;
    bool _loaded = false;
};
