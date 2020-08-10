#ifndef PITCH_DETECTOR_H
#define PITCH_DETECTOR_H

#include <cmath>
#include "Bela.h"
#include <libraries/math_neon/math_neon.h>
#include <libraries/Biquad/Biquad.h>
#include "utility.h"

class PitchDetector
{
private:
  float _sampleRate;
  Biquad lpFilter;

  int _minDistance;
  int _maxDistance;
  int _bufferLength;
  float *_inputRingBuffer;
  int _inputBufferIdx = 0;

  const int _nrOfPitchMarks = 5;
  int *_negativePitchMarks;
  int _negativePitchMarkIdx = 0;
  int *_positivePitchMarks;
  int _positivePitchMarkIdx = 0;

  float _sampleSlopes[3];
  float _rms = 0.f;
  float *_squaredInputSamples;

  float _estimatedPeriod;
  float _estimateConfidence = 0.f;
  float _filteredEstimateConfidence = 0.f;

  //DEBUG
  float _positivePitchTrigger = 0;
  float _negativePitchTrigger = 0;

public:
  PitchDetector(int sampleRate, float lowestTrackableFrequency, float highestTrackableFrequency)
  {
    this->_sampleRate = sampleRate;
    this->_minDistance = sampleRate / highestTrackableFrequency;
    this->_maxDistance = sampleRate / lowestTrackableFrequency;
    this->_bufferLength = this->_maxDistance * 2.f;
    this->_inputRingBuffer = new float[this->_bufferLength];
    this->_positivePitchMarks = new int[_nrOfPitchMarks];
    this->_negativePitchMarks = new int[_nrOfPitchMarks];

    this->lpFilter.setup(highestTrackableFrequency, sampleRate, Biquad::lowpass, 0.707, 0);

    this->_squaredInputSamples = new float[this->_maxDistance];
    for (int i = 0; i < this->_maxDistance; i++)
    {
      this->_squaredInputSamples[i] = 0.0f;
    }
  }

  ~PitchDetector()
  {
  }

  void process(float inSample)
  {
    //Do this fiiiirst
    this->_inputBufferIdx++;
    this->_inputBufferIdx %= this->_bufferLength;

    float filteredInSample = this->lpFilter.process(inSample);
    float rms = this->processRMS(filteredInSample);
    /////
    // ATTENTION TEST TO MAKE RMS response "FASTER"
    //
    rms = this->processRMS(filteredInSample);
    rms = this->processRMS(filteredInSample);
    float processedSample = 0.f;
    if (fabsf_neon(filteredInSample) > rms)
    {
      processedSample = filteredInSample - copysign(rms, filteredInSample);
    }

    //Well. It's actuuuually not a future sample. Obviously. But I think it's easier to reason of it like that. (one sample delay)
    float prevProcessedSample = this->_inputRingBuffer[wrapBufferSample(this->_inputBufferIdx - 2, this->_bufferLength)];
    float currentProcessedSample = this->_inputRingBuffer[wrapBufferSample(this->_inputBufferIdx - 1, this->_bufferLength)];
    float futureProcessedSample = this->_inputRingBuffer[this->_inputBufferIdx] = processedSample;

    static bool positiveMarkOnThisBumpSaved = false;
    static bool negativeMarkOnThisBumpSaved = false;
    if (currentProcessedSample == 0.f)
    {
      positiveMarkOnThisBumpSaved = false;
      negativeMarkOnThisBumpSaved = false;
    }
    else
    {
      if (!positiveMarkOnThisBumpSaved && currentProcessedSample > 0.f && prevProcessedSample < currentProcessedSample && currentProcessedSample > futureProcessedSample)
      {
        positiveMarkOnThisBumpSaved = true;
        this->_positivePitchTrigger = .5f;
        savePitchMark(true);
        calculatePitchEstimate();
      }
      else if (!negativeMarkOnThisBumpSaved && currentProcessedSample < 0.f && prevProcessedSample > currentProcessedSample && currentProcessedSample < futureProcessedSample)
      {
        negativeMarkOnThisBumpSaved = true;
        this->_negativePitchTrigger = -.5f;
        savePitchMark(false);
        calculatePitchEstimate();
      }
    }
    float triggerSlope = 2.f / this->_minDistance;
    _negativePitchTrigger = fmin(0, _negativePitchTrigger + triggerSlope);

    _positivePitchTrigger = fmax(0, _positivePitchTrigger - triggerSlope);

    updatePitchMarks();

    float confidenceSpeed = 0.01f;
    _filteredEstimateConfidence = (1.f - confidenceSpeed) * _filteredEstimateConfidence + confidenceSpeed * _estimateConfidence;

    // this->_sampleSlopes[0] = this->_sampleSlopes[1];
    // this->_sampleSlopes[1] = this->_sampleSlopes[2];
    // this->_sampleSlopes[2] = inSample - prevInSample;
  }

