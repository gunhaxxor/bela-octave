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
  compareIndexStop = compareIndexStart + correlationWindowSize;
  amdfIsDone = false;
}

bool Amdf::updateAMDF(){
  magSum = 0;
  for (int currentCompareIndex = compareIndexStart, i = 0; currentCompareIndex < compareIndexStop; currentCompareIndex+=jumpLengthBetweenTestedSamples, i+=jumpLengthBetweenTestedSamples) {
    int k = (currentCompareIndex - bufferLength)%bufferLength;
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
  }

  currentSearchIndex++;

  return amdfIsDone;
  // return bestSoFarIndex;
}
