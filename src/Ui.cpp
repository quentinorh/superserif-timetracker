#include "Ui.h"

#include <math.h>
#include <string.h>
#include <cstring>

#include "Config.h"
#include "ui_fonts.h"

namespace {

// ------------------------------------------------------------- geometry ----

constexpr int W = EPD_WIDTH;   // 960
constexpr int H = EPD_HEIGHT;  // 540
constexpr int PAD = 24;
constexpr int PAD_LEFT = PAD - 3;    // 21
constexpr int PAD_RIGHT = PAD - 6;   // 18
constexpr int CONTENT_W = W - PAD - PAD_RIGHT;
constexpr int MAIN_CONTENT_W = W - PAD_LEFT - PAD_RIGHT;

constexpr int HEADER_PAD = 16;
constexpr int ICON_BTN = 64;
constexpr int ICON_GAP = 8;
constexpr int HEADER_BOTTOM = HEADER_PAD + ICON_BTN;  // 80
constexpr int ROW_TOP = HEADER_BOTTOM + 10;           // 88
constexpr int ROWS_PER_PAGE = 3;
constexpr int BOTTOM_MARGIN = 16;
constexpr int ROW_AREA = H - ROW_TOP - BOTTOM_MARGIN;
constexpr int ROW_STRIDE = ROW_AREA / ROWS_PER_PAGE;
constexpr int ROW_H = ROW_STRIDE - 8;
constexpr int WIFI_ROWS_PER_PAGE = 6;

constexpr int PLAY_R = 34;           // 26 * 1.3
constexpr int PLAY_CX = 73;          // 56 * 1.3
constexpr int TEXT_X_OFF = 145;      // 100 * 1.3, plus 5 px after play/pause

constexpr int FOOTER_RULE_Y = 464;
constexpr int BTN_Y = 472;
constexpr int BTN_H = 48;
constexpr int STROKE = 3;

// ---------------------------------------------------------------- inks -----
// 8-bit for the pixel primitives, 4-bit for the font renderer.
// Mutated by applyPalette() so the same paint path works in both themes.

uint8_t INK = 0x00;
uint8_t PAPER = 0xFF;

uint8_t T_INK = 0;
uint8_t T_PAPER = 15;

void applyPalette(bool dark)
{
    if (dark) {
        INK = 0xFF;
        PAPER = 0x00;
        T_INK = 15;
        T_PAPER = 0;
    } else {
        INK = 0x00;
        PAPER = 0xFF;
        T_INK = 0;
        T_PAPER = 15;
    }
}

// Rows have to be merged into few bands: every band costs a fixed ~250 ms of
// waveform overhead, so a handful of tall bands beats many thin ones.
constexpr int BAND_MERGE_GAP = 64;
constexpr size_t MAX_BANDS = 3;

// Full-width strips keep packed 4bpp rows aligned. Cropping in X used to
// cut through glyphs because the driver expects `area.width` to match the
// buffer stride.
//
// epd_draw_grayscale_image can only darken (BLACK_ON_WHITE). WHITE_ON_WHITE
// lightens the dark pixels of the image it is given — LilyGo/epdiy's way to
// undraw without epd_clear_area, which inverts the whole band (see
// epd_driver.h DrawMode_t and LilyGo-EPD47#52). Unchanged pixels are packed
// as 0xF so the waveform is a no-op there.
int snapBandTop(int y)
{
    if (y < ROW_TOP) {
        return 0;
    }
    int index = (y - ROW_TOP) / ROW_STRIDE;
    if (index < 0) {
        index = 0;
    }
    if (index >= ROWS_PER_PAGE) {
        return ROW_TOP + ROWS_PER_PAGE * ROW_STRIDE;
    }
    return ROW_TOP + index * ROW_STRIDE;
}

int snapBandBottom(int yExclusive)
{
    int y = yExclusive - 1;
    if (y < ROW_TOP) {
        return ROW_TOP;
    }
    int index = (y - ROW_TOP) / ROW_STRIDE;
    if (index < 0) {
        return ROW_TOP;
    }
    // Last row (and the leftover margin) goes to the bottom of the panel:
    // the main list has no footer bar anymore.
    if (index >= ROWS_PER_PAGE - 1) {
        return H;
    }
    return ROW_TOP + (index + 1) * ROW_STRIDE;
}

// 4bpp packed: high nibble = odd x, low nibble = even x. 0 = black, 15 = paper.
// WHITE_ON_WHITE lightens dark nibbles; BLACK_ON_WHITE darkens them. 0xF is a
// no-op in both modes, so unchanged pixels stay off the waveform.
uint8_t diffNibble(uint8_t oldN, uint8_t newN, bool lighten)
{
    if (oldN == newN) {
        return 0xF;
    }
    return lighten ? oldN : newN;
}

void strokeRect(int x, int y, int w, int h, uint8_t color, uint8_t *fb)
{
    for (int i = 0; i < STROKE; i++) {
        int ww = w - 2 * i;
        int hh = h - 2 * i;
        if (ww <= 0 || hh <= 0) {
            break;
        }
        epd_draw_rect(x + i, y + i, ww, hh, color, fb);
    }
}

void strokeCircle(int cx, int cy, int r, uint8_t color, uint8_t *fb)
{
    for (int i = 0; i < STROKE; i++) {
        if (r - i <= 0) {
            break;
        }
        epd_draw_circle(cx, cy, r - i, color, fb);
    }
}

void strokeHLine(int y, uint8_t color, uint8_t *fb)
{
    epd_fill_rect(0, y, W, STROKE, color, fb);
}

bool buildDiffBand(uint8_t *dst, const uint8_t *oldp, const uint8_t *newp, size_t bytes,
                   bool lighten)
{
    bool any = false;
    for (size_t i = 0; i < bytes; i++) {
        uint8_t o = oldp[i];
        uint8_t n = newp[i];
        uint8_t lo = diffNibble(o & 0x0F, n & 0x0F, lighten);
        uint8_t hi = diffNibble(o >> 4, n >> 4, lighten);
        dst[i] = (uint8_t)(lo | (hi << 4));
        if (dst[i] != 0xFF) {
            any = true;
        }
    }
    return any;
}

// ------------------------------------------------------------ formatting ---

String fmtHours(float hours)
{
    return Tracker::formatHoursMinutes(hours);
}

String fmtTotal(float hours)
{
    return Tracker::formatHoursMinutes(hours);
}

String fmtElapsed(uint32_t seconds)
{
    return Tracker::formatHoursMinutes((float)seconds / 3600.0f);
}

// Latin-1 aware uppercase so é/à/ç survive as É/À/Ç. Anything outside
// ASCII + Latin-1 is copied through: the font cannot render it anyway.
String toUpperFr(const String &value)
{
    String out;
    out.reserve(value.length());
    const unsigned n = value.length();
    for (unsigned i = 0; i < n;) {
        uint8_t c = (uint8_t)value[i];
        if (c < 0x80) {
            if (c >= 'a' && c <= 'z') {
                c = (uint8_t)(c - 32);
            }
            out += (char)c;
            i++;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < n) {
            uint8_t d = (uint8_t)value[i + 1];
            if (c == 0xC3 && d >= 0xA0 && d <= 0xBE) {
                d = (uint8_t)(d - 0x20);
            }
            out += (char)c;
            out += (char)d;
            i += 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < n) {
            out += (char)c;
            out += value[i + 1];
            out += value[i + 2];
            i += 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < n) {
            out += value.substring(i, i + 4);
            i += 4;
        } else {
            out += (char)c;
            i++;
        }
    }
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

bool Ui::begin()
{
    const size_t bytes = (size_t)W * H / 2;
    _buffer = (uint8_t *)ps_calloc(1, bytes);
    _onGlass = (uint8_t *)ps_calloc(1, bytes);
    _scratch = (uint8_t *)ps_calloc(1, bytes);
    if (!_buffer || !_onGlass) {
        log_e("framebuffer allocation failed (needs PSRAM)");
        return false;
    }
    if (!_scratch) {
        log_w("diff scratch allocation failed; partials will undraw the whole band");
    }

    epd_init();
    clearBuffer();
    memset(_onGlass, 0xFF, bytes);
    return true;
}

void Ui::tickSpin()
{
    _spinStep = (uint8_t)((_spinStep + 1) % 16);
}

void Ui::resetSpin()
{
    _spinStep = 0;
}

void Ui::clearBuffer()
{
    applyPalette(_dark);
    memset(_buffer, PAPER, (size_t)W * H / 2);
}

void Ui::setDark(bool dark)
{
    if (_dark != dark) {
        _glassValid = false;
    }
    _dark = dark;
}

void Ui::setScreen(Screen screen)
{
    _screen = screen;
}

void Ui::setPerson(const String &id, const String &name)
{
    _personId = id;
    _personName = name;
}

void Ui::setKeyboard(const String &ssid, const String &password, bool shift, bool symbols,
                     bool reveal)
{
    _kbSsid = ssid;
    _kbPass = password;
    _kbShift = shift;
    _kbSymbols = symbols;
    _kbReveal = reveal;
}

void Ui::setStatus(const String &text, bool error)
{
    _status = text;
    _statusIsError = error;
}

// ---------------------------------------------------------------------------
// Pagination
// ---------------------------------------------------------------------------

int Ui::rowsPerPage() const
{
    return ROWS_PER_PAGE;
}

int Ui::pageCount() const
{
    // The main list shows at most three rows and has no pager.
    return 1;
}

int Ui::wifiRowsPerPage() const
{
    return WIFI_ROWS_PER_PAGE;
}

int Ui::wifiPageCount() const
{
    int total = _wifiNets ? (int)_wifiNets->size() : 0;
    if (total <= 0) {
        return 1;
    }
    return (total + WIFI_ROWS_PER_PAGE - 1) / WIFI_ROWS_PER_PAGE;
}

void Ui::setPage(int page)
{
    _page = page;
    clampPage();
}

void Ui::clampPage()
{
    int pages = 1;
    if (_screen == Screen::Wifi) {
        pages = wifiPageCount();
    } else if (_screen == Screen::People) {
        int n = _people ? (int)_people->size() : 0;
        pages = n <= 0 ? 1 : (n + 8) / 9;  // 9 cards per page
    } else {
        pages = pageCount();
    }
    if (_page >= pages) {
        _page = pages - 1;
    }
    if (_page < 0) {
        _page = 0;
    }
}

// ---------------------------------------------------------------------------
// Text primitives
// ---------------------------------------------------------------------------

int Ui::textWidth(const GFXfont &font, const String &value) const
{
    String shown = toUpperFr(value);
    if (shown.length() == 0) {
        return 0;
    }
    // get_text_bounds walks the string and advances the cursor, so the cursor
    // is the layout width (the reported `w` is only the inked extent).
    int32_t cursorX = 0, cursorY = 0, x1 = 0, y1 = 0, w = 0, h = 0;
    get_text_bounds(&font, shown.c_str(), &cursorX, &cursorY, &x1, &y1, &w, &h, nullptr);
    return (int)cursorX;
}

int Ui::centreBaseline(const GFXfont &font, int y, int h) const
{
    int glyphHeight = font.ascender - font.descender;  // descender is negative
    return y + (h - glyphHeight) / 2 + font.ascender;
}

void Ui::writeGlyphs(const GFXfont &font, const String &value, int x, int baseline, Align align,
                     uint8_t ink4, uint8_t paper4)
{
    if (value.length() == 0) {
        return;
    }
    if (ink4 > 15) {
        ink4 = T_INK;
    }
    if (paper4 > 15) {
        paper4 = T_PAPER;
    }

    int32_t cursorX = x;
    if (align != Align::Left) {
        int32_t cx = 0, cy = 0, x1 = 0, y1 = 0, w = 0, h = 0;
        get_text_bounds(&font, value.c_str(), &cx, &cy, &x1, &y1, &w, &h, nullptr);
        cursorX -= (align == Align::Center) ? (int)cx / 2 : (int)cx;
    }
    int32_t cursorY = baseline;

    FontProperties props{};
    props.fg_color = ink4;
    props.bg_color = paper4;
    props.fallback_glyph = '?';
    props.flags = 0;

    write_mode(&font, value.c_str(), &cursorX, &cursorY, _buffer, BLACK_ON_WHITE, &props);
}

void Ui::text(const GFXfont &font, const String &value, int x, int baseline, Align align,
              uint8_t ink4, uint8_t paper4)
{
    writeGlyphs(font, toUpperFr(value), x, baseline, align, ink4, paper4);
}

void Ui::textAsIs(const GFXfont &font, const String &value, int x, int baseline, Align align,
                 uint8_t ink4, uint8_t paper4)
{
    writeGlyphs(font, value, x, baseline, align, ink4, paper4);
}

String Ui::fit(const GFXfont &font, const String &value, int maxWidth) const
{
    if (textWidth(font, value) <= maxWidth) {
        return value;
    }

    const String ellipsis = "...";
    int ellipsisWidth = textWidth(font, ellipsis);
    int len = value.length();
    while (len > 0) {
        len--;
        // Never cut in the middle of a UTF-8 sequence.
        while (len > 0 && ((uint8_t)value[len] & 0xC0) == 0x80) {
            len--;
        }
        String cut = value.substring(0, len);
        if (textWidth(font, cut) + ellipsisWidth <= maxWidth) {
            return cut + ellipsis;
        }
    }
    return ellipsis;
}

void Ui::addHit(int x, int y, int w, int h, Action action, int index)
{
    _hits.push_back({(int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h, action, index});
}

Action Ui::hitTest(int16_t x, int16_t y, int &index) const
{
    // A little slop: fingers are wide and the panel has a bezel offset.
    constexpr int SLOP = 6;
    for (const Hit &hit : _hits) {
        if (x >= hit.x - SLOP && x <= hit.x + hit.w + SLOP && y >= hit.y - SLOP &&
            y <= hit.y + hit.h + SLOP) {
            index = hit.index;
            return hit.action;
        }
    }
    index = -1;
    return Action::None;
}

// ---------------------------------------------------------------------------
// Shared chrome
// ---------------------------------------------------------------------------

void Ui::paintHeader()
{
    constexpr int chipW = 280;
    const int btnY = HEADER_PAD;
    const int btnH = ICON_BTN;
    const int settingsX = W - PAD_RIGHT - ICON_BTN;

    strokeRect(PAD_LEFT, btnY, chipW, btnH, INK, _buffer);
    epd_fill_circle(PAD_LEFT + 28, btnY + btnH / 2, 7, INK, _buffer);

    String who = _personName.length() ? _personName : String(F("Attribuer"));
    text(UiBody, fit(UiBody, who, chipW - 96), PAD_LEFT + 48, centreBaseline(UiBody, btnY, btnH),
         Align::Left);
    epd_fill_triangle(PAD_LEFT + chipW - 36, btnY + btnH / 2 - 6, PAD_LEFT + chipW - 20,
                      btnY + btnH / 2 - 6, PAD_LEFT + chipW - 28, btnY + btnH / 2 + 8, INK, _buffer);
    addHit(PAD_LEFT, btnY, chipW, btnH, Action::OpenPeople, 0);

    paintIconButton(settingsX, btnY, ICON_BTN, btnH, Action::OpenSettings);
    paintIcon(IconSettings, settingsX, btnY, ICON_BTN, btnH);

    const int statusX = PAD_LEFT + chipW + 16;
    const int statusW = settingsX - 16 - statusX;
    text(UiSmall, fit(UiSmall, _status, statusW), statusX, btnY + 28, Align::Left, T_INK);
    text(UiSmall, fit(UiSmall, _hint, statusW), statusX, btnY + 52, Align::Left, T_INK);
}

void Ui::paintCornerButton(int fromRight, const UiIcon &icon, Action action)
{
    const int x = W - PAD_RIGHT - ICON_BTN - fromRight * (ICON_BTN + ICON_GAP);
    paintIconButton(x, HEADER_PAD, ICON_BTN, ICON_BTN, action);
    paintIcon(icon, x, HEADER_PAD, ICON_BTN, ICON_BTN);
}

void Ui::paintSubHeader(const String &title, const String &subtitle, bool back, bool refresh,
                        bool theme)
{
    int fromRight = 0;
    if (back) {
        paintCornerButton(fromRight, IconBack, Action::Back);
        fromRight++;
    }
    if (theme) {
        paintCornerButton(fromRight, _dark ? IconMoon : IconSun, Action::ToggleTheme);
        fromRight++;
    }
    if (refresh) {
        paintCornerButton(fromRight, IconRefresh, Action::WifiRescan);
        fromRight++;
    }

    const int titleRight = W - PAD_RIGHT - fromRight * (ICON_BTN + ICON_GAP) - 16;
    const int budget = titleRight - PAD;
    if (subtitle.length() > 0) {
        text(UiTitle, fit(UiTitle, title, budget), PAD, HEADER_PAD + 30, Align::Left);
        text(UiSmall, fit(UiSmall, subtitle, budget), PAD, HEADER_PAD + 56, Align::Left, T_INK);
    } else {
        text(UiTitle, fit(UiTitle, title, budget), PAD,
             centreBaseline(UiTitle, HEADER_PAD, ICON_BTN), Align::Left);
    }
}

void Ui::paintFooterRule()
{
    strokeHLine(FOOTER_RULE_Y, INK, _buffer);
}

void Ui::paintButton(int x, int y, int w, int h, const String &label, Action action, int index,
                     bool emphasised)
{
    if (emphasised) {
        epd_fill_rect(x, y, w, h, INK, _buffer);
        text(UiBody, fit(UiBody, label, w - 24), x + w / 2, centreBaseline(UiBody, y, h),
             Align::Center, T_PAPER, T_INK);
    } else {
        strokeRect(x, y, w, h, INK, _buffer);
        text(UiBody, fit(UiBody, label, w - 24), x + w / 2, centreBaseline(UiBody, y, h),
             Align::Center, T_INK);
    }
    addHit(x, y, w, h, action, index);
}

void Ui::paintIconButton(int x, int y, int w, int h, Action action)
{
    strokeRect(x, y, w, h, INK, _buffer);
    addHit(x, y, w, h, action, 0);
}

void Ui::paintIcon(const UiIcon &icon, int bx, int by, int bw, int bh)
{
    const int x0 = bx + (bw - icon.w) / 2;
    const int y0 = by + (bh - icon.h) / 2;
    for (int y = 0; y < icon.h; y++) {
        for (int x = 0; x < icon.w; x++) {
            uint8_t byte = icon.bits[y * icon.stride + (x >> 3)];
            if (byte & (0x80 >> (x & 7))) {
                epd_draw_pixel(x0 + x, y0 + y, INK, _buffer);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Project list
// ---------------------------------------------------------------------------

void Ui::paintPlayButton(int cx, int cy, bool running)
{
    if (running) {
        epd_fill_rect(cx - 12, cy - 16, 9, 32, PAPER, _buffer);
        epd_fill_rect(cx + 4, cy - 16, 9, 32, PAPER, _buffer);

        constexpr int dots = 16;
        constexpr int dotR = 3;
        constexpr float ring = (float)PLAY_R - (float)STROKE / 2.0f;
        const int lit = _spinStep % dots;
        for (int i = 0; i < dots; i++) {
            const float angle = (float)i * (2.0f * (float)M_PI / (float)dots) - (float)M_PI / 2.0f;
            const int dx = (int)lroundf(cosf(angle) * ring);
            const int dy = (int)lroundf(sinf(angle) * ring);
            if (i == lit) {
                epd_fill_circle(cx + dx, cy + dy, dotR, INK, _buffer);
            } else {
                epd_fill_circle(cx + dx, cy + dy, dotR, PAPER, _buffer);
            }
        }
    } else {
        strokeCircle(cx, cy, PLAY_R, INK, _buffer);
        epd_fill_triangle(cx - 10, cy - 18, cx - 10, cy + 18, cx + 18, cy, INK, _buffer);
    }
}

void Ui::paintProgressBar(int x, int y, int w, int h, int pct, bool invert)
{
    const uint8_t fg = invert ? PAPER : INK;
    if (pct < 0) {
        for (int i = 0; i + 8 <= w; i += 16) {
            epd_fill_rect(x + i, y + h / 2 - 2, 8, 4, fg, _buffer);
        }
        return;
    }

    strokeRect(x, y, w, h, fg, _buffer);
    int inner = h - 2 * STROKE;
    int fill = (int)((long)(w - 2 * STROKE) * (pct > 100 ? 100 : pct) / 100);
    if (fill > 0 && inner > 0) {
        epd_fill_rect(x + STROKE, y + STROKE, fill, inner, fg, _buffer);
    }
    if (pct > 100) {
        uint8_t hatch = invert ? INK : PAPER;
        for (int i = 4; i + 3 <= w; i += 16) {
            epd_fill_rect(x + i, y + STROKE, 3, inner > 0 ? inner : h, hatch, _buffer);
        }
    }
}

void Ui::paintProjectRow(int row, const Project &project, bool running, int projectIndex)
{
    const int y = ROW_TOP + row * ROW_STRIDE;
    const uint8_t fg = running ? T_PAPER : T_INK;
    const uint8_t bg = running ? T_INK : T_PAPER;

    if (running) {
        epd_fill_rect(PAD_LEFT, y, MAIN_CONTENT_W, ROW_H, INK, _buffer);
    } else {
        strokeRect(PAD_LEFT, y, MAIN_CONTENT_W, ROW_H, INK, _buffer);
    }

    paintPlayButton(PAD_LEFT + PLAY_CX, y + ROW_H / 2, running);
    addHit(PAD_LEFT, y, MAIN_CONTENT_W, ROW_H, Action::ToggleTimer, projectIndex);

    float done = (running && _tracker) ? _tracker->projectedDone() : project.done;
    int pct = project.progressPct(done);

    String value = fmtHours(done);
    if (project.hasTotal) {
        value += String(" / ") + fmtTotal(project.total);
    }

    const int textX = PAD_LEFT + TEXT_X_OFF;
    const int right = PAD_LEFT + MAIN_CONTENT_W - 16;
    String pctStr = pct >= 0 ? (String(pct) + "%") : String("NC");
    const int pctW = textWidth(UiDisplay, pctStr);
    const int nameBudget = right - pctW - 24 - textX - (project.moonmoon ? 56 : 0);
    const GFXfont &nameFont =
        textWidth(UiTitle, project.name) <= nameBudget ? UiTitle : UiBodyBold;
    String name = fit(nameFont, project.name, nameBudget > 80 ? nameBudget : 80);
    text(nameFont, name, textX, y + 44, Align::Left, fg, bg);

    if (project.moonmoon) {
        int tagX = textX + textWidth(nameFont, name) + 12;
        strokeRect(tagX, y + 22, 40, 26, running ? PAPER : INK, _buffer);
        text(UiMicro, "MM", tagX + 20, y + 41, Align::Center, fg, bg);
    }

    text(UiDisplay, pctStr, right, y + 52, Align::Right, fg, bg);

    const int metaY = y + ROW_H / 2 + 14;
    text(UiBody, value, textX, metaY, Align::Left, fg, bg);

    paintProgressBar(textX, y + ROW_H - 30, right - textX, 14, pct, running);
}

void Ui::paintProjects()
{
    paintHeader();

    const int total = _projects ? (int)_projects->size() : 0;
    if (total == 0) {
        const int mid = ROW_TOP + ROW_AREA / 2;
        text(UiTitle, F("Aucun projet en cours"), W / 2, mid - 16, Align::Center, T_INK);
        String empty = _personName.length()
                           ? String(F("Aucun projet « En cours » pour ")) + _personName + "."
                           : String(F("Attribue d'abord cet écran à un membre."));
        text(UiBody, fit(UiBody, empty, CONTENT_W), W / 2, mid + 24, Align::Center, T_INK);
    } else {
        const int show = total < ROWS_PER_PAGE ? total : ROWS_PER_PAGE;
        for (int row = 0; row < show; row++) {
            const Project &project = (*_projects)[row];
            bool running =
                _tracker && _tracker->active() && _tracker->projectId() == project.id;
            paintProjectRow(row, project, running, row);
        }
    }
}

// ---------------------------------------------------------------------------
// Person picker
// ---------------------------------------------------------------------------

void Ui::paintPeople()
{
    const int people = _people ? (int)_people->size() : 0;
    const int first = _page * 9;
    const int remaining = people - first;
    const int cards = remaining > 9 ? 9 : (remaining > 0 ? remaining : 0);

    paintSubHeader(F("Utilisateur de cet écran"),
                   F("Un écran = un membre. Modifiable ici à tout moment."),
                   _personId.length() > 0, false);

    constexpr int cardH = 100;
    constexpr int cardGap = 24;
    constexpr int cardCols = 3;
    const int gridLeft = PAD;
    const int gridRight = W - PAD_RIGHT;
    const int cardW = (gridRight - gridLeft - (cardCols - 1) * cardGap) / cardCols;
    const int xs[3] = {gridLeft, gridLeft + cardW + cardGap,
                       gridLeft + 2 * (cardW + cardGap)};
    constexpr int ys[3] = {96, 216, 336};

    if (people == 0) {
        if (!_dataReady && !_statusIsError) {
            text(UiBody, F("Chargement des membres..."), W / 2, 250, Align::Center, T_INK);
            text(UiSmall, F("Synchronisation avec Lineup"), W / 2, 286, Align::Center, T_INK);
        } else if (_statusIsError) {
            text(UiBody, F("Impossible de charger les membres"), W / 2, 250, Align::Center, T_INK);
            text(UiSmall, fit(UiSmall, _hint.length() ? _hint : _status, CONTENT_W - 48), W / 2, 286,
                 Align::Center, T_INK);
        } else {
            text(UiBody, F("Aucun membre dans les projets en cours."), W / 2, 250, Align::Center,
                 T_INK);
            text(UiSmall, F("Personne n'est encore dans l'équipe Lineup."), W / 2, 286,
                 Align::Center, T_INK);
        }
    }

    for (int i = 0; i < cards; i++) {
        const int x = xs[i % 3];
        const int y = ys[i / 3];
        const Person &person = (*_people)[first + i];
        const bool selected = person.id == _personId;
        const GFXfont &nameFont =
            textWidth(UiTitle, person.name) <= cardW - 56 ? UiTitle : UiBody;

        if (selected) {
            epd_fill_rect(x, y, cardW, cardH, INK, _buffer);
            strokeRect(x, y, cardW, cardH, PAPER, _buffer);
            epd_fill_circle(x + cardW - 24, y + 24, 6, PAPER, _buffer);
            text(nameFont, fit(nameFont, person.name, cardW - 56), x + cardW / 2, y + 58,
                 Align::Center, T_PAPER, T_INK);
        } else {
            strokeRect(x, y, cardW, cardH, INK, _buffer);
            text(nameFont, fit(nameFont, person.name, cardW - 56), x + cardW / 2, y + 58,
                 Align::Center);
        }

        addHit(x, y, cardW, cardH, Action::PickPerson, first + i);
    }

    if (people > 9) {
        int pages = (people + 8) / 9;
        paintButton(PAD, BTN_Y, 64, BTN_H, "<", Action::PrevPage, 0);
        text(UiSmall, String(_page + 1) + " / " + String(pages), 128,
             centreBaseline(UiSmall, BTN_Y, BTN_H), Align::Center, T_INK);
        paintButton(168, BTN_Y, 64, BTN_H, ">", Action::NextPage, 0);
    }
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

void Ui::paintSettings()
{
    paintSubHeader(F("Réglages"), String(), true, false, true);

    struct Line {
        String label;
        String value;
    };

    Line lines[] = {
        {F("Utilisateur"), _personName.length() ? _personName : String(F("non attribué"))},
        {F("Réseau Wi-Fi"), _net.connected ? _net.ssid : String(F("non connecté"))},
        {F("Adresse IP"), _net.connected ? _net.ip : String("-")},
        {F("Signal"), _net.connected ? (String(_net.rssi) + " dBm") : String("-")},
        {F("Accès Internet"), _net.internet ? String(F("oui")) : String(F("non"))},
        {F("Réseaux enregistrés"), String(_savedNetworks)},
        {F("API"), String(API_HOST)},
        {F("Actions en attente"),
         _tracker ? String((unsigned)_tracker->queuedActions()) : String("0")},
        {F("Mémoire libre"), String(ESP.getFreeHeap() / 1024) + F(" Ko  ·  PSRAM ") +
                                 String(ESP.getFreePsram() / 1024) + F(" Ko")},
    };

    int y = HEADER_BOTTOM + 24;
    for (const Line &line : lines) {
        text(UiSmall, line.label, PAD, y, Align::Left, T_INK);
        text(UiBody, fit(UiBody, line.value, 540), 380, y, Align::Left, T_INK);
        y += 40;
    }

    paintButton(PAD, 463, 288, 52, F("Choisir un Wi-Fi"), Action::OpenWifi, 0);
    paintButton(336, 463, 288, 52, F("Oublier ce réseau"), Action::ForgetWifi, 0);
    paintButton(648, 463, 288, 52, F("Nettoyer l'écran"), Action::CleanScreen, 0);
}

// ---------------------------------------------------------------------------
// Wi-Fi onboarding
// ---------------------------------------------------------------------------

void Ui::paintWifi()
{
    String sub = _net.note.length() ? _net.note
                                    : (_wifiScanning ? String(F("Recherche des réseaux..."))
                                                     : String(F("Appuie sur un réseau pour te connecter.")));
    paintSubHeader(F("Wi-Fi"), sub, true, true);

    const int total = _wifiNets ? (int)_wifiNets->size() : 0;
    const int first = _page * WIFI_ROWS_PER_PAGE;
    constexpr int rowH = 52;
    constexpr int listTop = HEADER_BOTTOM + 14;

    if (total == 0 && !_wifiScanning) {
        text(UiBody, F("Aucun réseau trouvé."), W / 2, 250, Align::Center, T_INK);
    }

    for (int row = 0; row < WIFI_ROWS_PER_PAGE; row++) {
        int index = first + row;
        if (index >= total) {
            break;
        }
        const WifiNetwork &net = (*_wifiNets)[index];
        int y = listTop + row * rowH;
        strokeRect(PAD, y, CONTENT_W, rowH - 6, INK, _buffer);
        text(UiBody, fit(UiBody, net.ssid, 620), PAD + 20, centreBaseline(UiBody, y, rowH - 6),
             Align::Left);
        String meta = (net.secure ? String(F("protégé  ·  ")) : String(F("ouvert  ·  "))) +
                      String(net.rssi) + " dBm";
        text(UiSmall, meta, PAD + CONTENT_W - 16, centreBaseline(UiSmall, y, rowH - 6), Align::Right,
             T_INK);
        addHit(PAD, y, CONTENT_W, rowH - 6, Action::WifiPick, index);
    }

    if (wifiPageCount() > 1) {
        paintButton(PAD, BTN_Y, 64, BTN_H, "<", Action::PrevPage, 0);
        text(UiSmall, String(_page + 1) + " / " + String(wifiPageCount()), 128,
             centreBaseline(UiSmall, BTN_Y, BTN_H), Align::Center, T_INK);
        paintButton(168, BTN_Y, 64, BTN_H, ">", Action::NextPage, 0);
    }
}

void Ui::paintKey(int x, int y, int w, int h, const String &label, Action action, int index,
                  bool emphasised)
{
    if (emphasised) {
        epd_fill_rect(x, y, w, h, INK, _buffer);
        text(UiBody, label, x + w / 2, centreBaseline(UiBody, y, h), Align::Center, T_PAPER, T_INK);
    } else {
        strokeRect(x, y, w, h, INK, _buffer);
        text(UiBody, label, x + w / 2, centreBaseline(UiBody, y, h), Align::Center, T_INK);
    }
    addHit(x, y, w, h, action, index);
}

void Ui::paintKeyboard()
{
    String note = _net.note.length() ? _net.note : (String(F("Réseau : ")) + _kbSsid);
    paintSubHeader(F("Mot de passe"), note, true, false);

    strokeRect(PAD, 92, CONTENT_W - 140, 48, INK, _buffer);
    String shown;
    if (_kbReveal) {
        shown = _kbPass;
    } else {
        for (unsigned i = 0; i < _kbPass.length(); i++) {
            shown += '*';
        }
    }
    if (shown.length() == 0) {
        text(UiBody, F("saisir le mot de passe"), PAD + 16, centreBaseline(UiBody, 92, 48),
             Align::Left, T_INK);
    } else {
        textAsIs(UiBody, fit(UiBody, shown, CONTENT_W - 172), PAD + 16, centreBaseline(UiBody, 92, 48),
                 Align::Left);
    }
    paintButton(W - PAD_RIGHT - 133, 92, 133, 48, _kbReveal ? String(F("Masquer")) : String(F("Voir")),
                Action::WifiReveal, 0);

    const char *letters[3] = {"azertyuiop", "qsdfghjklm", "wxcvbn,.-'"};
    const char *lettersUp[3] = {"AZERTYUIOP", "QSDFGHJKLM", "WXCVBN;:?!"};
    const char *symbols[3] = {"1234567890", "@#$%&*+=/", "()[]{}<>"};
    const char **rows = _kbSymbols ? symbols : (_kbShift ? lettersUp : letters);

    constexpr int keyH = 52;
    constexpr int gap = 4;
    const int top = 156;

    for (int r = 0; r < 3; r++) {
        int n = (int)strlen(rows[r]);
        int keyW = (CONTENT_W - (n - 1) * gap) / n;
        int rowWidth = n * keyW + (n - 1) * gap;
        int x0 = PAD + (CONTENT_W - rowWidth) / 2;
        int y = top + r * (keyH + gap);
        for (int c = 0; c < n; c++) {
            char ch = rows[r][c];
            char label[2] = {ch, 0};
            paintKey(x0 + c * (keyW + gap), y, keyW, keyH, label, Action::WifiKey, (int)(uint8_t)ch);
        }
    }

    int y = top + 3 * (keyH + gap);
    paintKey(PAD, y, 140, keyH, _kbShift ? String(F("MAJ")) : String(F("maj")), Action::WifiShift, 0,
             _kbShift && !_kbSymbols);
    paintKey(PAD + 148, y, 140, keyH, _kbSymbols ? String("ABC") : String("123"), Action::WifiSymbols,
             0, _kbSymbols);
    paintKey(PAD + 296, y, 400, keyH, F("espace"), Action::WifiKey, (int)' ');
    paintKey(PAD + 704, y, CONTENT_W - 704, keyH, F("<"), Action::WifiBackspace, 0);

    paintButton(W - PAD - 240, BTN_Y, 240, BTN_H, F("Connecter"), Action::WifiConnect, 0, true);
}

// ---------------------------------------------------------------------------
// Splash
// ---------------------------------------------------------------------------

void Ui::paintBoot()
{
    paintIcon(LogoSuperserif, (W - LogoSuperserif.w) / 2, 236 - LogoSuperserif.h,
              LogoSuperserif.w, LogoSuperserif.h);
    text(UiBody, F("Time Tracker"), W / 2, 282, Align::Center, T_INK);
    epd_fill_rect(W / 2 - 120, 316, 240, STROKE, INK, _buffer);
    text(UiSmall, _status.length() ? _status : String(F("Démarrage...")), W / 2, 356, Align::Center,
         T_INK);
    text(UiMicro, _hint, W / 2, 384, Align::Center, T_INK);
}

// ---------------------------------------------------------------------------
// Compose and push
// ---------------------------------------------------------------------------

void Ui::render(bool forceFull, bool countPartial)
{
    applyPalette(_dark);
    clearBuffer();
    _hits.clear();

    switch (_screen) {
        case Screen::Boot: paintBoot(); break;
        case Screen::Projects: paintProjects(); break;
        case Screen::People: paintPeople(); break;
        case Screen::Settings: paintSettings(); break;
        case Screen::Wifi: paintWifi(); break;
        case Screen::Keyboard: paintKeyboard(); break;
    }

    push(forceFull, countPartial);
}

void Ui::pushBand(int top, int bottom)
{
    const size_t stride = (size_t)W / 2;
    const size_t bytes = stride * (size_t)(bottom - top);
    const size_t offset = (size_t)top * stride;

    Rect_t area = {
        .x = 0,
        .y = top,
        .width = W,
        .height = bottom - top,
    };

    const uint8_t *oldp = _onGlass + offset;
    const uint8_t *newp = _buffer + offset;

    if (_scratch) {
        if (buildDiffBand(_scratch, oldp, newp, bytes, /*lighten=*/true)) {
            epd_draw_image(area, _scratch, WHITE_ON_WHITE);
        }
        if (buildDiffBand(_scratch, oldp, newp, bytes, /*lighten=*/false)) {
            epd_draw_image(area, _scratch, BLACK_ON_WHITE);
        }
        return;
    }

    epd_draw_image(area, const_cast<uint8_t *>(oldp), WHITE_ON_WHITE);
    epd_draw_image(area, const_cast<uint8_t *>(newp), BLACK_ON_WHITE);
}

bool Ui::playButtonRect(int &x, int &y, int &w, int &h) const
{
    if (_screen != Screen::Projects || !_tracker || !_tracker->active() || !_projects) {
        return false;
    }
    int row = -1;
    const int n = (int)_projects->size() < ROWS_PER_PAGE ? (int)_projects->size() : ROWS_PER_PAGE;
    for (int i = 0; i < n; i++) {
        if ((*_projects)[i].id == _tracker->projectId()) {
            row = i;
            break;
        }
    }
    if (row < 0) {
        return false;
    }
    const int cx = PAD_LEFT + PLAY_CX;
    const int cy = ROW_TOP + row * ROW_STRIDE + ROW_H / 2;
    const int pad = PLAY_R + 8;
    x = (cx - pad) & ~1;
    y = cy - pad;
    if (x < 0) {
        x = 0;
    }
    if (y < 0) {
        y = 0;
    }
    w = (2 * pad + 2) & ~1;
    h = 2 * pad;
    if (x + w > W) {
        w = (W - x) & ~1;
    }
    if (y + h > H) {
        h = H - y;
    }
    return w >= 8 && h >= 8;
}

bool Ui::differsOutside(int x, int y, int w, int h) const
{
    const size_t stride = (size_t)W / 2;
    const int x1 = x + w;
    const int left = x / 2;
    const int right = (x1 + 1) / 2;
    for (int row = 0; row < H; row++) {
        const uint8_t *a = _buffer + (size_t)row * stride;
        const uint8_t *b = _onGlass + (size_t)row * stride;
        if (row < y || row >= y + h) {
            if (memcmp(a, b, stride) != 0) {
                return true;
            }
            continue;
        }
        if (left > 0 && memcmp(a, b, (size_t)left) != 0) {
            return true;
        }
        if (right < (int)stride &&
            memcmp(a + right, b + right, stride - (size_t)right) != 0) {
            return true;
        }
    }
    return false;
}

void Ui::pushRect(int x, int y, int w, int h)
{
    const size_t packedStride = (size_t)w / 2;
    const size_t bytes = packedStride * (size_t)h;
    const size_t fullStride = (size_t)W / 2;
    if (!_scratch || bytes == 0 || bytes * 4 > (size_t)W * H / 2) {
        pushBand(y, y + h);
        return;
    }

    uint8_t *packedOld = _scratch;
    uint8_t *packedNew = _scratch + bytes;
    uint8_t *lighten = _scratch + 2 * bytes;
    uint8_t *darken = _scratch + 3 * bytes;
    for (int row = 0; row < h; row++) {
        const size_t src = ((size_t)(y + row) * fullStride) + (size_t)(x / 2);
        memcpy(packedOld + (size_t)row * packedStride, _onGlass + src, packedStride);
        memcpy(packedNew + (size_t)row * packedStride, _buffer + src, packedStride);
    }

    Rect_t area = {
        .x = x,
        .y = y,
        .width = w,
        .height = h,
    };
    const bool needLighten = buildDiffBand(lighten, packedOld, packedNew, bytes, true);
    const bool needDarken = buildDiffBand(darken, packedOld, packedNew, bytes, false);
    if (!needLighten && !needDarken) {
        return;
    }
    // Interleave the two polarities so the old hole fills while the new one
    // opens — sequential passes looked like two missing dots, then one.
    vTaskDelay(10);
    for (uint8_t k = 0; k < 15; k++) {
        if (needDarken) {
            epd_draw_image_frame(area, darken, BLACK_ON_WHITE, k);
        }
        if (needLighten) {
            epd_draw_image_frame(area, lighten, WHITE_ON_WHITE, k);
        }
        vTaskDelay(5);
    }
}

void Ui::push(bool forceFull, bool countPartial)
{
    const size_t stride = (size_t)W / 2;

    // Invert the whole panel only to clear ghosting. Navigation and live
    // digits use the no-flash WHITE_ON_WHITE / BLACK_ON_WHITE pair.
    bool ghostingSweep = !_glassValid || _partialPushes >= PARTIALS_BEFORE_FULL ||
                         (millis() - _lastFullPushMs) > FULL_REFRESH_MS;
    // Typing a password would otherwise trip the 24-partial sweep mid-entry.
    if (ghostingSweep && _screen == Screen::Keyboard && _glassValid) {
        ghostingSweep = false;
    }

    if (ghostingSweep) {
        epd_poweron();
        epd_clear();
        epd_draw_grayscale_image(epd_full_screen(), _buffer);
        epd_poweroff();
        _partialPushes = 0;
        _lastFullPushMs = millis();
        memcpy(_onGlass, _buffer, stride * H);
        _glassValid = true;
        return;
    }

    struct Band {
        int top;
        int bottom;  // exclusive
    };
    std::vector<Band> bands;

    if (forceFull) {
        bands.push_back({0, H});
    } else {
        int row = 0;
        while (row < H) {
            if (memcmp(_buffer + row * stride, _onGlass + row * stride, stride) == 0) {
                row++;
                continue;
            }
            int top = row;
            int lastDirty = row;
            row++;
            while (row < H) {
                if (memcmp(_buffer + row * stride, _onGlass + row * stride, stride) != 0) {
                    lastDirty = row;
                } else if (row - lastDirty > BAND_MERGE_GAP) {
                    break;
                }
                row++;
            }
            int snappedTop = snapBandTop(top);
            int snappedBottom = snapBandBottom(lastDirty + 1);
            if (snappedBottom <= snappedTop) {
                snappedBottom = lastDirty + 1;
                snappedTop = top;
            }
            if (!bands.empty() && snappedTop <= bands.back().bottom) {
                if (snappedBottom > bands.back().bottom) {
                    bands.back().bottom = snappedBottom;
                }
            } else {
                bands.push_back({snappedTop, snappedBottom});
            }
        }

        if (bands.empty()) {
            return;
        }

        int dirtyRows = 0;
        for (const Band &band : bands) {
            dirtyRows += band.bottom - band.top;
        }
        if (bands.size() > MAX_BANDS || dirtyRows > H * 3 / 5) {
            bands.clear();
            bands.push_back({0, H});
        }
    }

    epd_poweron();
    int px = 0, py = 0, pw = 0, ph = 0;
    const bool spinOnly = !forceFull && !countPartial && playButtonRect(px, py, pw, ph) &&
                          !differsOutside(px, py, pw, ph);
    if (spinOnly) {
        pushRect(px, py, pw, ph);
    } else {
        for (const Band &band : bands) {
            pushBand(band.top, band.bottom);
        }
    }
    epd_poweroff();
    if (countPartial && _partialPushes < 255) {
        _partialPushes++;
    }

    memcpy(_onGlass, _buffer, stride * H);
    _glassValid = true;
}

void Ui::cleanGhosting()
{
    epd_poweron();
    epd_clear_area_cycles(epd_full_screen(), 8, 50);
    epd_poweroff();
    _glassValid = false;
    render(/*forceFull=*/true);
}
