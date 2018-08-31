#include "pitch_shifter.h"

float PitchShifter::process(float inSample)
{
  hasJumped = false;
  inputRingBuffer[inputPointer] = inSample;
  float fadingOutPointer = wrapBufferSample(outputPointer - fadingPointerOffset, inputRingBufferSize);

  float fadingInSample, fadingOutSample;
  if (interpolationMode == 1)
  {
    // This is sinc interpolation
    // TODO: use one combined interpolation that use both fade samples in some way. Instead of interpolating two times.
    fadingInSample = interpolateFromRingBuffer(outputPointer, inputRingBuffer, inputRingBufferSize);
    fadingOutSample = interpolateFromRingBuffer(fadingOutPointer, inputRingBuffer, inputRingBufferSize);
  }
  else
  {
    //Let's try with linear interpolation and see how it sounds...
    int integral;
    float fractional = modf_neon(outputPointer, &integral);

    fadingInSample = inputRingBuffer[integral] + fractional * (inputRingBuffer[(integral + 1) % inputRingBufferSize] - inputRingBuffer[integral]);
    // Calculate new integral for the fadeoutpointer. Should be the same fractional so we reuse that.
    integral = wrapBufferSample(integral - fadingPointerOffset, inputRingBufferSize);
    fadingOutSample = inputRingBuffer[integral] + fractional * (inputRingBuffer[(integral + 1) % inputRingBufferSize] - inputRingBuffer[integral]);
  }
  float combinedSample = crossfadeValue * fadingInSample + (1.0f - crossfadeValue) * fadingOutSample;
  // float combinedSample = fadingOutSample;

  //jump if needed and save jumpLength
  int distanceBetweenInOut = wrapBufferSample(inputPointer - (int)outputPointer, inputRingBufferSize);
  if (distanceBetweenInOut > maxSampleDelay)
  {
    // jumpLength = distanceBetweenInOut; Only do this if we don't set jumpLength from outside (i.e. the amdf/pitchtracker)!
    fadingPointerOffset = jumpLength;
    outputPointer = wrapBufferSample(outputPointer + jumpLength, inputRingBufferSize);
    hasJumped = true;

    // float samplesUntilNextJump = distanceBetweenInOut
    float jumpLengthInPitchedSamples = jumpLength / (1.0001f - pitchRatio);
    // Do we really need to check for zero here??? Let's try without...
    // crossFadeIncrement = jumpLengthInPitchedSamples < 0.00001 ? 0.0 : 1.0f / jumpLengthInPitchedSamples;
    crossfadeIncrement = 1.0f / jumpLengthInPitchedSamples;
    crossfadeIncrement *= 2.0f; // Let's double the fadespeed I.E. fadelength be 1/2 of the jumplength.
    crossfadeValue = 0.0f;
  }

  //update crossfade
  crossfadeValue += crossfadeIncrement;
  crossfadeValue = fmin(crossfadeValue, 1.0f);
  //Increment and wrap around the bufferpointers
  ++inputPointer %= inputRingBufferSize;
  outputPointer += pitchRatio;
  outputPointer = wrapBufferSample(outputPointer, inputRingBufferSize);

  return combinedSample;
}

int periodMarkers[6] = {0, 0, 0, 0, 0, 0};

