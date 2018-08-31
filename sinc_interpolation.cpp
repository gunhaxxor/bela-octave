#include "sinc_interpolation.h"

// float crossFadeIncrement = 0.001f;
const int sincTableScaleFactor = 10000;
const int sincLength = 10;
const int sincLengthBothSides = (sincLength * 2) + 1; //We want to use sincLength on both sides of the affected sample.
const int windowedSincTableSize = sincLengthBothSides * sincTableScaleFactor;

float blackmanWindow[sincLengthBothSides];

float windowedSincTable[windowedSincTableSize];
float windowedSincTableDifferences[windowedSincTableSize];

float normalizedSinc(float x)
{
  x *= M_PI;
  if (x == 0)
  {
    return 1.0;
  }
  return sin(x) / x; // will be used when building sinctable, so we want to use the best sin function.
}

float getBlackman(float x, float M)
{
  return 0.42 - 0.5 * cos(2 * M_PI * x / M) + 0.08 * cos(4 * M_PI * x / M);
}

// NOTE: Not correctly defined in ranges outside M
float getBlackmanFast(float x, float M)
{
  return 0.42 - 0.5 * cosf_neon(2 * M_PI * x / M) + 0.08 * cosf_neon(4 * M_PI * x / M);
}

void initializeWindowedSincTable()
{
  float previousY = 0;
  for (std::size_t i = 0; i < sizeof(windowedSincTable) / sizeof(windowedSincTable[0]); i++)
  {
    float xSinc = ((float)i) / sincTableScaleFactor - sincLength;
    float xBlackman = ((float)i) / sincTableScaleFactor;
    float y = normalizedSinc(xSinc);
    y *= getBlackman(xBlackman, sincLengthBothSides - 1);
    windowedSincTable[i] = y;
    windowedSincTableDifferences[i] = y - previousY;
    previousY = y;
  }
}

// How does it look with a sincLength of 6? sincLengthBothSides is 13
//                         x
//   *   *   *   *   *   *   *   *   *   *   *   *
// |   |   |   |   |   |   |   |   |   |   |   |   |
// 0   1   2   3   4   5   6   7   8   9   10  11  12
//-6  -5  -4  -3  -2  -1   0   1   2   3   4   5   6
float interpolateFromRingBuffer(float index, float *ringBuffer, int ringBufferSize)
{
  // First calculate the fractional so we know where to shift the sinc function.
  int integral;
  float fractional = modf_neon(index, &integral);
  // What am I doing here? I don't remember and I don't understand it now... eh...
  int scaledFractional;
  float indexRatio = modf_neon(fractional * sincTableScaleFactor, &scaledFractional);

  int currentSampleIndex = (int)integral - sincLength;
  // int fadingSampleIndex = currentSampleIndex - fadingOutputPointerOffset;

  float combinedSamples = 0;

  float sincValue;
  int x;
  assert(windowedSincTable[0] < 0.000001f);
  assert(windowedSincTable[(sincLengthBothSides - 1) * sincTableScaleFactor] < 0.000001f);
  assert(windowedSincTable[sincLength * sincTableScaleFactor] == 1.0f);

  for (int i = 1; i < sincLengthBothSides; i++)
  {
    currentSampleIndex = wrapBufferSample(currentSampleIndex, ringBufferSize);
    // fadingSampleIndex = wrapBufferSample(fadingSampleIndex, ringBufferSize);

    x = i * sincTableScaleFactor - (int)scaledFractional;
    //linear interpolation from sinc table (difference is precalculated in setup)
    sincValue = windowedSincTable[x] + windowedSincTableDifferences[x + 1] * indexRatio;
    // TODO: Make the crossfade correct! We are using same crossfadeValue for neighbouring samples in the sinc interpolation
    // Maybe use a table with fade values, we can then retrieve them in here by offsetting with i.
    // Or, perhaps, in some way calculate how much adjacent samples differ in fadevalue and do something like crossfadeValue + (fadedifferencebetweensamples * (i - sincLength)
    // Oh, well. Let's fuck that for now and just listen to it :-D
    // float tempCrossfade = crossfadeValue - ((i - sincLength) * crossFadeIncrement);
    //// THIS IS SOME CODE USED TO BE ABLE TO INTERPOLATE TWO SAMPLES SIMULTANEOUSLY.
    //// SAVING ONE CALL TO THE INTERPOLATE FUNCTION
    // combinedSamples += sincValue * ((tempCrossfade * ringBuffer[fadingSampleIndex]) + (1.0f - tempCrossfade) * ringBuffer[currentSampleIndex]);
    combinedSamples += sincValue * ringBuffer[currentSampleIndex];

    currentSampleIndex++;
    // fadingSampleIndex++;
  }
  return combinedSamples;
}