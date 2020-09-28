#include "Main.h"

#include "Renderer.h"

#include <xmmintrin.h>
#include <immintrin.h>

#include "Renderer_Fast_Routines.h"

#include "Renderer_Vulkan.h"


#define STB_IMAGE_IMPLEMENTATION
#if OS_WINDOWS
#define STBI_WINDOWS_UTF8
#endif
#include "stb_image.h"

static_assert(renderer.channels_count == fast_channels_count);




void Renderer::init(int initial_width, int initial_height)
{
	ZoneScoped;

	make_array(&mask_stack, 32, c_allocator);
	make_hash_map(&textures, 32, c_allocator);

	set_sizes_from_framebuffer_size(initial_width, initial_height);

	if (use_vulkan)
	{
		vk.init();
	}
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
	if (!redraw_current_frame) return;

	if (use_vulkan)
		vk.frame_begin();

	renderer.clear();

	renderer.mask_stack.count = 0;
	renderer.recalculate_mask_buffer();
}

void Renderer::frame_end()
{
	if (!redraw_current_frame) return;

	if (use_vulkan)
	{
		vk.frame_end();
	}
	else
	{
		if (!redraw_current_frame) return;


#if OS_WINDOWS
		TracyCZoneNC(framebuffer_copy_zone, "Copy renderer.framebuffer to windows_friendly_framebuffer", 0xff0000, true);

		u32 windows_framebuffer_width = align(width, 4);
		u64 windows_framebuffer_size = 3 * windows_framebuffer_width * height;

		TracyCZoneNC(allocate_windows_friendly_zone, "allocate windows friendly framebuffer", 0xff0000, true);
		u8* windows_friendly_framebuffer = allocate<u8>(windows_framebuffer_size, frame_allocator);
		TracyCZoneEnd(allocate_windows_friendly_zone);

		
		{
			for (int y = 0; y < height; y++)
			{
				//ZoneScopedN("copy line");

				int windows_framebuffer_y_offset  = y * windows_framebuffer_width * 3;
				int renderer_framebuffer_y_offset = y * width * channels_count;

				u8* dest = windows_friendly_framebuffer + windows_framebuffer_y_offset;
				u8* src  = framebuffer + renderer_framebuffer_y_offset;


				const int batch_size = 128 / 32;

				int times     = width / batch_size;
				int remainder = width % batch_size; // @TODO: maybe just align renderer's lines by 8, so we can get rid of remainder????


#if 1 || TYPER_SIMD
				for (int i = 0; i < times; i++)
				{

					__m128i source         = _mm_lddqu_si128((__m128i*) src);
					__m128i windows_pixels = _mm_shuffle_epi8(source, _mm_set_epi8(0,
						16, 17, 18,   12, 13, 14,   8, 9, 10,     4, 5, 6,      0, 1, 2)); // Skip alpha byte and shuffle rgb to bgr.

					_mm_storeu_si128((__m128i*) dest, windows_pixels);

					dest += 3 * batch_size;
					src  += channels_count * batch_size;
				}

				int remaining_start = times * batch_size;
				for (int x = remaining_start; x < (remaining_start + remainder); x++)
				{
					dest[0] = src[2];
					dest[1] = src[1];
					dest[2] = src[0];

					src  += channels_count;
					dest += 3;
				}
#else
				for (int x = 0; x < width; x++)
				{
					// Windows has b,g,r pixel color order.
					dest[0] = src[2];
					dest[1] = src[1];
					dest[2] = src[0];
					
					dest += 3;
					src += channels_count;
				}
#endif

			}
		}

		TracyCZoneEnd(framebuffer_copy_zone);

		// Draw on the dc
		{
    		ZoneScoped("SetDIBitsToDevice");

			BITMAPINFO bi = { };
			memset(&bi, 0, sizeof(bi));

			bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
			bi.bmiHeader.biWidth  = windows_framebuffer_width;
			bi.bmiHeader.biHeight = height;
			bi.bmiHeader.biPlanes = 1;
			bi.bmiHeader.biBitCount = 24;
			bi.bmiHeader.biCompression = BI_RGB;
			bi.bmiHeader.biSizeImage = 0;

			int result = SetDIBitsToDevice(windows.dc,
				0, 0, windows_framebuffer_width, height,
				0, 0, 0, height,
				(void*) windows_friendly_framebuffer, &bi, DIB_RGB_COLORS);

		}
#endif

	}
}


