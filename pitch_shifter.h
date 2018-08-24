#ifndef PITCHSHIFTER_H
#define PITCHSHIFTER_H

class PitchShifter
{
public:
  PitchShifter(int sampleRate)
  {
  }

private:
  float *ringBuffer;
};

#endif

////////////TODO: Fix fade with amdfjump. I took it away from the sinc interpolation now since I want less spaghetticode
// with unexpected dependencies. A clean interpolation :-)
// Maybe create a special (overloaded?) version of the interpolation that take two indices and a fadevalue?
// The alternative is to run the interpolation twice for each pitcher (would rather avoid that!)
// Ooor. Can we mix all the fade samples together before interpolation? Into a new small array that spans the windowed sinc length.