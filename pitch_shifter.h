#ifndef PITCHSHIFTER_H
#define PITCHSHIFTER_H

#include "math_neon.h"
#include "sinc_interpolation.h"
#include "utility.h"
#include <cmath>

#undef NDEBUG
#include "filter.h"
#include <Scope.h>
#include <algorithm>
#include <assert.h>

class PitchShifter {
public:
  PitchShifter(int sampleRate, float lowestTrackableFrequency,
               float highestTrackableFrequency, float pitchRatio) {
    // this->scope = scope;
    this->sampleRate = sampleRate;
    this->lowestTrackableFrequency = lowestTrackableFrequency;
    this->highestTrackableFrequency = highestTrackableFrequency;
    this->lowestTrackableNotePeriod = sampleRate / lowestTrackableFrequency;
    this->highestTrackableNotePeriod = sampleRate / highestTrackableFrequency;
    this->inputRingBufferSize = lowestTrackableNotePeriod * 16;
    this->inputRingBuffer = new float[inputRingBufferSize];
    // this->lowPassedRingBuffer = new float[inputRingBufferSize];

    this->maxSampleDelay = lowestTrackableNotePeriod * 2;
    this->jumpLength =
        maxSampleDelay; // just a default value if we don't provide continuous
                        // period length tracking

    this->pitchRatio = pitchRatio;

    // PSOLA stuff
    // latestStartedGrain->isPlaying = true;
  };

  void setPitchRatio(float pitchRatio) {
    this->pitchRatio = fmin(1.0f, fmax(pitchRatio, 0.001f));
  };
  void setJumpLength(int jumpLength) { this->jumpLength = jumpLength; };
  void setPitchEstimatePeriod(float period) {
    this->pitchEstimatePeriod = (float)period;
  };
  void setInterpolationsMode(int mode) { this->interpolationMode = mode; };
  float process(float inSample);
  float PSOLA(float inSample);

  // private:
  // parameters
  float pitchRatio;
  int jumpLength = 1;
  int pitchEstimatePeriod = 1;
  int interpolationMode = 1;

  // constructor-initialized variables
  float sampleRate;
  float lowestTrackableFrequency;
  float lowestTrackableNotePeriod;
  float highestTrackableFrequency;
  float highestTrackableNotePeriod;
  int inputRingBufferSize;
  float *inputRingBuffer;
  // float *lowPassedRingBuffer;
  int maxSampleDelay;

  // completely internal variables
  int inputPointer = 0;
  float outputPointer = 0.0f;
  int fadingPointerOffset = 0;
  float crossfadeValue = 0.0;
  float crossfadeIncrement = 0;
  float crossfadeTime = 0.0f;

  // PSOLA
  static const int nrOfGrains = 8;
  struct grain {
    // bool active;
    int startIndex = 0;
    int endIndex = 0;
    int length = 1;
    int pitchPeriod = 0;
    int playhead = 0;
    bool isPlaying = false;
    // int samplesSinceStarted = 0;
    // bool isStarted = false;
    // int activeCounter = 0;
    float playheadNormalized = 0.0f;
    float currentAmplitude = 0.0f;
    float currentSample = 0.0f;
  } grains[nrOfGrains];

  grain *newestGrain = &grains[0];
  grain *latestStartedGrain = &grains[0];

  // grain *fadeInGrain = &grains[0];
  // grain *fadeOutGrain = &grains[1];
  // grain *freeGrain = &grains[2];

  float fadeInAmplitude = 0.0f;
  float fadeOutAmplitude = 0.0f;

  float pitchMarkCandidateValue = 0.f;
  int pitchMarkCandidateIndexOffset = 0;

  int previousPitchmarkIndexOffset = 0;

  bool latestPitchMarkUsed = false;
  float pitchMarkValue = 0.0f;
  int pitchMarkIndexOffset = 0;

  // debug variables
  // Scope* scope;

  float pitchMarkScopeDebug = 0;
  float pitchMarkCandidateScopeDebug = 0;

  bool hasJumped = false;
  float tempCrossfade;
  bool activeIsFree = false;
  bool newestIsFree = false;
  bool newestIsActive = false;
};

#endif

////////////TODO: Fix fade with amdfjump. I took it away from the sinc
/// interpolation now since I want less spaghetticode
// with unexpected dependencies. A clean interpolation :-)
// Maybe create a special (overloaded?) version of the interpolation that take
// two indices and a fadevalue? The alternative is to run the interpolation
// twice for each pitcher (would rather avoid that!) Ooor. Can we mix all the
// fade samples together before interpolation? Into a new small array that spans
// the windowed sinc length.