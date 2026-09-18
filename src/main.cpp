// Lineup timetracker -- LilyGo T5 4.7" e-paper (ESP32-S3)
//
// One screen per team member. Shows that person's running projects, starts
// or stops the clock via API sessions, and reads hours.done back from Lineup.

#ifndef BOARD_HAS_PSRAM
#error "Enable OPI PSRAM: the 960x540 framebuffer does not fit in internal RAM"
#endif

#include <Arduino.h>
#include <vector>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ApiClient.h"
#include "Config.h"
#include "Model.h"
#include "Store.h"
#include "TouchInput.h"
#include "Tracker.h"
#include "Ui.h"
#include "WifiPortal.h"

namespace {

Store store;
ApiClient api;
WifiPortal net;
Tracker tracker;
TouchInput touch;
Ui ui;

std::vector<Project> visibleProjects;
std::vector<Person> people;

String personId;
String personName;

volatile bool renderPending = false;
volatile bool renderFull = false;
volatile bool renderCountPartial = true;

constexpr uint32_t JOB_SESSION = 1u;
constexpr uint32_t JOB_REFRESH = 2u;
TaskHandle_t apiTaskHandle = nullptr;

uint32_t lastFetchAttemptMs = 0;
uint32_t lastFetchOkMs = 0;
uint32_t lastTimerPaintMs = 0;
uint32_t lastSpinPaintMs = 0;
bool bootDone = false;
String fetchError;

bool wifiFromSettings = false;
String kbSsid;
String kbPass;
bool kbShift = false;
bool kbSymbols = false;
bool kbReveal = false;

void requestRender(bool full, bool countPartial = true)
{
    if (!renderPending) {
        renderCountPartial = countPartial;
    } else {
        renderCountPartial = renderCountPartial || countPartial;
    }
    renderPending = true;
    renderFull = renderFull || full;
}

bool hasAssignedUser()
{
    return personId.length() > 0;
}

void goHome()
{
    ui.setPage(0);
    if (!hasAssignedUser()) {
        ui.setScreen(Screen::People);
    } else {
        ui.setScreen(Screen::Projects);
    }
    requestRender(true);
}

// ---------------------------------------------------------------------------
// Data
// ---------------------------------------------------------------------------

bool refreshData()
{
    lastFetchAttemptMs = millis();

    if (!net.online()) {
        fetchError = F("Hors ligne");
        return false;
    }

    std::vector<Person> fetchedPeople;
    if (!api.fetchPeople(fetchedPeople)) {
        fetchError = api.lastError();
        log_w("people fetch failed: %s", fetchError.c_str());
        return false;
    }
    people = std::move(fetchedPeople);

    tracker.setPerson(personId);

    if (!hasAssignedUser()) {
        visibleProjects.clear();
        fetchError = "";
        lastFetchOkMs = millis();
        log_i("membres=%u  (écran non attribué)", (unsigned)people.size());
        return true;
    }

    std::vector<Project> fetched;
    if (!api.fetchProjects(fetched, personId)) {
        fetchError = api.lastError();
        log_w("projects fetch failed: %s", fetchError.c_str());
        return false;
    }

    visibleProjects = std::move(fetched);
    tracker.pull();
    tracker.applyHours(visibleProjects);
    ui.clampPage();

    fetchError = "";
    lastFetchOkMs = millis();
    log_i("projets=%u  membres=%u", (unsigned)visibleProjects.size(), (unsigned)people.size());
    return true;
}

void requestApi(uint32_t jobs)
{
    if (apiTaskHandle) {
        xTaskNotify(apiTaskHandle, jobs, eSetBits);
        return;
    }
    if (jobs & JOB_SESSION) {
        tracker.flushNow();
        tracker.pull();
        tracker.applyHours(visibleProjects);
    }
    if (jobs & JOB_REFRESH) {
        refreshData();
    }
}

void apiWorker(void *)
{
    uint32_t jobs = 0;
    for (;;) {
        BaseType_t got = xTaskNotifyWait(0, 0xFFFFFFFFu, &jobs, pdMS_TO_TICKS(15000));
        tracker.setOnline(net.online());
        if (got != pdTRUE) {
            tracker.loop(net.online());
            if (tracker.takeDirty()) {
                requestRender(false);
            }
            continue;
        }
        if (jobs & JOB_SESSION) {
            tracker.flushNow();
            tracker.pull();
            tracker.applyHours(visibleProjects);
            if (tracker.lastError().length() > 0) {
                fetchError = tracker.lastError();
            } else if (net.online()) {
                fetchError = "";
            }
            requestRender(false);
        }
        if (jobs & JOB_REFRESH) {
            refreshData();
            bootDone = true;
            requestRender(false);
        }
    }
}

// ---------------------------------------------------------------------------
// Status lines
// ---------------------------------------------------------------------------

String fmtAge(uint32_t seconds)
{
    uint32_t minutes = seconds / 60;
    if (minutes < 60) {
        return String(F("il y a ")) + minutes + F(" min");
    }
    return String(F("il y a ")) + (minutes / 60) + F(" h");
}

void updateStatusLines()
{
    String status;
    String hint;
    bool error = false;

    if (!hasAssignedUser()) {
        status = F("Écran non attribué");
        error = true;
    } else if (tracker.active()) {
        status = F("Timer actif");
        hint = tracker.projectName();
        if (hint.length()) {
            hint += F("  ·  ");
        }
        hint += tracker.elapsedLabel();
    } else if (visibleProjects.empty()) {
        status = F("Aucun projet en cours");
    } else {
        size_t n = visibleProjects.size();
        status = String((unsigned)n) + (n == 1 ? F(" projet en cours") : F(" projets en cours"));
    }

    if (!tracker.active()) {
        if (!net.online()) {
            hint = F("Hors ligne, reconnexion en cours");
            error = true;
        } else if (fetchError.length() > 0) {
            hint = fetchError;
            error = true;
        } else if (lastFetchOkMs > 0) {
            uint32_t age = (millis() - lastFetchOkMs) / 1000UL;
            hint = age < 10UL * 60UL ? String(F("Aucun timer"))
                                     : String(F("Données ")) + fmtAge(age);
        } else {
            hint = F("Aucun timer");
        }
    } else if (!net.online()) {
        hint += F("  ·  hors ligne");
        error = true;
    } else if (fetchError.length() > 0) {
        hint += String(F("  ·  ")) + fetchError;
        error = true;
    }

    if (tracker.queuedActions() > 0) {
        String queued = String(F("Sync"));
        hint = hint.length() ? hint + F("  ·  ") + queued : queued;
    }

    ui.setStatus(status, error);
    ui.setHint(hint);
}

void showBusy(const String &message)
{
    ui.setStatus(message, false);
    ui.setNet(net.info());
    ui.render(false);
}

void scanAndShowWifi(bool switchedToWifi)
{
    ui.setWifiNetworks(&net.networks());
    ui.setWifiScanning(true);
    ui.setNet(net.info());
    ui.render(switchedToWifi);

    net.startScan();

    ui.setWifiScanning(false);
    ui.setNet(net.info());
    requestRender(switchedToWifi);
}

void openWifiPicker(bool fromSettings, bool rescan = true)
{
    wifiFromSettings = fromSettings;
    ui.setPage(0);
    ui.setScreen(Screen::Wifi);
    if (rescan) {
        scanAndShowWifi(/*switchedToWifi=*/true);
    } else {
        ui.setWifiScanning(false);
        ui.setWifiNetworks(&net.networks());
        ui.setNet(net.info());
        ui.render(true);
    }
}

void openKeyboard(const String &ssid)
{
    kbSsid = ssid;
    kbPass = "";
    kbShift = false;
    kbSymbols = false;
    kbReveal = false;
    ui.setKeyboard(kbSsid, kbPass, kbShift, kbSymbols, kbReveal);
    ui.setScreen(Screen::Keyboard);
    requestRender(true);
}

void leaveWifi()
{
    if (wifiFromSettings) {
        wifiFromSettings = false;
        ui.setScreen(Screen::Settings);
        requestRender(true);
        return;
    }
    goHome();
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void toggleTimerOnRow(int index)
{
    if (index < 0 || index >= (int)visibleProjects.size()) {
        return;
    }
    Project project = visibleProjects[index];

    tracker.toggle(project);
    lastTimerPaintMs = millis();
    lastSpinPaintMs = millis();
    if (tracker.active()) {
        ui.resetSpin();
    }
    requestRender(false);
    requestApi(JOB_SESSION);
}

void selectPerson(int index)
{
    if (index < 0 || index >= (int)people.size()) {
        return;
    }
    personId = people[index].id;
    personName = people[index].name;
    store.setUser(personId, personName);
    tracker.setPerson(personId);
    ui.setPerson(personId, personName);
    ui.setPage(0);
    ui.setScreen(Screen::Projects);
    ui.setStatus(F("Chargement des projets..."), false);
    requestRender(true);
    requestApi(JOB_REFRESH);
}

void handleWifiPick(int index)
{
    if (index < 0 || index >= (int)net.networks().size()) {
        return;
    }
    const WifiNetwork &chosen = net.networks()[index];
    if (!chosen.secure) {
        net.join(chosen.ssid, "");
        requestRender(false);
        return;
    }
    openKeyboard(chosen.ssid);
}

void typeKey(int code)
{
    if (code <= 0 || kbPass.length() >= 63) {
        return;
    }
    kbPass += (char)code;
    if (kbShift) {
        kbShift = false;
    }
    ui.setKeyboard(kbSsid, kbPass, kbShift, kbSymbols, kbReveal);
    requestRender(false);
}

int pageCountForScreen()
{
    if (ui.screen() == Screen::Wifi) {
        return ui.wifiPageCount();
    }
    if (ui.screen() == Screen::People) {
        int n = (int)people.size();
        return n <= 0 ? 1 : (n + 8) / 9;
    }
    return ui.pageCount();
}

void handleTouch()
{
    int16_t x = 0;
    int16_t y = 0;
    if (!touch.tapped(x, y)) {
        return;
    }

    int index = -1;
    Action action = ui.hitTest(x, y, index);
    if (action != Action::None) {
        log_d("tap %d,%d -> action %d (index %d)", x, y, (int)action, index);
    }

    const int pages = pageCountForScreen();

    switch (action) {
        case Action::None: break;

        case Action::OpenPeople:
            ui.setPage(0);
            ui.setScreen(Screen::People);
            requestRender(true);
            break;

        case Action::PickPerson: selectPerson(index); break;

        case Action::OpenSettings:
            ui.setScreen(Screen::Settings);
            requestRender(true);
            break;

        case Action::Back:
            if (ui.screen() == Screen::Keyboard) {
                ui.setScreen(Screen::Wifi);
                requestRender(true);
            } else if (ui.screen() == Screen::Wifi) {
                leaveWifi();
            } else {
                goHome();
            }
            break;

        case Action::ToggleTimer: toggleTimerOnRow(index); break;

        case Action::PrevPage:
            ui.setPage((ui.page() + pages - 1) % pages);
            requestRender(false);
            break;

        case Action::NextPage:
            ui.setPage((ui.page() + 1) % pages);
            requestRender(false);
            break;

        case Action::Refresh:
            requestApi(JOB_REFRESH);
            break;

        case Action::ToggleTheme: {
            bool dark = !ui.dark();
            store.setDarkTheme(dark);
            ui.setDark(dark);
            requestRender(true);
            break;
        }

        case Action::OpenWifi: openWifiPicker(/*fromSettings=*/true); break;

        case Action::ForgetWifi:
            showBusy(F("Réseau oublié"));
            net.forgetCurrentNetwork();
            openWifiPicker(/*fromSettings=*/true);
            break;

        case Action::CleanScreen:
            ui.cleanGhosting();
            break;

        case Action::WifiPick: handleWifiPick(index); break;

        case Action::WifiRescan:
            scanAndShowWifi(/*switchedToWifi=*/false);
            break;

        case Action::WifiKey: typeKey(index); break;

        case Action::WifiBackspace:
            if (kbPass.length() > 0) {
                kbPass.remove(kbPass.length() - 1);
                ui.setKeyboard(kbSsid, kbPass, kbShift, kbSymbols, kbReveal);
                requestRender(false);
            }
            break;

        case Action::WifiShift:
            kbShift = !kbShift;
            ui.setKeyboard(kbSsid, kbPass, kbShift, kbSymbols, kbReveal);
            requestRender(false);
            break;

        case Action::WifiSymbols:
            kbSymbols = !kbSymbols;
            kbShift = false;
            ui.setKeyboard(kbSsid, kbPass, kbShift, kbSymbols, kbReveal);
            requestRender(false);
            break;

        case Action::WifiReveal:
            kbReveal = !kbReveal;
            ui.setKeyboard(kbSsid, kbPass, kbShift, kbSymbols, kbReveal);
            requestRender(false);
            break;

        case Action::WifiConnect:
            net.join(kbSsid, kbPass);
            requestRender(false);
            break;
    }
}

void followSetupState()
{
    const bool onWifiUi = ui.screen() == Screen::Wifi || ui.screen() == Screen::Keyboard;
    net.setAutoJoin(!onWifiUi);

    if (!onWifiUi && store.networkCount() == 0 && !net.online() && ui.screen() != Screen::Settings) {
        openWifiPicker(/*fromSettings=*/false);
    }

    // Saved credentials exist but none came up (or they all failed): don't
    // sit on the boot splash saying "Connexion au Wi-Fi..." forever.
    if (ui.screen() == Screen::Boot && !net.online() &&
        net.joinState() == WifiPortal::Join::Idle && net.triedAllSaved()) {
        openWifiPicker(/*fromSettings=*/false);
    }

    // Only leave the picker after a join that just succeeded — not because
    // the device was already online when the user opened it from Settings.
    if (net.joinState() == WifiPortal::Join::Success) {
        if (onWifiUi) {
            if (!bootDone) {
                wifiFromSettings = false;
                ui.setScreen(Screen::Boot);
                ui.setStatus(F("Chargement des projets..."), false);
                requestRender(true);
            } else {
                leaveWifi();
            }
        } else if (ui.screen() == Screen::Boot && !bootDone) {
            ui.setStatus(F("Chargement des projets..."), false);
            requestRender(false);
        }
    }

    if (ui.screen() == Screen::Boot && lastFetchAttemptMs > 0 &&
        millis() - lastFetchAttemptMs > BOOT_LEAVE_MS) {
        bootDone = true;
    }

    if (ui.screen() == Screen::Boot && bootDone) {
        goHome();
    }
}

}  // namespace

// ---------------------------------------------------------------------------

void setup()
{
    Serial.begin(115200);
    delay(200);
    Serial.println(F("\nLineup timetracker -- LilyGo T5 4.7\""));

    store.begin();
    api.begin();

    if (!ui.begin()) {
        Serial.println(F("framebuffer allocation failed, check the PSRAM setting"));
        while (true) {
            delay(1000);
        }
    }

    ui.setDark(store.darkTheme());
    ui.setScreen(Screen::Boot);
    ui.setStatus(F("Démarrage..."), false);
    ui.render(true);

    if (!touch.begin()) {
        ui.setHint(F("Dalle tactile introuvable : vérifie la nappe du GT911."));
    }

    personId = store.userId();
    personName = store.userName();

    ui.setPerson(personId, personName);
    ui.setProjects(&visibleProjects);
    ui.setPeople(&people);
    ui.setTracker(&tracker);
    ui.setSavedNetworks(store.networkCount());
    ui.setWifiNetworks(&net.networks());

    tracker.begin(&store, &api);
    tracker.setPerson(personId);
    net.begin(&store);

    xTaskCreate(apiWorker, "api", 12288, nullptr, 1, &apiTaskHandle);

    if (store.networkCount() == 0) {
        ui.setStatus(F("Aucun réseau Wi-Fi enregistré"), false);
        ui.setNet(net.info());
        ui.render(false);
        openWifiPicker(/*fromSettings=*/false);
    } else {
        ui.setStatus(F("Connexion au Wi-Fi..."), false);
        ui.setNet(net.info());
        ui.render(false);
        net.connectSavedNow();
    }
}

void loop()
{
    net.loop();

    if (net.takeChanged()) {
        ui.setNet(net.info());
        ui.setSavedNetworks(store.networkCount());
        ui.setWifiNetworks(&net.networks());
        ui.setWifiScanning(net.scanning());
        ui.setKeyboard(kbSsid, kbPass, kbShift, kbSymbols, kbReveal);
        requestRender(false);
    }

    const bool online = net.online();
    tracker.setOnline(online);

    followSetupState();
    handleTouch();

    const uint32_t fetchEvery =
        (fetchError.length() > 0 || lastFetchOkMs == 0) ? PROJECT_RETRY_MS : PROJECT_REFRESH_MS;
    if (online && (lastFetchAttemptMs == 0 || millis() - lastFetchAttemptMs > fetchEvery)) {
        lastFetchAttemptMs = millis();
        requestApi(JOB_REFRESH);
    }

    if (!renderPending && tracker.active() && ui.screen() == Screen::Projects &&
        millis() - lastSpinPaintMs > SPINNER_MS && !touch.busy()) {
        ui.tickSpin();
        requestRender(false, false);
    }

    if (tracker.active() && millis() - lastTimerPaintMs > TIMER_TICK_MS) {
        lastTimerPaintMs = millis();
        requestRender(false);
    }

    if (renderPending) {
        renderPending = false;
        bool full = renderFull;
        renderFull = false;
        bool countPartial = renderCountPartial;
        renderCountPartial = true;

        NetInfo info = net.info();
        if (ui.screen() != Screen::Boot) {
            updateStatusLines();
        } else if (info.note.length() > 0) {
            ui.setHint(info.note);
        }
        ui.setNet(info);
        ui.setSavedNetworks(store.networkCount());
        ui.setWifiScanning(net.scanning());
        ui.setDataReady(lastFetchOkMs > 0);
        ui.render(full, countPartial);
        lastSpinPaintMs = millis();
    }

    delay(5);
}
