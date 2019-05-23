#include "pitch_shifter.h"
#include <Bela.h>

// The first AMDF pitchshifter I created. This one relies on getting the
// jumpLength of the separate running AMDF algorithm
float PitchShifter::process(float inSample) {
  hasJumped = false;
  inputRingBuffer[inputPointer] = inSample;
  // lowPassedRingBuffer =
  float fadingOutPointer = wrapBufferSample(outputPointer - fadingPointerOffset,
                                            inputRingBufferSize);

  float fadingInSample, fadingOutSample;
  if (interpolationMode == 1) {
    // This is sinc interpolation
    // TODO: use one combined interpolation that use both fade samples in some
    // way. Instead of interpolating two times.
    fadingInSample = interpolateFromRingBuffer(outputPointer, inputRingBuffer,
                                               inputRingBufferSize);
    fadingOutSample = interpolateFromRingBuffer(
        fadingOutPointer, inputRingBuffer, inputRingBufferSize);
  } else {
    // Let's try with linear interpolation and see how it sounds...
    int integral;
    float fractional = modf_neon(outputPointer, &integral);

    fadingInSample =
        inputRingBuffer[integral] +
        fractional * (inputRingBuffer[(integral + 1) % inputRingBufferSize] -
                      inputRingBuffer[integral]);
    // Calculate new integral for the fadeoutpointer. Should be the same
    // fractional so we reuse that.
    integral =
        wrapBufferSample(integral - fadingPointerOffset, inputRingBufferSize);
    fadingOutSample =
        inputRingBuffer[integral] +
        fractional * (inputRingBuffer[(integral + 1) % inputRingBufferSize] -
                      inputRingBuffer[integral]);
  }
  float combinedSample = crossfadeValue * fadingInSample +
                         (1.0f - crossfadeValue) * fadingOutSample;
  // float combinedSample = fadingOutSample;

  // jump if needed and save jumpLength

  // TODO: Verify that this distance check is correct!
  int distanceBetweenInOut =
      wrapBufferSample(inputPointer - (int)outputPointer, inputRingBufferSize);
  if (distanceBetweenInOut > maxSampleDelay) {
    // jumpLength = distanceBetweenInOut; //Only do this if we don't set
    // jumpLength from outside (i.e. the amdf/pitchtracker)!
    fadingPointerOffset = jumpLength;
    outputPointer =
        wrapBufferSample(outputPointer + jumpLength, inputRingBufferSize);
    hasJumped = true;

    // float samplesUntilNextJump = distanceBetweenInOut
    float jumpLengthInPitchedSamples = jumpLength / (1.0001f - pitchRatio);
    // Do we really need to check for zero here??? Let's try without...
    // crossFadeIncrement = jumpLengthInPitchedSamples < 0.00001 ? 0.0 : 1.0f /
    // jumpLengthInPitchedSamples;
    crossfadeIncrement = 1.0f / jumpLengthInPitchedSamples;
    crossfadeIncrement *= 2.0f; // Let's double the fadespeed I.E. fadelength be
                                // 1/2 of the jumplength.
    crossfadeValue = 0.0f;
  }

  // update crossfade
  crossfadeValue += crossfadeIncrement;
  crossfadeValue = fmin(crossfadeValue, 1.0f);
  // Increment and wrap around the bufferpointers
  ++inputPointer %= inputRingBufferSize;
  outputPointer += pitchRatio;
  outputPointer = wrapBufferSample(outputPointer, inputRingBufferSize);

  return combinedSample;
}

