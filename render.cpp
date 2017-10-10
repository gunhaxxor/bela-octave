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

float inverseSampleRate;
float phase = 0;

// float lowestTrackableFrequency = 100;
// float longestTrackablePeriod;

Scope scope;

#define LOWESTNOTEPERIOD 300 //300 CORRESPONDS TO pitch of 137 Hz if samplerate is 44.1 kHz
#define HIGHESTNOTEPERIOD 50 //100 CORRESPONDS TO pitch of 441 Hz if samplerate is 44.1 kHz
#define RINGBUFFER_SIZE LOWESTNOTEPERIOD * 4 *8
int inputPointer = 0;
// float fadingOutputPointer = 0;
double outputPointer = 0;
double outputPointerSpeed = 0.8f;
float ringBuffer[RINGBUFFER_SIZE];
float lowPassedRingBuffer[RINGBUFFER_SIZE];

const int sincLength = 6;
const int sincLengthBothSides = (sincLength * 2); //We want to use sinclength on both sides of the affected sample.
float blackmanWindow[sincLengthBothSides];
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
  return sinf_neon(x)/x;
}

void initializeBlackmanWindow(){
  for (size_t x = 0; x < sincLengthBothSides; x++) {
    blackmanWindow[x] = 0.42 - 0.5 * cos(2 * M_PI * x / (sincLengthBothSides - 1)) + 0.08 * cos(4 * M_PI * x / (sincLengthBothSides-1));
  }
}

float getBlackman(float x, float M){
  return 0.42 - 0.5 * cosf_neon(2 * M_PI * x / M) + 0.08 * cosf_neon(4 * M_PI * x / M);
}

bool setup(BelaContext *context, void *userData)
{
	scope.setup(5, context->audioSampleRate);
	inverseSampleRate = 1.0 / context->audioSampleRate;
	// longestTrackablePeriod = context->audioSampleRate / lowestTrackableFrequency;
  initializeBlackmanWindow();
	return true;
}


// TODO: Make this function moar optimized!
// TODO: Make my own sincfv function that is basically an edited version of sinfv_neon
float xValues[sincLengthBothSides];
float sincValues[sincLengthBothSides];
float combinedSamples;
double fractional;
int currentSampleIndex;
inline float interpolate(double index){
	//First calculate the fractional so we know where to shift the sinc function.
  // currentSampleIndex;
  // fractional = modf_neon(index, &currentSampleIndex);
  double integral;
  fractional = modf(index, &integral);
  currentSampleIndex = (int)integral - sincLength;
  int fadingSampleIndex = currentSampleIndex - previousJumpDistance;

  combinedSamples = 0;

	for (int i = 0; i < sincLengthBothSides; i++) {
		// if(currentSampleIndex < 0){
		// 	currentSampleIndex += RINGBUFFER_SIZE;
		// }
    xValues[i] = (i - sincLength - fractional) * M_PI;
    blackmanWindow[i] = getBlackman(i + 1 - fractional, sincLengthBothSides+1);
    // xValues[i] *= M_PI;
  }

  sinfv_neon(xValues, sincLengthBothSides, sincValues);

  for (int i = 0; i < sincLengthBothSides; i++) {
    currentSampleIndex = wrapBufferSample(currentSampleIndex);
    fadingSampleIndex = wrapBufferSample(fadingSampleIndex);

    xValues[i] == 0 ? sincValues[i] = 1.0f : (sincValues[i] /= xValues[i]) *= blackmanWindow[i];
		combinedSamples += sincValues[i] * ((1.0f - crossfadeValue) * ringBuffer[currentSampleIndex] + crossfadeValue * ringBuffer[fadingSampleIndex]);

		currentSampleIndex++;
    fadingSampleIndex++;
  }

	return combinedSamples;
}

const float amdf_C = 3.0/8.0;
// const int dMax = LOWESTNOTEPERIOD * 2;
const int correlationWindowSize = LOWESTNOTEPERIOD * amdf_C;
const int searchWindowSize = LOWESTNOTEPERIOD - correlationWindowSize;

float bestSoFar;
int bestSoFarIndex;
int bestSoFarIndexJump;
int searchIndexStart;
int searchIndexStop;
int currentSearchIndex;
int compareIndexStart;
int compareIndexStop;
bool amdfIsDone = true;
float magSum = 0;
void initiateAMDF(){
  bestSoFar = 10000000.0f;
  searchIndexStart = (int) outputPointer;//wrapBufferSample(inputPointer - LOWESTNOTEPERIOD);
  bestSoFarIndex = searchIndexStart;
  searchIndexStop = searchIndexStart + searchWindowSize;

  //initiate outer loop
  currentSearchIndex = searchIndexStart;

  //initiate inner loop
  compareIndexStart = inputPointer - correlationWindowSize;
  compareIndexStop = compareIndexStart + correlationWindowSize;
  amdfIsDone = false;
}

