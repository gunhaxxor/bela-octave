#ifndef AMDF_H
#define AMDF_H

#include <math_neon.h>
#include <cmath>
#include "utility.h"

class Amdf
{
public:
  int jumpValue;
  int previousJumpValue;

  float pitchEstimate;
  float frequencyEstimate;
  float frequencyEstimateConfidence;
  int amdfValue;
  bool amdfIsDone = true;
  float jumpDifference = 0;

  float pitchtrackingAmdfScore;
  float previousPitchTrackingAmdfScores[2] = {5.0, 5.0};
  bool atTurnPoint = false;

  //debug
  bool minimiPoint = false;

  Amdf(int longestExpectedPeriodOfSignal, int shortestExpectedPeriodOfSignal)
  {
    correlationWindowSize = longestExpectedPeriodOfSignal * amdf_C;
    // searchWindowSize = longestExpectedPeriodOfSignal - correlationWindowSize;
    searchWindowSize = longestExpectedPeriodOfSignal - shortestExpectedPeriodOfSignal; // - correlationWindowSize;
    nrOfTestedSamplesInCorrelationWindow = correlationWindowSize / jumpLengthBetweenTestedSamples;

    weightIncrement = maxWeight / searchWindowSize;
  }

  void setup(int sampleRate)
  {
    this->sampleRate = sampleRate;
  }
  void initiateAMDF(int searchIndexStart, int compareIndexStart, float *sampleBuffer, int bufferLength);
  bool updateAMDF();

  // private:
  const float amdf_C = 2.0 / 8.0;
  const int jumpLengthBetweenTestedSamples = 15;
  const float maxWeight = 0.09f;
  const float inverseLog_2 = 1 / logf(2);
  float weight;
  float weightIncrement;
  float filter_C = 0.5;

  float amdfScore; // low value means good correlation. high value means big difference between the compared windows
  float bestSoFar;
  int bestSoFarIndex;
  int bestSoFarIndexJump;

  float pitchEstimateAveraged = 0;

  float previousFrequencyEstimate = 0;
  float frequencyEstimateAveraged = 0;
  // float pitchtrackingAmdfScore;
  float pitchtrackingBestSoFar;
  float pitchtrackingBestIndexJump;

  int bufferLength;
  int correlationWindowSize;
  float nrOfTestedSamplesInCorrelationWindow;
  int searchWindowSize;

  int currentSearchIndex;

  int sampleRate;
  float *sampleBuffer;
  int searchIndexStart;
  int searchIndexStop;
  int compareIndexStart;
  int compareIndexStop;
};

#endif