// DroneVoice engine — the sound, shared by every board.
//
// Signal path:
//   6 detuned oscillators (4 at root, 2 at a selectable interval)
//   + optional sine sub an octave down
//   (+ optional stereo audio input)
//   -> stereo 24 dB ladder filters swept by a slow LFO
//   -> ReverbSc
//
// Each board's main file owns the controls, LEDs and patch storage, fills in
// a Patch, and calls Engine::Process() once per audio block.

#pragma once

#include "daisysp.h"
#include "daisysp-lgpl.h"
#include <cmath>
#include <cstdint>
#include <cstddef>

namespace dronevoice
{
using namespace daisysp;

// ---------------------------------------------------------------- constants
constexpr int kNumVoices     = 6;
constexpr int kNumRoot       = 4; // voices 0..3 at root, 4..5 at interval
constexpr int kNumParamPages = 4; // pitch, filter, motion, space
constexpr int kNumSlots      = 8;

static const uint8_t kWaves[] = {Oscillator::WAVE_POLYBLEP_SAW,
                                 Oscillator::WAVE_POLYBLEP_SQUARE,
                                 Oscillator::WAVE_POLYBLEP_TRI,
                                 Oscillator::WAVE_SIN};
constexpr int        kNumWaves = sizeof(kWaves) / sizeof(kWaves[0]);

// unison, 5th, octave, 4th, min 3rd, maj 3rd, min 7th
static const float kIntervals[]  = {0.f, 7.f, 12.f, 5.f, 3.f, 4.f, 10.f};
constexpr int      kNumIntervals = sizeof(kIntervals) / sizeof(kIntervals[0]);

// Detune position of each voice, -1..+1, scaled by the detune control.
static const float kSpread[kNumVoices] = {-1.0f, -0.33f, 0.33f, 1.0f, -0.6f, 0.6f};
// Stereo placement (0 = left, 1 = right).
static const float kPan[kNumVoices] = {0.15f, 0.7f, 0.3f, 0.85f, 0.25f, 0.75f};

// ------------------------------------------------------------------ patches
// Everything that makes up a sound. Laid out with no padding so it can be
// stored in flash and compared with memcmp.
struct Patch
{
    float   v[kNumParamPages][2]; // control values per page, 0..1
    uint8_t wave;                 // index into kWaves
    uint8_t interval;             // index into kIntervals
    uint8_t sub;                  // 0 / 1
    uint8_t reserved;
};

//                         root  detune   cutoff  res    rate   depth   mix   size   wave int sub
static const Patch kFactory[kNumSlots] = {
    {{{0.33f, 0.35f}, {0.45f, 0.25f}, {0.35f, 0.50f}, {0.40f, 0.70f}}, 0, 1, 1, 0}, // 1 Classic fifth
    {{{0.50f, 0.15f}, {0.70f, 0.10f}, {0.20f, 0.30f}, {0.60f, 0.85f}}, 3, 2, 0, 0}, // 2 Glass
    {{{0.25f, 0.20f}, {0.30f, 0.95f}, {0.25f, 0.60f}, {0.50f, 0.80f}}, 0, 0, 1, 0}, // 3 Singing filter
    {{{0.20f, 0.80f}, {0.30f, 0.30f}, {0.50f, 0.70f}, {0.45f, 0.75f}}, 1, 4, 1, 0}, // 4 Dark swarm
    {{{0.36f, 0.40f}, {0.50f, 0.40f}, {0.10f, 0.90f}, {0.55f, 0.90f}}, 2, 3, 1, 0}, // 5 Tidal
    {{{0.45f, 0.60f}, {0.75f, 0.20f}, {0.45f, 0.40f}, {0.70f, 0.95f}}, 0, 2, 0, 0}, // 6 Bright wash
    {{{0.30f, 0.30f}, {0.40f, 0.50f}, {0.30f, 0.50f}, {0.50f, 0.80f}}, 0, 6, 1, 0}, // 7 Minor seventh
    {{{0.05f, 0.50f}, {0.20f, 0.60f}, {0.15f, 0.80f}, {0.35f, 0.60f}}, 1, 1, 1, 0}, // 8 Low rumble
};

inline void SanitizePatch(Patch &p)
{
    p.wave     = p.wave % kNumWaves;
    p.interval = p.interval % kNumIntervals;
    p.sub      = p.sub ? 1 : 0;
}

// Map a 0..1 value exponentially between lo and hi.
inline float ExpMap(float x, float lo, float hi)
{
    return lo * powf(hi / lo, x);
}

// The root as a MIDI note: 24..60 (C1..C4), quantised to semitones.
inline float RootNote(const Patch &p)
{
    return roundf(24.f + p.v[0][0] * 36.f);
}

enum class InputMode
{
    DroneOnly,      // audio input ignored (the Pod, or Legio switch up)
    DronePlusInput, // input mixed into the filter alongside the drone
    InputOnly,      // oscillators off: the filter, motion and reverb process the input
};

// ------------------------------------------------------------------- engine
class Engine
{
  public:
    // `verb` must outlive the engine; put it in SDRAM (DSY_SDRAM_BSS).
    void Init(float sample_rate, size_t block_size, ReverbSc *verb)
    {
        sr_   = sample_rate;
        verb_ = verb;
        const float block_rate = sample_rate / (float)block_size;

        for(int i = 0; i < kNumVoices; i++)
        {
            osc_[i].Init(sample_rate);
            osc_[i].SetWaveform(kWaves[0]);
            osc_[i].SetAmp(0.18f);
            osc_[i].PhaseAdd(i * 0.17f); // avoid all voices starting in phase

            drift_[i].Init(block_rate);
            drift_[i].SetFreq(0.05f + 0.03f * i); // each voice wanders at its own pace

            // Constant-power pan
            gainL_[i] = cosf(kPan[i] * HALFPI_F);
            gainR_[i] = sinf(kPan[i] * HALFPI_F);
        }

        sub_.Init(sample_rate);
        sub_.SetWaveform(Oscillator::WAVE_SIN);
        sub_.SetAmp(1.f);

        filtL_.Init(sample_rate);
        filtR_.Init(sample_rate);
        filtL_.SetPassbandGain(0.25f);
        filtR_.SetPassbandGain(0.25f);
        filtL_.SetInputDrive(1.2f);
        filtR_.SetInputDrive(1.2f);

        verb_->Init(sample_rate);
        verb_->SetLpFreq(9000.f);
    }

