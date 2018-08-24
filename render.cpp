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
#include "delay.h"
#include "filter.h"
#include "waveshaper.h"
#include "amdf.h"
#include "sinc_interpolation.h"
#include "dc_blocker.h"

float inverseSampleRate;
float phase = 0;

Scope scope;

Oscillator osc;

DcBlocker dcBlocker;

#define SAMPLERATE 44100

// #define LOWESTTRACKABLEFREQUENCY 103
// #define HIGHESTTRACKABLEFREQUENCY 882

// #define LOWESTNOTEPERIOD 424 //424 CORRESPONDS TO pitch of ~104 Hz if samplerate is 44.1 kHz
// #define HIGHESTNOTEPERIOD 50 //50 CORRESPONDS TO pitch of 882 Hz if samplerate is 44.1 kHz
// #define ringBufferSize LOWESTNOTEPERIOD * 4

const int lowestTrackableFrequency = 95;
const int highestTrackableFrequency = 800;
const int lowestTrackableNotePeriod = SAMPLERATE / lowestTrackableFrequency;
const int highestTrackableNotePeriod = SAMPLERATE / highestTrackableFrequency;
const int ringBufferSize = lowestTrackableNotePeriod * 8;

int inputPointer = 0;
float outputPointer = 0;
float fadingOutputPointerOffset = 0;
float outputPointerSpeed = 0.75f;
float ringBuffer[ringBufferSize];

float lowPassedRingBuffer[ringBufferSize];
float lowPassedSample = 0;

//RMS stuff
const float rms_C = 0.0005;
float squareSum = 0;
float rmsValue = 0;

//Interpolation stuff!
float crossFadeIncrement = 0.001f;
// const int sincTableScaleFactor = 10000;
// const int sincLength = 10;
// const int sincLengthBothSides = (sincLength * 2) + 1; //We want to use sincLength on both sides of the affected sample.
// const int windowedSincTableSize = sincLengthBothSides * sincTableScaleFactor;
// float blackmanWindow[sincLengthBothSides];

// float windowedSincTable[windowedSincTableSize];
// float windowedSincTableDifferences[windowedSincTableSize];

//Amdf stuff
Amdf amdf = Amdf(lowestTrackableNotePeriod, highestTrackableNotePeriod);
float crossfadeValue = 0;

// float pitchRingBuffer1[ringBufferSize];
// float pitchRingBuffer2[ringBufferSize];

Delay audioDelay = Delay(SAMPLERATE, 1.0f);

Filter combFilter = Filter(SAMPLERATE, Filter::COMB);

Waveshaper waveshaper = Waveshaper(Waveshaper::TANH);

float bypassProcess(float inSample)
{
  return inSample;
}

float (*effectSlots[])(float) = {
    &bypassProcess,
    &bypassProcess,
    &bypassProcess,
    &bypassProcess,
    &bypassProcess,
    &bypassProcess,
    &bypassProcess,
    &bypassProcess};

float in_l = 0,
      in_r = 0, out_l = 0, out_r = 0;

// inline float normalizedSinc(float x)
// {
//   x *= M_PI;
//   if (x == 0)
//   {
//     return 1.0;
//   }
//   return sin(x) / x; // will be used when building sinctable, so we want to use the best sin function.
// }

// float getBlackman(float x, float M)
// {
//   return 0.42 - 0.5 * cos(2 * M_PI * x / M) + 0.08 * cos(4 * M_PI * x / M);
// }

// void initializeWindowedSincTable()
// {
//   float previousY = 0;
//   for (size_t i = 0; i < sizeof(windowedSincTable) / sizeof(windowedSincTable[0]); i++)
//   {
//     float xSinc = ((float)i) / sincTableScaleFactor - sincLength;
//     float xBlackman = ((float)i) / sincTableScaleFactor;
//     float y = normalizedSinc(xSinc);
//     y *= getBlackman(xBlackman, sincLengthBothSides - 1);
//     windowedSincTable[i] = y;
//     windowedSincTableDifferences[i] = y - previousY;
//     previousY = y;
//   }
// }

