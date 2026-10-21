// DroneVoice — a slow-moving stereo drone voice for the Daisy Pod
//
// Signal path:
//   6 detuned oscillators (4 at root, 2 at a selectable interval)
//   + optional sine sub an octave down
//   -> stereo 24 dB ladder filters swept by a slow LFO
//   -> ReverbSc (lives in SDRAM)
//
// Controls
//   Encoder turn  : change page (LED 1 colour shows the page)
//   Encoder press : cycle waveform  saw -> square -> triangle -> sine
//   Button 1      : cycle interval of the upper pair
//                   unison, 5th, octave, 4th, min 3rd, maj 3rd, min 7th
//   Button 2      : sub oscillator on/off
//
//   Page   LED 1    Knob 1                 Knob 2
//   0      red      root pitch (C1..C4)    detune spread
//   1      green    filter cutoff          resonance
//   2      blue     motion rate            motion depth
//   3      purple   reverb mix             reverb size
//   4      white    patch slot (1..8)      -
//
// Patch page (4):
//   Knob 1 picks one of 8 slots; LED 2 shows its colour
//     (red, orange, yellow, green, cyan, blue, purple, white).
//     LED 2 is steady on the slot that is currently loaded, blinking otherwise.
//   Button 1        : load the selected slot (LED 1 flashes green)
//   Button 2 (hold) : after 1 second, save to the selected slot (LED 1 flashes red)
//   The last slot loaded or saved is recalled at power-up.
//   All 8 slots start out as factory presets.
//
// Knobs use soft takeover: after a page change or patch load a knob does
// nothing until you sweep it past the stored value. LED 1 is dim while a
// knob on the current page hasn't been picked up yet, bright once both have.
// LED 2 breathes with the motion LFO (white = sub on, amber = sub off).

#include "daisy_pod.h"
#include "daisysp.h"
#include "daisysp-lgpl.h"
#include <cmath>
#include <cstring>

using namespace daisy;
using namespace daisysp;

static DaisyPod pod;

// ---------------------------------------------------------------- voices
static constexpr int kNumVoices = 6;
static constexpr int kNumRoot   = 4; // voices 0..3 at root, 4..5 at interval

static Oscillator            osc[kNumVoices];
static Oscillator            sub;
static SmoothRandomGenerator drift[kNumVoices];
static LadderFilter          filtL, filtR;
static ReverbSc DSY_SDRAM_BSS verb;

// Detune position of each voice, -1..+1, scaled by the detune knob.
static const float kSpread[kNumVoices] = {-1.0f, -0.33f, 0.33f, 1.0f, -0.6f, 0.6f};
// Stereo placement (0 = left, 1 = right).
static const float kPan[kNumVoices] = {0.15f, 0.7f, 0.3f, 0.85f, 0.25f, 0.75f};
static float       gainL[kNumVoices], gainR[kNumVoices];

static const uint8_t kWaves[]  = {Oscillator::WAVE_POLYBLEP_SAW,
                                 Oscillator::WAVE_POLYBLEP_SQUARE,
                                 Oscillator::WAVE_POLYBLEP_TRI,
                                 Oscillator::WAVE_SIN};
static constexpr int kNumWaves = sizeof(kWaves) / sizeof(kWaves[0]);

static const float   kIntervals[]  = {0.f, 7.f, 12.f, 5.f, 3.f, 4.f, 10.f};
static constexpr int kNumIntervals = sizeof(kIntervals) / sizeof(kIntervals[0]);

// ------------------------------------------------------------- patches
static constexpr int kNumParamPages = 4;
static constexpr int kPatchPage     = 4;
static constexpr int kNumPages      = 5;
static constexpr int kNumSlots      = 8;

// Everything that makes up a sound. Laid out with no padding so it can be
// compared with memcmp.
struct Patch
{
    float   v[kNumParamPages][2]; // knob values per page, 0..1
    uint8_t wave;                 // index into kWaves
    uint8_t interval;             // index into kIntervals
    uint8_t sub;                  // 0 / 1
    uint8_t reserved;
};

struct Store
{
    uint32_t version;
    Patch    slots[kNumSlots];
    uint8_t  bootSlot;
    uint8_t  reserved[3];

    bool operator==(const Store &o) const
    {
        return memcmp(this, &o, sizeof(Store)) == 0;
    }
    bool operator!=(const Store &o) const { return !(*this == o); }
};