void Renderer::clear()
{
	if (use_vulkan) return;

	ZoneScoped;

	memset(framebuffer, 0, framebuffer_size);
}

void Renderer::set_sizes_from_framebuffer_size(int new_framebuffer_width, int new_framebuffer_height)
{
#if OS_WINDOWS
	if (IsMaximized(windows.hwnd))
	{
		framebuffer_margin = Rect::make(0, 0, 0, 0); // 4 is the size of maximized window horizontal appendix on Windows 10.
	}
	else
	{
		framebuffer_margin = Rect::make(0, 0, 0, 0); 
	}
#endif

	framebuffer_width  = new_framebuffer_width;
	framebuffer_height = new_framebuffer_height;

	width  = new_framebuffer_width  - (framebuffer_margin.x_left + framebuffer_margin.x_right);
	height = new_framebuffer_height - (framebuffer_margin.y_top  + framebuffer_margin.y_bottom);

}

void Renderer::resize(int new_width, int new_height)
{
	ZoneScoped;


	if (new_width  == framebuffer_width &&
		new_height == framebuffer_height) return;

	set_sizes_from_framebuffer_size(new_width, new_height);


	if (use_vulkan)
	{
		vk.recreate_swapchain();
	}
	else
	{

		framebuffer_size = width * height * channels_count * sizeof(u8);



		mask_buffer_bytes_per_line = align(width, 8); // @Crash: happens if i specify (width / 8) instead of just width.
		
		mask_buffer_size = mask_buffer_bytes_per_line * height * sizeof(u8);

		if (framebuffer)
		{
			c_allocator.free(framebuffer, code_location());
		}

		if (mask_buffer)
		{
			c_allocator.free(mask_buffer, code_location());
		}

		framebuffer = (u8*) c_allocator.alloc(framebuffer_size, code_location());
		assert(is_aligned((u64) framebuffer, 4));

		mask_buffer = (u8*) c_allocator.alloc(mask_buffer_size, code_location());
	}

	if (!use_vulkan)
	{
		recalculate_mask_buffer();
	}
}





void Renderer::set_pixel(int x, int y, rgba rgba)
{
	if (mask_buffer_pixel(x, y) == false) return;

	u8* ptr = framebuffer + (y * width * channels_count) + x * channels_count;

	memcpy(ptr, &rgba, sizeof(rgba.r) * 4);
}

void Renderer::set_pixel_checked(int x, int y, rgba rgba)
{
	if (x < 0 || x >= width || y < 0 || y >= height) return;

	if (mask_buffer_pixel(x, y) == false) return;
	
	u8* ptr = framebuffer + (y * width * channels_count) + x * channels_count;

	memcpy(ptr, &rgba, sizeof(rgba.r) * 4);
}

void Renderer::set_pixel_blended_and_checked(int x, int y, rgba rgba)
{
	if (x < 0 || x >= width || y < 0 || y >= height) return;

	if (mask_buffer_pixel(x, y) == false) return;
	
	u8* ptr = framebuffer + (y * width * channels_count) + x * channels_count;

	u32 inverse_alpha = 255 - rgba.a;



	// Is this particular case compiler will generate good assembly already if you enable optimization.
	//   The difference in speed if negligible
#if !TYPER_SIMD || 1
	ptr[0] = ((rgba.a * rgba.r) + (inverse_alpha * ptr[0])) / (255);
	ptr[1] = ((rgba.a * rgba.g) + (inverse_alpha * ptr[1])) / (255);
	ptr[2] = ((rgba.a * rgba.b) + (inverse_alpha * ptr[2])) / (255);
#else

	__m128i pixel_a = _mm_set_epi32(rgba.a, rgba.a, rgba.a, rgba.a);

	__m128i pixel_rgb = _mm_loadu_si32(&rgba);
	pixel_rgb = _mm_cvtepu8_epi32(pixel_rgb);

	__m128i pixel_alpha_multiplied = _mm_mullo_epi32(pixel_a, pixel_rgb);
		

	__m128i inverse_alpha_128 = _mm_set_epi32(inverse_alpha, inverse_alpha, inverse_alpha, inverse_alpha);

	__m128i dst_rgb = _mm_loadu_si32(ptr);
	dst_rgb = _mm_cvtepu8_epi32(dst_rgb);

	__m128i dst_inverse_alpha_multiplied = _mm_mullo_epi32(inverse_alpha_128, dst_rgb);

	__m128i rgb_added = _mm_add_epi32(pixel_alpha_multiplied, dst_inverse_alpha_multiplied);

	__m128i rgb_normalized = _mm_srli_epi32(rgb_added, 8); // divide by 256

	__m128i packed_result = _mm_packs_epi32(rgb_normalized, rgb_normalized);
	packed_result = _mm_packus_epi16(packed_result, packed_result);
	
	_mm_storeu_si32(ptr, packed_result);

#endif
	
	ptr[3] = clamp(0, 255, rgba.a + ptr[3]);
}

