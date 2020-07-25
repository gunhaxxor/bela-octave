#ifndef UTILITY_H
#define UTILITY_H

#include <cmath>
#include <libraries/math_neon/math_neon.h>

// inline int16_t floatToQ11(float value)
// {
// 	return value * (0x01 << 11);
// }

// inline float Q11ToFloat(int16_t value)
// {
// 	return ((float)value) / (0x01 << 11);
// }

inline int wrapBufferSample(int n, int size)
{
  n += size;
  n %= size;
  return n;
}

inline float wrapBufferSample(float n, int size)
{
  return fmodf_neon(n + size, size);
}

inline double wrapBufferSample(double n, int size)
{
  return fmodf_neon(n + size, size);
}

inline float hannCrossFade(float t)
{
  float e_t = 0.5f;
  float o_t;
  if (fabs(t) < 1)
  {
    o_t = 0.5f * sinf_neon(M_PI / 2 * t);
  }
  else
  {
    // sign function
    o_t = ((t > 0.0f) - (t < 0.0f)) / 2.0f;
  }

  return e_t + o_t;
}

inline float constantPowerHannCrossfade(float t)
{
  return sqrtf_neon(hannCrossFade(t));
}

inline float getHann(float x, float M)
{

  if (x < 0.0f || x > M)
  {
    return 0.0f;
  }
  return 0.5f * (1 - cos(2 * M_PI * x / M));
}

inline float getHannFast(float x, float M)
{

  if (x < 0.0f || x > M)
  {
    return 0.0f;
  }
  return 0.5f * (1 - cosf_neon(2 * M_PI * x / M));
}

#endif
