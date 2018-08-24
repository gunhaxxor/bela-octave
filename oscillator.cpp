#include "oscillator.h"
#include <math_neon.h>

void Oscillator::setMode(OscillatorMode mode)
{
    mOscillatorMode = mode;
}

void Oscillator::setMode(float mode)
{
    int modeValue = (int)mode;
    this->setMode((OscillatorMode)modeValue);
}

void Oscillator::setFrequency(float frequency)
{
    mFrequency = frequency;
    updateIncrement();
}

void Oscillator::setSampleRate(float sampleRate)
{
    mSampleRate = sampleRate;
    updateIncrement();
}

void Oscillator::updateIncrement()
{
    mPhaseIncrement = mFrequency * 2 * mPI / mSampleRate;
}

// Expects a t parameter that represents a position on the period scaled from 0 - 1, and where the center of the polyblep is at 0.
// The internal code takes care of the negative side of the polyblep and rescaling to 1.0 t per sample.
float Oscillator::polyBLEP(float t)
{
    // dt is how much (between 0 and 1) of a period each sample is
    float dt = mPhaseIncrement / twoPI;
    // Check if t is at the start of the period (within 1 samples distance).
    // 0 <= t < 1
    if (t < dt)
    {
        t /= dt; //Rescale so that the t is incrementing 1 for each sample
        return t + t - t * t - 1.0;
    }
    // -1 < t < 0
    // Check if t is at the far end of the period (within 1 samples distance).
    else if (t > 1.0 - dt)
    {
        t = (t - 1.0) / dt; //shift one period to the left (to become negative) and scale to 1.0 increments per sample
        return t * t + t + t + 1.0;
    }
    // 0 otherwise
    else
        return 0.0;
}

float Oscillator::naiveWaveform()
{
    float value;
    switch (mOscillatorMode)
    {
    case OSCILLATOR_MODE_SINE:
        value = sinf(mPhase);
        break;
    case OSCILLATOR_MODE_SAW:
        value = (2.0 * mPhase / (twoPI)) - 1.0;
        break;
    case OSCILLATOR_MODE_SQUARE:
        if (mPhase < mPI)
        {
            value = 1.0;
        }
        else
        {
            value = -1.0;
        }
        break;
    case OSCILLATOR_MODE_TRIANGLE:
        value = -1.0 + (2.0 * mPhase / twoPI);
        value = 2.0 * (fabs(value) - 0.5);
        break;
    default:
        break;
    }
    return value;
}

float Oscillator::nextSample()
{
    // t is where in the period we are scaled from 0 to 1
    float t = mPhase / twoPI;
    float value = naiveWaveform();

    if (mOscillatorMode == OSCILLATOR_MODE_SINE)
    {
        value = naiveWaveform();
    }
    else if (mOscillatorMode == OSCILLATOR_MODE_SAW)
    {
        value -= polyBLEP(t);
    }
    else
    {
        value = naiveWaveform();
        value += polyBLEP(t);
        value -= polyBLEP(fmodf_neon(t + 0.5, 1.0));

        //If it's triangle we use the square by integrating it
        if (mOscillatorMode == OSCILLATOR_MODE_TRIANGLE)
        {
            // Leaky integrator: y[n] = A * x[n] + (1 - A) * y[n-1]
            value = mPhaseIncrement * value + (1 - mPhaseIncrement) * lastOutput;
            lastOutput = value;
        }
    }

    mPhase += mPhaseIncrement;
    while (mPhase >= twoPI)
    {
        mPhase -= twoPI;
    }
    return value;
}
