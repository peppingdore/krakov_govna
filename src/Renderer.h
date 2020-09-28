#pragma once

#include "b_lib/Basic.h"
#include "b_lib/Font.h"
#include "b_lib/Math.h"
#include "b_lib/Color.h"

#if OS_WINDOWS
#define VK_USE_PLATFORM_WIN32_KHR
#elif OS_LINUX
#define VK_USE_PLATFORM_XLIB_KHR
#endif

#include "vulkan/vulkan.h"

#include "Video_Memory_Allocator.h"


constexpr int default_fade_width = 36;

constexpr u32 RECT_OUTLINE_LEFT_EDGE   = 1;
constexpr u32 RECT_OUTLINE_RIGHT_EDGE  = 1 << 1;
constexpr u32 RECT_OUTLINE_BOTTOM_EDGE = 1 << 2;
constexpr u32 RECT_OUTLINE_TOP_EDGE    = 1 << 3;
constexpr u32 RECT_ALL_OUTLINE_EDGES = RECT_OUTLINE_LEFT_EDGE | RECT_OUTLINE_RIGHT_EDGE | RECT_OUTLINE_TOP_EDGE | RECT_OUTLINE_BOTTOM_EDGE;



enum class Texture_Format
{
	Monochrome,
	RGB,
	RGBA,
};

struct Texture
{
	Unicode_String name;

	void* image_buffer;
	int width;
	int height;

	u64 size;

	Texture_Format format;

	bool is_on_gpu = false;

	VkImage      image;
	VkImageView  image_view;
	VkSampler    image_sampler;
	Vulkan_Memory_Allocation image_memory;
};



struct Renderer
{
	// For usage from outside, framebuffer size shrinked by margin.
	int height;
	int width;

	int framebuffer_width;
	int framebuffer_height;

	float scaling = 1.0;
	bool use_dpi_scaling = true;


	struct Mask
	{
		Rect rect;
		bool inversed;
	};
	Dynamic_Array<Mask> mask_stack;


	Hash_Map<Unicode_String, Texture> textures;

	Texture* find_texture(Unicode_String name);
	Texture* load_texture(Unicode_String path);

	void recalculate_mask_buffer();

	void push_mask(Mask mask)
	{
		mask_stack.add(mask);
		recalculate_mask_buffer();
	}
	void pop_mask()
	{
		assert(mask_stack.count > 0);
		mask_stack.count -= 1;
		recalculate_mask_buffer();
	}


	void draw_texture(Rect rect, Texture* texture);

	void draw_rect(Rect rect, rgba rgba);
	void draw_rect_with_alpha_fade(Rect rect, rgba rgba, int alpha_left, int alpha_right);

	void draw_line(int x_start, int y_start, int x_end, int y_end, rgba color);

	inline void draw_rect_outline(Rect rect, rgba color, u32 edges = RECT_ALL_OUTLINE_EDGES)
	{
		if (edges & RECT_OUTLINE_TOP_EDGE)
			draw_line(rect.x_left, rect.y_top,    rect.x_right, rect.y_top,    color);
		
		if (edges & RECT_OUTLINE_BOTTOM_EDGE)
			draw_line(rect.x_left, rect.y_bottom - 1, rect.x_right, rect.y_bottom - 1, color);


		if (edges & RECT_OUTLINE_LEFT_EDGE)
			draw_line(rect.x_left,  rect.y_bottom, rect.x_left,  rect.y_top, color);

		if (edges & RECT_OUTLINE_RIGHT_EDGE)
			draw_line(rect.x_right + 1, rect.y_bottom, rect.x_right + 1, rect.y_top, color);
	}



	void draw_glyph(Glyph* glyph, int x, int y, rgba color);

	void draw_text(Font::Face* face, Unicode_String str, int x, int y, rgba color = rgba(255, 255, 255, 255));
	void draw_text_culled(Font::Face* face, Unicode_String str, int x, int y, Rect cull_rect, rgba color = rgba(255, 255, 255, 255));


	void clear();
	void resize(int new_width, int new_height);

	// Do not call from outside
	void set_sizes_from_framebuffer_size(int new_framebuffer_width, int new_framebuffer_height);

	void init(int initial_width, int initial_height);

	bool should_resize();

	void frame_begin();
	void frame_end();


	template <typename T>
	inline T scaled(T number)
	{
		return scale<T>(number, scaling);		
	}
};
inline Renderer renderer;

struct Scoped_Mask
{
	Scoped_Mask(Renderer::Mask mask)
	{
		renderer.push_mask(mask);
	}

	~Scoped_Mask()
	{
		renderer.pop_mask();
	}
};

#define scoped_renderer_mask( __rect, __inversed ) Scoped_Mask CONCAT(__scoped, __LINE__) ( { .rect = __rect, .inversed = __inversed} );\


