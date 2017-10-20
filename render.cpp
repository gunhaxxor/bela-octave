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
#undef NDEBUG
#include <assert.h>

#include "oscillator.h"

float inverseSampleRate;
float phase = 0;

// float lowestTrackableFrequency = 100;
// float longestTrackablePeriod;

Scope scope;

Oscillator osc;

#define LOWESTNOTEPERIOD 424 //300 CORRESPONDS TO pitch of 147 Hz if samplerate is 44.1 kHz
#define HIGHESTNOTEPERIOD 50 //100 CORRESPONDS TO pitch of 441 Hz if samplerate is 44.1 kHz
#define RINGBUFFER_SIZE LOWESTNOTEPERIOD * 4
int inputPointer = 0;
double outputPointer = 0;
double outputPointerSpeed = 1.0f;
float ringBuffer[RINGBUFFER_SIZE];

float lowPassedRingBuffer[RINGBUFFER_SIZE];
float lowPassedSample = 0;

//RMS stuff
const float rms_C = 0.005;
float squareSum = 0;
float rmsValue = 0;

//Interpolation stuff!
const int sincTableScaleFactor = 10000;
const int sincLength = 5;
const int sincLengthBothSides = (sincLength * 2) + 1; //We want to use sincLength on both sides of the affected sample.
const int windowedSincTableSize = sincLengthBothSides * sincTableScaleFactor;
float blackmanWindow[sincLengthBothSides];

float windowedSincTable[windowedSincTableSize];
float windowedSincTableDifferences[windowedSincTableSize];

//Amdf stuff
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
  return sin(x)/x;
  // return sinf_neon(x)/x;
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
float interpolateFromTable(float index){
  // First calculate the fractional so we know where to shift the sinc function.
  int integral;
  float fractional = modf_neon(index, &integral);
  int scaledFractional;
  float indexRatio = modf_neon(fractional*sincTableScaleFactor, &scaledFractional);

  int currentSampleIndex = (int)integral - sincLength;
  int fadingSampleIndex = currentSampleIndex - previousJumpDistance;

  float combinedSamples = 0;
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
    combinedSamples += sincValue * ( (1.0f - crossfadeValue) * ringBuffer[currentSampleIndex] + crossfadeValue * ringBuffer[fadingSampleIndex]);
    currentSampleIndex++;
    fadingSampleIndex++;
  }
	return combinedSamples;
}

bool setup(BelaContext *context, void *userData)
{
	scope.setup(5, context->audioSampleRate, 1);
  scope.setSlider(0, 0.25, 1.0, 0.05, 1.0);
	inverseSampleRate = 1.0 / context->audioSampleRate;
  initializeWindowedSincTable();
	return true;
}

const float amdf_C = 3.0/8.0;
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
    for (int currentCompareIndex = compareIndexStart, i = 0; currentCompareIndex < compareIndexStop; currentCompareIndex+=2, i+=2) {
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

int tableIndex = 0;
float frequency = 330;
float jumpPulse = 0;
float phase2 = 0;


void render(BelaContext *context, void *userData)
{
	for(unsigned int n = 0; n < context->audioFrames; n++) {
    //read input
    in_l = audioRead(context, n, 0);

    // // Create sine wave
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

    // phase2+=2.0 * M_PI * 330 * 10 * inverseSampleRate;
    // in_l += sinf_neon(phase2) * 0.05f;

		ringBuffer[inputPointer] = in_l;

    //test with moving pitch shift
    outputPointerSpeed = scope.getSliderValue(0);
    if(outputPointerSpeed < 0.25){
      outputPointerSpeed = 1.0f;
    }


		lowPassedSample = 0.1f * ringBuffer[inputPointer] + 0.9 * lowPassedSample; //+ 0.3f * lowPassedRingBuffer[wrapBufferSample(inputPointer-1)] + 0.5f * lowPassedRingBuffer[wrapBufferSample(inputPointer -2)];

    squareSum = (1.0f - rms_C) * squareSum + rms_C * in_l * in_l;
    rmsValue = sqrt(squareSum);

    out_l = interpolateFromTable(outputPointer);

    double waveValue = osc.nextSample();
    out_l = 0.06f * rmsValue  * waveValue + 0.1 * out_l;

		audioWrite(context, n, 0, out_l);
    audioWrite(context, n, 1, out_l);

		//Increment all da pointers!!!!
		++inputPointer %= RINGBUFFER_SIZE;

		outputPointer += outputPointerSpeed;
    outputPointer = wrapBufferSample(outputPointer);

    jumpPulse -= 0.01;
    jumpPulse = max(jumpPulse, 0.0f);

    crossfadeValue -= 0.003;
    crossfadeValue = max(crossfadeValue, 0.0f);

    if (!amdfIsDone) {
      if(amdf()){
        previousJumpDistance = bestSoFarIndexJump;
        osc.setFrequency(0.5* context->audioSampleRate / bestSoFarIndexJump);
        outputPointer = wrapBufferSample(outputPointer + bestSoFarIndexJump);
        scope.trigger();
        jumpPulse = 0.5f;
        crossfadeValue = 1.0f;
      }
    }
    int distanceBetweenInOut = wrapBufferSample(inputPointer - outputPointer);
    float proportionalDistance = ((float)distanceBetweenInOut) / ((float) RINGBUFFER_SIZE);
    if(amdfIsDone && distanceBetweenInOut > RINGBUFFER_SIZE/4){
      initiateAMDF();
      // jumpPulse = 1.0;
    }

    float outputPointerLocation = ((float)outputPointer) / ((float) RINGBUFFER_SIZE);
    float inputPointerLocation = ((float)inputPointer) / ((float) RINGBUFFER_SIZE);

    // if(inputPointer%sincLengthBothSides == 0){
    //   jumpPulse = 0.2f;
    // }

    // float plottedSincTableValue = inputPointer < windowedSincTableSize?windowedSincTable[inputPointer]:0.43;
    tableIndex++;
    tableIndex %= windowedSincTableSize;
    float plottedSincTableValue = windowedSincTable[tableIndex];
    scope.log(in_l, out_l, rmsValue, waveValue, crossfadeValue);//, lowPassedRingBuffer[inputPointer]);

	}
	// rt_printf("pointerIndices: %i, %i\n", inputPointer, (int)outputPointer);
	// rt_printf("audio: %f, %f\n", in_l, out_l);
}

void cleanup(BelaContext *context, void *userData)
{

}
