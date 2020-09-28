#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable

#define MASK_SHADER
#include "uniform.glsl.h"

void main()
{
	gl_Position = map_vertex_index_to_vertex(gl_VertexIndex, u.screen_size, u.rect);
}