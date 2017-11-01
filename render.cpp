/*
 ____  _____ _        _
| __ )| ____| |      / \
|  _ \|  _| | |     / _ \
| |_) | |___| |___ / ___ \
|____/|_____|_____/_/   \_\

The platform for ultra-low latency audio and sensor processing

http://bela.io

A project of the Augmented Instruments Laboratory within the
Centre for Digital Music at Queen Mary University of London.
http://www.eecs.qmul.ac.uk/~andrewm

(c) 2016 Augmented Instruments Laboratory: Andrew McPherson,
	Astrid Bin, Liam Donovan, Christian Heinrichs, Robert Jack,
	Giulio Moro, Laurel Pardue, Victor Zappi. All rights reserved.

The Bela software is distributed under the GNU Lesser General Public License
(LGPL 3.0), available    here: https://www.gnu.org/licenses/lgpl-3.0.txt
*/
#include <Bela.h>
#include <Scope.h>
#include <cmath>
#include <math_neon.h>
#include "utility.h"
// #undef NDEBUG
#include <assert.h>

#include "oscillator.h"
#include "amdf.h"
#include "dc_blocker.h"

float inverseSampleRate;
float phase = 0;

// float lowestTrackableFrequency = 100;
// float longestTrackablePeriod;

Scope scope;

Oscillator osc;
Oscillator osc2;

DcBlocker dcBlocker;

#define LOWESTNOTEPERIOD 424 //424 CORRESPONDS TO pitch of ~104 Hz if samplerate is 44.1 kHz
#define HIGHESTNOTEPERIOD 50 //50 CORRESPONDS TO pitch of 882 Hz if samplerate is 44.1 kHz
#define RINGBUFFER_SIZE LOWESTNOTEPERIOD * 8
int inputPointer = 0;
double outputPointer = 0;
double fadingOutputPointerOffset = 0;
double outputPointerSpeed = 0.75f;
float ringBuffer[RINGBUFFER_SIZE];

float lowPassedRingBuffer[RINGBUFFER_SIZE];
float lowPassedSample = 0;

//RMS stuff
const float rms_C = 0.005;
float squareSum = 0;
float rmsValue = 0;

//Interpolation stuff!
float crossFadeIncrement = 0.001f;
const int sincTableScaleFactor = 10000;
const int sincLength = 10;
const int sincLengthBothSides = (sincLength * 2) + 1; //We want to use sincLength on both sides of the affected sample.
const int windowedSincTableSize = sincLengthBothSides * sincTableScaleFactor;
float blackmanWindow[sincLengthBothSides];

float windowedSincTable[windowedSincTableSize];
float windowedSincTableDifferences[windowedSincTableSize];

//Amdf stuff
Amdf amdf = Amdf(LOWESTNOTEPERIOD, HIGHESTNOTEPERIOD);
float crossfadeValue = 0;
int previousJumpDistance = 0;

float in_l = 0, in_r = 0, out_l = 0, out_r = 0;

inline int wrapBufferSample(int n){
  n += RINGBUFFER_SIZE;
  n %= RINGBUFFER_SIZE;
	return n;
}

inline float wrapBufferSample(float n){
  return fmodf_neon(n + RINGBUFFER_SIZE, RINGBUFFER_SIZE);
}

inline double wrapBufferSample(double n){
  return fmodf_neon(n + RINGBUFFER_SIZE, RINGBUFFER_SIZE);
}

inline double normalizedSinc(double x){
	x *= M_PI;
	if(x == 0){
		return 1.0;
	}
  return sin(x)/x; // will be used when building sinctable, so we want to use the best sin function.
}

float getBlackman(float x, float M){
  return 0.42 - 0.5 * cos(2 * M_PI * x / M) + 0.08 * cos(4 * M_PI * x / M);
}

