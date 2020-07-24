#include "amdf.h"
// #include <math_neon.h>
// #include <cmath>
// #include "utility.h"

// TODO: Find the places where we actually have to do ringbuffer wrap. Should
// avoid it where not necessary.
// void Amdf::initiateAMDF(int searchIndexStart, int compareIndexEnd) //, float
// *sampleBuffer, int bufferLength)
void Amdf::initiateAMDF()
{
  // this->sampleBuffer = sampleBuffer;
  // this->bufferLength = bufferLength;

  bestSoFar = pitchtrackingBestSoFar = 10000000.0f;
  weight = 0.0f;
  previousPitchTrackingAmdfScores[0] = 10000000.0f;
  this->amdfScore = 0.0f;

  // we allow loop boundaries to be negative values. We only bufferwrap when
  // actually fetching the samples from the ringbuffer. Thus it's safe to
  // compare these index variables for when to finish the loop.
  this->searchIndexStart = inputPointer - correlationWindowSize;
  searchIndexStop = this->searchIndexStart + searchWindowSize;

  // initiate outer loop
  currentSearchIndex = this->searchIndexStart;

  // initiate inner loop

  this->compareIndexStart = searchIndexStart - this->highestTrackableNotePeriod;
  this->compareIndexStop = compareIndexStart + correlationWindowSize;
  amdfIsDone = false;
  pitchEstimateReady = false;

  this->requiredCyclesToComplete = searchWindowSize;
  this->progress = 0.0f;
}

