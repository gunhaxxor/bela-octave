#ifndef WAVESHAPER_H
#define WAVESHAPER_H

#include <libraries/math_neon/math_neon.h>
#include <cmath>

class Waveshaper
{
public:
  enum shaperTypeEnum
  {
    FOLDBACK,
    TANH,
    SINE,
    DISTORSION
  } shaperType;

  Waveshaper(shaperTypeEnum shaperType)
  {
    this->shaperType = shaperType;
  }

  float process(float inSample)
  {
    float outSample;
    switch (shaperType)
    {
    case FOLDBACK:
      outSample = foldback(inSample, 1.0);
      break;
    case TANH:
      outSample = tanh(inSample);
      break;
    case SINE:
      outSample = sine(inSample);
      break;
    case DISTORSION:
      outSample = distorsion(inSample);
      break;
    }
    return outSample /= drive;
  }

  float foldback(float in, float threshold)
  {
    in *= drive;
    if (in > threshold || in < -threshold)
    {
      in = fabs(fabs(fmodf_neon(in - threshold, threshold * 4)) - threshold * 2) - threshold;
    }
    return in;
  }

  float tanh(float in)
  {
    return tanhf_neon(in * drive);
  }

  float sine(float in)
  {
    return sinf_neon(in * drive);
  }

  float distorsion(float in)
  {
    in *= drive;
    float distSample = 1.5f * in - 0.5f * in * in * in;
    return distSample * 0.7;
  }

  void setDrive(float drive)
  {
    this->drive = drive + 0.00001f;
  }

  void setShaperType(shaperTypeEnum shaperType)
  {
    this->shaperType = shaperType;
  }

  void setShaperType(float typeValue)
  {
    int type = (int)typeValue;
    this->setShaperType((shaperTypeEnum)type);
  }

private:
  float drive = 1.0f;
};

#endif