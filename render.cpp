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
#include <cmath>
#include <libraries/math_neon/math_neon.h>
#include <libraries/Scope/Scope.h>
#include <libraries/Gui/Gui.h>
#include <libraries/GuiController/GuiController.h>

// Let's undefine this so we can get debug output!
#undef NDEBUG
#include <cassert>

#include "utility.h"
#include "delay.h"
#include "filter.h"
#include "oscillator.h"
#include "pitch_shifter.h"
#include "pitch_detector.h"
#include "waveshaper.h"

#include "amdf.h"
#include "dc_blocker.h"
#include "sinc_interpolation.h"

float inverseSampleRate;

// float sinePhase = 0;
float sineFrequency = 400;

Scope scope;
Gui gui;
GuiController controller;

Oscillator osc;
Oscillator osc2;

DcBlocker dcBlocker;

#define SAMPLERATE 44100

const int lowestTrackableFrequency = 95;
const int highestTrackableFrequency = 1000;
const int lowestTrackableNotePeriod = SAMPLERATE / lowestTrackableFrequency;
const int highestTrackableNotePeriod = SAMPLERATE / highestTrackableFrequency;

int inputPointer = 0;

// RMS stuff
const float rms_C = 0.005;
float squareSum = 0;
float rmsValue = 0;

// Amdf stuff
Amdf amdf = Amdf(lowestTrackableNotePeriod, highestTrackableNotePeriod);

Delay audioDelay = Delay(SAMPLERATE, 1.0f);

Filter combFilter = Filter(SAMPLERATE, Filter::COMB);

Waveshaper waveshaper = Waveshaper(Waveshaper::TANH);

PitchShifter pitchShifter = PitchShifter(SAMPLERATE, 95.0f, 800.0f, 0.5f);

PitchDetector pitchDetector = PitchDetector(SAMPLERATE, lowestTrackableFrequency, highestTrackableFrequency);

float bypassProcess(float inSample) { return inSample; }

float (*effectSlots[])(float) = {&bypassProcess, &bypassProcess, &bypassProcess,
                                 &bypassProcess, &bypassProcess, &bypassProcess,
                                 &bypassProcess, &bypassProcess};

float in_l = 0, in_r = 0, out_l = 0, out_r = 0;

unsigned int dryMixSliderIdx, pitchMixSliderIdx, pitchIntervalSliderIdx, pitchTypeSliderIdx, synthMixSliderIdx, synthPitchSliderIdx, synthWaveformSliderIdx;

