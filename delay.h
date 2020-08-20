#include <cmath>
#include <libraries/math_neon/math_neon.h>
#include "audio_effect_interface.hpp"

class Delay : public AudioEffect
{
public:
  float delayTime = 0.1;

  float feedback = 0.5;
  // float amplitude = 0.5;
  Delay(int sampleRate, float maxDelayTime)
  {
    this->sampleRate = sampleRate;
    this->maxDelayTime = maxDelayTime;
    this->setDelayTime(this->delayTime);
    this->ringBufferSize = ((int)sampleRate * maxDelayTime) + 10;
    this->ringBuffer = new float[this->ringBufferSize];
  }
  ~Delay()
  {
    delete this->ringBuffer;
  }

  float process(float sample)
  {
    insertSample(sample);
    return getSample();
  }

  void setDelayTime(float delayTime)
  {
    this->delayTime = std::fmin(delayTime, this->maxDelayTime);
    this->sampleOffset = this->sampleRate * this->delayTime;
  }

private:
  int sampleRate;
  float maxDelayTime;
  int ringBufferSize;
  float *ringBuffer;
  int sampleOffset;
  int inputIndex = 0;
  float getSample()
  {
    int samplePos = inputIndex - this->sampleOffset;
    samplePos += this->ringBufferSize;
    samplePos %= this->ringBufferSize;
    float fetchedSample = this->ringBuffer[samplePos];
    // //feedback part
    // this->ringBuffer[this->inputIndex] = this->ringBuffer[this->inputIndex] + feedback * fetchedSample;
    return fetchedSample;
  }
  void insertSample(float sample)
  {
    int delayPos = inputIndex - this->sampleOffset;
    delayPos += this->ringBufferSize;
    delayPos %= this->ringBufferSize;
    float echoSample = this->ringBuffer[delayPos];
    //feedback part
    this->ringBuffer[this->inputIndex] = sample + feedback * echoSample;

    this->inputIndex++;
    this->inputIndex += this->ringBufferSize;
    this->inputIndex %= this->ringBufferSize;
  }
};