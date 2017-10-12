#ifndef UTILITY_H
#define UTILITY_H

#include <cmath>
#include <math_neon.h>

inline int16_t floatToQ11( float value)
{
	return value * (0x01 << 11);
}

inline float Q11ToFloat( int16_t value)
{
	return ((float) value) / (0x01 << 11);
}


#endif