int samplesUntilNewGrain = 0;
float PitchShifter::PSOLA(float inSample)
{
  hasJumped = false;
  inputRingBuffer[inputPointer] = inSample;

  // Should we jump to a new grain?
  // TODO: When should we actuuually jump?? Is it really when fadeout is finished?
  if (fadeOutGrain->playhead >= fadeOutGrain->length / pitchRatio)
  {

    // Time to start playback of a new grain
    if (fadeInGrain != newestGrain && fadeOutGrain != fadeInGrain)
    {
      freeGrain = fadeOutGrain;
      fadeOutGrain = fadeInGrain;
      fadeInGrain = newestGrain;
    }
    else if (fadeInGrain != newestGrain && fadeOutGrain == fadeInGrain)
    {
      // freeGrain = freeGrain2;
      // fadeInGrain = newestGrain;
    }
    else if (fadeInGrain == newestGrain && fadeOutGrain != fadeInGrain)
    {
      // fadeOutGrain is finished and hence now free
      // freeGrain2 = fadeOutGrain;
      // There is no newer than fadeInGrain, so set as both fade in and fade out
      // fadeOutGrain = fadeInGrain;

      grain *tmpGrain = fadeInGrain;
      fadeInGrain = fadeOutGrain;
      fadeOutGrain = tmpGrain;

      *fadeInGrain = *fadeOutGrain;
      fadeInGrain->playhead = 0;

      /////////////CURRENT BUG IS THTAT IT*S NOT POSSIBRU TO HAVE DIFFERENT PLAYHEAD POSITIONS IN SAME GRAIN
      // WHEN WE WANT TO CROSSFADE IT WITH ITSELF.
      // SHOULD WE RESORT TO COPY THE GRAINS AROUND INSTEAD??
      // EASIER TO UNDERSTAND AND ALLOWS PLAYING SAME GRAIN IN MULTIPLE INSTANCES.
      // BUT NOT AS OPTIMIZED I THINK...
    }
    else if (fadeInGrain == newestGrain && fadeOutGrain == fadeInGrain)
    {
      // No free grain that is newer, and fade in and fade out is already the same grain.
      // No need to change anything
    }

    // crossfadeValue = -1.0f;
    // int grainLength = wrapBufferSample(activeGrain->endIndex - activeGrain->startIndex, inputRingBufferSize);
    // crossfadeIncrement = 2.0f / activeGrain->length;

    // samplesUntilNewGrain = activeGrain->length;

    // Let's jump to the grains start position
    // outputPointer = activeGrain->startIndex;
  }

  //TODO: Actually allow creation of a new grain as soon as we can be certain it's playback will not overtake inputPointer.
  // will be related to formant (playbackspeed of grains) and grainsize.

  // create new grain
  int possibleGrainSize = wrapBufferSample(inputPointer - newestGrain->endIndex, inputRingBufferSize);
  int actualGrainSize = jumpLength;
  if (possibleGrainSize >= actualGrainSize)
  {
    int startIndex = newestGrain->endIndex;
    int length = actualGrainSize;
    int endIndex = wrapBufferSample(startIndex + length, inputRingBufferSize);
    newestGrain = freeGrain;
    newestGrain->startIndex = startIndex;
    newestGrain->endIndex = endIndex;
    newestGrain->length = length;
    newestGrain->playhead = 0;

    hasJumped = true;
  };

  // activeIsFree = activeGrain == freeGrain;
  // newestIsFree = newestGrain == freeGrain;
  // newestIsActive = newestGrain == activeGrain;

  crossfadeValue += crossfadeIncrement;
  tempCrossfade = fmax(0.0f, (1.0 - fabsf_neon(crossfadeValue)));

  // tempCrossfade = getBlackmanFast(activeGrain->playhead, activeGrain->length);

  // TODO: avoid this weird check if possible
  // if (activeGrain->playhead > activeGrain->length)
  // {
  //   tempCrossfade = 0.0f;
  // }

  // outputPointer = wrapBufferSample(activeGrain->startIndex + activeGrain->playhead, inputRingBufferSize);
  int fadeOutSample = inputRingBuffer[wrapBufferSample(fadeOutGrain->startIndex + fadeOutGrain->playhead, inputRingBufferSize)];
  int fadeInSample = inputRingBuffer[wrapBufferSample(fadeInGrain->startIndex + fadeInGrain->playhead, inputRingBufferSize)];

  // float combinedSample = tempCrossfade * inputRingBuffer[(int)outputPointer];
  float combinedSample = crossfadeValue * fadeInSample + (1.0f - crossfadeValue) * fadeOutSample;

  ++inputPointer %= inputRingBufferSize;
  // outputPointer++;
  // outputPointer = wrapBufferSample(outputPointer, inputRingBufferSize);
  // samplesUntilNewGrain--;
  // activeGrain->playhead++;

  fadeInGrain->playheadNormalized = (float)fadeInGrain->playhead / ((float)fadeInGrain->length * 2);
  fadeInGrain->playhead++;
  fadeOutGrain->playheadNormalized = (float)fadeOutGrain->playhead / ((float)fadeOutGrain->length * 2);
  fadeOutGrain->playhead++;

  return combinedSample;
}