// DroneVoice for the Daisy Pod
//
// The sound lives in ../common/drone_engine.h; this file is the Pod's
// controls, LEDs and patch storage.
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
#include "../common/drone_engine.h"
#include <cstring>

using namespace daisy;
using namespace daisysp;
using namespace dronevoice;

static DaisyPod              pod;
static Engine                engine;
static ReverbSc DSY_SDRAM_BSS verb;

// ------------------------------------------------------------- storage
static constexpr int kPatchPage = 4;
static constexpr int kNumPages  = 5;

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
static uint32_t blockCount = 0;

// Saving erases flash, which takes too long for the audio callback, so the
// callback hands the request to the main loop.
static volatile bool pendingSave = false;
static Patch         pendingPatch;
static int           pendingSlot = 0;

// LED 1 flash feedback for load / save
static int   flashMs = 0;
static float flashCol[3];

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
    float      *vals   = cur.v[page];
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

    blockCount++;
    if(flashMs > 0)
        flashMs -= (int)((size / 2) * 1000.f / pod.AudioSampleRate() + 0.5f);

    // The Pod's audio input is not used.
    engine.Process(cur, 0.f, InputMode::DroneOnly, in, out, size);

    UpdateLeds(engine.Lfo());
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

    InitStorage();
    engine.Init(pod.AudioSampleRate(), 48, &verb);
    engine.JumpTo(cur); // start on pitch, no glide

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
