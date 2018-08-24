#ifndef SINC_INTERPOLATION_H
#define SINC_INTERPOLATION_H

#include <math_neon.h>
#include <cmath>
#include "utility.h"
#include <assert.h>

float normalizedSinc(float x);
float getBlackman(float x, float M);
void initializeWindowedSincTable();
float interpolateFromRingBuffer(float index, float *ringBuffer, int ringBufferSize);

#endif