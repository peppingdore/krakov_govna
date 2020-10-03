#version 450

#include "basic.glsl.h"


layout(std140, set = 1, binding = 0) uniform Material_Data {
	vec4 color;
} mat;


layout(location = 0) out vec4 out_color;


void main()
{
	out_color = mat.color;
}