// params.h: Ghost Voice. A friendly ghost wails along above whatever you play. The note is
// tracked from the guitar; a soft, theremin-like "oooo" follows it an octave (or more) up,
// but slowly: it swoops from note to note and swells in late, so fast runs become one long
// eerie wail. Held notes start to shiver like a singing saw, and everything drifts into fog.
//
//   input -> pitch tracker -> glide -> "oo" voice (sines + breath) * late swell * shiver
//         -> FOG (long dark plate, mostly the ghost, a little guitar) -> + DRY -> limiter
#ifndef PARAMS_H
#define PARAMS_H

#include <stdint.h>

#include <resona/module.h>

#define MODULE_ID "org.resona.creative.ghost-note.voice"
#define MODULE_NAME "Ghost Voice"
#define MODULE_TITLE "GHOST VOICE"
#define MODULE_VERSION "1.0.0"

enum Param : uint32_t { P_HAUNT, P_INTERVAL, P_GLIDE, P_SHIVER, P_FOG, P_DRY, P_LEVEL, P_COUNT };

const resona_param kParams[P_COUNT] = {
    {"HAUNT", 0.0f, 100.0f, 60.0f, "%"},
    {"INTERVAL", 0.0f, 2.0f, 1.0f, ""},
    {"GLIDE", 0.0f, 100.0f, 45.0f, "%"},
    {"SHIVER", 0.0f, 100.0f, 45.0f, "%"},
    {"FOG", 0.0f, 100.0f, 45.0f, "%"},
    {"DRY", 0.0f, 100.0f, 100.0f, "%"},
    {"LEVEL", -12.0f, 6.0f, 0.0f, "dB"},
};

static const char *const kIntervalNames[3] = {"UNISON", "OCTAVE", "OCT+FIFTH"};
#define PX_NONE {0, 0.0f, 0, nullptr}
const resona_param_ext kParamExt[P_COUNT] = {
    PX_NONE, {RESONA_PARAM_CHOICE, 1.0f, 3, kIntervalNames}, PX_NONE, PX_NONE, PX_NONE, PX_NONE, PX_NONE,
};

#endif
