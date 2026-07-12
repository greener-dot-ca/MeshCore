#include "Arduino.h"
#ifdef PIN_BUZZER
#include "buzzer.h"

void genericBuzzer::begin() {
//    Serial.print("DBG: Setting up buzzer on pin ");
//    Serial.println(PIN_BUZZER);
    #ifdef PIN_BUZZER_EN
      pinMode(PIN_BUZZER_EN, OUTPUT);
      digitalWrite(PIN_BUZZER_EN, HIGH);
    #endif

    quiet(false);
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW); // need to pull low by default to avoid extreme power draw
}

void genericBuzzer::play(const char *melody) {
    if (rtttl::isPlaying()) rtttl::stop();    // interrupt existing
    if (_seq_active) { noTone(PIN_BUZZER); _seq_active = false; }

    if (_is_quiet) return;

    rtttl::begin(PIN_BUZZER,melody);
//    Serial.print("DBG: Playing melody - isQuiet: ");
//    Serial.println(isQuiet());
}

void genericBuzzer::startSeg() {
    const ToneSeg &s = _segs[_seg_idx];
    if (s.freq) tone(PIN_BUZZER, s.freq); else noTone(PIN_BUZZER);
    _seg_until = millis() + s.ms;
}

// One-shot tick using tone()'s 3-arg form: the PWM peripheral counts out `ms`
// worth of pulses and stops itself in hardware, so the duration is exact even
// when a blocking e-ink refresh keeps loop() from running (playTones() relies on
// loop() to call noTone(), so a stalled loop stretches those tones -- this does
// not). Cancels any sequence/melody in progress; brief, fine for an activity tick.
void genericBuzzer::chirp(uint16_t freq, uint16_t ms) {
    if (_is_quiet || freq == 0 || ms == 0) return;
    if (rtttl::isPlaying()) rtttl::stop();
    if (_seq_active) { _seq_active = false; }
    tone(PIN_BUZZER, freq, ms);
}

void genericBuzzer::playTones(const ToneSeg *segs, uint8_t n) {
    if (rtttl::isPlaying()) rtttl::stop();    // interrupt existing
    if (_is_quiet || n == 0) return;

    if (n > MAX_SEGS) n = MAX_SEGS;
    memcpy(_segs, segs, n * sizeof(ToneSeg));
    _seg_count = n;
    _seg_idx = 0;
    _seq_active = true;
    startSeg();
}

bool genericBuzzer::isPlaying() {
    return _seq_active || rtttl::isPlaying();
}

void genericBuzzer::loop() {
    if (_seq_active) {
        if (millis() >= _seg_until) {
            if (++_seg_idx >= _seg_count) { noTone(PIN_BUZZER); _seq_active = false; }
            else startSeg();
        }
        return;
    }
    if (!rtttl::done()) rtttl::play();
}

void genericBuzzer::startup() {
    play(startup_song);
}

void genericBuzzer::shutdown() {
    play(shutdown_song);
}

void genericBuzzer::quiet(bool buzzer_state) {
    _is_quiet = buzzer_state;
    if (_is_quiet) {   // cut any sound in progress
        if (rtttl::isPlaying()) rtttl::stop();
        if (_seq_active) { noTone(PIN_BUZZER); _seq_active = false; }
    }
#ifdef PIN_BUZZER_EN
    if (_is_quiet) {
      digitalWrite(PIN_BUZZER_EN, LOW);
    } else {
      digitalWrite(PIN_BUZZER_EN, HIGH);
    }
#endif
}

bool genericBuzzer::isQuiet() {
    return _is_quiet;
}

#endif  // ifdef PIN_BUZZER