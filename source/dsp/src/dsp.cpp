// dsp.cpp: Ghost Voice.
#include "dsp.h"

namespace {

const float kThr = 0.004f;          // about -48 dBFS: quieter than this is not a note
const float kTopLog = 10.9f;        // the ghost never sings above about 1.9 kHz: it drops an octave

void reset_voice(Dsp &d)
{
    d.track_lp.reset(); d.track_hp.reset(); d.track_env.reset(); d.env.reset(); d.env_slow.reset(); d.body.reset();
    d.side = 0; d.since_rise = 0.0f; d.last_log = 0.0f; d.target_log = d.freq_log = 7.5f;
    d.gate_open = false;
    d.ph = 0.0f; d.inc = 180.0f * fx::kInvRate; d.amp = d.amp_t = 0.0f; d.held = 0.0f; d.shiver_now = 0.0f; d.wob = 0.0f;
    d.breath_g = 0.05f;
    d.vib.ph = 0.0f;
    d.drift.reset(); d.rng.reset(); d.breath.reset(); d.soft.reset();
    d.tick = 0;
}

}  // namespace

bool dsp_init(Dsp &d, fx::Pool &pool)
{
    const bool ok = d.fog.attach(pool, kFogSize, 60.0f);
    d.fog.set_predelay_ms(45.0f);
    d.fog.set_lowcut_hz(180.0f);
    d.fog.set_bandwidth_hz(5000.0f);
    d.fog.set_damp_hz(2800.0f);
    d.fog.set_mod(0.8f, 0.37f);
    d.fog.set_decay(5.0f);
    d.fog_dark.set_hz(3600.0f);
    d.track_lp.set(fx::Biquad::LP, 700.0f, 0.55f);
    d.track_hp.set(fx::Biquad::HP, 60.0f, 0.7f);
    d.track_env.set(1.0f, 30.0f);
    d.env.set(2.0f, 120.0f);
    d.env_slow.set(40.0f, 200.0f);
    d.body.set(60.0f, 900.0f);      // how loud you are playing, slowly: the ghost matches it
    d.vib.set_hz(5.5f);
    d.drift.init(0x6057u, 1.3f);
    d.rng.seed(0xB00B00u);
    d.soft.set_hz(4200.0f);
    d.limiter.init();
    d.interval = 1.0f; d.glide_c = 0.01f; d.swell_c = 0.0001f; d.shiver = 0.45f;
    reset_voice(d);
    d.haunt.init(0.6f, 40.0f);
    d.fog_amt.init(0.45f, 60.0f);
    d.dry.init(1.0f, 30.0f);
    d.level.init(1.0f, 30.0f);
    return ok;
}

void dsp_settle(Dsp &d) { d.haunt.snap(); d.fog_amt.snap(); d.dry.snap(); d.level.snap(); }

void dsp_reset(Dsp &d)
{
    reset_voice(d);
    d.fog.clear(); d.fog_dark.reset();
    d.limiter.reset();
    dsp_settle(d);
}

void dsp_param(Dsp &d, uint32_t index, float v)
{
    switch (index) {
    case P_HAUNT: d.haunt.set(fx::pct(v)); break;
    case P_INTERVAL: { const int k = (int)(v + 0.5f); d.interval = k <= 0 ? 0.0f : (k == 1 ? 1.0f : 1.5849625f); break; }
    case P_GLIDE: {
        const float g = fx::pct(v);
        const float port_ms = 50.0f * resona_exp2f(4.0f * g);            // 50 .. 800 ms to arrive
        d.glide_c = 1.0f - resona_expf(-8.0f / (port_ms * 0.33f * 48.0f));   // per 8 samples
        d.swell_c = fx::coef_ms((150.0f + 250.0f * g) * 0.4f);           // 150 .. 400 ms fade-in
        break;
    }
    case P_SHIVER: d.shiver = fx::pct(v); d.vib.set_hz(4.8f + 2.4f * d.shiver); break;
    case P_FOG: { const float f = fx::pct(v); d.fog_amt.set(f); d.fog.set_decay(2.5f + 6.5f * f); break; }
    case P_DRY: d.dry.set(fx::pct(v)); break;
    case P_LEVEL: d.level.set(resona_db_to_lin(v)); break;
    default: break;
    }
}

