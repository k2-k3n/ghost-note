// Ghost Note: a friendly ghost haunts your guitar and wails along above whatever you play. MIT.
//
//   SELECTOR   turn: HAUNT (how loud the ghost sings)     click: where it sings (unison / octave / octave + fifth)
//   CUSTOMIZE  turn: FOG (the long dark reverb)           click: SHIVER (calm / shaky / terrified)   long press: reset
//
// The sound is the nested dsp/ pack (Ghost Voice); this file is the graveyard. The App cannot
// read the pack's state, so the ghost acts on the meters: it floats higher and glows brighter
// the louder you play, sings "O" while you play, shouts BOO! on a hard pick (which scares the
// bats), and hides behind its tombstone after nine seconds of silence. The artwork
// uses a layered cloth ghost and moonlit backdrop (tools/encode_graveyard.py).
//
// The Web, Mobile and MIDI controllers (controller/, CCs on channel 1, see README.md) reach every
// setting the knobs reach, SHIVER in fine steps, plus GLIDE, DRY and LEVEL, and four hold pads:
// BOO (the ghost at full voice, shouting), WAIL (the slowest, shakiest swoop), HIGH (octave and a
// fifth) and VANISH (it hides; the guitar and the fog stay).
#include <resona/platform.h>
#include <resona/startup_helpers.h>
#include "splash_data.h"
#include "kit.h"
#include "art.h"
#include "graveyard.h"
#include "controller_link.h"

