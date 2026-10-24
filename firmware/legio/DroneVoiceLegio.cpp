// DroneVoice for Noise Engineering Legio
//
// The sound lives in ../common/drone_engine.h; this file is Legio's
// controls, LEDs, pitch input and patch storage.
//
// Controls
//   Encoder turn   : change page (left LED colour shows the page)
//   Encoder press  : sub oscillator on/off
//                    (on the Patch page: press to load, hold 1 s to save)
//   Left switch    : pitch input mode
//                      up     quantised to semitones
//                      centre free (smooth glides)
//                      down   sample and hold on each gate, quantised
//   Right switch   : audio input
//                      up     drone only
//                      centre drone + input, both through filter and reverb
//                      down   input only (Legio becomes a filter/reverb effect)
//   Pitch input    : 1 V/oct, added to the root
//   Gate input     : left switch down: samples the pitch input
//                    otherwise: steps to the next interval
//
//   Page   Left LED   Top knob               Bottom knob
//   0      red        root pitch (C1..C4)    detune spread
//   1      green      filter cutoff          resonance
//   2      blue       motion rate            motion depth
//   3      purple     reverb mix             reverb size
//   4      yellow     waveform (4 zones)     interval (7 zones)
//   5      white      patch slot (1..8)      -
//
// Knobs use soft takeover: after a page change or patch load a knob does
// nothing until you turn it past the stored value (on page 4, until it
// reaches the zone of the current setting). The left LED is dim until both
// knobs on the page have been picked up.
// The right LED breathes with the motion LFO (white = sub on, amber = off).
// On the Patch page it shows the selected slot's colour instead: steady if
// that slot is loaded, blinking if not.
//
// Pitch calibration: hold the encoder while powering up, then follow the
// LEDs (see README). Until calibrated, the pitch input reading at power-up
// is treated as 0 V and a 10 V span is assumed.

#include "daisy_legio.h"
#include "../common/drone_engine.h"
#include <cstring>

using namespace daisy;
using namespace daisysp;
using namespace dronevoice;

static DaisyLegio            hw;
static Engine                engine;
static ReverbSc DSY_SDRAM_BSS verb;

static constexpr int kVoicePage = 4;
static constexpr int kPatchPage = 5;
static constexpr int kNumPages  = 6;

// ------------------------------------------------------------- storage
struct Store
{
    uint32_t version;
    Patch    slots[kNumSlots];
    uint8_t  bootSlot;
    uint8_t  calibrated;
    uint8_t  reserved[2];
    float    pitchZero;     // pitch input reading at 0 V
    float    pitchPerVolt;  // change in reading per volt

    bool operator==(const Store &o) const
    {
        return memcmp(this, &o, sizeof(Store)) == 0;
    }
    bool operator!=(const Store &o) const { return !(*this == o); }
};

static constexpr uint32_t kStoreVersion  = 0xD2A1;
static constexpr uint32_t kStoreOffset   = 0x400000;
static constexpr float    kDefaultPerVolt = 0.1f; // assumes a 10 V span
static PersistentStorage<Store> storage(hw.seed.qspi);

// Active pitch calibration (copied from storage, or the defaults)
static float pitchZero    = 0.5f;
static float pitchPerVolt = kDefaultPerVolt;

// ------------------------------------------------------------- UI state
static Patch    cur;
static int      page        = 0;
static bool     picked[2]   = {false, false};
static int      curSlot     = 0;
static int      selSlot     = 0;
static bool     saveLatched = false;
static bool     lastGate    = false;
static float    heldSemis   = 0.f;
static uint32_t blockCount  = 0;

static volatile bool pendingSave = false;
static Patch         pendingPatch;
static int           pendingSlot = 0;

static int   flashMs = 0;
static float flashCol[3];