void dsp_process(Dsp &d, const float *in, float *out, uint32_t frames)
{
    const float release_c = fx::coef_ms(220.0f);
    for (uint32_t i = 0; i < frames; ++i) {
        const float x = in[i];
        const float e = d.env.process(x);
        const float es = d.env_slow.process(x);
        const float body = d.body.process(x);

        // pitch: a Schmitt trigger on the filtered string; a note counts once two periods agree
        const float f = d.track_hp.process(d.track_lp.process(x));
        const float h = 0.25f * d.track_env.process(f) + 1.0e-5f;
        d.since_rise += 1.0f;
        if (d.side <= 0 && f > h) {
            d.side = 1;
            const float hz = fx::kRate / d.since_rise;
            if (hz > 65.0f && hz < 1400.0f && e > kThr) {
                const float lg = resona_log2f(hz);
                if (fx::absf(lg - d.last_log) < 0.045f) d.target_log = 0.5f * (lg + d.last_log);
                d.last_log = lg;
            }
            d.since_rise = 0.0f;
        } else if (d.side >= 0 && f < -h) {
            d.side = -1;
        }

        // the gate, and how long the note has been held (a hard re-pick starts the count again)
        if (!d.gate_open && e > kThr * 1.5f) { d.gate_open = true; d.held = 0.0f; }
        else if (d.gate_open && e < kThr) d.gate_open = false;
        if (d.gate_open && e > es * 2.2f && e > kThr * 3.0f) d.held *= 0.9995f;
        if (d.gate_open) d.held += fx::kInvRate;

        if ((d.tick++ & 7u) == 0u) {
            float want = d.target_log + d.interval;
            if (want > kTopLog) want -= 1.0f;
            if (want > kTopLog) want -= 1.0f;
            // out of silence the ghost scoops up into the note; otherwise it swoops from the last one
            if (d.amp < 1.0e-4f && !d.gate_open) d.freq_log = want - 0.25f;
            d.freq_log += d.glide_c * (want - d.freq_log);
            // shiver: grows over the first two seconds of a held note
            const float grow = fx::clampf(d.held * 0.5f, 0.0f, 1.0f);
            d.shiver_now += 0.004f * (d.shiver * (0.25f + 0.75f * grow) - d.shiver_now);
            float lfo = 0.0f;
            for (int k = 0; k < 8; ++k) lfo = d.vib.sine();          // keep the LFO at audio rate time
            float dr = 0.0f;
            for (int k = 0; k < 8; ++k) dr = d.drift.next();
            d.wob = lfo;
            const float cents = d.shiver_now * (0.055f * lfo + 0.02f * dr) + 0.004f * dr;   // octaves: up to +-65 cents
            d.inc = fx::clampf(resona_exp2f(d.freq_log + cents), 40.0f, 2400.0f) * fx::kInvRate;
            // the voice is as loud as the playing (slow), so it sits just behind the guitar
            d.amp_t = d.gate_open ? fx::clampf(body * 2.6f, 0.0f, 0.5f) : 0.0f;
            d.breath_g = fx::tan_small(fx::kPi * fx::clampf(d.inc * fx::kRate * 2.0f, 300.0f, 5000.0f) * fx::kInvRate);
        }
        d.amp = fx::flush(d.amp + (d.amp_t > d.amp ? d.swell_c : release_c) * (d.amp_t - d.amp));

        // "oooo": a sine with a little second and third harmonic, and breath around the octave
        d.ph += d.inc; if (d.ph >= 1.0f) d.ph -= 1.0f;
        const float p2 = fx::wrap01(d.ph + d.ph), p3 = fx::wrap01(fx::wrap01(d.ph + d.ph) + d.ph);
        const float tone = fx::sin01(d.ph) + 0.16f * fx::sin01(p2) + 0.07f * fx::sin01(p3);
        float lp, bp;
        d.breath.process(d.rng.bi(), d.breath_g, 0.5f, lp, bp);
        const float trem = 1.0f - d.shiver_now * 0.45f * (0.5f - 0.5f * d.wob);
        const float hn = d.haunt.next();
        const float ghost = d.soft.process((tone + 0.10f * bp) * d.amp * trem) * hn * resona_sqrtf(hn) * 0.70f;

        // fog: mostly the ghost, a little of the guitar
        const float fa = d.fog_amt.next();
        const float wet = d.fog_dark.process(d.fog.process(ghost * 0.9f + x * 0.3f)) * fa * 0.9f;

        out[i] = d.limiter.process((x * d.dry.next() + ghost * (1.0f - 0.25f * fa) + wet) * d.level.next());
    }
}
