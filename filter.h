#ifndef FILTER_H
#define FILTER_H

#include <cmath>
#include <math_neon.h>

class Filter
{
public:
  enum filterTypeEnum
  {
    COMB,
    LOPASSRES,
    STATEVARIABLE
  } filterType;

  // float amplitude = 0.5;
  Filter(int sampleRate, filterTypeEnum filterType)
  {
    this->filterType = filterType;
    this->sampleRate = sampleRate;
    //combfilter
    combRingBufferSize = ((int)sampleRate * combMaxTimeDelay) + 10;
    combRingBuffer = new float[combRingBufferSize];
    this->combSampleDelay = sampleRate * combTimeDelay;
  }
  ~Filter()
  {
    delete this->combRingBuffer;
  }

  void setFilterType(filterTypeEnum filterType) { this->filterType = filterType; }

  void setFilterType(float typeValue)
  {
    int type = (int)typeValue;
    this->setFilterType((filterTypeEnum)type);
  }

  float process(float inSample)
  {
    float outSample = 0;
    switch (filterType)
    {
    case COMB:
      outSample = this->combProcess(inSample);
      break;
    case LOPASSRES:
      outSample = this->lopassresProcess(inSample);
      break;
    }
    return outSample;
  }

  void setCutoff(float cutoff)
  {
    switch (filterType)
    {
    case COMB:
      combSetCutoff(cutoff);
      break;
    case LOPASSRES:
      lopassresSetCutoff(cutoff);
      break;
    }
  }

  void setResonance(float resonance)
  {
    switch (filterType)
    {
    case COMB:
      combSetResonance(resonance);
      break;
    case LOPASSRES:
      lopassresSetResonance(resonance);
      break;
    }
  }

  void setPolarity(bool isPlus) { this->combPolarity = isPlus ? 1.0f : -1.0f; }

private:
  int sampleRate;

  //COMBFILTER
  const float combMaxTimeDelay = 0.070; // maxTimeDelay in seconds
  const float combMaxResonance = 0.95f;
  int combRingBufferSize;
  float *combRingBuffer;
  int inputIndex = 0;
  float combTimeDelay = 0.001;
  int combSampleDelay;
  float combFeedforward = .9f;
  float combFeedback = .9f;
  float combPolarity = 1.0;
  void combSetCutoff(float cutoff);
  void combSetResonance(float resonance);
  float combProcess(float inSample);

  //LOWPASSRES FILTER
  //Code taken from maximillian library
  //
  // float gain;
  float lopassresCutoff = 10000.0;
  float lopassresResonance = 0.0;
  float input;
  float output;
  // float inputs[10];
  // float outputs[10];
  // float cutoff1;
  float x = 0.0f; //speed
  float y = 0.0f; //pos
  float z = 0.0f; //pole
  float c = 0.0f; //filter coefficient
  void lopassresSetCutoff(float cutoff);
  void lopassresSetResonance(float resonance);
  float lopassresProcess(float input);
};

//State variable filter
// yl(n) = F1 yb(n) + yl(n − 1)
// yb(n) = F1 yh(n) + yb(n − 1)
// yh(n) = x(n) − yl(n − 1) − Q1 yb(n − 1)
//
// F1 = 2 sin(πfc/fs), and Q1 = 2d

//Maximillian lopass with res

//awesome. cuttof is freq in hz. res is between 1 and whatever. Watch out!
// float maxiFilter::lores

#endif