void Amdf::process(float inSample)
{

  float squaredInSample = inSample * inSample;
  squareSum -= squareSumSamples[squareSumSamplesIndex];
  squareSumSamples[squareSumSamplesIndex] = squaredInSample;
  squareSum += squaredInSample;

  squareSumSamplesIndex++;
  squareSumSamplesIndex %= squareSumSamplesSize;

  // squareSum = (1.0f - rms_C) * squareSum + rms_C * in_l * in_l;
  rmsValue = sqrt(squareSum / squareSumSamplesSize);

  float normalizingFactor = 1.0 / rmsValue;

  normalizedInSample = 0.5 * normalizingFactor * inSample;

  this->inputRingBuffer[inputPointer] = inSample;
  this->normalizedRingBuffer[inputPointer] = normalizedInSample;
  this->lowPassedRingBuffer[inputPointer] =
      lopass.process(this->inputRingBuffer[inputPointer]);
  ++inputPointer %= this->bufferLength;

  this->inputPointerProgress =
      (float)this->inputPointer / (float)this->bufferLength;

  if (amdfIsDone)
  {
    return;
  }

  this->amdfScore = pitchtrackingAmdfScore =
      0.0f; // initialize before running the summation
  int nrOfTestedSamplesInCorrelationWindow = 0;
  for (int currentCompareIndex = compareIndexStart, i = 0;
       currentCompareIndex < compareIndexStop;
       currentCompareIndex += jumpLengthBetweenTestedSamples,
           i += jumpLengthBetweenTestedSamples)
  {
    int k = wrapBufferSample(currentCompareIndex, bufferLength);
    int km = wrapBufferSample(currentSearchIndex + i, bufferLength);
    amdfScore += fabsf_neon(normalizedRingBuffer[km] - normalizedRingBuffer[k]);
    nrOfTestedSamplesInCorrelationWindow++;
  }
  this->amdfScore /= (float)nrOfTestedSamplesInCorrelationWindow;
  if (amdfScore <= bestSoFar)
  {
    bestSoFar = amdfScore;
    bestSoFarIndexJump = currentSearchIndex - compareIndexStart;
  }

  // weight starts small and becomes higher with growing jumpdistance.
  // This is to give smaller periods better "score" when detecting pitch.
  atLocalMinimi = false; // This variable is used to identify local minimi
                         // points in the amdf score.
  pitchtrackingAmdfScore = amdfScore + weight;
  atLocalMinimi =
      previousPitchTrackingAmdfScores[0] < previousPitchTrackingAmdfScores[1] &&
      pitchtrackingAmdfScore > previousPitchTrackingAmdfScores[0];
  float pitchPeriodFractionalOffset = 0;
  if (atLocalMinimi)
  {
    // weight -= weightIncrement*5;
    // weight += (pitchtrackingAmdfScore - previousPitchTrackingAmdfScores[0])
    // * 0.1f;
    weight += 0.05;

    // A VEEERY crude way to estimate intersample position:
    float a = previousPitchTrackingAmdfScores[1];
    float b = previousPitchTrackingAmdfScores[0];
    float c = pitchtrackingAmdfScore;
    float leftRatio = a / b;
    float rightRatio = c / b;
    float total = leftRatio + rightRatio;
    float normalizedInterSamplePos = leftRatio / total;
    // convert from being between 0 and 1 to be between -1 and +1
    pitchPeriodFractionalOffset = (normalizedInterSamplePos - 0.5f) * 2.0f;
  }

  previousPitchTrackingAmdfScores[1] = previousPitchTrackingAmdfScores[0];
  previousPitchTrackingAmdfScores[0] = pitchtrackingAmdfScore;

  if (pitchEstimateReady || (pitchtrackingAmdfScore < 0.25 && atLocalMinimi))
  {
    pitchEstimateReady = true;
    this->pitchEstimate =
        pitchtrackingBestIndexJump + pitchPeriodFractionalOffset;
  }

  if (pitchtrackingAmdfScore < pitchtrackingBestSoFar)
  {
    pitchtrackingBestSoFar = pitchtrackingAmdfScore;
    pitchtrackingBestIndexJump = currentSearchIndex - compareIndexStart;
  }
  else
  {
  }

  if (currentSearchIndex < searchIndexStop)
  {
    amdfIsDone = false;
  }
  else
  {
    amdfIsDone = true;

    this->amdfValue = bestSoFar;
    // this->jumpValue = bestSoFarIndexJump;
    this->jumpValue = this->pitchEstimate;

    // this->jumpDifference = filter_C * std::abs(this->jumpValue -
    // bestSoFarIndexJump) + (1.0f - filter_C) * this->jumpDifference;
    float newFreqEstimate = (this->sampleRate / pitchtrackingBestIndexJump);
    float average_C = 0.3;
    frequencyEstimateAveraged = average_C * newFreqEstimate +
                                (1.0f - average_C) * frequencyEstimateAveraged;

    // float newPitchEstimate = 69 + 12 * logf_neon(newFreqEstimate/440.0f) *
    // this->inverseLog_2; float newPitchEstimate = 0.5 *
    // logf_neon(newFreqEstimate / 440.0f) * this->inverseLog_2;
    // pitchEstimateAveraged = average_C * newPitchEstimate + (1.0f - average_C)
    // * pitchEstimateAveraged;

    // Calculate the change in semitones from the previous pitchestimate
    float ratio = newFreqEstimate / this->previousFrequencyEstimate;
    float semiTones = logf_neon(ratio) * this->inverseLog_2;
    // float score = std::fmax(0.0f, 1.0f - (ratio - 1.0f) * 10);
    // this->frequencyEstimateConfidence = std::fmax(1.0 -
    // fabsf_neon(semiTones), 0.0f);

    // this->frequencyEstimateConfidence = 1.0 -
    // (std::fmin(fabsf_neon(semiTones), 1.0f));

    // TODO: Tune these parameters and possibly make the addition a choosable
    // parameters.
    // TODO: make the increase of confidence be exponential to duration of
    // (almost) same pitch In order to make the volume come up real fast when we
    // get a stable pitch track.
    this->frequencyEstimateConfidence -= 0.8f * fabsf_neon(semiTones);
    this->frequencyEstimateConfidence += 0.18;
    this->frequencyEstimateConfidence =
        std::fmin(1.0f, std::fmax(0.0f, this->frequencyEstimateConfidence));

    this->previousFrequencyEstimate = newFreqEstimate;

    // if (this->frequencyEstimateConfidence < 0.4)
    // {
    //   this->frequencyEstimateConfidence = 0.0;
    // }

    float estimate_C = 0.3f;
    // this->frequencyEstimate = estimate_C * newFreqEstimate + (1.0 -
    // estimate_C) * this->frequencyEstimate;
    this->frequencyEstimate = newFreqEstimate;
    // this->pitchEstimate = estimate_C * newPitchEstimate + (1.0 - estimate_C)
    // * this->pitchEstimate;
  }

  currentSearchIndex++;
  this->progress = (float)(currentSearchIndex - searchIndexStart) /
                   (float)this->requiredCyclesToComplete;
}