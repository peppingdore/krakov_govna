#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable

layout(location = 0) in vec2 texture_coord;

layout(location = 0) out vec4 out_color;


layout(binding = 1) uniform sampler2D texture_sampler;


#define GENERAL_SHADER
#include "uniform.glsl.h"

layout(location = 2) in flat Batched_Draw_Command_Block u;


void main()
{
	vec4 color = u.color / 255;


	switch (u.draw_type)
	{
		case DRAW_TYPE_RECT:
		{
			out_color = color * color.a;
		}
		break;

		case DRAW_TYPE_FADED_RECT:
		{
			float coeff = mix(u.faded_rect_left_alpha, u.faded_rect_right_alpha, texture_coord.x);

			color.xyz = color.xyz * color.a;
			
			out_color = color * color.a * coeff;
		}
		break;

		case DRAW_TYPE_GLYPH:
		{
			float alpha = texture(texture_sampler, vec2(
				mix(u.atlas_x_left,   u.atlas_x_right, texture_coord.x),
				mix(u.atlas_y_bottom, u.atlas_y_top,   texture_coord.y)))[0];
			
			// WTF?? why call gamma_correct 2 times??
			alpha = gamma_correct(gamma_correct(alpha));

			out_color = color * alpha * color.a;
		}
		break;
	}
}