static const float kSlotCol[kNumSlots][3] = {{1.f, 0.f, 0.f},
                                             {1.f, 0.35f, 0.f},
                                             {1.f, 1.f, 0.f},
                                             {0.f, 1.f, 0.f},
                                             {0.f, 1.f, 1.f},
                                             {0.f, 0.f, 1.f},
                                             {0.7f, 0.f, 1.f},
                                             {1.f, 1.f, 1.f}};

static void Flash(float r, float g, float b)
{
    flashCol[0] = r;
    flashCol[1] = g;
    flashCol[2] = b;
    flashMs     = 400;
}

static void LoadPatch(const Patch &p)
{
    cur = p;
    SanitizePatch(cur);
    picked[0] = picked[1] = false;
}

static float PitchSemis()
{
    const float raw   = hw.controls[DaisyLegio::CONTROL_PITCH].Value();
    const float volts = (raw - pitchZero) / pitchPerVolt;
    return fclamp(volts * 12.f, -60.f, 60.f);
}

static void UpdateControls()
{
    hw.ProcessAllControls();

    // Page change
    const int inc = hw.encoder.Increment();
    if(inc != 0)
    {
        page      = (page + inc + kNumPages) % kNumPages;
        picked[0] = picked[1] = false;
    }

    // Gate: sample & hold the pitch, or step the interval
    const bool gate   = hw.Gate();
    const bool rising = gate && !lastGate;
    lastGate          = gate;
    const int pitchMode = hw.sw[DaisyLegio::SW_LEFT].Read();
    if(rising)
    {
        if(pitchMode == Switch3::POS_DOWN)
            heldSemis = roundf(PitchSemis());
        else
        {
            cur.interval = (cur.interval + 1) % kNumIntervals;
            if(page == kVoicePage)
                picked[1] = false; // don't let the knob snap it back
        }
    }

    const float raw[2] = {hw.controls[DaisyLegio::CONTROL_KNOB_TOP].Value(),
                          hw.controls[DaisyLegio::CONTROL_KNOB_BOTTOM].Value()};

    if(page == kPatchPage)
    {
        selSlot = (int)(raw[0] * kNumSlots);
        if(selSlot >= kNumSlots)
            selSlot = kNumSlots - 1;

        // Press = load (on release), hold 1 s = save
        if(hw.encoder.Pressed() && !saveLatched
           && hw.encoder.TimeHeldMs() >= 1000.f)
        {
            saveLatched  = true;
            pendingPatch = cur;
            pendingSlot  = selSlot;
            pendingSave  = true;
            curSlot      = selSlot;
            Flash(1.f, 0.f, 0.f);
        }
        if(hw.encoder.FallingEdge())
        {
            if(!saveLatched)
            {
                LoadPatch(storage.GetSettings().slots[selSlot]);
                curSlot = selSlot;
                Flash(0.f, 1.f, 0.f);
                pendingPatch = cur;
                pendingSlot  = -1 - selSlot; // only update bootSlot
                pendingSave  = true;
            }
            saveLatched = false;
        }
        return;
    }

    if(hw.encoder.RisingEdge())
        cur.sub = !cur.sub;

    if(page == kVoicePage)
    {
        // Discrete settings: a knob picks up once it reaches the zone of
        // the current setting.
        int zone[2] = {(int)(raw[0] * kNumWaves), (int)(raw[1] * kNumIntervals)};
        if(zone[0] >= kNumWaves)
            zone[0] = kNumWaves - 1;
        if(zone[1] >= kNumIntervals)
            zone[1] = kNumIntervals - 1;
        if(!picked[0] && zone[0] == cur.wave)
            picked[0] = true;
        if(!picked[1] && zone[1] == cur.interval)
            picked[1] = true;
        if(picked[0])
            cur.wave = (uint8_t)zone[0];
        if(picked[1])
            cur.interval = (uint8_t)zone[1];
        return;
    }

    // Soft-takeover knobs
    float *vals = cur.v[page];
    for(int k = 0; k < 2; k++)
    {
        if(!picked[k] && fabsf(raw[k] - vals[k]) < 0.03f)
            picked[k] = true;
        if(picked[k])
            vals[k] = raw[k];
    }
}