void Renderer::recalculate_mask_buffer()
{
	if (!redraw_current_frame) return;


	Dynamic_Array<Mask> margin_offsetted_mask_stack = make_array<Mask>(mask_stack.count, frame_allocator);

	for (Mask& mask: mask_stack)
	{
		Mask new_mask = mask;
		new_mask.rect.move(framebuffer_margin.x_left, framebuffer_margin.y_bottom);

		margin_offsetted_mask_stack.add(new_mask);
	}

	if (use_vulkan)
	{
		vk.recalculate_mask_buffer(margin_offsetted_mask_stack);
		return;
	}

	ZoneScopedC(0xff0000);



	{
		ZoneScopedNC("fill mask buffer with 0xffffffff", 0xff0000);
		memset(mask_buffer, 0xffffffff, mask_buffer_size);
	}


	auto draw_rect_on_mask_buffer = [&](Rect rect, bool value)
	{
		ZoneScopedNC("draw_rect_on_mask_buffer", 0xff0000);

#if TYPER_SIMD && 0
		__m128i fill_value = value ? _mm_set_epi64x(s64_max, s64_max) : _mm_set_epi64x(0, 0);
#else
		u64 fill_value = value ? u64_max : 0;
#endif

		int rect_left_clamped  = clamp(0, width, rect.x_left);
		int rect_right_clamped = clamp(0, width, rect.x_right);

		int width = rect_right_clamped - rect_left_clamped;

		for (int y = clamp(0, height, rect.y_bottom); y < clamp(0, height, rect.y_top); y++)
		{
			// The reason this doesn't uses SIMD is that there are no shift instruction for __mm128i taking non-constant variable.
#if TYPER_SIMD && 0
			u8* addr = mask_buffer + (mask_buffer_bytes_per_line) * y + (rect_left_clamped / (8 * 16));

			{
				__m128i addr_value = _mm_loadu_si128((__m128i*) addr);

				__m128i store_mask = _mm_srli_si128(_mm_set_epi64x(s64_max, s64_max), rect_left_clamped % 128);
				__m128i saved_bits = _mm_andnot_si128(store_mask, addr_value);

				__m128i value_to_store = _mm_and_si128(store_mask, fill_value);
			
				_mm_storeu_si128((__m128i*) addr, _mm_or_si128(_mm_and_si128(value_to_store, addr_value), saved_bits));

				addr += 128 / 8;
			}

			int x = rect_left_clamped + (128 - (rect_left_clamped % 128));
			assert(x % 128 == 0);
			while (true)
			{
				if (x + 128 >= rect_right_clamped)
				{
					int shift_amount = 128 - (rect_left_clamped - x);
					__m128i store_mask = _mm_slli_si128(_mm_srli_si128(_mm_set_epi64x(s64_max, s64_max), shift_amount), shift_amount);

					__m128i addr_value = _mm_loadu_si128(__m128i*) addr);
					__m128i saved_bits = _mm_andnot_si128(store_mask, addr_value);

					__m128i value_to_store = _mm_and_si128(store_mask, fill_value);

					_mm_storeu_si128((__m128i*) addr, _mm_or_si128(_mm_and_si128(value_to_store, addr_value), saved_bits));
				}
				else
				{
					_mm_storeu_si128((__m128i*) addr, _mm_and_si128(fill_value, _mm_loadu_si128((__m128i*) addr)));
				}

				addr += 16;
				x += 128;
			}

#else

			u64* addr = ((u64*) (mask_buffer + mask_buffer_bytes_per_line * y)) + (rect_left_clamped / 64);

			{
				u64 store_mask = (u64_max >> rect_left_clamped % 64);

				int devil_number = width - int(64 - (rect_left_clamped % 64));

				if (devil_number <= 0)
				{
					int shift_amount = -devil_number;
					store_mask = (store_mask >> shift_amount) << shift_amount;
				}

				u64 saved_bits = *addr & (~store_mask);

				u64 value_to_store = store_mask & fill_value;
				*addr = (value_to_store & *addr) | saved_bits;

				if (devil_number <= 0) continue;


				addr += 1;
			}

			int x = rect_left_clamped + (64 - (rect_left_clamped % 64));
			assert(x % 64 == 0);
			while (true)
			{
				if (x + 64 >= rect_right_clamped)
				{
					int shift_amount = (64 - (rect_right_clamped - x));
					u64 store_mask = u64_max << shift_amount;

					u64 value_to_store = store_mask & fill_value;

					u64 saved_bits = *addr & (~store_mask);
					*addr = (value_to_store & *addr) | saved_bits;
					break;
				}
				else
				{
					*addr = fill_value & *addr;
				}

				addr += 1;
				x += 64;
			}
#endif
		}
	};

	for (Mask mask: margin_offsetted_mask_stack)
	{
		// :MaskStackOuterRects
		// This rects will cover whole screen
		//  Values drawn by this rects are depend on whether mask is reversed.

		// This rects can be fucked up, like right_x less that left_x, but draw_rect_on_mask_buffer
		//  will be fine.
		Rect left_rect = Rect::make(
			0,
			0,
			mask.rect.x_left,
			renderer.height
		);

		Rect bottom_rect = Rect::make(
			mask.rect.x_left,
			0,
			mask.rect.x_right,
			mask.rect.y_bottom
		);
		
		Rect right_rect = Rect::make(
			mask.rect.x_right,
			0,
			renderer.width,
			renderer.height
		);

		Rect top_rect = Rect::make(
			mask.rect.x_left,
			mask.rect.y_top,
			mask.rect.x_right,
			renderer.height
		);



		bool outside_value;
		bool inside_value;

		if (mask.inversed)
		{
			outside_value = true;
			inside_value = false;
		}
		else
		{
			outside_value = false;
			inside_value  = true;
		}

		draw_rect_on_mask_buffer(left_rect,   outside_value);
		draw_rect_on_mask_buffer(bottom_rect, outside_value);
		draw_rect_on_mask_buffer(right_rect,  outside_value);
		draw_rect_on_mask_buffer(top_rect,    outside_value);

		draw_rect_on_mask_buffer(mask.rect, inside_value);
	}
}

