#pragma once

#include "b_lib/Math.h"


inline float ease(float x)
{
	return x < 0.5
	  ? (1 - sqrt(1 - pow(2 * x, 2))) / 2
	  : (sqrt(1 - pow(-2 * x + 2, 2)) + 1) / 2;
};
