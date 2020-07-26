#ifndef PITCH_DETECTOR_H
#define PITCH_DETECTOR_H

#include <cmath>
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
  float *_squaredInputSamples;

  float _estimatedPeriod;

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
    float rms = this->getRMS(inSample);
    float processedSample = 0.f;
    if (fabsf_neon(filteredInSample) > rms)
    {
      processedSample = filteredInSample - copysign(rms, filteredInSample);
    }

    //Well. It's actuuuually not a future sample. Obviously. But I think it's easier to reason of it like that. (one sample delay)
    float prevProcessedSample = this->_inputRingBuffer[wrapBufferSample(this->_inputBufferIdx - 2, this->_bufferLength)];
    float currentProcessedSample = this->_inputRingBuffer[wrapBufferSample(this->_inputBufferIdx - 1, this->_bufferLength)];
    float futureProcessedSample = this->_inputRingBuffer[this->_inputBufferIdx] = processedSample;

    // static float maxOnThisMark = 0.f;
    // static float minOnThisMark = 0.f;
    if (currentProcessedSample != 0.f)
    {
      // maxOnThisMark = 0.f;
      // minOnThisMark = 0.f;
      if (currentProcessedSample > 0.f && prevProcessedSample < currentProcessedSample && currentProcessedSample > futureProcessedSample)
      {
        this->_positivePitchTrigger = .5f;
        savePitchMark(true);
        calculatePitchEstimate();
      }
      else if (currentProcessedSample < 0.f && prevProcessedSample > currentProcessedSample && currentProcessedSample < futureProcessedSample)
      {
        this->_negativePitchTrigger = -.5f;
        savePitchMark(false);
        calculatePitchEstimate();
      }
    }
    float triggerSlope = 2.f / this->_minDistance;
    _negativePitchTrigger = fmin(0, _negativePitchTrigger + triggerSlope);

    _positivePitchTrigger = fmax(0, _positivePitchTrigger - triggerSlope);

    updatePitchMarks();

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
    if (_positivePitchMarks[wrapBufferSample(_positivePitchMarkIdx + 1, _nrOfPitchMarks)] < _negativePitchMarks[wrapBufferSample(_negativePitchMarkIdx + 1, _nrOfPitchMarks)])
    {
      //use negative p marks
      pitchMarks = _negativePitchMarks;
      idx = _negativePitchMarkIdx;
    }

    //Calculate distance between two most recent p marks!
    float distance = pitchMarks[wrapBufferSample(idx - 1, _nrOfPitchMarks)] - pitchMarks[idx];
    if (_minDistance > distance || distance > _maxDistance)
    {
      return;
    }

    //if small difference between previous estimate, we lowpassfilter (to compensate for intersample errors)
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

  float getRMS(float inSample)
  {
    float squaredInSample = inSample * inSample;
    static float squareSum = 0;
    static int squaredInputSamplesIdx = 0;
    squareSum -= this->_squaredInputSamples[squaredInputSamplesIdx];
    this->_squaredInputSamples[squaredInputSamplesIdx] = squaredInSample;
    squareSum += squaredInSample;

    squaredInputSamplesIdx++;
    squaredInputSamplesIdx %= this->_maxDistance;

    // squareSum = (1.0f - rms_C) * squareSum + rms_C * in_l * in_l;
    return sqrt(squareSum / this->_maxDistance);

    // float normalizingFactor = 1.0 / rmsValue;

    // normalizedInSample = 0.5 * normalizingFactor * inSample;
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
};

#endif