namespace {

const resona_platform *P;
const char kPack[] = "org.resona.creative.ghost-note.voice";   // dsp/resona.project.json
enum { HAUNT, INTERVAL, GLIDE, SHIVER, FOG, DRY, LEVEL, PARAMS };
const float kShiver[3] = {25.0f, 60.0f, 100.0f};

struct SavedV1 { uint32_t magic; int haunt, interval, fog, shiver; };
struct Saved { uint32_t magic; int haunt, interval, fog, shiver, shiver_amt, glide, dry, level; };
const uint32_t kMagicV1 = 0x47485331u;   // "GHS1"
const uint32_t kMagic = 0x47485332u;     // "GHS2"

// The controllers' MIDI map: CC on channel 1 (controller/shared/profile.js says the same).
enum { CC_HAUNT = 20, CC_INTERVAL, CC_FOG, CC_SHIVER, CC_GLIDE, CC_DRY, CC_LEVEL,
       CC_BOO = 30, CC_WAIL, CC_HIGH, CC_VANISH };

struct State {
    kit::Audio audio;
    kit::Levels lv;
    kit::Saver saver;
    Saved s;
    uint32_t ms, idle, blink_at, seed, touched_at, boo_at, glad_at;
    int booing, touched;   // touched: 1 HAUNT, 2 FOG (whose value to show for a moment)
    float lift, hide;             // smoothed: how high it floats (0..1), how far it has sunk behind the stone (0..1)
    int boo_held, wail, high, vanish;   // pads held on a controller
    ctl::Link link;
};

const int kBinds = 11;
ctl::Bind kBind[kBinds];
void bind(State *st)
{
    Saved &s = st->s;
    const ctl::Bind b[kBinds] = {
        {CC_HAUNT, ctl::RANGE, 0, 100, &s.haunt}, {CC_INTERVAL, ctl::STEPS, 0, 2, &s.interval},
        {CC_FOG, ctl::RANGE, 0, 100, &s.fog}, {CC_SHIVER, ctl::RANGE, 0, 100, &s.shiver_amt},
        {CC_GLIDE, ctl::RANGE, 0, 100, &s.glide}, {CC_DRY, ctl::RANGE, 0, 100, &s.dry},
        {CC_LEVEL, ctl::RANGE, -12, 6, &s.level},
        {CC_BOO, ctl::TRIGGER, 0, 1, nullptr}, {CC_WAIL, ctl::TRIGGER, 0, 1, nullptr},
        {CC_HIGH, ctl::TRIGGER, 0, 1, nullptr}, {CC_VANISH, ctl::TRIGGER, 0, 1, nullptr},
    };
    for (int i = 0; i < kBinds; ++i) kBind[i] = b[i];
}

// SHIVER's three looks (calm / shaky / terrified) from its amount; the knob's click steps 25 / 60 / 100
int shiver_step(int amt) { return amt < 43 ? 0 : (amt < 80 ? 1 : 2); }

uint32_t rnd(State *st) { st->seed = st->seed * 1664525u + 1013904223u; return st->seed >> 8; }

// The pads override the settings while held: VANISH silences the ghost whatever else is held.
int haunt_now(const State *st) { return st->vanish ? 0 : (st->boo_held ? 100 : st->s.haunt); }
int interval_now(const State *st) { return st->high ? 2 : st->s.interval; }
int shiver_now(const State *st) { return st->wail ? 2 : st->s.shiver; }

void push(State *st)
{
    kit::audio_set(P, st->audio, HAUNT, (float)haunt_now(st));
    kit::audio_set(P, st->audio, INTERVAL, (float)interval_now(st));
    kit::audio_set(P, st->audio, GLIDE, st->wail ? 100.0f : (float)st->s.glide);
    kit::audio_set(P, st->audio, SHIVER, st->wail ? 100.0f : (float)st->s.shiver_amt);
    kit::audio_set(P, st->audio, FOG, (float)st->s.fog);
    kit::audio_set(P, st->audio, DRY, (float)st->s.dry);
    kit::audio_set(P, st->audio, LEVEL, (float)st->s.level);
}

void defaults(State *st) { st->s = {kMagic, 60, 1, 45, 0, 25, 45, 100, 0}; }

void boo(State *st) { st->booing = 1; st->boo_at = st->ms; }

/* ---- drawing ---------------------------------------------------------------------------- */

// Clip the ghost and its face against the foreground tombstone.
void blit_x(kit::Canvas &c, const kit::Sprite &sp, const uint16_t *pal, int x, int y, bool flip)
{
    x += sp.ox; y += sp.oy;
    for (int j = 0; j < sp.h; ++j) {
        const int Y = y + j;
        if (Y < 0 || Y >= kit::Canvas::H) continue;
        const uint8_t *row = sp.px + (uint32_t)j * sp.w;
        uint16_t *dst = c.px + (uint32_t)Y * c.stride;
        for (int i = 0; i < sp.w; ++i) {
            const uint8_t v = row[flip ? sp.w - 1 - i : i];
            const int X = x + i;
            if (v && X >= 0 && X < kit::Canvas::W && Y < kStoneTop[X]) dst[X] = pal[v];
        }
    }
}

void draw_fog(kit::Canvas &c, uint32_t ms, int fog)
{
    if (!fog) return;
    const float phase = (float)(ms % 24000u) * (6.2831853f / 24000.0f);
    const uint16_t blue = kit::rgb(151, 180, 227);
    const int bands = 1 + fog / 34;
    for (int k = 0; k < bands; ++k) {
        for (int x = 0; x < c.W; ++x) {
            const float wave = resona_sinf(x * 0.026f + phase * (k & 1 ? -2 : 3) + k * 1.7f);
            const int mid = 151 - 9 * k + (int)(wave * (4 + k));
            for (int j = -4; j <= 4; ++j) {
                const int falloff = j < 0 ? 5 + j : 5 - j;
                uint16_t &pixel = c.px[(mid + j) * c.stride + x];
                pixel = kit::mix(pixel, blue, fog * (0.0009f * falloff) * (0.65f + 0.35f * wave));
            }
        }
    }
}

void draw(State *st)
{
    kit::Canvas c;
    if (!c.begin(P)) return;
    const uint16_t *pal = kArtPalette;
    const uint16_t ink = kit::rgb(18, 14, 44), cream = kit::rgb(255, 250, 236);
    const uint32_t ms = st->ms;
    const uint32_t boo_t = ms - st->boo_at;
    const bool shout = st->booing && boo_t < 800u;          // BOO! and arms up
    const bool fright = st->booing && boo_t < 1600u;        // the bats take longer to calm down
    if (st->booing && boo_t >= 1600u) st->booing = 0;
    const bool glad = ms - st->glad_at < 700u && st->glad_at;
    const bool hidden = st->hide > 0.5f;
    const int shiver = shiver_now(st), interval = interval_now(st), haunt = haunt_now(st);
    const bool playing = st->lv.in > 0.12f;

    c.backdrop(kYard, kYardPalette);
    const float entry = kit::clampf((float)ms / 580.0f, 0.0f, 1.0f);
    const float reveal = entry * entry * (3.0f - 2.0f * entry);
    // Keep a visible idle float and ease it out as the ghost sinks behind the stone.
    const float phase = (float)(ms % 2400u) * (6.2831853f / 2400.0f);
    const float bob = 8.0f * resona_sinf(phase);
    const float hide = st->hide > 1.0f - reveal ? st->hide : 1.0f - reveal;
    float yf = kit::lerp(20.0f + bob - 9.0f * st->lift, 101.0f, hide);
    if (shout && boo_t < 160u) yf -= 4.0f * (1.0f - (float)boo_t / 160.0f);
    const int cx = 164 + (!hidden && shiver ? (int)((ms / (shiver == 2 ? 50u : 110u)) & 1u) : 0);
    const int gy = (int)yf + (!hidden && shiver == 2 ? (int)((ms / 70u) & 1u) : 0);
    uint16_t cloth[sizeof kGhostPalette / sizeof kGhostPalette[0]];
    const float light = kit::clampf(0.08f + 0.52f * st->lift + (shout ? 0.14f : 0.0f), 0.0f, 0.8f);
    const uint16_t shade = kit::rgb(33, 46, 87);
    for (unsigned i = 1; i < sizeof cloth / sizeof cloth[0]; ++i) {
        const uint16_t color = kit::mix(kGhostPalette[i], cream, light);
        cloth[i] = kit::mix(color, shade, (100 - haunt) * 0.004f);
    }
    if (hide < 0.99f) blit_x(c, kClothGhost, cloth, cx, gy, false);
    // the little ghost friends: one, two or three for unison, octave, octave and a fifth
    static const int kFx[3] = {212, 228, 206}, kFy[3] = {52, 30, 12};
    for (int k = 0; k <= interval; ++k) {
        const float ph = (float)(ms % 6283u) * 0.001f * (1.6f + 0.3f * (float)k) + (float)k * 2.0f;
        c.blit(kArt_friend, pal, kFx[k] + (shout ? 3 + k : 0), kFy[k] + (int)(2.5f * resona_sinf(ph)), k == 1);
    }

    // its face
    const uint32_t since_blink = ms - st->blink_at;
    const kit::Sprite *eye = &kArt_eye_open, *mouth = &kArt_mouth_smile;
    if (shout) { eye = &kArt_eye_wide; mouth = &kArt_mouth_boo; }
    else if (hidden) { if (since_blink < 160u) eye = &kArt_eye_shut; }
    else if (glad) { eye = &kArt_eye_happy; mouth = &kArt_mouth_small; }
    else {
        if (shiver == 2) eye = &kArt_eye_worry;
        else if (since_blink < 120u) eye = &kArt_eye_shut;
        if (st->lv.in > 0.45f) mouth = &kArt_mouth_open;
        else if (playing) mouth = &kArt_mouth_small;
        else if (shiver) mouth = &kArt_mouth_wavy;
    }
    if (hide < 0.99f) {
        blit_x(c, *eye, pal, cx - 9, gy + 31, false);
        blit_x(c, *eye, pal, cx + 9, gy + 31, true);
        blit_x(c, *mouth, pal, cx, gy + 44, false);
    }

    draw_fog(c, ms, st->s.fog);

    // BOO! startles two bats out of the tree
    if (fright) {
        const float t = (float)boo_t / 1600.0f;
        for (int k = 0; k < 2; ++k) {
            const int bx = (k ? 36 : 66) + (int)((k ? -70.0f : 150.0f) * t);
            const int by = (k ? 22 : 28) - (int)(34.0f * t) + (int)(5.0f * resona_sinf(t * 16.0f + (float)k));
            c.blit(((boo_t / 80u) + (uint32_t)k) & 1u ? kArt_bat_1 : kArt_bat_0, pal, bx, by);
        }
    }
    if (shout) {
        const int bx = kit::clampi(cx + 18, 0, 319 - kArt_bubble.w), by = kit::clampi(gy - 8, 0, 100);
        c.blit(kArt_bubble, pal, bx, by);
        c.text(bx + (kArt_bubble.w - c.text_w(2, "BOO!")) / 2 + 1, by + 11, 2, ink, "BOO!");
    }

    // a knob's value shows for a moment after it is turned
    if (ms - st->touched_at < 1300u && st->touched) {
        char num[16];
        P->format(num, sizeof num, st->touched == 1 ? "HAUNT %d" : "FOG %d", st->touched == 1 ? st->s.haunt : st->s.fog);
        c.text_s(8, 146, 1, cream, ink, num);
    }

    c.banner(kit::audio_message(st->audio, "INSTALL GHOST VOICE IN LAB"), ink, kit::rgb(248, 238, 184));
    if (entry < 1.0f) {
        for (int y = 0; y < c.H; ++y) {
            uint16_t *row = c.px + y * c.stride;
            for (int x = 0; x < c.W; ++x) row[x] = kit::mix(0, row[x], reveal);
        }
    }
    c.present(P);
}

/* ---- the App ---------------------------------------------------------------------------- */

int init(void *state, const resona_platform *platform)
{
    State *st = static_cast<State *>(state);
    P = platform;
    const int startup = resona_startup_splash_apply(P, kSplash, sizeof kSplash);
    if (startup < 0 && startup != RESONA_ERR_UNSUPPORTED)
        P->log("Startup animation could not be saved");
    defaults(st);
    Saved v;
    SavedV1 v1;
    Saved &s = st->s;
    if (kit::load(P, v, kMagic)) s = v;
    else if (kit::load(P, v1, kMagicV1)) {
        s.haunt = v1.haunt; s.interval = v1.interval; s.fog = v1.fog;
        s.shiver_amt = (int)kShiver[kit::clampi(v1.shiver, 0, 2)];
    }
    s.haunt = kit::clampi(s.haunt, 0, 100); s.interval = kit::clampi(s.interval, 0, 2); s.fog = kit::clampi(s.fog, 0, 100);
    s.shiver_amt = kit::clampi(s.shiver_amt, 0, 100); s.shiver = shiver_step(s.shiver_amt);
    s.glide = kit::clampi(s.glide, 0, 100); s.dry = kit::clampi(s.dry, 0, 100); s.level = kit::clampi(s.level, -12, 6);
    bind(st);
    st->seed = 0xB00B00u;
    push(st);
    return 0;
}

void event(void *state, const resona_app_event *ev)
{
    State *st = static_cast<State *>(state);
    if (kit::audio_event(P, st->audio, ev, st->ms)) return;
    ctl::Msg m;
    if (ctl::receive(P, st->link, kBind, kBinds, ev, m)) {
        if (!m.changed) return;
        if (m.cc == CC_BOO) { st->boo_held = m.down; if (m.down) boo(st); }
        else if (m.cc == CC_WAIL) st->wail = m.down;
        else if (m.cc == CC_HIGH) { st->high = m.down; if (m.down) st->glad_at = st->ms ? st->ms : 1u; }
        else if (m.cc == CC_VANISH) st->vanish = m.down;
        else kit::save_later(st->saver, st->ms);
        if (m.cc == CC_HAUNT || m.cc == CC_FOG) {
            st->touched = m.cc == CC_HAUNT ? 1 : 2;
            st->touched_at = st->ms;
            st->glad_at = st->ms ? st->ms : 1u;
        } else if (m.cc == CC_SHIVER) {
            const int step = shiver_step(st->s.shiver_amt);
            if (step != st->s.shiver) { st->s.shiver = step; boo(st); }
        } else if (m.cc == CC_INTERVAL) boo(st);
        if (!st->vanish) st->idle = 0;
        push(st);
        return;
    }
    if (ev->kind != RESONA_APP_EV_INPUT) return;
    const int top = ev->u.input.input == RESONA_INPUT_SELECTOR;
    const int kind = (int)ev->u.input.kind, v = ev->u.input.value;
    if (kind == RESONA_EVENT_TURN) {
        if (top) st->s.haunt = kit::clampi(st->s.haunt + v * 2, 0, 100);
        else st->s.fog = kit::clampi(st->s.fog + v * 2, 0, 100);
        st->touched = top ? 1 : 2;
        st->touched_at = st->ms;
        st->glad_at = st->ms ? st->ms : 1u;      // it is pleased to be noticed
    } else if (kind == RESONA_EVENT_CLICK) {
        if (top) st->s.interval = (st->s.interval + 1) % 3;
        else { st->s.shiver = (shiver_step(st->s.shiver_amt) + 1) % 3; st->s.shiver_amt = (int)kShiver[st->s.shiver]; }
        boo(st);
    } else if (kind == RESONA_EVENT_LONG && !top) {
        defaults(st);
        boo(st);
    } else {
        return;
    }
    st->idle = 0;
    push(st);
    kit::save_later(st->saver, st->ms);
}

void frame(void *state, uint32_t ms)
{
    State *st = static_cast<State *>(state);
    st->ms += ms;
    kit::audio_tick(P, st->audio, kPack, PARAMS, st->ms);
    kit::levels_tick(P, st->lv, ms);
    kit::save_tick(P, st->saver, st->s, st->ms);
    ctl::sync(P, st->link, kBind, kBinds, st->ms);
    if (st->lv.raw_in >= 2) st->idle = 0; else st->idle += ms;
    if (st->lv.hit > 0.95f && (!st->booing || st->ms - st->boo_at > 1800u)) boo(st);
    if (st->ms - st->blink_at > 2400u + (rnd(st) % 3000u)) st->blink_at = st->ms;
    const float k = kit::clampf((float)ms / 220.0f, 0.0f, 1.0f);
    st->lift += k * (kit::clampf(st->lv.out > st->lv.in ? st->lv.out : st->lv.in, 0.0f, 1.0f) - st->lift);
    const float want = st->idle > 9000u || st->vanish ? 1.0f : 0.0f;
    const float step = (float)ms / (want > st->hide ? 700.0f : 300.0f);
    st->hide = want > st->hide ? kit::clampf(st->hide + step, 0.0f, 1.0f) : kit::clampf(st->hide - step, 0.0f, 1.0f);
    draw(st);
}

const resona_app kApp = {
    RESONA_PLATFORM_VERSION, sizeof(resona_app),
    RESONA_APP_ID, RESONA_APP_NAME, RESONA_APP_VERSION,
    sizeof(State), 0,
    init, event, frame,
};

}  // namespace

extern "C" const resona_app *resona_app_get(void) { return &kApp; }
