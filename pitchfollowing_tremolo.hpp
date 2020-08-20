#ifndef PITCHFOLLOWING_TREMOLO_HPP
#define PITCHFOLLOWING_TREMOLO_HPP

#include <Bela.h>
#include <libraries/math_neon/math_neon.h>
#include "audio_effect_interface.hpp"

class PitchFollowingTremolo : public AudioEffect
{
private:
  float _inverseSampleRate = 1.f / 44100;
  float _phase = 0.f;
  float _intensity = 0.8;
  float _frequency = 100.f;
  float _inverseDefaultPitch = 1.f / 400.f;
  float _currentPitch = 400.f;
  float _pitchFollowAmount = 1.f;

public:
  PitchFollowingTremolo(int sampleRate)
  {
    _inverseSampleRate = 1.f / sampleRate;
  }
  void setFollowedPitch(float frequency)
  {
    _currentPitch = frequency;
  }
  void setBaseFrequency(float frequency)
  {
    _frequency = frequency;
  }
  void setPitchFollowAmount(float pitchFollowAmount)
  {
    _pitchFollowAmount = pitchFollowAmount;
  }
  void setIntensity(float intensity)
  {
    _intensity = intensity;
  }
  float process(float inSample)
  {
    // // follows tracked pitch
    // phase += tremoloPitch * 2.0 * M_PI * frequency * inverseSampleRate;
    // doesn't track pitch
    float pitchFollowingSpeed = map(_pitchFollowAmount, 0.f, 1.f, 1.f, _currentPitch * _inverseDefaultPitch);
    _phase += _frequency * 2.0 * M_PI * pitchFollowingSpeed * _inverseSampleRate;

    if (_phase > M_PI)
    {
      _phase -= 2.0 * M_PI;
    }
    float tremoloSampleScale = sinf_neon(_phase);
    tremoloSampleScale = 0.5f * (tremoloSampleScale + 1.f) * _intensity;

    // Apply tremolo/AM
    return inSample * (1.f - tremoloSampleScale);
  }
};

#endif