#include "oscillator.h"

void Oscillator::setMode(OscillatorMode mode) {
    mOscillatorMode = mode;
}

void Oscillator::setFrequency(double frequency) {
    mFrequency = frequency;
    updateIncrement();
}

void Oscillator::setSampleRate(double sampleRate) {
    mSampleRate = sampleRate;
    updateIncrement();
}

void Oscillator::updateIncrement() {
    mPhaseIncrement = mFrequency * 2 * mPI / mSampleRate;
}

double Oscillator::polyBLEP(double t)
{
    double dt = mPhaseIncrement / twoPI;
    // 0 <= t < 1
    if (t < dt) {
        t /= dt;
        return t+t - t*t - 1.0;
    }
    // -1 < t < 0
    else if (t > 1.0 - dt) {
        t = (t - 1.0) / dt;
        return t*t + t+t + 1.0;
    }
    // 0 otherwise
    else return 0.0;
}

double Oscillator::naiveWaveform() {
    double value;
    switch (mOscillatorMode) {
        case OSCILLATOR_MODE_SINE:
            value = sin(mPhase);
            break;
        case OSCILLATOR_MODE_SAW:
            value = (2.0 * mPhase / (twoPI)) - 1.0;
            break;
        case OSCILLATOR_MODE_SQUARE:
            if (mPhase < mPI) {
                value = 1.0;
            } else {
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

double Oscillator::nextSample() {
    double t = mPhase / twoPI;
    double value = naiveWaveform();

    if (mOscillatorMode == OSCILLATOR_MODE_SAW) {
        value -= polyBLEP(t);
    }

    mPhase += mPhaseIncrement;
    while (mPhase >= twoPI) {
        mPhase -= twoPI;
    }
    return value;
}
