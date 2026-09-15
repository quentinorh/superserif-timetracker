#pragma once

#include <Arduino.h>

#include <vector>

#include "Model.h"
#include "Tracker.h"
#include "epd_driver.h"
#include "ui_icons.h"

enum class Screen : uint8_t { Boot, Projects, People, Settings, Wifi, Keyboard };

enum class Action : uint8_t {
    None,
    OpenPeople,
    PickPerson,
    OpenSettings,
    Back,
    ToggleTimer,
    PrevPage,
    NextPage,
    Refresh,
    OpenWifi,
    ForgetWifi,
    CleanScreen,
    WifiPick,
    WifiRescan,
    WifiKey,
    WifiBackspace,
    WifiShift,
    WifiSymbols,
    WifiReveal,
    WifiConnect,
    ToggleTheme,
};

class Ui {
public:
    bool begin();

    void setScreen(Screen screen);
    Screen screen() const { return _screen; }

    void setProjects(const std::vector<Project> *projects) { _projects = projects; }
    void setPeople(const std::vector<Person> *people) { _people = people; }
    void setPerson(const String &id, const String &name);
    void setNet(const NetInfo &net) { _net = net; }
    void setTracker(const Tracker *tracker) { _tracker = tracker; }
    void setStatus(const String &text, bool error);
    void setHint(const String &text) { _hint = text; }
    void setDataReady(bool ready) { _dataReady = ready; }
    void setSavedNetworks(uint8_t count) { _savedNetworks = count; }
    void setDark(bool dark);
    bool dark() const { return _dark; }

    void setWifiNetworks(const std::vector<WifiNetwork> *networks) { _wifiNets = networks; }
    void setWifiScanning(bool scanning) { _wifiScanning = scanning; }
    void setKeyboard(const String &ssid, const String &password, bool shift, bool symbols,
                     bool reveal);

    int rowsPerPage() const;
    int pageCount() const;
    int page() const { return _page; }
    void setPage(int page);
    void clampPage();

    int wifiRowsPerPage() const;
    int wifiPageCount() const;

    void render(bool forceFull = false, bool countPartial = true);
    void cleanGhosting();
    void tickSpin();
    void resetSpin();

    Action hitTest(int16_t x, int16_t y, int &index) const;

private:
    struct Hit {
        int16_t x, y, w, h;
        Action action;
        int index;
    };

    enum class Align : uint8_t { Left, Center, Right };

    void paintBoot();
    void paintProjects();
    void paintPeople();
    void paintSettings();
    void paintWifi();
    void paintKeyboard();

    void paintHeader();
    void paintSubHeader(const String &title, const String &subtitle, bool back, bool refresh,
                        bool theme = false);
    void paintCornerButton(int fromRight, const UiIcon &icon, Action action);
    void paintProjectRow(int row, const Project &project, bool running, int projectIndex);
    void paintProgressBar(int x, int y, int w, int h, int pct, bool invert = false);
    void paintPlayButton(int cx, int cy, bool running);
    void paintButton(int x, int y, int w, int h, const String &label, Action action, int index,
                     bool emphasised = false);
    void paintIconButton(int x, int y, int w, int h, Action action);
    void paintIcon(const UiIcon &icon, int bx, int by, int bw, int bh);
    void paintFooterRule();
    void paintKey(int x, int y, int w, int h, const String &label, Action action, int index,
                  bool emphasised = false);

    void clearBuffer();
    void pushBand(int top, int bottom);
    bool playButtonRect(int &x, int &y, int &w, int &h) const;
    bool differsOutside(int x, int y, int w, int h) const;
    void pushRect(int x, int y, int w, int h);
    int textWidth(const GFXfont &font, const String &text) const;
    int centreBaseline(const GFXfont &font, int y, int h) const;
    void text(const GFXfont &font, const String &value, int x, int baseline, Align align,
              uint8_t ink4 = 16, uint8_t paper4 = 16);
    void textAsIs(const GFXfont &font, const String &value, int x, int baseline, Align align,
                 uint8_t ink4 = 16, uint8_t paper4 = 16);
    void writeGlyphs(const GFXfont &font, const String &value, int x, int baseline, Align align,
                     uint8_t ink4, uint8_t paper4);
    String fit(const GFXfont &font, const String &value, int maxWidth) const;
    void addHit(int x, int y, int w, int h, Action action, int index);

    void push(bool forceFull, bool countPartial);

    const std::vector<Project> *_projects = nullptr;
    const std::vector<Person> *_people = nullptr;
    const std::vector<WifiNetwork> *_wifiNets = nullptr;
    const Tracker *_tracker = nullptr;

    uint8_t *_buffer = nullptr;
    uint8_t *_onGlass = nullptr;
    uint8_t *_scratch = nullptr;

    Screen _screen = Screen::Boot;
    NetInfo _net;
    String _personId;
    String _personName;
    String _status;
    String _hint;
    bool _statusIsError = false;
    bool _dataReady = false;
    bool _dark = false;
    uint8_t _savedNetworks = 0;
    int _page = 0;

    bool _wifiScanning = false;
    String _kbSsid;
    String _kbPass;
    bool _kbShift = false;
    bool _kbSymbols = false;
    bool _kbReveal = false;

    std::vector<Hit> _hits;
    uint8_t _partialPushes = 0;
    uint32_t _lastFullPushMs = 0;
    bool _glassValid = false;
    uint8_t _spinStep = 0;
};
