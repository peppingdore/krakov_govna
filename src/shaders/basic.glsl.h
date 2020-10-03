#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable

layout(std140, set = 0, binding = 0) uniform Global_Data {
	ivec2 screen_size;

};


struct Standard_Vertex_Output
{
	vec3 position;
	vec2 uv;
};