#include "filter.h"

//COMB FILTER
void Filter::combSetCutoff(float cutoff)
{
  float period = 1.0 / cutoff;
  combTimeDelay = std::fmin(period, combMaxTimeDelay);
  combSampleDelay = combTimeDelay * sampleRate;
}
void Filter::combSetResonance(float resonance)
{
  this->combFeedforward = std::fmin(resonance, combMaxResonance);
  this->combFeedback = std::fmin(resonance, combMaxResonance);
}
float Filter::combProcess(float inSample)
{
  int delayPos = inputIndex - combSampleDelay;
  delayPos += combRingBufferSize;
  delayPos %= combRingBufferSize;

  // this->combRingBuffer[inputIndex] = inSample;
  // float y = this->combRingBuffer[this->inputIndex] + combRingBuffer[delayPos];

  //Let's try to make a universal comb filter!!!
  float y = inSample + this->combRingBuffer[delayPos];
  //feedback
  this->combRingBuffer[inputIndex] = combPolarity * combFeedback * y;
  //feedforward
  this->combRingBuffer[inputIndex] = combPolarity * combFeedforward * inSample;

  inputIndex++;
  inputIndex += combRingBufferSize;
  inputIndex %= combRingBufferSize;

  return y;
}

//LOPASSRES FILTER
void Filter::lopassresSetCutoff(float cutoff)
{
  lopassresCutoff = cutoff;
}
void Filter::lopassresSetResonance(float resonance)
{
  lopassresResonance = resonance * 20;
}
float Filter::lopassresProcess(float input)
{
  if (lopassresCutoff < 10)
    lopassresCutoff = 10;
  if (lopassresCutoff > (sampleRate))
    lopassresCutoff = (sampleRate);
  if (lopassresResonance < 1.)
    lopassresResonance = 1.;
  z = cos(2 * M_PI * lopassresCutoff / sampleRate);
  c = 2 - 2 * z;
  float r = (sqrt(2.0) * sqrt(-pow((z - 1.0), 3.0)) + lopassresResonance * (z - 1)) / (lopassresResonance * (z - 1));
  x = x + (input - y) * c;
  y = y + x;
  x = x * r;
  float output = y;
  return (output);
}