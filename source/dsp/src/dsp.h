// dsp.h: Ghost Voice's state.
#ifndef DSP_H
#define DSP_H

#include <stdint.h>

#include "fx_core.hpp"
#include "params.h"

constexpr float kFogSize = 1.5f;
constexpr uint32_t kPoolFloats = fx::Plate::floats(kFogSize, 60.0f) + 16u;

struct Dsp {
    // pitch tracking (as a guitar synth does it), with two agreeing periods before a note counts
    fx::Biquad track_lp, track_hp;
    fx::Follower track_env, env, env_slow, body;
    int side;
    float since_rise, last_log, target_log, freq_log;
    bool gate_open;
    // the ghost
    float ph, inc, amp, amp_t, held, shiver_now, wob;
    float interval, glide_c, swell_c, shiver, breath_g;
    fx::Lfo vib;
    fx::Drift drift;
    fx::Rng rng;
    fx::Svf breath;
    fx::LowPass1 soft;
    uint32_t tick;
    // the fog
    fx::Plate fog;
    fx::LowPass1 fog_dark;
    fx::Limiter limiter;
    fx::Smooth haunt, fog_amt, dry, level;
};

bool dsp_init(Dsp &d, fx::Pool &pool);
void dsp_settle(Dsp &d);
void dsp_reset(Dsp &d);
void dsp_param(Dsp &d, uint32_t index, float value);
void dsp_process(Dsp &d, const float *in, float *out, uint32_t frames);

#endif
