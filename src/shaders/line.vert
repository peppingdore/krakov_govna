#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable


#define LINE_SHADER
#include "uniform.glsl.h"

void main()
{
	vec3 pos = vec3(0, 0, 0); 

	float point_x = 0.0;
	float point_y = 0.0;


	switch (gl_VertexIndex)
	{
		case 0:
			point_x = float(u.line_start.x) / float(u.screen_size.x);
			point_y = float(u.line_start.y) / float(u.screen_size.y);
			break;

		case 1:
			point_x = float(u.line_end.x) / float(u.screen_size.x);
			point_y = float(u.line_end.y) / float(u.screen_size.y);
			break;

	}


	pos.x = (point_x - 0.5) * 2.0;
	pos.y = (point_y - 0.5) * 2.0;

	// pos.x += 1.0 / float(u.screen_size.x);
	// pos.y += 1.0 / float(u.screen_size.y);


	pos.y = -pos.y;

	gl_Position = vec4(pos, 1.0);
}