static void UpdateLeds(float lfo)
{
    // Page colours: red, green, blue, purple, yellow, white
    static const float kPageCol[kNumPages][3] = {{1.f, 0.f, 0.f},
                                                 {0.f, 1.f, 0.f},
                                                 {0.f, 0.f, 1.f},
                                                 {0.7f, 0.f, 1.f},
                                                 {1.f, 0.8f, 0.f},
                                                 {1.f, 1.f, 1.f}};

    if(flashMs > 0)
    {
        const bool on = (flashMs / 50) % 2 == 0;
        hw.SetLed(DaisyLegio::LED_LEFT,
                  on ? flashCol[0] : 0.f,
                  on ? flashCol[1] : 0.f,
                  on ? flashCol[2] : 0.f);
    }
    else
    {
        const bool  full = page == kPatchPage || (picked[0] && picked[1]);
        const float b    = full ? 1.0f : 0.2f;
        hw.SetLed(DaisyLegio::LED_LEFT,
                  kPageCol[page][0] * b,
                  kPageCol[page][1] * b,
                  kPageCol[page][2] * b);
    }

    if(page == kPatchPage)
    {
        const bool  blinkOff = selSlot != curSlot && (blockCount / 250) % 2;
        const float b        = blinkOff ? 0.f : 0.8f;
        hw.SetLed(DaisyLegio::LED_RIGHT,
                  kSlotCol[selSlot][0] * b,
                  kSlotCol[selSlot][1] * b,
                  kSlotCol[selSlot][2] * b);
    }
    else
    {
        const float breath = 0.1f + 0.6f * (0.5f + 0.5f * lfo);
        if(cur.sub)
            hw.SetLed(DaisyLegio::LED_RIGHT, breath, breath, breath);
        else
            hw.SetLed(DaisyLegio::LED_RIGHT, breath, breath * 0.45f, 0.f);
    }
    hw.UpdateLeds();
}

static void AudioCallback(AudioHandle::InterleavingInputBuffer  in,
                          AudioHandle::InterleavingOutputBuffer out,
                          size_t                                size)
{
    UpdateControls();

    blockCount++;
    if(flashMs > 0)
        flashMs -= (int)((size / 2) * 1000.f / hw.AudioSampleRate() + 0.5f);

    // Pitch input
    float     semis;
    const int pitchMode = hw.sw[DaisyLegio::SW_LEFT].Read();
    if(pitchMode == Switch3::POS_UP)
        semis = roundf(PitchSemis());
    else if(pitchMode == Switch3::POS_CENTER)
        semis = PitchSemis();
    else
        semis = heldSemis;

    // Audio input
    InputMode mode;
    switch(hw.sw[DaisyLegio::SW_RIGHT].Read())
    {
        case Switch3::POS_UP: mode = InputMode::DroneOnly; break;
        case Switch3::POS_DOWN: mode = InputMode::InputOnly; break;
        default: mode = InputMode::DronePlusInput; break;
    }

    engine.Process(cur, semis, mode, in, out, size);

    UpdateLeds(engine.Lfo());
}

// ------------------------------------------------------- calibration
// These run before audio starts, from the main loop.

static float AveragePitchReading(int ms)
{
    float sum = 0.f;
    for(int i = 0; i < ms; i++)
    {
        hw.ProcessAnalogControls();
        sum += hw.controls[DaisyLegio::CONTROL_PITCH].Value();
        hw.DelayMs(1);
    }
    return sum / (float)ms;
}

static void BlinkBoth(float r, float g, float b, uint32_t t)
{
    const bool on = (t / 250) % 2 == 0;
    hw.SetLed(DaisyLegio::LED_LEFT, on ? r : 0.f, on ? g : 0.f, on ? b : 0.f);
    hw.SetLed(DaisyLegio::LED_RIGHT, on ? r : 0.f, on ? g : 0.f, on ? b : 0.f);
    hw.UpdateLeds();
}