// Bump this if the layout of Patch or Store changes; stored data with a
// different version is replaced by the factory presets.
static constexpr uint32_t kStoreVersion = 0xD201;

//                    root  detune   cutoff  res    rate   depth   mix   size   wave int sub
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

// Stored about 4 MB into the 8 MB QSPI flash, well clear of any program
// image a bootloader might place at the start of the chip.
static constexpr uint32_t kStoreOffset = 0x400000;
static PersistentStorage<Store> storage(pod.seed.qspi);

// ------------------------------------------------------------- UI state
static Patch cur;               // the live sound
static int   page        = 0;
static bool  picked[2]   = {false, false};
static int   curSlot     = 0;   // slot last loaded or saved
static int   selSlot     = 0;   // slot under knob 1 on the patch page
static bool  saveLatched = false;

// Saving erases flash, which takes too long for the audio callback, so the
// callback hands the request to the main loop.
static volatile bool pendingSave = false;
static Patch         pendingPatch;
static int           pendingSlot = 0;

// LED 1 flash feedback for load / save
static int   flashMs = 0;
static float flashCol[3];

// --------------------------------------------------------- DSP state
static float    sampleRate;
static float    lfoPhase  = 0.f; // filter motion
static float    lfo2Phase = 0.f; // amplitude breathing
static float    rootSm    = 36.f; // smoothed MIDI root (glides between notes)
static float    cutoffSm  = 800.f;
static float    ampSm     = 1.f;
static uint32_t blockCount = 0;

// Map a 0..1 value exponentially between lo and hi.
static inline float ExpMap(float x, float lo, float hi)
{
    return lo * powf(hi / lo, x);
}

static void Flash(float r, float g, float b)
{
    flashCol[0] = r;
    flashCol[1] = g;
    flashCol[2] = b;
    flashMs     = 400;
}

static void LoadPatch(const Patch &p)
{
    cur          = p;
    cur.wave     = cur.wave % kNumWaves;
    cur.interval = cur.interval % kNumIntervals;
    picked[0] = picked[1] = false; // knobs must be picked up again
}

static void UpdateControls()
{
    pod.ProcessAllControls();

    // Page change
    int inc = pod.encoder.Increment();
    if(inc != 0)
    {
        page      = (page + inc + kNumPages) % kNumPages;
        picked[0] = picked[1] = false;
    }

    if(pod.encoder.RisingEdge())
        cur.wave = (cur.wave + 1) % kNumWaves;

    if(page == kPatchPage)
    {
        // Slot select follows knob 1 directly (no takeover needed).
        selSlot = (int)(pod.knob1.Value() * kNumSlots);
        if(selSlot >= kNumSlots)
            selSlot = kNumSlots - 1;

        if(pod.button1.RisingEdge())
        {
            LoadPatch(storage.GetSettings().slots[selSlot]);
            curSlot = selSlot;
            Flash(0.f, 1.f, 0.f);
            // Remember it for power-up (written by the main loop)
            pendingPatch = cur;
            pendingSlot  = -1 - selSlot; // negative = only update bootSlot
            pendingSave  = true;
        }

        if(!pod.button2.Pressed())
            saveLatched = false;
        else if(!saveLatched && pod.button2.TimeHeldMs() >= 1000.f)
        {
            saveLatched  = true;
            pendingPatch = cur;
            pendingSlot  = selSlot;
            pendingSave  = true;
            curSlot      = selSlot;
            Flash(1.f, 0.f, 0.f);
        }
        return;
    }

    // Soft-takeover knobs
    float *vals = cur.v[page];
    const float raw[2] = {pod.knob1.Value(), pod.knob2.Value()};
    for(int k = 0; k < 2; k++)
    {
        if(!picked[k] && fabsf(raw[k] - vals[k]) < 0.03f)
            picked[k] = true;
        if(picked[k])
            vals[k] = raw[k];
    }

    if(pod.button1.RisingEdge())
        cur.interval = (cur.interval + 1) % kNumIntervals;
    if(pod.button2.RisingEdge())
        cur.sub = !cur.sub;
}

