#include "Store.h"

namespace {
constexpr char NAMESPACE[] = "lineup";

String netKey(const char *prefix, uint8_t index)
{
    return String(prefix) + String(index);
}
}  // namespace

void Store::begin()
{
    _prefs.begin(NAMESPACE, false);
    loadNetworks();
}

// ---------------------------------------------------------------------------
// Wi-Fi networks
// ---------------------------------------------------------------------------

void Store::loadNetworks()
{
    _count = _prefs.getUChar("net_n", 0);
    if (_count > MAX_NETWORKS) {
        _count = MAX_NETWORKS;
    }
    for (uint8_t i = 0; i < _count; i++) {
        _networks[i].ssid = _prefs.getString(netKey("net_s", i).c_str(), "");
        _networks[i].password = _prefs.getString(netKey("net_p", i).c_str(), "");
    }
    // Drop anything that got truncated by a partial write.
    uint8_t valid = 0;
    for (uint8_t i = 0; i < _count; i++) {
        if (_networks[i].ssid.length() > 0) {
            _networks[valid++] = _networks[i];
        }
    }
    _count = valid;
    _loaded = true;
}

void Store::storeNetworks()
{
    _prefs.putUChar("net_n", _count);
    for (uint8_t i = 0; i < _count; i++) {
        _prefs.putString(netKey("net_s", i).c_str(), _networks[i].ssid);
        _prefs.putString(netKey("net_p", i).c_str(), _networks[i].password);
    }
    for (uint8_t i = _count; i < MAX_NETWORKS; i++) {
        _prefs.remove(netKey("net_s", i).c_str());
        _prefs.remove(netKey("net_p", i).c_str());
    }
}

uint8_t Store::networkCount()
{
    return _count;
}

bool Store::network(uint8_t index, String &ssid, String &password)
{
    if (index >= _count) {
        return false;
    }
    ssid = _networks[index].ssid;
    password = _networks[index].password;
    return true;
}

void Store::rememberNetwork(const String &ssid, const String &password)
{
    if (ssid.length() == 0) {
        return;
    }

    // Move to the front so the most recently working network is tried first.
    uint8_t existing = MAX_NETWORKS;
    for (uint8_t i = 0; i < _count; i++) {
        if (_networks[i].ssid == ssid) {
            existing = i;
            break;
        }
    }

    uint8_t shiftFrom = (existing < MAX_NETWORKS) ? existing : (uint8_t)(MAX_NETWORKS - 1);
    if (existing == MAX_NETWORKS && _count < MAX_NETWORKS) {
        shiftFrom = _count;
        _count++;
    }
    for (uint8_t i = shiftFrom; i > 0; i--) {
        _networks[i] = _networks[i - 1];
    }
    _networks[0].ssid = ssid;
    _networks[0].password = password;

    storeNetworks();
}

void Store::forgetNetwork(const String &ssid)
{
    uint8_t write = 0;
    for (uint8_t i = 0; i < _count; i++) {
        if (_networks[i].ssid != ssid) {
            _networks[write++] = _networks[i];
        }
    }
    if (write == _count) {
        return;
    }
    _count = write;
    storeNetworks();
}

void Store::forgetAllNetworks()
{
    _count = 0;
    storeNetworks();
}

// ---------------------------------------------------------------------------
// Selected user
// ---------------------------------------------------------------------------

String Store::userId()
{
    return _prefs.getString("user_id", "");
}

String Store::userName()
{
    return _prefs.getString("user_name", "");
}

void Store::setUser(const String &id, const String &name)
{
    _prefs.putString("user_id", id);
    _prefs.putString("user_name", name);
}

// ---------------------------------------------------------------------------
// Timer session
// ---------------------------------------------------------------------------

bool Store::hasSession()
{
    return _prefs.getString("ses_id", "").length() > 0;
}

String Store::sessionProjectId()
{
    return _prefs.getString("ses_id", "");
}

String Store::sessionProjectName()
{
    return _prefs.getString("ses_name", "");
}

void Store::saveSession(const String &projectId, const String &projectName)
{
    _prefs.putString("ses_id", projectId);
    _prefs.putString("ses_name", projectName);
    _prefs.remove("ses_base");
    _prefs.remove("ses_carry");
}

void Store::clearSession()
{
    _prefs.remove("ses_id");
    _prefs.remove("ses_name");
    _prefs.remove("ses_base");
    _prefs.remove("ses_carry");
}

// ---------------------------------------------------------------------------
// Pending session actions
// ---------------------------------------------------------------------------

String Store::pendingActions()
{
    return _prefs.getString("pending", "");
}

void Store::setPendingActions(const String &blob)
{
    if (blob.length() == 0) {
        _prefs.remove("pending");
    } else {
        _prefs.putString("pending", blob);
    }
}

bool Store::darkTheme()
{
    return _prefs.getBool("dark", false);
}

void Store::setDarkTheme(bool dark)
{
    _prefs.putBool("dark", dark);
}