// Blink a colour on both LEDs until the encoder is pressed and released.
static void WaitForPress(float r, float g, float b)
{
    for(uint32_t t = 0;; t++)
    {
        hw.ProcessDigitalControls();
        BlinkBoth(r, g, b, t);
        if(hw.encoder.FallingEdge())
            return;
        hw.DelayMs(1);
    }
}

static void ShowFor(float r, float g, float b, uint32_t ms)
{
    for(uint32_t t = 0; t < ms; t++)
    {
        BlinkBoth(r, g, b, t);
        hw.DelayMs(1);
    }
}

static void Calibrate()
{
    // Wait for the encoder held at power-up to be released.
    while(hw.encoder.Pressed())
    {
        hw.ProcessDigitalControls();
        hw.DelayMs(1);
    }

    // Step 1: blue — patch exactly 1 V into the pitch input, press.
    WaitForPress(0.f, 0.f, 1.f);
    hw.DelayMs(300);
    const float r1 = AveragePitchReading(200);

    // Step 2: purple — patch exactly 3 V, press.
    WaitForPress(0.7f, 0.f, 1.f);
    hw.DelayMs(300);
    const float r3 = AveragePitchReading(200);

    const float perVolt = (r3 - r1) * 0.5f;
    if(fabsf(perVolt) < 0.02f)
    {
        // Readings barely changed: probably nothing patched. Keep the old
        // calibration and show red.
        ShowFor(1.f, 0.f, 0.f, 2000);
        return;
    }

    Store &s       = storage.GetSettings();
    s.pitchPerVolt = perVolt;
    s.pitchZero    = r1 - perVolt;
    s.calibrated   = 1;
    storage.Save();
    ShowFor(0.f, 1.f, 0.f, 1000);
}

// -------------------------------------------------------------- startup
static void InitStorage()
{
    Store defaults;
    memset(&defaults, 0, sizeof(defaults));
    defaults.version = kStoreVersion;
    for(int i = 0; i < kNumSlots; i++)
        defaults.slots[i] = kFactory[i];
    defaults.bootSlot     = 0;
    defaults.calibrated   = 0;
    defaults.pitchZero    = 0.5f;
    defaults.pitchPerVolt = kDefaultPerVolt;

    storage.Init(defaults, kStoreOffset);
    if(storage.GetSettings().version != kStoreVersion)
        storage.RestoreDefaults();
}

int main(void)
{
    hw.Init(); // also sets the block size to 48 (1 ms at 48 kHz)

    InitStorage();
    hw.StartAdc();

    // Hold the encoder at power-up to calibrate the pitch input.
    for(int i = 0; i < 30; i++)
    {
        hw.ProcessDigitalControls();
        hw.DelayMs(1);
    }
    if(hw.encoder.Pressed())
        Calibrate();

    const Store &s = storage.GetSettings();
    if(s.calibrated)
    {
        pitchZero    = s.pitchZero;
        pitchPerVolt = s.pitchPerVolt;
    }
    else
    {
        // Treat whatever is at the input now as 0 V.
        pitchZero    = AveragePitchReading(50);
        pitchPerVolt = kDefaultPerVolt;
    }

    curSlot = s.bootSlot < kNumSlots ? s.bootSlot : 0;
    selSlot = curSlot;
    LoadPatch(s.slots[curSlot]);

    engine.Init(hw.AudioSampleRate(), hw.AudioBlockSize(), &verb);
    engine.JumpTo(cur);
    lastGate = hw.Gate();

    hw.StartAudio(AudioCallback);

    while(1)
    {
        if(pendingSave)
        {
            Store &st = storage.GetSettings();
            if(pendingSlot >= 0)
            {
                st.slots[pendingSlot] = pendingPatch;
                st.bootSlot           = (uint8_t)pendingSlot;
            }
            else
            {
                st.bootSlot = (uint8_t)(-1 - pendingSlot);
            }
            pendingSave = false;
            storage.Save();
        }
    }
}