// // How does it look with a sincLength of 6? sincLengthBothSides is 13
// //                         x
// //   *   *   *   *   *   *   *   *   *   *   *   *
// // |   |   |   |   |   |   |   |   |   |   |   |   |
// // 0   1   2   3   4   5   6   7   8   9   10  11  12
// //-6  -5  -4  -3  -2  -1   0   1   2   3   4   5   6
// float interpolateFromTable(float index)
// {
//   // First calculate the fractional so we know where to shift the sinc function.
//   int integral;
//   float fractional = modf_neon(index, &integral);
//   int scaledFractional;
//   float indexRatio = modf_neon(fractional * sincTableScaleFactor, &scaledFractional);

//   int currentSampleIndex = (int)integral - sincLength;
//   int fadingSampleIndex = currentSampleIndex - fadingOutputPointerOffset;

//   float combinedSamples = 0;

//   float sincValue;
//   int x;
//   assert(windowedSincTable[0] < 0.000001f);
//   assert(windowedSincTable[(sincLengthBothSides - 1) * sincTableScaleFactor] < 0.000001f);
//   assert(windowedSincTable[sincLength * sincTableScaleFactor] == 1.0f);

//   for (int i = 1; i < sincLengthBothSides; i++)
//   {
//     currentSampleIndex = wrapBufferSample(currentSampleIndex, ringBufferSize);
//     fadingSampleIndex = wrapBufferSample(fadingSampleIndex, ringBufferSize);

//     x = i * sincTableScaleFactor - (int)scaledFractional;
//     sincValue = windowedSincTable[x] + windowedSincTableDifferences[x + 1] * indexRatio;
//     // TODO: Make the crossfade correct! We are using same crossfadeValue for neighbouring samples in the sinc interpolation
//     // Maybe use a table with fade values, we can then retrieve them in here by offsetting with i.
//     // Or, perhaps, in some way calculate how much adjacent samples differ in fadevalue and do something like crossfadeValue + (fadedifferencebetweensamples * (i - sincLength)
//     // Oh, well. Let's fuck that for now and just listen to it :-D
//     float tempCrossfade = crossfadeValue - ((i - sincLength) * crossFadeIncrement);
//     combinedSamples += sincValue * ((tempCrossfade * ringBuffer[fadingSampleIndex]) + (1.0f - tempCrossfade) * ringBuffer[currentSampleIndex]);

//     currentSampleIndex++;
//     fadingSampleIndex++;
//   }
//   return combinedSamples;
// }

bool setup(BelaContext *context, void *userData)
{
  // lowestTrackableNotePeriod = context->audioSampleRate / LOWESTTRACKABLEFREQUENCY;
  // highestTrackableNotePeriod = context->audioSampleRate / HIGHESTTRACKABLEFREQUENCY;
  // ringBufferSize = lowestTrackableNotePeriod * 4;

  scope.setup(5, context->audioSampleRate, 12);
  // scope.setSlider(0, 1.0, 16.0, 0.00001, 1.0f);
  scope.setSlider(0, 0.0, 1.0, 0.00001, 0.5f, "dry mix");
  scope.setSlider(1, 0.0, 1.0, 0.00001, 0.0f, "pitch mix");
  scope.setSlider(2, 0.25, 1.0, 0.00001, .5f, "pitch interval");
  scope.setSlider(3, 0.0, 1.0, 0.00001, 0.5f, "synth mix");
  scope.setSlider(4, 0.25, 2.0, 0.00001, 0.5f, "synth pitch");
  scope.setSlider(5, 0.0, 4.0, 1.0, 0.0f, "synth waveform");
  scope.setSlider(6, 0.0, 1.0, 1.0, 0.0f, "filter type");
  scope.setSlider(7, 20.0, 10000.0, 0.1, 500.0, "filter cutoff");
  scope.setSlider(8, 0.0f, 1.0f, 0.00001, 0.0f, "filter resonance");
  scope.setSlider(9, 0.0f, 1.0f, 0.00001, 0.0f, "env to cutoff");
  scope.setSlider(10, 0.0f, 3.0f, 1.0, 0.0f, "shaper type");
  scope.setSlider(11, 0.0f, 10.0f, 0.00001, 1.0f, "shaper drive");

  inverseSampleRate = 1.0 / context->audioSampleRate;

  initializeWindowedSincTable();

  amdf.setup(context->audioSampleRate);
  amdf.initiateAMDF(inputPointer - lowestTrackableNotePeriod, inputPointer, ringBuffer, ringBufferSize);
  return true;
}

