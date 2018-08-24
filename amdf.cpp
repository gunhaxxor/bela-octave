#include "amdf.h"
// #include <math_neon.h>
// #include <cmath>
// #include "utility.h"

//TODO: Find the places where we actually have to do ringbuffer wrap. Should avoid it where not necessary.
void Amdf::initiateAMDF(int searchIndexStart, int compareIndexStart, float *sampleBuffer, int bufferLength)
{
  this->sampleBuffer = sampleBuffer;
  this->bufferLength = bufferLength;

  bestSoFar = pitchtrackingBestSoFar = 10000000.0f;
  weight = this->maxWeight;
  previousPitchTrackingAmdfScores[0] = 10000000.0f;
  // this->searchIndexStart = searchIndexStart;
  // searchIndexStop = searchIndexStart + searchWindowSize;
  this->searchIndexStart = (searchIndexStart - correlationWindowSize);
  searchIndexStop = this->searchIndexStart + searchWindowSize;

  //initiate outer loop
  currentSearchIndex = this->searchIndexStart;

  //initiate inner loop
  this->compareIndexStart = compareIndexStart - correlationWindowSize;
  compareIndexStop = compareIndexStart;
  amdfIsDone = false;
}

bool Amdf::updateAMDF()
{
  amdfScore = pitchtrackingAmdfScore = 0;
  for (int currentCompareIndex = compareIndexStart, i = 0; currentCompareIndex < compareIndexStop; currentCompareIndex += jumpLengthBetweenTestedSamples, i += jumpLengthBetweenTestedSamples)
  {
    // int k = (currentCompareIndex + bufferLength) % bufferLength;
    int k = wrapBufferSample(currentCompareIndex, bufferLength);
    // int km = (currentSearchIndex + i + bufferLength) % bufferLength;
    int km = wrapBufferSample(currentSearchIndex + i, bufferLength);
    amdfScore += fabsf_neon(sampleBuffer[km] - sampleBuffer[k]);
  }
  amdfScore /= nrOfTestedSamplesInCorrelationWindow;
  if (amdfScore <= bestSoFar)
  {
    bestSoFar = amdfScore;
    bestSoFarIndex = currentSearchIndex % bufferLength;
    // bestSoFarIndexJump = (compareIndexStart - currentSearchIndex + bufferLength) % bufferLength; //wrap around
    bestSoFarIndexJump = wrapBufferSample(compareIndexStart - currentSearchIndex, bufferLength);
  }
  // TODO: Find a smart way to keep track of both the shortest best score (for pitch detection)
  // and the longest (for jumping the outputPointer) without too much extra processing

  // weight starts high and becomes smaller with shrinking jumpdistance.
  // This is to give smaller periods better "score" when detecting pitch.
  atTurnPoint = false; // This variable is used to identify local minimi points in the amdf score.
  pitchtrackingAmdfScore = amdfScore + weight;
  if (currentSearchIndex % 5 == 0)
  {
    atTurnPoint = //previousPitchTrackingAmdfScores[1] > previousPitchTrackingAmdfScores[0]
        previousPitchTrackingAmdfScores[0] < pitchtrackingAmdfScore;
    // atTurnPoint = previousPitchTrackingAmdfScores[0] > pitchtrackingAmdfScore;
    if (atTurnPoint)
    {
      // weight -= weightIncrement*5;
      weight -= (pitchtrackingAmdfScore - previousPitchTrackingAmdfScores[0]) * 0.1f;
    }

    previousPitchTrackingAmdfScores[1] = previousPitchTrackingAmdfScores[0];
    previousPitchTrackingAmdfScores[0] = pitchtrackingAmdfScore;
  }
  if (pitchtrackingAmdfScore < pitchtrackingBestSoFar)
  {
    pitchtrackingBestSoFar = pitchtrackingAmdfScore;
    // pitchtrackingBestIndexJump = (compareIndexStart - currentSearchIndex + bufferLength) % bufferLength; //wrap around
    pitchtrackingBestIndexJump = wrapBufferSample(compareIndexStart - currentSearchIndex, bufferLength);
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
    this->previousJumpValue = this->jumpValue;
    this->jumpValue = bestSoFarIndexJump;

    //this->jumpDifference = filter_C * std::abs(this->jumpValue - bestSoFarIndexJump) + (1.0f - filter_C) * this->jumpDifference;
    float newFreqEstimate = (this->sampleRate / pitchtrackingBestIndexJump);
    float average_C = 0.1;
    frequencyEstimateAveraged = average_C * newFreqEstimate + (1.0f - average_C) * frequencyEstimateAveraged;

    // float newPitchEstimate = 69 + 12 * logf_neon(newFreqEstimate/440.0f) * this->inverseLog_2;
    float newPitchEstimate = 0.5 * logf_neon(newFreqEstimate / 440.0f) * this->inverseLog_2;
    // pitchEstimateAveraged = average_C * newPitchEstimate + (1.0f - average_C) * pitchEstimateAveraged;

    // float ratio = newFreqEstimate / frequencyEstimateAveraged;

    float ratio = newFreqEstimate / this->previousFrequencyEstimate;
    float semiTones = logf_neon(ratio) * this->inverseLog_2;
    // float score = std::fmax(0.0f, 1.0f - (ratio - 1.0f) * 10);
    // this->frequencyEstimateConfidence = std::fmax(1.0 - fabsf_neon(semiTones), 0.0f);

    // this->frequencyEstimateConfidence = 1.0 - (std::fmin(fabsf_neon(semiTones), 1.0f));

    // TODO: Tune these parameters and possibly make the addition a choosable parameters.
    // TODO: make the increase of confidence be exponential to duration of (almost) same pitch
    // In order to make the volume come up real fast when we get a stable pitch track.
    this->frequencyEstimateConfidence -= 0.8f * fabsf_neon(semiTones);
    this->frequencyEstimateConfidence += 0.18;
    this->frequencyEstimateConfidence = std::fmin(1.0f, std::fmax(0.0f, this->frequencyEstimateConfidence));

    this->previousFrequencyEstimate = newFreqEstimate;

    // if (this->frequencyEstimateConfidence < 0.4)
    // {
    //   this->frequencyEstimateConfidence = 0.0;
    // }

    float estimate_C = 0.3f;
    this->frequencyEstimate = estimate_C * newFreqEstimate + (1.0 - estimate_C) * this->frequencyEstimate;
    this->pitchEstimate = estimate_C * newPitchEstimate + (1.0 - estimate_C) * this->pitchEstimate;
  }

  currentSearchIndex++;
  // weight -= weightIncrement;

  return amdfIsDone;
  // return bestSoFarIndex;
}