    // Jump the smoothed pitch and cutoff straight to a patch's values, so
    // the first sound at power-up doesn't glide in from elsewhere.
    void JumpTo(const Patch &p)
    {
        rootSm_   = RootNote(p);
        cutoffSm_ = ExpMap(p.v[1][0], 40.f, 8000.f);
    }

    // Phase of the filter LFO as -1..1, for LEDs.
    float Lfo() const { return lfoL_; }

    // Render one interleaved stereo block.
    //   pitch_cv : semitones added to the root (e.g. from a 1 V/oct input)
    void Process(const Patch &p,
                 float        pitch_cv,
                 InputMode    mode,
                 const float *in,
                 float       *out,
                 size_t       size)
    {
        const size_t frames = size / 2;
        const float  dt     = (float)frames / sr_;

        // ---- Map patch values to parameters
        // Page 0: root (MIDI 24..60, quantised to semitones), detune 0..30 cents
        const float rootTarget = RootNote(p);
        const float detuneCt   = p.v[0][1] * p.v[0][1] * 30.f;
        // Page 1: cutoff 40 Hz..8 kHz, resonance 0..1.5
        const float cutoffBase = ExpMap(p.v[1][0], 40.f, 8000.f);
        const float res        = p.v[1][1] * 1.5f;
        // Page 2: motion rate 0.005..0.5 Hz, depth 0..1
        const float motionHz = ExpMap(p.v[2][0], 0.005f, 0.5f);
        const float depth    = p.v[2][1];
        // Page 3: reverb mix 0..1, feedback 0.6..0.985
        const float mix      = p.v[3][0];
        const float feedback = 0.6f + p.v[3][1] * 0.385f;

        // ---- Slow modulation, updated once per block
        lfoPhase_ += motionHz * dt;
        if(lfoPhase_ >= 1.f)
            lfoPhase_ -= 1.f;
        lfo2Phase_ += motionHz * 0.37f * dt;
        if(lfo2Phase_ >= 1.f)
            lfo2Phase_ -= 1.f;

        const float lfoL = sinf(TWOPI_F * lfoPhase_);
        const float lfoR = sinf(TWOPI_F * (lfoPhase_ + 0.25f)); // quarter-cycle offset
        const float lfo2 = sinf(TWOPI_F * lfo2Phase_);
        lfoL_            = lfoL;

        // Glide the root slowly so pitch changes swell rather than jump.
        // Pitch CV is smoothed much faster (about 20 ms) so it tracks a
        // sequencer but doesn't step audibly.
        fonepole(rootSm_, rootTarget, 0.004f);
        fonepole(cvSm_, pitch_cv, 0.05f);
        fonepole(cutoffSm_, cutoffBase, 0.05f);
        fonepole(ampSm_, 1.f - depth * 0.3f * (0.5f + 0.5f * lfo2), 0.05f);

        // Filter sweeps up to +-2 octaves around the base cutoff.
        const float sweep = depth * 2.f;
        filtL_.SetFreq(fclamp(cutoffSm_ * exp2f(lfoL * sweep), 20.f, 16000.f));
        filtR_.SetFreq(fclamp(cutoffSm_ * exp2f(lfoR * sweep), 20.f, 16000.f));
        filtL_.SetRes(res);
        filtR_.SetRes(res);

        verb_->SetFeedback(feedback);

        // Oscillator pitches: detune spread + random drift (more with depth).
        const float root     = fclamp(rootSm_ + cvSm_, 12.f, 108.f);
        const float driftCt  = 2.f + depth * 10.f;
        const float interval = kIntervals[p.interval % kNumIntervals];
        const uint8_t wave   = kWaves[p.wave % kNumWaves];
        for(int i = 0; i < kNumVoices; i++)
        {
            const float semis = (i < kNumRoot ? 0.f : interval);
            float       cents = kSpread[i] * detuneCt;
            // SmoothRandomGenerator outputs -1..1; it's advanced per block,
            // so its rate is set relative to the block rate in Init.
            cents += drift_[i].Process() * driftCt;
            osc_[i].SetFreq(mtof(root + semis + cents * 0.01f));
            osc_[i].SetWaveform(wave);
        }
        sub_.SetFreq(mtof(root - 12.f));

        const bool  drone  = mode != InputMode::InputOnly;
        const float inGain = mode == InputMode::DroneOnly ? 0.f : 1.f;
        const float subAmp = p.sub ? 0.3f : 0.f;
        // Equal-power dry/wet crossfade
        const float dryG = cosf(mix * HALFPI_F);
        const float wetG = sinf(mix * HALFPI_F);

        for(size_t i = 0; i < size; i += 2)
        {
            float l = 0.f, r = 0.f;
            if(drone)
            {
                for(int v = 0; v < kNumVoices; v++)
                {
                    const float s = osc_[v].Process();
                    l += s * gainL_[v];
                    r += s * gainR_[v];
                }
                const float s = sub_.Process() * subAmp;
                l += s;
                r += s;
            }
            if(inGain > 0.f)
            {
                l += in[i] * inGain;
                r += in[i + 1] * inGain;
            }

            l = filtL_.Process(l) * ampSm_;
            r = filtR_.Process(r) * ampSm_;

            float wl, wr;
            verb_->Process(l, r, &wl, &wr);

            out[i]     = SoftClip(l * dryG + wl * wetG);
            out[i + 1] = SoftClip(r * dryG + wr * wetG);
        }
    }

  private:
    float                 sr_   = 48000.f;
    ReverbSc             *verb_ = nullptr;
    Oscillator            osc_[kNumVoices];
    Oscillator            sub_;
    SmoothRandomGenerator drift_[kNumVoices];
    LadderFilter          filtL_, filtR_;
    float                 gainL_[kNumVoices], gainR_[kNumVoices];

    float lfoPhase_  = 0.f; // filter motion
    float lfo2Phase_ = 0.f; // amplitude breathing
    float lfoL_      = 0.f;
    float rootSm_    = 36.f; // smoothed MIDI root (glides between notes)
    float cvSm_      = 0.f;  // smoothed pitch CV in semitones
    float cutoffSm_  = 800.f;
    float ampSm_     = 1.f;
};

} // namespace dronevoice
