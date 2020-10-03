#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable


layout(location = 0) out vec2 texture_coord;

#define IMM_GLYPH_RECT_SHADER
#include "imm_uniform.glsl.h"

layout(location = 2) out Batched_Draw_Command_Block out_u;


void main()
{
	Batched_Draw_Command_Block u = uniform_buffer.commands[gl_InstanceIndex];

	out_u = u;

	texture_coord = map_vertex_index_to_texture_coord(gl_VertexIndex);
	gl_Position   = map_vertex_index_to_vertex(gl_VertexIndex, u.screen_size, u.rect);
}