// int tableIndex = 0;
float frequency = 100;
float jumpPulse = 0;
float phase2 = 0;

float oscTargetAmplitude;
float oscAmplitude;
float oscAmplitudeIncrement = .00001f;

float filteredAmplitudeC = 0.002;
float filteredAmplitude = 0.0f;

void render(BelaContext *context, void *userData)
{
  for (unsigned int n = 0; n < context->audioFrames; n++)
  {
    //read input, with dc blocking
    in_l = dcBlocker.filter(audioRead(context, n, 0));
    // in_r = dcBlocker.filter(audioRead(context, n, 1));

    // //bypass
    // audioWrite(context, n, 0, audioRead(context, n, 0));
    // audioWrite(context, n, 1, audioRead(context, n, 1));
    // continue;

    // float factor = scope.getSliderValue(0);
    // // Create sine wave
    // phase += factor * 2.0 * M_PI * frequency * inverseSampleRate;
    // // float sample = phase;
    // float sample = sin(phase);
    // in_l = 0.08f * sample;
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

    audioDelay.insertSample(in_l);

    //RMS calculations
    squareSum = (1.0f - rms_C) * squareSum + rms_C * in_l * in_l;
    rmsValue = sqrt(squareSum);

    //Envelope follower calculations
    filteredAmplitude = filteredAmplitudeC * fabsf_neon(in_l) + (1 - filteredAmplitudeC) * filteredAmplitude;

    float pitchedSample = interpolateFromRingBuffer(outputPointer, ringBuffer, ringBufferSize);

    float waveValue = osc.nextSample();

    float dryMix = scope.getSliderValue(0);

    float pitchMix = scope.getSliderValue(1);

    outputPointerSpeed = scope.getSliderValue(2);

    float synthMix = scope.getSliderValue(3);

    float synthPitch = scope.getSliderValue(4);

    osc.setMode(scope.getSliderValue(5));

    // float tremoloPitch = scope.getSliderValue(5);

    // float tremoloMix = scope.getSliderValue(6);

    float shaperType = scope.getSliderValue(10);

    float shaperDrive = scope.getSliderValue(11);

    waveshaper.setDrive(shaperDrive);
    waveshaper.setShaperType(shaperType);

    combFilter.setFilterType(scope.getSliderValue(6));
    float envToCutoff = scope.getSliderValue(9);
    float cutoffModulation = rmsValue * envToCutoff * 5000.0 - envToCutoff * 5000.0;
    float cutoff = scope.getSliderValue(7) + cutoffModulation;
    combFilter.setCutoff(cutoff);
    combFilter.setResonance(scope.getSliderValue(8));
    // osc.setFrequency(synthPitch * 220.0);

    // oscTargetAmplitude = amdf.frequencyEstimateScore;
    // if (oscAmplitude > oscTargetAmplitude + oscAmplitudeIncrement)
    // {
    //   oscAmplitude -= oscAmplitudeIncrement;
    // }
    // else if (oscAmplitude < oscTargetAmplitude)
    // {
    //   oscAmplitude += 2 * oscAmplitudeIncrement;
    // }

    // // follows tracked pitch
    // phase += tremoloPitch * 2.0 * M_PI * frequency * inverseSampleRate;
    // // doesn't track pitch
    // // phase += tremoloPitch * 2.0 * M_PI * 200.0 * inverseSampleRate;

    // if (phase > M_PI)
    // {
    //   phase -= 2.0 * M_PI;
    // }
    // float tremoloSample = sinf_neon(phase);
    // tremoloSample = 0.5f * (tremoloSample + 1) * tremoloMix;

    float waveThreshold = 0.016;
    out_l = dryMix * in_l
            //Noise for test purposes
            // + synthMix * (-1.0f + 2.0 * static_cast<float>(rand()) / static_cast<float>(RAND_MAX))
            // just synth note for test
            // + synthMix * waveValue
            // + synthMix * max(rmsValue - waveThreshold, 0.0f) * waveValue
            + synthMix * rmsValue * amdf.frequencyEstimateConfidence * waveValue
            // + synthMix * filteredAmplitude * waveValue
            // + synthMix * sinf_neon(pitchMix * in_l);
            + pitchMix * pitchedSample
        //delay
        // + 0.2f * audioDelay.getSample()
        ;

    out_l = combFilter.process(out_l);

    out_l = waveshaper.process(out_l);

    //Apply tremolo/AM
    // out_l = out_l * (1.0 - tremoloSample);

    audioWrite(context, n, 0, out_l);
    audioWrite(context, n, 1, out_l);

    //Increment all da pointers!!!!
    ++inputPointer %= ringBufferSize;

    outputPointer += outputPointerSpeed;
    outputPointer = wrapBufferSample(outputPointer, ringBufferSize);
    //
    // jumpPulse -= 0.01;
    // jumpPulse = max(jumpPulse, 0.0f);

    crossfadeValue -= crossFadeIncrement;
    crossfadeValue = max(crossfadeValue, 0.0f);

    if (!amdf.amdfIsDone)
    {
      if (amdf.updateAMDF())
      {
        if (amdf.frequencyEstimate < highestTrackableFrequency)
        {
          // osc.setFrequency(0.5f *amdf.frequencyEstimate);
          frequency = 440.0f * powf_neon(2, 2 * amdf.pitchEstimate);
          osc.setFrequency(synthPitch * 0.5f * frequency);
        }
        amdf.initiateAMDF(inputPointer - lowestTrackableNotePeriod, inputPointer, ringBuffer, ringBufferSize);
        scope.trigger();
      }
    }

    int distanceBetweenInOut = wrapBufferSample(inputPointer - outputPointer, ringBufferSize);
    if (distanceBetweenInOut > ringBufferSize / 4)
    {
      fadingOutputPointerOffset = amdf.jumpValue; //We're gonna jump this far. so save the distance to be able to crossfade to were we are now.
      outputPointer = wrapBufferSample(outputPointer + amdf.jumpValue, ringBufferSize);
      // TODO: Better solution for avoiding divbyzero. Now we're using 1.1 to tackle that.
      float pitchedSamplesUntilNewJump = (amdf.jumpValue / (1.1 - outputPointerSpeed));
      crossFadeIncrement = 1.0f / pitchedSamplesUntilNewJump;
      crossfadeValue = 1.0f;
      // scope.trigger();
    }

    // float outputPointerLocation = ((float)outputPointer) / ((float)ringBufferSize);
    // float inputPointerLocation = ((float)inputPointer) / ((float)ringBufferSize);

    // tableIndex++;
    // tableIndex %= windowedSincTableSize;
    // float plottedSincTableValue = windowedSincTable[tableIndex];
    // scope.log(in_l, out_l, 0.6 * (int)amdf.atTurnPoint, 10*amdf.weight, amdf.pitchtrackingAmdfScore);//, lowPassedRingBuffer[inputPointer]);
    // scope.log(in_l, rmsValue, filteredAmplitude, amdf.frequencyEstimateConfidence, 0.5);
    scope.log(in_l, out_l);
  }
  // rt_printf("magSum: %i, %i\n", ringBufferSize, amdf.bufferLength);
  // rt_printf("audio: %f, %f\n", in_l, out_l);
}

void cleanup(BelaContext *context, void *userData)
{
}