static void UpdateLeds(float lfo)
{
    // Page colours: red, green, blue, purple, white
    static const float kPageCol[kNumPages][3] = {{1.f, 0.f, 0.f},
                                                 {0.f, 1.f, 0.f},
                                                 {0.f, 0.f, 1.f},
                                                 {0.7f, 0.f, 1.f},
                                                 {1.f, 1.f, 1.f}};
    // Slot colours: red, orange, yellow, green, cyan, blue, purple, white
    static const float kSlotCol[kNumSlots][3] = {{1.f, 0.f, 0.f},
                                                 {1.f, 0.35f, 0.f},
                                                 {1.f, 1.f, 0.f},
                                                 {0.f, 1.f, 0.f},
                                                 {0.f, 1.f, 1.f},
                                                 {0.f, 0.f, 1.f},
                                                 {0.7f, 0.f, 1.f},
                                                 {1.f, 1.f, 1.f}};

    // LED 1: page colour, or a fast blink of the flash colour after load/save
    if(flashMs > 0)
    {
        const bool on = (flashMs / 50) % 2 == 0;
        pod.led1.Set(on ? flashCol[0] : 0.f,
                     on ? flashCol[1] : 0.f,
                     on ? flashCol[2] : 0.f);
    }
    else
    {
        const bool  full = page == kPatchPage || (picked[0] && picked[1]);
        const float b    = full ? 1.0f : 0.2f;
        pod.led1.Set(
            kPageCol[page][0] * b, kPageCol[page][1] * b, kPageCol[page][2] * b);
    }

    // LED 2: selected slot on the patch page, otherwise breathes with the LFO
    if(page == kPatchPage)
    {
        const bool  blinkOff = selSlot != curSlot && (blockCount / 250) % 2;
        const float b        = blinkOff ? 0.f : 0.8f;
        pod.led2.Set(kSlotCol[selSlot][0] * b,
                     kSlotCol[selSlot][1] * b,
                     kSlotCol[selSlot][2] * b);
    }
    else
    {
        const float breath = 0.1f + 0.6f * (0.5f + 0.5f * lfo);
        if(cur.sub)
            pod.led2.Set(breath, breath, breath);
        else
            pod.led2.Set(breath, breath * 0.45f, 0.f);
    }
    pod.UpdateLeds();
}

static void AudioCallback(AudioHandle::InterleavingInputBuffer  in,
                          AudioHandle::InterleavingOutputBuffer out,
                          size_t                                size)
{
    UpdateControls();

    const size_t frames = size / 2;
    const float  dt     = (float)frames / sampleRate;
    blockCount++;
    if(flashMs > 0)
        flashMs -= (int)(dt * 1000.f + 0.5f);

    // ---- Map patch values to parameters
    // Page 0: root (MIDI 24..60, quantised to semitones), detune 0..30 cents
    const float rootTarget = roundf(24.f + cur.v[0][0] * 36.f);
    const float detuneCt   = cur.v[0][1] * cur.v[0][1] * 30.f;
    // Page 1: cutoff 40 Hz..8 kHz, resonance 0..1.5
    const float cutoffBase = ExpMap(cur.v[1][0], 40.f, 8000.f);
    const float res        = cur.v[1][1] * 1.5f;
    // Page 2: motion rate 0.005..0.5 Hz, depth 0..1
    const float motionHz = ExpMap(cur.v[2][0], 0.005f, 0.5f);
    const float depth    = cur.v[2][1];
    // Page 3: reverb mix 0..1, feedback 0.6..0.985
    const float mix      = cur.v[3][0];
    const float feedback = 0.6f + cur.v[3][1] * 0.385f;

    // ---- Slow modulation, updated once per block
    lfoPhase += motionHz * dt;
    if(lfoPhase >= 1.f)
        lfoPhase -= 1.f;
    lfo2Phase += motionHz * 0.37f * dt;
    if(lfo2Phase >= 1.f)
        lfo2Phase -= 1.f;

    const float lfoL = sinf(TWOPI_F * lfoPhase);
    const float lfoR = sinf(TWOPI_F * (lfoPhase + 0.25f)); // quarter-cycle offset
    const float lfo2 = sinf(TWOPI_F * lfo2Phase);

    // Glide the root slowly so pitch changes swell rather than jump.
    fonepole(rootSm, rootTarget, 0.004f);
    fonepole(cutoffSm, cutoffBase, 0.05f);
    fonepole(ampSm, 1.f - depth * 0.3f * (0.5f + 0.5f * lfo2), 0.05f);

    // Filter sweeps up to +-2 octaves around the base cutoff.
    const float sweep = depth * 2.f;
    filtL.SetFreq(fclamp(cutoffSm * exp2f(lfoL * sweep), 20.f, 16000.f));
    filtR.SetFreq(fclamp(cutoffSm * exp2f(lfoR * sweep), 20.f, 16000.f));
    filtL.SetRes(res);
    filtR.SetRes(res);

    verb.SetFeedback(feedback);

    // Oscillator pitches: detune spread + random drift (more with depth).
    const float driftCt  = 2.f + depth * 10.f;
    const float interval = kIntervals[cur.interval];
    for(int i = 0; i < kNumVoices; i++)
    {
        const float semis = (i < kNumRoot ? 0.f : interval);
        float       cents = kSpread[i] * detuneCt;
        // SmoothRandomGenerator outputs -1..1; it's advanced per block, so
        // its rate is set relative to the block rate in Init.
        cents += drift[i].Process() * driftCt;
        osc[i].SetFreq(mtof(rootSm + semis + cents * 0.01f));
        osc[i].SetWaveform(kWaves[cur.wave]);
    }
    sub.SetFreq(mtof(rootSm - 12.f));

    UpdateLeds(lfoL);

    const float subAmp = cur.sub ? 0.3f : 0.f;
    // Equal-power dry/wet crossfade
    const float dryG = cosf(mix * HALFPI_F);
    const float wetG = sinf(mix * HALFPI_F);

    for(size_t i = 0; i < size; i += 2)
    {
        float l = 0.f, r = 0.f;
        for(int v = 0; v < kNumVoices; v++)
        {
            const float s = osc[v].Process();
            l += s * gainL[v];
            r += s * gainR[v];
        }
        const float s = sub.Process() * subAmp;
        l += s;
        r += s;

        l = filtL.Process(l) * ampSm;
        r = filtR.Process(r) * ampSm;

        float wl, wr;
        verb.Process(l, r, &wl, &wr);

        out[i]     = SoftClip(l * dryG + wl * wetG);
        out[i + 1] = SoftClip(r * dryG + wr * wetG);
    }
}

