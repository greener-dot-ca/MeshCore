#pragma once

#include <Arduino.h>
#include <NonBlockingRtttl.h>

/* class abstracts underlying RTTTL library 

    Just a simple implementation to start.  At the moment use same
    melody for message and discovery
    Suggest enum type for different sounds
    - on message
    - on discovery

    TODO
    - make message ring tone configurable

*/

// One step of a raw tone sequence (e.g. morse): a tone at `freq` Hz for `ms`,
// or silence when freq == 0. RTTTL can't express morse's 3:1 dit/dah ratio or
// exact gaps, so sequences play through this separate non-blocking path.
struct ToneSeg { uint16_t freq; uint16_t ms; };

class genericBuzzer
{
    public:
        void begin();  // set up buzzer port
        void play(const char *melody); // Generic play function
        void playTones(const ToneSeg *segs, uint8_t n);  // non-blocking raw tone sequence
        void loop();  // loop driven-nonblocking
        void startup();  // play startup sound
        void shutdown();  // play shutdown sound
        bool isPlaying();  // returns true if a sound is still playing else false
        void quiet(bool buzzer_state);  // enables or disables the buzzer
        bool isQuiet();  // get buzzer state on/off

    private:
        void startSeg();   // begin the current tone-sequence step

        // gemini's picks:
        const char *startup_song = "Startup:d=4,o=5,b=160:16c6,16e6,8g6";
        const char *shutdown_song = "Shutdown:d=4,o=5,b=100:8g5,16e5,16c5";

        bool _is_quiet = true;

        static const uint8_t MAX_SEGS = 48;
        ToneSeg       _segs[MAX_SEGS];
        uint8_t       _seg_count = 0;
        uint8_t       _seg_idx = 0;
        unsigned long _seg_until = 0;
        bool          _seq_active = false;
};
