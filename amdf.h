#ifndef AMDF_H
#define AMDF_H

#include <math_neon.h>
#include <cmath>
#include "utility.h"
#include "filter.h"

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
  int requiredCyclesToComplete = 0;
  float progress = 0.0f;
  float inputPointerProgress = 0.0f;

  Amdf(int longestExpectedPeriodOfSignal, int shortestExpectedPeriodOfSignal)
  {
    correlationWindowSize = longestExpectedPeriodOfSignal * amdf_C;
    searchWindowSize = longestExpectedPeriodOfSignal - correlationWindowSize;
    // searchWindowSize = longestExpectedPeriodOfSignal - shortestExpectedPeriodOfSignal; // - correlationWindowSize;
    nrOfTestedSamplesInCorrelationWindow = (float) correlationWindowSize / jumpLengthBetweenTestedSamples;

    weightIncrement = maxWeight / searchWindowSize;

    // this->inputRingBufferSize = lowestTrackableNotePeriod * 8;
    this->bufferLength = longestExpectedPeriodOfSignal * 8.0;
    this->inputRingBuffer = new float[this->bufferLength];
    this->lowPassedRingBuffer = new float[this->bufferLength];
    this->lowestTrackableNotePeriod = longestExpectedPeriodOfSignal;
    this->highestTrackableNotePeriod = shortestExpectedPeriodOfSignal;

    
  }

  void setup(int sampleRate)
  {
    this->sampleRate = sampleRate;
    lopass.setCutoff(this->sampleRate / highestTrackableNotePeriod);
    lopass.setResonance(3.0);
  }
  // void initiateAMDF(int searchIndexStart, int compareIndexStart);//, float *sampleBuffer, int bufferLength);
  void initiateAMDF();
  void process(float inSample);

  // private:
  float lowestTrackableNotePeriod;
  float highestTrackableNotePeriod;
  // int inputRingBufferSize;
  int bufferLength;
  int inputPointer = 0;
  float *inputRingBuffer;
  float *lowPassedRingBuffer;
  const float amdf_C = 2.0 / 8.0;
  const int jumpLengthBetweenTestedSamples = 5;
  const float maxWeight = 0.09f;
  const float inverseLog_2 = 1 / logf(2);
  float weight;
  float weightIncrement;
  float filter_C = 0.5;
  Filter lopass = Filter(44100, Filter::LOPASSRES);

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

  int correlationWindowSize;
  float nrOfTestedSamplesInCorrelationWindow;
  int searchWindowSize;

  int currentSearchIndex;

  int sampleRate;
  // float *sampleBuffer;
  int searchIndexStart;
  int searchIndexStop;
  int compareIndexStart;
  int compareIndexStop;
};

#endif