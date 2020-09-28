#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable

#define TEXTURE_SHADER
#include "uniform.glsl.h"

layout(location = 0) out vec2 texture_coord;

void main()
{
	texture_coord = map_vertex_index_to_texture_coord(gl_VertexIndex);
	gl_Position   = map_vertex_index_to_vertex(gl_VertexIndex, u.screen_size, u.rect);
}