bool Renderer::mask_buffer_pixel(int x, int y)
{
	//assert(x >= 0 && x < width && y >= 0 && y < height);

	// Keep size of read here equal to writes in recalculate_mask_buffer to not break endianness.
	//  ???????????

	u64* addr = (u64*) (mask_buffer + (y * mask_buffer_bytes_per_line)) + (x / 64);
	u64 value = *addr;

	u64 check_mask = (1ull << 63) >> (x % 64);

	return (value & check_mask);
}



void Renderer::draw_rect_software(int left_x, int bottom_y, int right_x, int top_y, rgba rgba, bool blended)
{
	ZoneScoped;

	if (!redraw_current_frame) return;

	assert(!use_vulkan);

	left_x   = clamp(0, this->width,  left_x);
	right_x  = clamp(0, this->width,  right_x);
	bottom_y = clamp(0, this->height, bottom_y);
	top_y    = clamp(0, this->height, top_y);

	int rect_width = right_x - left_x;
	int rect_height = top_y - bottom_y;

	if (!blended)
	{
		for (int y = bottom_y; y < top_y; y++)
		{
			// @TODO: make this not use AVX2 instructions
			#if 0 && TYPER_SIMD
			// Not sure if this giving any performance
			const int batch_size = 256 / 32;

			int times     = rect_width / batch_size;
			int remainder = rect_width % batch_size;

			u8* ptr = framebuffer + (y * channels_count * this->width) + (left_x * channels_count);
			u8* mask_buffer_line_start = mask_buffer + (y * this->width) + left_x;

			__m256i color4 = _mm256_set_epi8(
				rgba.a, rgba.b, rgba.g, rgba.r,
				rgba.a, rgba.b, rgba.g, rgba.r,
				rgba.a, rgba.b, rgba.g, rgba.r,
				rgba.a, rgba.b, rgba.g, rgba.r,
				rgba.a, rgba.b, rgba.g, rgba.r,
				rgba.a, rgba.b, rgba.g, rgba.r,
				rgba.a, rgba.b, rgba.g, rgba.r,
				rgba.a, rgba.b, rgba.g, rgba.r);

			u8* mask_ptr = mask_buffer_line_start;
			for (int i = 0; i < times; i++)
			{
				u64 mask_value = *((u64*) mask_ptr);
				__m256i mask = _mm256_set1_epi64x(mask_value);
				mask = _mm256_shuffle_epi8(mask, _mm256_set_epi8(
					39, 39, 39, 39,
					38, 38, 38, 38,
					37, 37, 37, 37,
					36, 36, 36, 36,

					3,3,3,3, 2,2,2,2, 1,1,1,1, 0,0,0,0));

				_mm256_maskstore_epi32((int*) ptr, mask, color4);

				ptr += 4 * batch_size;
				mask_ptr += batch_size;
			}

			int remaining_start = times * batch_size;
			for (int i = remaining_start; i < (remaining_start + remainder); i++)
			{
				if (mask_buffer_pixel(i + left_x, y))
				{
					memcpy(ptr, &rgba, sizeof(rgba));
				}
				ptr += 4;
			}

			#else
			for (int x = left_x; x < right_x; x++)
			{
				set_pixel(x, y, rgba);
			}
			#endif
		}
	}
	else
	{
		for (int y = bottom_y; y < top_y; y++)
		{
			for (int x = left_x; x < right_x; x++)
			{
				set_pixel_blended_and_checked(x, y, rgba);
			}
		}
	}
}