bool setup(BelaContext *context, void *userData)
{

  scope.setup(6, context->audioSampleRate);

  // Set up the GUI
  gui.setup(context->projectName);
  // and attach to it
  controller.setup(&gui, "Controls");

  // Arguments: name, default value, minimum, maximum, increment
  // store the return value to read from the slider later on
  dryMixSliderIdx = controller.addSlider("dry mix", 0.1f, 0.0, 1.0, 0.00001);
  pitchMixSliderIdx = controller.addSlider("pitch mix", 0.2f, 0.0, 1.0, 0.00001);
  pitchIntervalSliderIdx = controller.addSlider("pitch interval", 0.5f, 0.25, 1.0, 0.00001);
  pitchTypeSliderIdx = controller.addSlider("pitch shift type", 1.0f, 0.0, 1.0, 1.0);
  synthMixSliderIdx = controller.addSlider("synth mix", 0.1f, 0.0, 1.0, 0.00001);
  synthPitchSliderIdx = controller.addSlider("synth pitch", 0.5f, 0.25, 2.0, 0.00001);
  synthWaveformSliderIdx = controller.addSlider("synth waveform", 0.0f, 0.0, 4.0, 1.0);

  // controller.addSlider("filter cutoff", 0.0f, 20.0, 10000.0, 0.1);
  // controller.addSlider("filter resonance", 0.0f, 0.0f, 1.0f, 0.00001);
  // controller.addSlider("env to cutoff", 0.0f, 0.0f, 1.0f, 0.00001);
  // controller.addSlider("shaper type", 0.0f, 0.0f, 3.0f, 1.0);
  // controller.addSlider("shaper drive", 1.0f, 0.0f, 10.0f, 0.00001);

  inverseSampleRate = 1.0 / context->audioSampleRate;

  initializeWindowedSincTable();

  amdf.setup(context->audioSampleRate);
  amdf.initiateAMDF();
  return true;
}

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
    // read input, with dc blocking
    in_l = dcBlocker.filter(audioRead(context, n, 0));
    in_r = dcBlocker.filter(audioRead(context, n, 1));

    // combine input channels
    in_l += in_r;

    // //bypass
    // audioWrite(context, n, 0, audioRead(context, n, 0));
    // audioWrite(context, n, 1, audioRead(context, n, 1));
    // continue;

    // float factor = scope.getSliderValue(0);
    // Create sine wave
    static float sinePhase = 0;
    sinePhase += 2.0 * M_PI * sineFrequency * inverseSampleRate;

    static float sinePhase2 = 0;
    sinePhase2 += 2.0 * M_PI * sineFrequency * 0.5 * inverseSampleRate;

    float sineSample = sin(sinePhase);
    float sineSample2 = sin(sinePhase2);

    // in_l = sineSample * 0.4f + sineSample2 * 0.2;

    if (sinePhase > M_PI)
    {
      sinePhase -= 2.0 * M_PI;
    }
    if (sinePhase2 > M_PI)
    {
      sinePhase2 -= 2.0 * M_PI;
    }

    sineFrequency += 0.0001;
    if (sineFrequency > 881.0)
      sineFrequency = 105.0;

    audioDelay.insertSample(in_l);

    // RMS calculations
    squareSum = (1.0f - rms_C) * squareSum + rms_C * in_l * in_l;
    rmsValue = sqrt(squareSum);

    // Envelope follower calculations
    // filteredAmplitude = filteredAmplitudeC * fabsf_neon(in_l) + (1 -
    // filteredAmplitudeC) * filteredAmplitude;

    float waveValue = osc.nextSample();

    float dryMix = controller.getSliderValue(dryMixSliderIdx);

    float pitchMix = controller.getSliderValue(pitchMixSliderIdx);

    float synthMix = controller.getSliderValue(synthMixSliderIdx);

    float synthPitch = controller.getSliderValue(synthPitchSliderIdx);

    osc.setMode(controller.getSliderValue(synthWaveformSliderIdx));

    // float tremoloPitch = controller.getSliderValue(5);

    // float tremoloMix = controller.getSliderValue(6);

    // float shaperType = controller.getSliderValue(10);

    // float shaperDrive = controller.getSliderValue(11);

    // waveshaper.setDrive(shaperDrive);
    // waveshaper.setShaperType(shaperType);

    // combFilter.setFilterType(controller.getSliderValue(6));
    // float envToCutoff = controller.getSliderValue(9);
    // float cutoffModulation = rmsValue * envToCutoff * 5000.0 - envToCutoff *
    // 5000.0; float cutoff = controller.getSliderValue(7) + cutoffModulation;
    // combFilter.setCutoff(cutoff);
    // combFilter.setResonance(controller.getSliderValue(8));
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

    /// TESTING THE NEW PITCHSHIFTER
    // pitchShifter.setPitchRatio(controller.getSliderValue(2));
    // pitchShifter.setInterpolationsMode((int)controller.getSliderValue(6));
    float pitchedSample = 0.f;
    // if ((int)controller.getSliderValue(3) == 1)
    // {
    //   pitchedSample = pitchShifter.process(in_l);
    // }
    // else
    // {
    //   pitchedSample = pitchShifter.PSOLA(in_l);
    // }

    pitchDetector.process(in_l);
    if (110.f < pitchDetector.getFrequency() && pitchDetector.getFrequency() < highestTrackableFrequency)
    {
      osc.setFrequency(synthPitch * pitchDetector.getFrequency());
    }

    out_l =
        dryMix * in_l
        // Noise for test purposes
        // + synthMix * (-1.0f + 2.0 * static_cast<float>(rand()) /
        + synthMix * rmsValue * waveValue + pitchMix * pitchedSample
        // delay
        // + 0.2f * audioDelay.getSample()
        ;

    // out_l = combFilter.process(out_l);

    // out_l = waveshaper.process(out_l);

    // Apply tremolo/AM
    // out_l = out_l * (1.0 - tremoloSample);

    audioWrite(context, n, 0, out_l);
    audioWrite(context, n, 1, out_l);

    amdf.process(in_l);

    scope.log(in_l, out_l,
              pitchDetector.getProcessedSample(),
              pitchDetector.getRMS(in_l),
              pitchDetector.getTriggerSample(),
              pitchDetector.getFrequency() * 0.001
              // pitchShifter.grains[0].playheadNormalized,
              // pitchShifter.grains[0].currentSample;
              // pitchShifter.grains[1].playheadNormalized,
              // pitchShifter.grains[1].currentSample;
              // pitchShifter.grains[2].playheadNormalized,
              // pitchShifter.grains[2].currentSample;
              // pitchShifter.grains[3].playheadNormalized,
              // pitchShifter.grains[3].currentSample;
              // pitchShifter.grains[4].playheadNormalized,
              // pitchShifter.grains[4].currentSample;
              // pitchShifter.grains[5].playheadNormalized,
              // pitchShifter.grains[5].currentSample;
              // amdf.rmsValue,
              // rmsValue
              // amdf.amdfScore,
              // amdf.progress,
              // pitchedSample
              // amdf.inputPointerProgress,
              // amdf.pitchtrackingAmdfScore,
              // amdf.pitchEstimate,
              // pitchShifter.crossfadeValue
              // float(testCounter)/500.0f,
              // getBlackmanFast((testCounter++)-100, 200)
              // amdf.weight
              // pitchShifter.pitchMarkCandidateScopeDebug,
              // pitchShifter.pitchMarkScopeDebug,
              // amdf.frequencyEstimate / 1000.0
              // (pitchShifter.pitchMarkCandidateIndexOffset / 100.0),
              // pitchShifter.pitchMarkCandidateValue

    );

    if (amdf.pitchEstimateReady)
    {
      // gui.sendBuffer(0, amdf.pitchEstimateReady);
      // rt_printf("pitchEstimated: %f\n", amdf.pitchEstimate);
      // frequency = 440.0f * powf_neon(2, 2 * amdf.pitchEstimate);
      // osc.setFrequency(synthPitch * 0.5f * frequency);
    }

    // osc.setFrequency(synthPitch * 0.5f * amdf.frequencyEstimate);

    if (amdf.amdfIsDone)
    {
      if (amdf.frequencyEstimate < highestTrackableFrequency)
      {
        // osc.setFrequency(0.5f * amdf.frequencyEstimate);
        // frequency = 440.0f * powf_neon(2, 2 * amdf.pitchEstimate);
        // osc.setFrequency(synthPitch * 0.5f * frequency);
      }
      amdf.initiateAMDF();

      scope.trigger();

      pitchShifter.setJumpLength(amdf.jumpValue);
      pitchShifter.setPitchEstimatePeriod(amdf.pitchEstimate);
    }

    if (pitchShifter.hasJumped)
    {
      // scope.trigger();
    }
  }
  // rt_printf("magSum: %i, %i\n", ringBufferSize, amdf.bufferLength);
  // rt_printf("audio: %f, %f\n", in_l, out_l);

  // gui.sendBuffer(0, amdf.pitchEstimateReady);
}

void cleanup(BelaContext *context, void *userData) {}