//Old amdf code from render.cpp
// const float amdf_C = 3.0/8.0;
// const int correlationWindowSize = LOWESTNOTEPERIOD * amdf_C;
// const int searchWindowSize = LOWESTNOTEPERIOD - correlationWindowSize;
//
// float bestSoFar;
// int bestSoFarIndex;
// int bestSoFarIndexJump;
// int searchIndexStart;
// int searchIndexStop;
// int currentSearchIndex;
// int compareIndexStart;
// int compareIndexStop;
// bool amdfIsDone = true;
// float amdfScore = 0;
// void initiateAMDF_(){
//   bestSoFar = 10000000.0f;
//   searchIndexStart = (int) outputPointer;//wrapBufferSample(inputPointer - LOWESTNOTEPERIOD);
//   searchIndexStop = searchIndexStart + searchWindowSize;
//
//   //initiate outer loop
//   currentSearchIndex = searchIndexStart;
//
//   //initiate inner loop
//   compareIndexStart = inputPointer - correlationWindowSize;
//   compareIndexStop = compareIndexStart + correlationWindowSize;
//   amdfIsDone = false;
// }
//
// bool amdf_(){
//   amdfScore = 0;
//   for (int currentCompareIndex = compareIndexStart, i = 0; currentCompareIndex < compareIndexStop; currentCompareIndex+=2, i+=2) {
//     int k = wrapBufferSample(currentCompareIndex);
//     int km = wrapBufferSample(currentSearchIndex + i);
// 		amdfScore += fabsf_neon(ringBuffer[km] - ringBuffer[k]);
// 	}
//   amdfScore /= correlationWindowSize;
//   if(amdfScore < bestSoFar){
//     bestSoFar = amdfScore;
//     bestSoFarIndex = currentSearchIndex%RINGBUFFER_SIZE;
//     bestSoFarIndexJump = wrapBufferSample(compareIndexStart - currentSearchIndex);
//   }
//   if(currentSearchIndex < searchIndexStop){
//     amdfIsDone = false;
//   }else{
//     amdfIsDone = true;
//     amdfScore = bestSoFar;
//   }
//
//   currentSearchIndex++;
//
//   return amdfIsDone;
// }

// if (!amdfIsDone) {
//   if(amdf_()){
//     // rt_printf("amdf_ done with amdf");
//     previousJumpDistance = bestSoFarIndexJump;
//     osc.setFrequency(0.5* context->audioSampleRate / bestSoFarIndexJump);
//     outputPointer = wrapBufferSample(outputPointer + bestSoFarIndexJump);
//     scope.trigger();
//     jumpPulse = 0.5f;
//     crossfadeValue = 1.0f;
//   }
// }
// int distanceBetweenInOut_ = wrapBufferSample(inputPointer - outputPointer);
// float proportionalDistance = ((float)distanceBetweenInOut_) / ((float) RINGBUFFER_SIZE);
// if(amdfIsDone && distanceBetweenInOut_ > RINGBUFFER_SIZE/4){
//   initiateAMDF_();
//   // jumpPulse = 1.0;
// }