void Renderer::draw_rect_with_alpha_fade(Rect rect, rgba rgba, int alpha_left, int alpha_right)
{
	if (!redraw_current_frame) return;

	rect.move(framebuffer_margin.x_left, framebuffer_margin.y_bottom);

	if (use_vulkan)
	{
		vk.draw_faded_rect(rect, rgba, alpha_left, alpha_right);
		return;
	}

	int alpha_step = (alpha_right - alpha_left) / rect.width();

	for (int y = rect.y_bottom; y < rect.y_top; y++)
	{
		for (int x = rect.x_left; x < rect.x_right; x++)
		{
			u8 new_alpha = alpha_step * (x - rect.x_left);
			set_pixel_blended_and_checked(x, y, ::rgba(rgba.rgb_value, new_alpha * rgba.a / 255));
		}
	}
}

void Renderer::draw_line(int x_start, int y_start, int x_end, int y_end, rgba color)
{
	if (!redraw_current_frame) return;

	x_start += framebuffer_margin.x_left;
	x_end   += framebuffer_margin.x_left;
	
	y_start += framebuffer_margin.y_bottom;
	y_end   += framebuffer_margin.y_bottom;


	if (use_vulkan)
	{
		vk.draw_line(x_start, y_start, x_end, y_end, color);
		return;
	}

	if (x_end < x_start)
	{
		// swap(&x_end, &x_start);
	}

#if 0
	// Stolen from: https://en.wikipedia.org/wiki/Bresenham%27s_line_algorithm
	// The last piece of code in that page.

	int dx = abs(x_end - x_start);
	int sx = x_start < x_end ? 1 : -1;
	int dy = -abs(y_end - y_start);
	int sy = y_start < y_end ? 1 : -1;
	float err = dx + dy;  /* error value e_xy */
	while (true)
	{
		set_pixel_checked(x_start, y_start, color);
		if (x_start == x_end && y_start == y_end) break;

		float e2 = 2 * err;
		
		if (e2 >= dy)
		{
			err += dy; /* e_xy+e_x > 0 */
			x_start += sx;
		}
		if (e2 <= dx) /* e_xy+e_y < 0 */
		{
			err += dx;
			y_start += sy;
		}
	}
#else

	auto plot = [&](int x, int y, float alpha)
	{
		rgba new_color = color;
		//new_color.r = color.a * alpha / 255;
		//new_color.g = color.g * alpha / 255;
		//new_color.b = color.b * alpha / 255;
		new_color.a = gamma_correct(scale(color.a, alpha));

		set_pixel_blended_and_checked(x, y, new_color);
	};

	auto ipart = [](float x) -> float
	{
		return floor(x);
	};

	auto fpart = [](float x) -> float
	{
		return x - floor(x);
	};

	auto rfpart = [&](float x) -> float
	{
		return 1 - fpart(x);
	};


	bool steep = abs(y_end - y_start) > abs(x_end - x_start);
    
    if (steep)
    {
        swap(&x_start, &y_start);
        swap(&x_end, &y_end);
    }

    if (x_start > x_end)
    {
        swap(&x_start, &x_end);
        swap(&y_start, &y_end);
    }
    float dx = x_end - x_start;
    float dy = y_end - y_start;
    float gradient = dy / dx;
    if (dx == 0.0)
    {
        gradient = 1.0;
    }

    // handle first endpoint
    float xend = round(x_start);
    float yend = y_start + gradient * (xend - x_start);
    float xgap = rfpart(x_start + 0.5);
    float xpxl1 = xend; // this will be used in the main loop
    float ypxl1 = ipart(yend);
    if (steep)
    {
        plot(ypxl1,   xpxl1, rfpart(yend) * xgap);
        plot(ypxl1+1, xpxl1,  fpart(yend) * xgap);
    }
    else
    {
        plot(xpxl1, ypxl1  , rfpart(yend) * xgap);
        plot(xpxl1, ypxl1+1,  fpart(yend) * xgap);
    }
    float intery = yend + gradient; // first y-intersection for the main loop
    
    // handle second endpoint
    xend = round(x_end);
    yend = y_end + gradient * (xend - x_end);
    xgap = fpart(x_end + 0.5);
    float xpxl2 = xend; //this will be used in the main loop
    float ypxl2 = ipart(yend);
    if (steep)
    {
        plot(ypxl2  , xpxl2, rfpart(yend) * xgap);
        plot(ypxl2+1, xpxl2,  fpart(yend) * xgap);
    }
    else
    {
        plot(xpxl2, ypxl2,  rfpart(yend) * xgap);
        plot(xpxl2, ypxl2+1, fpart(yend) * xgap);
    }

    // main loop
    if (steep)
    {
    	int increment = xpxl1 + 1 <= xpxl2 - 1 ? 1 : -1;
        for (int x = xpxl1 + 1; x <= xpxl2 - 1; x += increment)
        {
            plot(ipart(intery)  , x, rfpart(intery));
            plot(ipart(intery)+1, x,  fpart(intery));
            intery = intery + gradient;
       }
    }
    else
    {
    	int increment = xpxl1 + 1 <= xpxl2 - 1 ? 1 : -1;

       	for (int x = xpxl1 + 1; x <= xpxl2 - 1; x += increment)
        {
            plot(x, ipart(intery),  rfpart(intery));
            plot(x, ipart(intery)+1, fpart(intery));
            intery = intery + gradient;
        }
    }
#endif
}

