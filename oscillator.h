#include <cmath>
#include <libraries/math_neon/math_neon.h>

// This oscillator is basically completely taken from here:
// http://www.martin-finke.de/blog/articles/audio-plugins-018-polyblep-oscillator/
// With some very minor adjustments for the structure of this project

class Oscillator
{
public:
  enum OscillatorMode
  {
    OSCILLATOR_MODE_SINE = 0,
    OSCILLATOR_MODE_SAW,
    OSCILLATOR_MODE_SQUARE,
    OSCILLATOR_MODE_TRIANGLE,
    kNumOscillatorModes
  };
  void setMode(OscillatorMode mode);
  void setMode(float mode);
  void setFrequency(float frequency);
  void setSampleRate(float sampleRate);
  void generate(float *buffer, int nFrames);
  inline void setMuted(bool muted) { isMuted = muted; }
  float nextSample();
  Oscillator() : mOscillatorMode(OSCILLATOR_MODE_SAW),
                 mPI(2 * acos(0.0)),
                 twoPI(2 * mPI),
                 isMuted(true),
                 mFrequency(110.0),
                 mPhase(0.0),
                 mSampleRate(44100.0) { updateIncrement(); };

private:
  float polyBLEP(float t);
  float naiveWaveform();
  float lastOutput;
  OscillatorMode mOscillatorMode;
  const float mPI;
  const float twoPI;
  bool isMuted;
  float mFrequency;
  float mPhase;
  float mSampleRate;
  float mPhaseIncrement;
  void updateIncrement();
};
