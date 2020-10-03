#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable

layout(location = 0) out vec4 out_color;

#define IMM_MASK_SHADER
#include "imm_uniform.glsl.h"

void main()
{
	out_color = vec4(1.0, 1.0, 1.0, 1.0);
}