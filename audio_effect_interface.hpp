#ifndef AUDIO_EFFECT_INTERFACE_H
#define AUDIO_EFFECT_INTERFACE_H

class AudioEffect
{
public:
  virtual float process(float inSample) = 0;
};

class BypassEffect : public AudioEffect
{
public:
  float process(float inSample) { return inSample; }
};

#endif