void initializeWindowedSincTable(){
  float previousY = 0;
  for (size_t i = 0; i < sizeof(windowedSincTable)/sizeof(windowedSincTable[0]); i++) {
    float xSinc = ((float) i)/sincTableScaleFactor - sincLength;
    float xBlackman = ((float) i)/sincTableScaleFactor;
    float y = normalizedSinc(xSinc);
    y *= getBlackman(xBlackman, sincLengthBothSides-1);
    windowedSincTable[i] = y;
    windowedSincTableDifferences[i] = y - previousY;
    previousY = y;
  }
}

// How does it look with a sincLength of 6? sincLengthBothSides is 13
//                         x
//   *   *   *   *   *   *   *   *   *   *   *   *
// |   |   |   |   |   |   |   |   |   |   |   |   |
// 0   1   2   3   4   5   6   7   8   9   10  11  12
//-6  -5  -4  -3  -2  -1   0   1   2   3   4   5   6

float fadeOutInterpolateSample;
float fadeInInterpolateSample;
float interpolateFromTable(float index){
  // First calculate the fractional so we know where to shift the sinc function.
  int integral;
  float fractional = modf_neon(index, &integral);
  int scaledFractional;
  float indexRatio = modf_neon(fractional*sincTableScaleFactor, &scaledFractional);

  int currentSampleIndex = (int)integral - sincLength;
  int fadingSampleIndex = currentSampleIndex - fadingOutputPointerOffset;

  float combinedSamples = 0;
  //Debug code
  fadeInInterpolateSample = fadeOutInterpolateSample = 0;

  float sincValue;
  int x;
  assert(windowedSincTable[0] < 0.000001f);
  assert(windowedSincTable[(sincLengthBothSides-1)*sincTableScaleFactor] < 0.000001f);
  assert(windowedSincTable[sincLength*sincTableScaleFactor] == 1.0f);

	for (int i = 1; i < sincLengthBothSides; i++) {
    currentSampleIndex = wrapBufferSample(currentSampleIndex);
    fadingSampleIndex = wrapBufferSample(fadingSampleIndex);

    x = i*sincTableScaleFactor - (int)scaledFractional;
    sincValue = windowedSincTable[x] + windowedSincTableDifferences[x+1] * indexRatio;
    // TODO: Make the crossfade correct! We are using same crossfadeValue for neighbouring samples in the sinc interpolation
    // Maybe use a table with fade values, we can then retrieve them in here by offsetting with i.
    // Or, perhaps, in some way calculate how much adjacent samples differ in fadevalue and do something like crossfadeValue + (fadedifferencebetweensamples * (i - sincLength)
    // Oh, well. Let's fuck that for now and just listen to it :-D
    float tempCrossfade = crossfadeValue - ((i - sincLength) * crossFadeIncrement);
    // tempCrossfade = 0.0;
    combinedSamples += sincValue * (
                              (tempCrossfade * ringBuffer[fadingSampleIndex])
                            + (1.0f - tempCrossfade) * ringBuffer[currentSampleIndex]
                          );

    // TODO: remove this when everything works!
    // Some debug code
    fadeInInterpolateSample += sincValue * (ringBuffer[currentSampleIndex] * (1.0f - tempCrossfade) );
    fadeOutInterpolateSample += sincValue * (ringBuffer[fadingSampleIndex] * tempCrossfade);


    currentSampleIndex++;
    fadingSampleIndex++;
  }
	return combinedSamples;
}

bool setup(BelaContext *context, void *userData)
{
	scope.setup(5, context->audioSampleRate, 2);
  scope.setSlider(0, 0.25, 1.0, 0.00001, .5f);
  // scope.setSlider(1, 100.0, 430.0, 0.0001, 104.0f);
  scope.setSlider(1, 0.0, 1.0, 0.00001, 1.0f);
	inverseSampleRate = 1.0 / context->audioSampleRate;
  initializeWindowedSincTable();
  amdf.setup(context->audioSampleRate);
  amdf.initiateAMDF(inputPointer - LOWESTNOTEPERIOD, inputPointer, ringBuffer, RINGBUFFER_SIZE);
	return true;
}

int tableIndex = 0;
float frequency = 100;
float jumpPulse = 0;
float phase2 = 0;


