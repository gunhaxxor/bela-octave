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

#include "audio_effect_interface.hpp"
#include "utility.h"
#include "delay.h"
#include "filter.h"
#include "oscillator.h"
#include "pitch_shifter.h"
#include "pitchfollowing_tremolo.hpp"
// #include "pitch_detector.h"
#include "waveshaper.h"

#include <q/pitch/pitch_detector.hpp>
#include <q/support/literals.hpp>
namespace q = cycfi::q;
using namespace q::literals;

// #include "amdf.h"
#include "dc_blocker.h"
#include "sinc_interpolation.h"

float inverseSampleRate;

Scope scope;
Gui gui;
GuiController controller;

Oscillator osc;
Oscillator osc2;

DcBlocker dcBlocker;

#define SAMPLERATE 44100

const int lowestTrackableFrequency = 95;
const int highestTrackableFrequency = 1200;
const int lowestTrackableNotePeriod = SAMPLERATE / lowestTrackableFrequency;
const int highestTrackableNotePeriod = SAMPLERATE / highestTrackableFrequency;

int inputPointer = 0;

// RMS stuff
const float rms_C = 0.005;
float squareSum = 0;
float rmsValue = 0;

Delay audioDelay = Delay(SAMPLERATE, 1.0f);

PitchFollowingTremolo tremolo = PitchFollowingTremolo(SAMPLERATE);

Filter combFilter = Filter(SAMPLERATE, Filter::COMB);

Waveshaper waveshaper = Waveshaper(Waveshaper::TANH);

q::pitch_detector pd(lowestTrackableFrequency, highestTrackableFrequency, SAMPLERATE, -45_dB);

PitchShifter pitchShifter = PitchShifter(SAMPLERATE, 95.0f, 800.0f, 0.5f);

BypassEffect bypass = BypassEffect();

AudioEffect *effectSlots[] = {&bypass, &bypass, &bypass,
                              &bypass, &bypass, &bypass,
                              &bypass, &bypass};
int effectSlotsSize = sizeof(effectSlots) / sizeof(effectSlots[0]);

float in_l = 0, in_r = 0, out_l = 0, out_r = 0;

unsigned int dryMixSliderIdx, pitchMixSliderIdx, pitchIntervalSliderIdx, pitchTypeSliderIdx, synthMixSliderIdx, synthPitchSliderIdx, synthWaveformSliderIdx, tremoloFrequencyIdx, tremoloIntensityIdx, tremoloPitchFollowAmountIdx;

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
  pitchMixSliderIdx = controller.addSlider("pitch mix", 0.3f, 0.0, 1.0, 0.00001);
  pitchIntervalSliderIdx = controller.addSlider("pitch interval", 0.5f, 0.25, 1.0, 0.00001);
  pitchTypeSliderIdx = controller.addSlider("pitch shift type", 1.0f, 0.0, 1.0, 1.0);
  synthMixSliderIdx = controller.addSlider("synth mix", 0.0f, 0.0, 1.0, 0.00001);
  synthPitchSliderIdx = controller.addSlider("synth pitch", 0.5f, 0.25, 2.0, 0.00001);
  synthWaveformSliderIdx = controller.addSlider("synth waveform", 2.0f, 0.0, 4.0, 1.0);
  tremoloFrequencyIdx = controller.addSlider("tremolo frequency", 2.0f, 0.1, 200.0, 0.001);
  tremoloIntensityIdx = controller.addSlider("tremolo intensity", 0.8f, 0.0f, 1.0f, 0.00001);
  tremoloPitchFollowAmountIdx = controller.addSlider("tremoloPitchFollow", 0.0f, 0.0f, 2.0f, 0.00001);
  // controller.addSlider("shaper type", 0.0f, 0.0f, 3.0f, 1.0);
  // controller.addSlider("shaper drive", 1.0f, 0.0f, 10.0f, 0.00001);

  // effectSlots[0] = &pitchShifter;
  // effectSlots[1] = &audioDelay;
  effectSlots[2] = &tremolo;

  inverseSampleRate = 1.0 / context->audioSampleRate;

  initializeWindowedSincTable();
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

    // audioDelay.insertSample(in_l);

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

    tremolo.setBaseFrequency(controller.getSliderValue(tremoloFrequencyIdx));
    tremolo.setPitchFollowAmount(controller.getSliderValue(tremoloPitchFollowAmountIdx));
    tremolo.setIntensity(controller.getSliderValue(tremoloIntensityIdx));

    /// TESTING THE NEW PITCHSHIFTER
    pitchShifter.setPitchRatio(controller.getSliderValue(pitchIntervalSliderIdx));
    float pitchShifterType = controller.getSliderValue(pitchTypeSliderIdx);
    float pitchedSample = 0.f;
    // pitchedSample = pitchShifter.process(in_l);

    bool pdIsReady = pd(in_l);
    static float trackedFrequency = 0.f;
    if (pdIsReady)
    {
      trackedFrequency = pd.get_frequency();
      tremolo.setFollowedPitch(trackedFrequency);
      float period = SAMPLERATE / trackedFrequency;
      if (pitchShifterType > 0.5f)
      {
        pitchShifter.setJumpLength(period);
      }
    }

    osc.setFrequency(synthPitch * trackedFrequency);

    for (int i = 0; i < effectSlotsSize; i++)
    {
      in_l = effectSlots[i]->process(in_l);
    }
    out_l = in_l;

    // out_l =
    //     dryMix * in_l
    //     // Noise for test purposes
    //     // + synthMix * (-1.0f + 2.0 * static_cast<float>(rand()) /
    //     + synthMix * rmsValue * waveValue
    //     //
    //     + pitchMix * pitchedSample;
    //delay
    // + 0.2f * audioDelay.getSample();

    // out_l = combFilter.process(out_l);

    // out_l = waveshaper.process(out_l);

    audioWrite(context, n, 0, out_l);
    audioWrite(context, n, 1, out_l);

    scope.log(
        in_l,
        out_l,
        trackedFrequency * 0.001);
  }
  // rt_printf("magSum: %i, %i\n", ringBufferSize, amdf.bufferLength);
  // rt_printf("audio: %f, %f\n", in_l, out_l);
}

void cleanup(BelaContext *context, void *userData) {}