void Renderer::draw_texture(Rect rect, Texture* texture)
{
	if (!redraw_current_frame) return;

	rect.move(framebuffer_margin.x_left, framebuffer_margin.y_bottom);

	if (use_vulkan)
	{
		return vk.draw_texture(rect, texture);
	}
	else
	{
		// @TODO: implement software texture renderer.
	}
}


void Renderer::draw_rect(Rect rect, rgba rgba)
{
	// ZoneScoped;

	if (!redraw_current_frame) return;

	rect.move(framebuffer_margin.x_left, framebuffer_margin.y_bottom);

	if (use_vulkan)
	{
		vk.draw_rect(rect, rgba);
	}
	else
	{
		bool blended = rgba.a != 255;
		draw_rect_software(rect.x_left, rect.y_bottom, rect.x_right, rect.y_top, rgba, blended);
	}
}


void Renderer::draw_glyph(Glyph* glyph, int x, int y, rgba color, bool gamma_correct)
{
	// ZoneScoped;

	if (!redraw_current_frame) return;

	x += framebuffer_margin.x_left;
	y += framebuffer_margin.y_bottom;

	if (use_vulkan)
	{
		vk.draw_glyph(glyph, x, y, color);
	}
	else
	{
		if (gamma_correct || true)
		{
			fast_draw_glyph<true>(framebuffer, mask_buffer, mask_buffer_bytes_per_line, width, height, glyph->image_buffer, glyph->width, glyph->height, x, y, color.r, color.g, color.b, color.a);
		}
		else
		{
			fast_draw_glyph<false>(framebuffer, mask_buffer, mask_buffer_bytes_per_line, width, height, glyph->image_buffer, glyph->width, glyph->height, x, y, color.r, color.g, color.b, color.a);
		}
	}
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
		draw_glyph(&iter.current_glyph, x + iter_previous_x + iter.current_glyph.left_offset, y - (iter.current_glyph.height - iter.current_glyph.top_offset), color, do_need_to_gamma_correct(face));

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

		draw_glyph(&iter.current_glyph, local_x, y - (iter.current_glyph.height - iter.current_glyph.top_offset), color, do_need_to_gamma_correct(face));

	}
}