static void InitStorage()
{
    Store defaults;
    memset(&defaults, 0, sizeof(defaults));
    defaults.version = kStoreVersion;
    for(int i = 0; i < kNumSlots; i++)
        defaults.slots[i] = kFactory[i];
    defaults.bootSlot = 0;

    storage.Init(defaults, kStoreOffset);
    if(storage.GetSettings().version != kStoreVersion)
        storage.RestoreDefaults();

    const Store &s = storage.GetSettings();
    curSlot        = s.bootSlot < kNumSlots ? s.bootSlot : 0;
    selSlot        = curSlot;
    LoadPatch(s.slots[curSlot]);
}

int main(void)
{
    pod.Init();
    pod.SetAudioBlockSize(48); // 1 ms at 48 kHz; controls update per block
    sampleRate = pod.AudioSampleRate();
    const float blockRate = sampleRate / 48.f;

    InitStorage();
    rootSm   = roundf(24.f + cur.v[0][0] * 36.f); // start on pitch, no glide
    cutoffSm = ExpMap(cur.v[1][0], 40.f, 8000.f);

    for(int i = 0; i < kNumVoices; i++)
    {
        osc[i].Init(sampleRate);
        osc[i].SetWaveform(kWaves[cur.wave]);
        osc[i].SetAmp(0.18f);
        osc[i].PhaseAdd(i * 0.17f); // avoid all voices starting in phase

        drift[i].Init(blockRate);
        drift[i].SetFreq(0.05f + 0.03f * i); // each voice wanders at its own pace

        // Constant-power pan
        gainL[i] = cosf(kPan[i] * HALFPI_F);
        gainR[i] = sinf(kPan[i] * HALFPI_F);
    }

    sub.Init(sampleRate);
    sub.SetWaveform(Oscillator::WAVE_SIN);
    sub.SetAmp(1.f);

    filtL.Init(sampleRate);
    filtR.Init(sampleRate);
    filtL.SetPassbandGain(0.25f);
    filtR.SetPassbandGain(0.25f);
    filtL.SetInputDrive(1.2f);
    filtR.SetInputDrive(1.2f);

    verb.Init(sampleRate);
    verb.SetLpFreq(9000.f);

    pod.StartAdc();
    pod.StartAudio(AudioCallback);

    while(1)
    {
        if(pendingSave)
        {
            Store &s = storage.GetSettings();
            if(pendingSlot >= 0)
            {
                s.slots[pendingSlot] = pendingPatch;
                s.bootSlot           = (uint8_t)pendingSlot;
            }
            else
            {
                s.bootSlot = (uint8_t)(-1 - pendingSlot);
            }
            pendingSave = false;
            storage.Save(); // only erases/writes if something changed
        }
    }
}
