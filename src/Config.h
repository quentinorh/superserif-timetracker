#pragma once

#include <stdint.h>

// ---------------------------------------------------------------- API -----
// API_BASE, API_HOST and API_TOKEN come from `.env` via tools/load_dotenv.py
// (PlatformIO -D flags). Empty fallbacks keep IntelliSense happy before a
// build. The extra script refuses to compile if API_BASE is missing.

#ifndef API_BASE
#define API_BASE ""
#endif
#ifndef API_HOST
#define API_HOST ""
#endif
#ifndef API_TOKEN
#define API_TOKEN ""
#endif

// Only "demarre" projects can receive hours (the API answers 409 otherwise).
constexpr char API_STATUS_FILTER[] = "demarre";

constexpr char DEVICE_HOSTNAME[] = "lineup";

// Probed to tell "associated to an access point" from "actually online".
// A captive portal answers 200 + HTML here instead of a bare 204.
constexpr char CONNECTIVITY_URL[] = "http://connectivitycheck.gstatic.com/generate_204";

// ------------------------------------------------------------ Timings -----

constexpr uint32_t PROJECT_REFRESH_MS = 5UL * 60UL * 1000UL;
constexpr uint32_t PROJECT_RETRY_MS = 15UL * 1000UL;
// Re-read the running session so hours.done stays in step with the server.
constexpr uint32_t SESSION_PULL_MS = 10UL * 60UL * 1000UL;
// Live row paint while a timer is running. Same cadence as SESSION_PULL_MS.
constexpr uint32_t TIMER_TICK_MS = 10UL * 60UL * 1000UL;
// 16-dot pause spinner: one step per interval (full turn ~ 12 s).
constexpr uint32_t SPINNER_MS = 750UL;

constexpr uint32_t STA_CONNECT_TIMEOUT_MS = 15UL * 1000UL;
constexpr uint32_t INTERNET_RECHECK_MS = 10UL * 60UL * 1000UL;
// Leave the splash even if the first API fetch hangs after Wi-Fi is up.
constexpr uint32_t BOOT_LEAVE_MS = 20UL * 1000UL;

constexpr uint32_t TOUCH_POLL_MS = 20UL;

// Partial updates drive only the pixels that changed (WHITE_ON_WHITE then
// BLACK_ON_WHITE, no invert). Ghosting still builds up, so a flashing full
// refresh is folded in once in a while. The Wi-Fi keyboard defers that sweep
// until the next screen so typing does not invert the panel.
constexpr uint8_t PARTIALS_BEFORE_FULL = 24;
constexpr uint32_t FULL_REFRESH_MS = 60UL * 60UL * 1000UL;

// ------------------------------------------------------------ Storage -----

constexpr uint8_t MAX_NETWORKS = 5;
