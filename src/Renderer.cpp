#include "Main.h"

#include "Renderer.h"

#include <xmmintrin.h>
#include <immintrin.h>

#include "Renderer_Vulkan.h"


#define STB_IMAGE_IMPLEMENTATION
#if OS_WINDOWS
#define STBI_WINDOWS_UTF8
#endif
#include "stb_image.h"



void Renderer::init(int initial_width, int initial_height)
{
	ZoneScoped;

	make_array(&mask_stack, 32, c_allocator);
	make_hash_map(&textures, 32, c_allocator);

	framebuffer_width  = initial_width;
	framebuffer_height = initial_height;

	vk.init();
}

bool Renderer::should_resize()
{
	return vk.swapchain_is_dead;
}


Texture* Renderer::find_texture(Unicode_String name)
{
	return textures.get(name);
}

Texture* Renderer::load_texture(Unicode_String path)
{
	int width;
	int height;
	int channels_count;
	
	u8 *data = stbi_load(path.to_utf8(frame_allocator), &width, &height, &channels_count, 0);
	
	if (!data)
		return NULL;

	defer { stbi_image_free(data); };

	if (channels_count != 4 &&
		channels_count != 3 && 
		channels_count != 1)
	{
		return NULL;
	}


	Texture texture;

	texture.width  = width;
	texture.height = height;

	texture.size = width * height * channels_count;

	texture.image_buffer = c_allocator.alloc(texture.size, code_location());
	memcpy(texture.image_buffer, data, texture.size);


	switch (channels_count)
	{
		case 4:
			texture.format = Texture_Format::RGBA;
			break;
		case 3:
			texture.format = Texture_Format::RGB;
			break;
		case 1:
			texture.format = Texture_Format::Monochrome;
			break;
	}


	texture.name = get_file_name_without_extension(path).copy_with(c_allocator);

	return textures.put(texture.name, texture);
}



void Renderer::frame_begin()
{
	vk.frame_begin();

	renderer.clear();

	renderer.mask_stack.count = 0;
	renderer.recalculate_mask_buffer();
}

void Renderer::frame_end()
{
	vk.frame_end();
}


void Renderer::clear()
{
	ZoneScoped;

	// @TODO: remove??
}


void Renderer::resize(int new_width, int new_height)
{
	ZoneScoped;


	if (new_width  == framebuffer_width &&
		new_height == framebuffer_height) return;


	framebuffer_width = new_width;
	framebuffer_height = new_height;

	vk.recreate_swapchain();
}




void Renderer::recalculate_mask_buffer()
{
	vk.recalculate_mask_buffer(mask_stack);
}


void Renderer::draw_rect_with_alpha_fade(Rect rect, rgba rgba, int alpha_left, int alpha_right)
{
	vk.draw_faded_rect(rect, rgba, alpha_left, alpha_right);
}

void Renderer::draw_line(int x_start, int y_start, int x_end, int y_end, rgba color)
{
	vk.draw_line(x_start, y_start, x_end, y_end, color);
}

void Renderer::draw_texture(Rect rect, Texture* texture)
{
	vk.draw_texture(rect, texture);
}


void Renderer::draw_rect(Rect rect, rgba rgba)
{
	vk.draw_rect(rect, rgba);
}


void Renderer::draw_glyph(Glyph* glyph, int x, int y, rgba color)
{
	vk.draw_glyph(glyph, x, y, color);
}

void Renderer::draw_text(Font::Face* face, Unicode_String str, int x, int y, rgba color)
{
	ZoneScoped;

	auto iter = string_by_glyphs(str, face);
	int iter_previous_x = iter.x;
	while (iter.next())
	{
		defer{ iter_previous_x = iter.x; };

		if (!iter.render_glyph) continue;

		// :GlyphLocalCoords:
		draw_glyph(&iter.current_glyph, x + iter_previous_x + iter.current_glyph.left_offset, y - (iter.current_glyph.height - iter.current_glyph.top_offset), color);
	}
}


void Renderer::draw_text_culled(Font::Face* face, Unicode_String str, int x, int y, Rect cull_rect, rgba color)
{
	ZoneScoped;

	auto iter = string_by_glyphs(str, face);
	int iter_previous_x = iter.x;
	while (iter.next())
	{
		defer{ iter_previous_x = iter.x; };

		if (!iter.render_glyph) continue;

		int local_x =  x + iter_previous_x + iter.current_glyph.left_offset;
		if (local_x > cull_rect.x_right) // Assuming left-to-right text.
		{
			break;
		}

		draw_glyph(&iter.current_glyph, local_x, y - (iter.current_glyph.height - iter.current_glyph.top_offset), color);

	}
}