  void calculatePitchEstimate()
  {
    //Pick the side that has the pitchmarks scattered across longest distance
    float period;
    int *pitchMarks = _positivePitchMarks;
    int idx = _positivePitchMarkIdx;
    int nrOfPitchmarkDistances = _nrOfPitchMarks - 1;
    if (_positivePitchMarks[wrapBufferSample(_positivePitchMarkIdx + 1, _nrOfPitchMarks)] < _negativePitchMarks[wrapBufferSample(_negativePitchMarkIdx + 1, _nrOfPitchMarks)])
    {
      //use negative p marks
      pitchMarks = _negativePitchMarks;
      idx = _negativePitchMarkIdx;
    }

    float accumulatedVariance = 0.f;
    int oldestPitchmark = pitchMarks[wrapBufferSample(idx + 1, _nrOfPitchMarks)];
    int newestPitchmark = pitchMarks[wrapBufferSample(idx, _nrOfPitchMarks)];
    float averageDistance = (oldestPitchmark - newestPitchmark) / nrOfPitchmarkDistances;
    for (int i = 0; i < nrOfPitchmarkDistances; i++)
    {
      float deltaDistance = pitchMarks[wrapBufferSample(idx - 1 - i, _nrOfPitchMarks)] - pitchMarks[wrapBufferSample(idx - i, _nrOfPitchMarks)];
      accumulatedVariance += fabsf(averageDistance - deltaDistance);
    }
    float confid = accumulatedVariance / nrOfPitchmarkDistances / averageDistance;
    this->_estimateConfidence = 1.f - constrain(confid, 0.f, 1.0f);

    if (this->_estimateConfidence < 0.8f)
    {
      return;
    }

    //Check so distances on both sides are somewhat similar
    if (_positivePitchMarks[wrapBufferSample(_positivePitchMarkIdx + 1, _nrOfPitchMarks)] < _negativePitchMarks[wrapBufferSample(_negativePitchMarkIdx + 1, _nrOfPitchMarks)] * 0.5f || _negativePitchMarks[wrapBufferSample(_negativePitchMarkIdx + 1, _nrOfPitchMarks)] < _positivePitchMarks[wrapBufferSample(_positivePitchMarkIdx + 1, _nrOfPitchMarks)] * 0.5f)
    {
      //This means the spread of pitchmarks on one side is at least twice as large as the other. Maybe an indication of bad signal.

      return;
    }

    //Calculate distance between two most recent p marks!
    float distance = pitchMarks[wrapBufferSample(idx - 1, _nrOfPitchMarks)] - pitchMarks[idx];
    if (_minDistance > distance || distance > _maxDistance)
    {
      return;
    }

    //if small difference between previous estimate, we lowpassfilter (to compensate for intersample distance errors)
    if (fabs(this->_estimatedPeriod - distance) < 20.f)
    {
      static float filterSpeed = 0.05f;
      this->_estimatedPeriod = this->_estimatedPeriod * (1.f - filterSpeed) + distance * filterSpeed;
    }
    else
    {
      this->_estimatedPeriod = distance;
    }
  }

  void updatePitchMarks()
  {
    for (int i = 0; i < _nrOfPitchMarks; i++)
    {
      _positivePitchMarks[i]++;
      _negativePitchMarks[i]++;
    }
  }

  void savePitchMark(bool positive = true)
  {
    if (positive)
    {
      _positivePitchMarkIdx++;
      _positivePitchMarkIdx %= _nrOfPitchMarks;
      _positivePitchMarks[_positivePitchMarkIdx] = 1;
    }
    else
    {
      _negativePitchMarkIdx++;
      _negativePitchMarkIdx %= _nrOfPitchMarks;
      _negativePitchMarks[_negativePitchMarkIdx] = 1;
    }
  }

  float getTriggerSample()
  {
    return _positivePitchTrigger + _negativePitchTrigger;
  }

  float processRMS(float inSample)
  {
    float squaredInSample = inSample * inSample;
    static float squareSum = 0;
    static int squaredInputSamplesIdx = 0;
    squareSum -= this->_squaredInputSamples[squaredInputSamplesIdx];
    this->_squaredInputSamples[squaredInputSamplesIdx] = squaredInSample;
    squareSum += squaredInSample;

    squaredInputSamplesIdx++;
    squaredInputSamplesIdx %= this->_maxDistance;

    this->_rms = sqrt(squareSum / this->_maxDistance);
    return _rms;

    // float normalizingFactor = 1.0 / rmsValue;

    // normalizedInSample = 0.5 * normalizingFactor * inSample;
  }

  float getRMS()
  {
    return _rms;
  }

  float getProcessedSample()
  {
    // return this->_sampleSlopes[2];
    return this->_inputRingBuffer[this->_inputBufferIdx];
  }

  float isOnPitchMark()
  {
    return 0.f;
  }

  float getFrequency()
  {
    if (this->_estimatedPeriod < 0.00001f)
      return 0.f;
    return this->_sampleRate / this->_estimatedPeriod;
  };
  float getPeriod()
  {
    return this->_estimatedPeriod;
  };

  float getConfidence()
  {
    return this->_filteredEstimateConfidence;
  }
};

#endif