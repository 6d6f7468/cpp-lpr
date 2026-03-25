
// very cool and fun looper by zane h aka poodlemoth :^)


// ### Uncomment if IntelliSense can't resolve DaisySP-LGPL classes ###
// #include "daisysp-lgpl.h"

#include "daisysp.h"
#include "hothouse.h"
#include <cmath>

using clevelandmusicco::Hothouse;
using daisy::AudioHandle;
using daisy::Led;
using daisy::SaiHandle;

Hothouse hw;



bool first = true;  //first loop (sets length)
bool rec   = false; //currently recording
bool play  = false; //currently playing

#define MAX_SIZE (48000 * 60 * 5) // 5 minutes @ 48k
float DSY_SDRAM_BSS buf[MAX_SIZE]; // declare buffer in SDRAM to store up to 5 minutes of 48khz audio

static int pos = 0, mod = MAX_SIZE, len = 0;
static float drywet_smoothed = 0.5f;

//DL4 playback rate/direction ctrl vars
static float head = 0.0f, rate = 1.0f;
static bool isHalf = false, isRev = false;
static int lastPos = 0;

//footswitch 2 double tap ctrl vars
static bool fs2Pending = false; // true if double tap window is active, false otherwise.
static uint32_t fs2TapMs = 0;
static const uint32_t fs2DoubleTapMs = 300; //time window that fswitch waits for second tap

//footswitch 1 hold ctrl var
static bool fs1Hold = false;

Led LED1, LED2;
bool bypass = true;




static inline void BufWrite(float x)
{
    const float overdub_keep = 1.0f;
    const float overdub_in   = 0.5f;

    buf[pos] = buf[pos] * overdub_keep + x * overdub_in;
    buf[pos] = tanhf(buf[pos]);
}


static inline void BufReset() //clear buffer & reinit vars
{
    play  = false;
    rec   = false;
   // first = true;

    pos = 0;
    len = 0;
    mod = MAX_SIZE;

    head = 0.0f;
    rate = 1.0f;
    isHalf = false;
    isRev    = false;

    fs2Pending = false;
    fs2TapMs = 0;
    lastPos = 0;
    fs1Hold = false;

 //  for(int i = 0; i < mod; i++) //fill loop buffer with 0s
   //    buf[i] = 0.0f;
}

static inline void SetRate()
{
    float mag = isHalf ? 0.5f : 1.0f;
    rate = isRev ? -mag : mag;
}


static inline void fswitchProcess()
{
    const uint32_t now = daisy::System::GetNow();

    // -------- FS1: record/overdub toggle, hold to clear --------
    if(hw.switches[Hothouse::FOOTSWITCH_1].RisingEdge())
    {
        if(first && rec)
        {
            first = false;
            mod   = len;
            len   = 0;

            if(mod > 0)
            {
                while(head >= mod) head -= mod;
                while(head < 0)    head += mod;
            }
        }

        rec = !rec;
        play = true;
        fs1Hold = true;

        if(rec && first)
            lastPos = pos;
    }

    if(fs1Hold && hw.switches[Hothouse::FOOTSWITCH_1].TimeHeldMs() >= 800)
    {
        BufReset();
        fs1Hold = false;
    }

    if(hw.switches[Hothouse::FOOTSWITCH_1].FallingEdge())
        fs1Hold = false;

    // -------- FS2: immediate single tap, second tap converts to double tap --------
    if(hw.switches[Hothouse::FOOTSWITCH_2].RisingEdge())
    {
        if(fs2Pending && (now - fs2TapMs) <= fs2DoubleTapMs)
        {
            // second tap arrived in time:
            // undo the single-tap action we already did
            isHalf = !isHalf;

            // now do the double-tap action
            isRev = !isRev;

            fs2Pending = false;
        }
        else
        {
            
            // treat first tap as a single tap immediately
            isHalf = !isHalf;

            // open double-tap window
            fs2Pending = true;
            fs2TapMs   = now;
            
        }
    }

    // if the window expires, just stop waiting
    if(fs2Pending && (now - fs2TapMs) > fs2DoubleTapMs)
    {
        fs2Pending = false;
    }

    SetRate();
}

static inline float NextSample(float in_samp)
{
    if(mod > 0)
    {
        while(head >= mod) head -= mod;
        while(head < 0)    head += mod;
    }

    pos = (int)head;
    if(pos < 0) pos = 0;
    if(pos >= mod) pos = mod - 1;

    if(rec)
    {
        BufWrite(in_samp);

        if(first && pos != lastPos)
        {
            len++;
            lastPos = pos;
        }
        else if(first && len == 0)
        {
            lastPos = pos;
        }
    }

    float wet = buf[pos];

    if(len >= MAX_SIZE)
    {
        first = false;
        mod   = MAX_SIZE;
        len   = 0;
    }

    if(play)
        head += rate;

    return wet;
}


void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size)
{
    hw.ProcessAllControls();
    fswitchProcess();

    float drywet_target = hw.knobs[Hothouse::KNOB_6].Process();

    for(size_t i = 0; i < size; ++i)
    {
        drywet_smoothed += 0.001f * (drywet_target - drywet_smoothed);

        float xin = in[0][i];
        float wet = NextSample(xin);
        float y;

        if(rec)
        {
            y = wet;
        }
        else
        {
            float dry_gain = cosf(drywet_smoothed * 1.5707963f);
            float wet_gain = sinf(drywet_smoothed * 1.5707963f);
            y = xin * dry_gain + wet * wet_gain * 1.25;
        }

        out[0][i] = y;
        out[1][i] = y;
    }
}

int main() {
  hw.Init();
  hw.SetAudioBlockSize(48);
  hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

  LED1.Init(hw.seed.GetPin(Hothouse::LED_1), false);
  LED2.Init(hw.seed.GetPin(Hothouse::LED_2), false);

  BufReset();

  hw.StartAdc();
  hw.StartAudio(AudioCallback);

  while(true)
  {
      hw.DelayMs(10);

      // LED1: record
      LED1.Set(rec ? 1.0f : (first ? 0.0f : 0.2f));
      LED1.Update();

      // LED2: reverse overrides half
      float s = 0.0f;
      if(isRev) s = 0.2f;
      else if(isHalf) s = 1.0f;
      LED2.Set(s);
      LED2.Update();
  }
}






static inline void StartStop(){
    //Triggered by Footswitch
    
    //Footswitch increments count
    //init count valure

    //if 1 --> Stop
    //if 2 --> Start
    // if > 2 --> set to 1

}