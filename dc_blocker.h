class DcBlocker{
public:
  float filter(float sample){
    // Do DC-blocking on the sum
		float out = gB0 * sample + gB1 * gLastX[0] + gB2 * gLastX[1]
						- gA1 * gLastY[0] - gA2 * gLastY[1];

		gLastX[1] = gLastX[0];
		gLastX[0] = sample;
		gLastY[1] = gLastY[0];
		gLastY[0] = out;

    return out;
  }

private:
  // High-pass filter on the input
  float gLastX[2] = {0};
  float gLastY[2] = {0};

  // These coefficients make a high-pass filter at 5Hz for 44.1kHz sample rate
  const double gB0 = 0.99949640;
  const double gB1 = -1.99899280;
  const double gB2 = gB0;
  const double gA1 = -1.99899254;
  const double gA2 = 0.99899305;
};