bool amdf (){
	// float bestSoFar = 10000000.0f;
  // int searchIndexStart = wrapBufferSample(inputPointer - LOWESTNOTEPERIOD);
	// int bestSoFarIndex = searchIndexStart;
  // int searchIndexStop = searchIndexStart + searchWindowSize;
  // for (int currentSearchIndex = searchIndexStart; currentSearchIndex < searchIndexStop; ++currentSearchIndex) {
    magSum = 0;
    for (int currentCompareIndex = compareIndexStart, i = 0; currentCompareIndex < compareIndexStop; ++currentCompareIndex, ++i) {
      int k = wrapBufferSample(currentCompareIndex);
      int km = wrapBufferSample(currentSearchIndex + i);
  		magSum += fabsf_neon(ringBuffer[km] - ringBuffer[k]);
  	}
    magSum /= correlationWindowSize;
    if(magSum < bestSoFar){
      bestSoFar = magSum;
      bestSoFarIndex = currentSearchIndex%RINGBUFFER_SIZE;
      bestSoFarIndexJump = wrapBufferSample(compareIndexStart - currentSearchIndex);
    }
  // }
  if(currentSearchIndex < searchIndexStop){
    amdfIsDone = false;
  }else{
    amdfIsDone = true;
    magSum = bestSoFar;
  }

  currentSearchIndex++;

  return amdfIsDone;
  // return bestSoFarIndex;
}

float frequency = 330;
float jumpPulse = 0;
void render(BelaContext *context, void *userData)
{
	for(unsigned int n = 0; n < context->audioFrames; n++) {
    //read input
    in_l = audioRead(context, n, 0);

    // Create sine wave
    // phase += 2.0 * M_PI * frequency * inverseSampleRate;
    // // float sample = phase;
    // float sample = sin(phase);
    // in_l = 0.1f * sample;
    //
		// if(phase > M_PI)
		// 	phase -= 2.0 * M_PI;
    //
    // // frequency += 0.0001;
    // if(frequency > 500.0)
    //   frequency=200.0;

		ringBuffer[inputPointer] = in_l;

    //test with moving pitch shift
    outputPointerSpeed -= 0.000001;
    if(outputPointerSpeed < 0.25){
      outputPointerSpeed = 1.0f;
    }


		// float lowPassedSample = 0.2f * ringBuffer[inputPointer] + 0.8f * lowPassedRingBuffer[wrapBufferSample(inputPointer-1)] + 0.0f * lowPassedRingBuffer[wrapBufferSample(inputPointer -2)];
		// lowPassedRingBuffer[inputPointer] = lowPassedSample;

    // if(fmodf_neon(outputPointer, sincLengthBothSides*2) < outputPointerSpeed){
      out_l = interpolate(outputPointer);
    // }

    // if(n < sincLengthBothSides){
    //   scope.log(in_l, out_l, blackmanWindow[n], sincValues[n]);//, lowPassedRingBuffer[inputPointer]);
    // }

    // out_l = in_l;
    // int flooredOutputPointer;
    // modf_neon(outputPointer, &flooredOutputPointer);
    // out_l = ringBuffer[flooredOutputPointer];

		audioWrite(context, n, 0, out_l);
    audioWrite(context, n, 1, out_l);

		//Increment all da pointers!!!!
		++inputPointer %= RINGBUFFER_SIZE;

		outputPointer += outputPointerSpeed;
    outputPointer = wrapBufferSample(outputPointer);
		// if(!(outputPointer < RINGBUFFER_SIZE)){
		// 	outputPointer -= RINGBUFFER_SIZE;
    // }

    // fadingOutputPointer += fadingOutputPointerSpeed;
    // if(!(fadingOutputPointer < RINGBUFFER_SIZE)){
    //   fadingOutputPointer -= RINGBUFFER_SIZE;
    // }

    jumpPulse -= 0.01;
    jumpPulse = max(jumpPulse, 0.0f);

    crossfadeValue -= 0.01;
    crossfadeValue = max(crossfadeValue, 0.0f);

    // if (!amdfIsDone) {
    //   if(amdf()){
    //     // rt_printf("amdf was done. Best length: %i\n", bestSoFarIndexJump);
    //     previousJumpDistance = bestSoFarIndexJump;
    //     outputPointer = wrapBufferSample(outputPointer + bestSoFarIndexJump);
    //     // int skipDistance = wrapBufferSample(newOutputPointer - outputPointer);
    //     // rt_printf("skip distance: %i\n", skipDistance);
    //     // outputPointer = newOutputPointer;
    //     // scope.trigger();
    //     jumpPulse = 0.5f;
    //     crossfadeValue = 1.0f;
    //   }
    // }
    // int distanceBetweenInOut = wrapBufferSample(inputPointer - outputPointer);
    // float proportionalDistance = ((float)distanceBetweenInOut) / ((float) RINGBUFFER_SIZE);
    // if(amdfIsDone && distanceBetweenInOut > RINGBUFFER_SIZE/4){
    //   initiateAMDF();
    //   // jumpPulse = 1.0;
    // }

    float outputPointerLocation = ((float)outputPointer) / ((float) RINGBUFFER_SIZE);
    float inputPointerLocation = ((float)inputPointer) / ((float) RINGBUFFER_SIZE);

    // if(inputPointer%sincLengthBothSides == 0){
    //   jumpPulse = 0.2f;
    // }
    // scope.log(in_l, out_l, outputPointerLocation, inputPointerLocation);//, lowPassedRingBuffer[inputPointer]);

	}
	// rt_printf("pointerIndices: %i, %i\n", inputPointer, (int)outputPointer);
	// rt_printf("audio: %f, %f\n", in_l, out_l);
}

void cleanup(BelaContext *context, void *userData)
{

}
