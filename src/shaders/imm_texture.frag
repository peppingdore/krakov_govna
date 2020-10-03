#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable

layout(location = 0) in vec2 texture_coord;

layout(location = 0) out vec4 out_color;

layout(binding = 0) uniform sampler2D texture_sampler;


#define IMM_TEXTURE_SHADER
#include "imm_uniform.glsl.h"


void main()
{
	vec4 color = texture(texture_sampler, texture_coord);

	out_color = color;
}