

#ifdef IMM_LINE_SHADER

layout(push_constant) uniform Line_Uniform_Block {
	ivec2 screen_size;
	ivec2 line_start;
	ivec2 line_end;
	vec4 color;
} u;
#endif

#ifdef IMM_MASK_SHADER

layout(push_constant) uniform Mask_Uniform_Block
{
	ivec2 screen_size;
	ivec4 rect;
} u;
#endif

#ifdef IMM_TEXTURE_SHADER
layout(push_constant) uniform Texture_Uniform_Block
{
	ivec2 screen_size;
	ivec4 rect;
} u;
#endif



#ifdef IMM_GLYPH_RECT_SHADER

const int DRAW_TYPE_RECT       = 0;
const int DRAW_TYPE_GLYPH      = 1;
const int DRAW_TYPE_FADED_RECT = 2;

const int max_batched_commands = 512; // Keep in sync with C++



struct Batched_Draw_Command_Block
{
	ivec2 screen_size;
	ivec4 rect;
	float faded_rect_left_alpha;
	float faded_rect_right_alpha;
	int draw_type;
	float atlas_x_left;
	float atlas_x_right;
	float atlas_y_bottom;
	float atlas_y_top;
	vec4  color;
};


layout(std140, binding = 0) uniform General_Uniform_Block
{
	Batched_Draw_Command_Block commands[max_batched_commands];
} uniform_buffer;
#endif


vec4 map_vertex_index_to_vertex(int vertex_index, ivec2 screen_size, ivec4 rect)
{
	int rect_width  = rect[2] - rect[0];
	int rect_height = rect[3] - rect[1];


	vec4 pos = vec4(0, 0, 0, 1); 

	float point_x = 0.0;
	float point_y = 0.0;


	switch (vertex_index)
	{

		case 0:
			point_x = float(rect[0]) / float(screen_size.x);
			point_y = float(rect[1]) / float(screen_size.y);
			break;

		case 1:
		case 4:
			point_x = float(rect[0]) / float(screen_size.x);
			point_y = float(rect[3]) / float(screen_size.y);
			break;

		case 2:
		case 3:
			point_x = float(rect[2]) / float(screen_size.x);
			point_y = float(rect[1]) / float(screen_size.y);
			break;

		case 5:
			point_x = float(rect[2]) / float(screen_size.x);
			point_y = float(rect[3]) / float(screen_size.y);
			break;
	}


	pos.x = (point_x - 0.5) * 2.0;
	pos.y = (point_y - 0.5) * 2.0;

	pos.y = -pos.y;

	return pos;
}

vec2 map_vertex_index_to_texture_coord(int vertex_index)
{
	switch (vertex_index)
	{
		case 0:
			return vec2(0, 1);
		case 1:
		case 4:
			return vec2(0, 0);

		case 2:
		case 3:
			return vec2(1, 1);

		case 5:
			return vec2(1, 0);
	
		default:
			return vec2(0, 0);
	}
}



const float gamma = 2.2;

float gamma_uncorrect(float n)
{
    return pow(n, gamma);
}

float gamma_correct(float n)
{
	#if 1
    return pow(n, 1.0 / gamma);
	#else

	float x = n;

	float result = 1.138 * sqrt(x) - 0.138 * x;
	return result;

	#endif
}