bool noGrainIsPlaying = true;
int samplesSinceLastGrainStarted = 0;
int pitchPeriodOfLatestGrain = 0;
float PitchShifter::PSOLA(float inSample) {
  inputRingBuffer[inputPointer] = inSample;

  // new pitchMarkCandidate
  if (inSample > pitchMarkCandidateValue) {
    pitchMarkCandidateValue = inSample;
    pitchMarkCandidateIndexOffset = 0;

    pitchMarkCandidateScopeDebug = 1.0f;
  }

  pitchMarkScopeDebug -= 0.01;
  pitchMarkCandidateScopeDebug -= 0.01;

  // ******************* Thoughts 2019-05-16 ****************/
  // We look for pitchmark by finding max. When it stops getting renewed the
  // indexOffset should eventually reach the magnitude of pitchperiod. We should
  // then approximately have that grains center. But to have some margin we
  // assume it's enough with half a pitchperiod we will start searching for a
  // new pitchmark when the previous indexoffset hav a magnitude of 0.5 *
  // pitchperiod

  // *************** New Idea 2019-01-04 ***********************/
  // Simply use max value for finding pitchmarks. Only use the pitchmarks for
  // (re)positioning the grains, not determine grain interval. If needed (i
  // guess it is?) use previous periods pitchmark, and perhaps store the
  // pitchmark as a "ratio" position. Eeeh. Or something.
  //
  // ***********************************************************/

  // Let's start the search for a new pitchmark after a half pitchperiod since
  // the last one
  if (abs(pitchMarkCandidateIndexOffset) > pitchEstimatePeriod * 0.5f) {
    latestPitchMarkUsed = false;
    // pitchMarkValue = pitchMarkCandidateValue;
    pitchMarkCandidateValue = 0.f;

    previousPitchmarkIndexOffset = pitchMarkIndexOffset;
    pitchMarkIndexOffset = pitchMarkCandidateIndexOffset;
    pitchMarkCandidateIndexOffset = 0;

    pitchMarkScopeDebug = pitchMarkCandidateScopeDebug;
    pitchMarkCandidateScopeDebug = 0.f;
  }

  // Should we jump to a new grain?
  // TODO: When should we aaaactually jump?? Should we use the pitchperiod from
  // when the grain was created or the latest pitchperiod?
  if (samplesSinceLastGrainStarted >= pitchPeriodOfLatestGrain) {
    // rt_printf(".");
    // Time to start playback of a new grain
    if (newestGrain) {
      // freeGrain = fadeOutGrain;
      // fadeOutGrain = fadeInGrain;
      // fadeInGrain = newestGrain;
      // fadeInGrain->playhead = 0;
      assert(newestGrain->playhead == 0);
      assert(!newestGrain->isPlaying);
      latestStartedGrain = newestGrain;
      newestGrain->isPlaying = true;
      // newestGrain->isStarted = true;
      pitchPeriodOfLatestGrain = newestGrain->pitchPeriod;
      newestGrain = nullptr;
      // rt_printf("starting a new grain\n");
    }
    // No new grain available to start. Need to repeat the most recent one
    else {
      grain *pickedGrain = nullptr;
      // Find a free grain
      for (int i = 0; i < this->nrOfGrains; i++) {
        if (!grains[i].isPlaying) {
          pickedGrain = &grains[i];
          // rt_printf("picked grain %i for playback\n", i);
          break;
        }
      }
      if (pickedGrain) {
        // copy all the properties of the latest started grain
        *pickedGrain = *latestStartedGrain;
        pickedGrain->playhead = 0;
        pickedGrain->playheadNormalized = 0.0f;
        pickedGrain->isPlaying =
            true; // start it in case it isn't already playing
        latestStartedGrain = pickedGrain;
        pitchPeriodOfLatestGrain = latestStartedGrain->pitchPeriod;
      } else {
        // rt_printf("couldn't find an available grain to use as copy
        // destination!");
      }
    }

    samplesSinceLastGrainStarted = 0;
  }

  // TODO: Actually allow creation of a new grain as soon as we can be certain
  // it's playback will not overtake inputPointer. will be related to formant
  // (playbackspeed of grains) and grainsize.

  grain *compareGrain = nullptr;
  if (newestGrain) {
    compareGrain = newestGrain;
  } else {
    compareGrain = latestStartedGrain;
  }

  int distanceFromLastGrain = wrapBufferSample(
      inputPointer - compareGrain->startIndex, inputRingBufferSize);

  // int grainSize = pitchEstimatePeriod * 2.0f;
  int pitchMarkInterval = (pitchMarkIndexOffset - previousPitchmarkIndexOffset);
  int grainSize = pitchMarkInterval * 2.0f;
  // ***************
  // Here we create the new grain.
  // if (distanceFromLastGrain >= pitchEstimatePeriod)
  if (distanceFromLastGrain >= grainSize)
  // if(!latestPitchMarkUsed && abs(pitchMarkIndexOffset) > pitchEstimatePeriod)
  {
    // latestPitchMarkUsed = true;
    // rt_printf("-");

    // int startIndex = wrapBufferSample(
    //     compareGrain->startIndex + pitchEstimatePeriod, inputRingBufferSize);

    // This is a bit weird. A lot of assumptions(!).
    // We take the pitchmark, which should be one fourth in on a period if it is
    // a sine wave. We assume that this is true for most waveshapes (not only
    // sines). then we move 1/4 of the pitchestimate to the right. This should
    // give us a zero crossing. Which would be a nice place to start (fade in) a
    // grain.
    int startIndex =
        inputPointer + pitchMarkIndexOffset +
        pitchEstimatePeriod / 4; // Be aware that pitchMarkIndexOffset
                                 // will hold a negative value
    int length = grainSize;
    int endIndex = wrapBufferSample(startIndex + length, inputRingBufferSize);

    for (int i = 0; i < this->nrOfGrains; i++) {
      if (!grains[i].isPlaying) {
        newestGrain = &grains[i];
        break;
      }
    }
    if (!newestGrain) {
      // rt_printf("Couldn't find an available grain to use as newestGrain.
      // Using compareGrain instead.");
      newestGrain = compareGrain;
      newestGrain->isPlaying = false;
    }

    // newestGrain = freeGrain;
    newestGrain->startIndex = startIndex;
    newestGrain->endIndex = endIndex;
    newestGrain->length = length;
    newestGrain->pitchPeriod = pitchMarkInterval / pitchRatio;
    newestGrain->playhead = 0;

    // rt_printf("Creating new grain. startIndex: %i, length: %i, pitchPeriod:
    // %i \n", startIndex, length, newestGrain->pitchPeriod);

    // if(noGrainIsPlaying && this->pitchEstimatePeriod != 0){
    //   newestGrain->isPlaying = true;
    //   rt_printf("starting the first grain!\n");
    //   noGrainIsPlaying = false;
    // }
  };

  // assert(fadeInGrain != fadeOutGrain);
  // assert(fadeOutGrain != freeGrain);
  // assert(freeGrain != fadeInGrain);

  // float fadeOutSample =
  // inputRingBuffer[wrapBufferSample(fadeOutGrain->startIndex +
  // fadeOutGrain->playhead, inputRingBufferSize)];

  // float fadeInSample =
  // inputRingBuffer[wrapBufferSample(fadeInGrain->startIndex +
  // fadeInGrain->playhead, inputRingBufferSize)];

  // fadeInGrain->currentAmplitude = hannCrossFade(crossfadeTime);
  // fadeInGrain->currentAmplitude = getBlackmanFast(fadeInGrain->playhead,
  // fadeInGrain->length);

  // fadeOutGrain->currentAmplitude = hannCrossFade(-crossfadeTime);
  // fadeOutGrain->currentAmplitude = getBlackmanFast(fadeOutGrain->playhead,
  // fadeOutGrain->length);

  // float combinedSample = fadeInGrain->currentAmplitude * fadeInSample +
  // fadeOutGrain->currentAmplitude * fadeOutSample;

  // crossfadeTime += 1.0f / 10;
  ++inputPointer %= inputRingBufferSize;

  float combinedSample = 0.0f;
  // *************************************
  // update all grains
  for (int i = 0; i < this->nrOfGrains; i++) {
    if (grains[i].isPlaying) {
      // TODO: Use hann window?
      // TODO: Implement pitchmarks so we get most energy from source material!
      // TODO: Variable playback speed of grains (formants).
      grains[i].currentAmplitude =
          getHannFast(grains[i].playhead, grains[i].length);
      float sample = inputRingBuffer[wrapBufferSample(
          grains[i].startIndex + grains[i].playhead, inputRingBufferSize)];
      grains[i].currentSample = grains[i].currentAmplitude * sample;
      combinedSample += grains[i].currentSample;

      grains[i].playhead++;
      grains[i].playheadNormalized =
          (float)grains[i].playhead / (grains[i].length);
    }

    // if(grains[i].isStarted){
    //   grains[i].samplesSinceStarted++;
    // }

    if (grains[i].isPlaying && grains[i].playhead >= grains[i].length) {
      grains[i].isPlaying = false;
    }
    // grains[i].activeCounter++;
    // grains[i].playhead = std::min(grains[i].playhead, grains[i].length);
    // grains[i].currentAmplitude = getBlackmanFast(grains[i].playhead,
    // grains[i].length);
  }

  samplesSinceLastGrainStarted++;

  previousPitchmarkIndexOffset--;
  pitchMarkCandidateIndexOffset--;
  pitchMarkIndexOffset--;

  return combinedSample;
}