void render(BelaContext *context, void *userData)
{
	for(unsigned int n = 0; n < context->audioFrames; n++) {
    //read input, with dc blocking
    in_l = dcBlocker.filter(audioRead(context, n, 0));

    // // Create sine wave
    // phase += 2.0 * M_PI * frequency * inverseSampleRate;
    // // float sample = phase;
    // float sample = sin(phase);
    // in_l = 0.1f * sample;
    //
		// if(phase > M_PI){
		// 	phase -= 2.0 * M_PI;
    //   // scope.trigger();
    // }

    // frequency += 0.001;
    // if(frequency > 881.0)
    //   frequency=105.0;
    // frequency = scope.getSliderValue(1);
    // osc2.setFrequency(frequency);
    // in_l = 0.3 * osc2.nextSample();

		ringBuffer[inputPointer] = in_l;

    outputPointerSpeed = scope.getSliderValue(0);

		// lowPassedSample = 0.1f * ringBuffer[inputPointer] + 0.9 * lowPassedSample; //+ 0.3f * lowPassedRingBuffer[wrapBufferSample(inputPointer-1)] + 0.5f * lowPassedRingBuffer[wrapBufferSample(inputPointer -2)];

    squareSum = (1.0f - rms_C) * squareSum + rms_C * in_l * in_l;
    rmsValue = sqrt(squareSum);

    out_l = interpolateFromTable(outputPointer);

    double waveValue = osc.nextSample();

    float mix = scope.getSliderValue(1);
    out_l = (1.0f - mix) * rmsValue  * waveValue +  mix * out_l;

		audioWrite(context, n, 0, out_l);
    audioWrite(context, n, 1, out_l);

		//Increment all da pointers!!!!
		++inputPointer %= RINGBUFFER_SIZE;

		outputPointer += outputPointerSpeed;
    outputPointer = wrapBufferSample(outputPointer);

    jumpPulse -= 0.01;
    jumpPulse = max(jumpPulse, 0.0f);

    crossfadeValue -= crossFadeIncrement;
    crossfadeValue = max(crossfadeValue, 0.0f);

    if(!amdf.amdfIsDone){
      if(amdf.updateAMDF()){
        previousJumpDistance = amdf.jumpValue;
        if(amdf.jumpDifference < 0.9 && amdf.frequencyEstimate < (context->audioSampleRate/HIGHESTNOTEPERIOD)){
          osc.setFrequency(0.25f *amdf.frequencyEstimate);
        }
        amdf.initiateAMDF(inputPointer - LOWESTNOTEPERIOD, inputPointer, ringBuffer, RINGBUFFER_SIZE);
        // scope.trigger();
      }
    }

    int distanceBetweenInOut = wrapBufferSample(inputPointer - outputPointer);
    if(distanceBetweenInOut > RINGBUFFER_SIZE/4){
      fadingOutputPointerOffset = previousJumpDistance;
      outputPointer = wrapBufferSample(outputPointer + amdf.jumpValue);
      // TODO: Better solution for avoiding divbyzero. Now we're using 1.1 to tackle that.
      float samplesUntilNewJump = (amdf.jumpValue/(1.1 - outputPointerSpeed));
      crossFadeIncrement = 1.0f / samplesUntilNewJump;
      crossfadeValue = 1.0f;
      scope.trigger();
    }

    float outputPointerLocation = ((float)outputPointer) / ((float) RINGBUFFER_SIZE);
    float inputPointerLocation = ((float)inputPointer) / ((float) RINGBUFFER_SIZE);

    tableIndex++;
    tableIndex %= windowedSincTableSize;
    float plottedSincTableValue = windowedSincTable[tableIndex];
    scope.log(inputPointerLocation, outputPointerLocation, crossfadeValue, fadeInInterpolateSample, fadeOutInterpolateSample);//, lowPassedRingBuffer[inputPointer]);

	}
	// rt_printf("magSum: %i, %i\n", RINGBUFFER_SIZE, amdf.bufferLength);
	// rt_printf("audio: %f, %f\n", in_l, out_l);
}

void cleanup(BelaContext *context, void *userData)
{

}
