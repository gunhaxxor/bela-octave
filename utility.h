#ifndef UTILITY_H
#define UTILITY_H

#include <cmath>
#include <math_neon.h>

// inline int16_t floatToQ11(float value)
// {
// 	return value * (0x01 << 11);
// }

// inline float Q11ToFloat(int16_t value)
// {
// 	return ((float)value) / (0x01 << 11);
// }

inline int wrapBufferSample(int n, int size)
{
	n += size;
	n %= size;
	return n;
}

inline float wrapBufferSample(float n, int size)
{
	return fmodf_neon(n + size, size);
}

inline double wrapBufferSample(double n, int size)
{
	return fmodf_neon(n + size, size);
}

inline float hannCrossFade(float t){
	float e_t = 0.5f;
	float o_t;
	if(fabs(t) < 1){
		o_t = 0.5f * sinf_neon(PI/2 * t);
	}else{
		//sign function
		o_t =	((t > 0.0f) - (t < 0.0f)) / 2.0f;
	}

	return e_t + o_t;
}

#endif
