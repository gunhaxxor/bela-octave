#include "amdf.h"
#include <math_neon.h>


void Amdf::initiateAMDF(int searchIndexStart, int compareIndexStart, float* sampleBuffer, int bufferLength){
  this->sampleBuffer = sampleBuffer;
  this->bufferLength = bufferLength;

  bestSoFar = 10000000.0f;
  this->searchIndexStart = searchIndexStart;
  searchIndexStop = searchIndexStart + searchWindowSize;

  //initiate outer loop
  currentSearchIndex = searchIndexStart;

  //initiate inner loop
  this->compareIndexStart = compareIndexStart - correlationWindowSize;
  compareIndexStop = compareIndexStart;
  amdfIsDone = false;
}

bool Amdf::updateAMDF(){
  magSum = 0;
  for (int currentCompareIndex = compareIndexStart, i = 0; currentCompareIndex < compareIndexStop; currentCompareIndex+=jumpLengthBetweenTestedSamples, i+=jumpLengthBetweenTestedSamples) {
    int k = (currentCompareIndex + bufferLength)%bufferLength;
    // int k = wrapBufferSample(currentCompareIndex);
    int km = (currentSearchIndex + i + bufferLength)%bufferLength;
    // int km = wrapBufferSample(currentSearchIndex + i);
  	magSum += fabsf_neon(sampleBuffer[km] - sampleBuffer[k]);
  }
  magSum /= correlationWindowSize;
  if(magSum < bestSoFar){
    bestSoFar = magSum;
    bestSoFarIndex = currentSearchIndex%bufferLength;
    bestSoFarIndexJump = (compareIndexStart - currentSearchIndex + bufferLength)%bufferLength; //wrap around
    // bestSoFarIndexJump = wrapBufferSample(compareIndexStart - currentSearchIndex);
  }
  if(currentSearchIndex < searchIndexStop){
    amdfIsDone = false;
  }else{
    amdfIsDone = true;
    this->amdfValue = bestSoFar;
    this->jumpValue = bestSoFarIndexJump;
  }

  currentSearchIndex++;

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
// float magSum = 0;
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
//   magSum = 0;
//   for (int currentCompareIndex = compareIndexStart, i = 0; currentCompareIndex < compareIndexStop; currentCompareIndex+=2, i+=2) {
//     int k = wrapBufferSample(currentCompareIndex);
//     int km = wrapBufferSample(currentSearchIndex + i);
// 		magSum += fabsf_neon(ringBuffer[km] - ringBuffer[k]);
// 	}
//   magSum /= correlationWindowSize;
//   if(magSum < bestSoFar){
//     bestSoFar = magSum;
//     bestSoFarIndex = currentSearchIndex%RINGBUFFER_SIZE;
//     bestSoFarIndexJump = wrapBufferSample(compareIndexStart - currentSearchIndex);
//   }
//   if(currentSearchIndex < searchIndexStop){
//     amdfIsDone = false;
//   }else{
//     amdfIsDone = true;
//     magSum = bestSoFar;
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
