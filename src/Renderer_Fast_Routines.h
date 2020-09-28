#pragma once

#include <xmmintrin.h>
#include <immintrin.h>

#include <string.h>


typedef char s8;
typedef unsigned char u8;

typedef short s16;
typedef unsigned short u16;

typedef int s32;
typedef unsigned int u32;

typedef long long s64;
typedef unsigned long long u64;

typedef float f32;
typedef double f64;

typedef u32 b32;


constexpr s32 fast_channels_count = 4;

force_inline float super_fast_sqrt(float x)
{
#if 1
	__m128 result = _mm_rsqrt_ps(_mm_set_ss(x));
	return 1.0 / _mm_cvtss_f32(result);
#else
	int i = *(int*)&x;
	i = 0x5f3759df - (i >> 1);
	float r = *(float*)&i;
	r = r * (1.5f - 0.5f * x * r * r);
	return r * x;
#endif
}

force_inline u32 gamma_correct(u32 pixel)
{
	float x = pixel / 255.0;

	float result = 1.138 * super_fast_sqrt(x) - 0.138 * x;
	return result * 255.0;
}


force_inline bool fast_mask_buffer_pixel(u8* mask_buffer, u32 mask_buffer_bytes_per_line, s32 x, s32 y)
{
	u64* addr = (u64*) (mask_buffer + (y * mask_buffer_bytes_per_line)) + (x / 64);
	u64 value = *addr;

	u64 check_mask = (1ull << 63) >> (x % 64);

	return (value & check_mask);
}

force_inline u32 fast_clamp(u32 n, u32 max)
{
	if (n > max)
	{
		return max;
	}
	return n;
}

template <bool use_gamma_correction>
void  __fastcall fast_draw_glyph(u8* framebuffer, u8* mask_buffer, u32 mask_buffer_bytes_per_line, u32 framebuffer_width, u32 framebuffer_height, u8* glyph_data, u32 glyph_width, u32 glyph_height,   s32 x, s32 y,   u8 r, u8 g, u8 b, u8 a)
{
	u8* line = (glyph_data) + (glyph_width * (glyph_height - 1));

#if 1
	s32 draw_height = glyph_height;

	s32 top_y = y + glyph_height;
	s32 top_appendix = top_y - framebuffer_height;

	if (top_appendix > 0)
	{
		draw_height -= top_appendix;
	}

	if (y < 0)
	{
		line -= glyph_width * (-y);
		draw_height -= (-y);
		y = 0;
	}




	s32 draw_width = glyph_width;
	s32 draw_x_start = 0;

	s32 right_x = x + glyph_width;
	s32 right_appendix = right_x - framebuffer_width;

	if (right_appendix > 0)
	{
		draw_width -= right_appendix;
	}

	if (x < 0)
	{
		draw_height -= (-x);
		x = 0;
		draw_x_start -= x;
	}
#else
	s32 draw_height = glyph_height;
#endif

	s32 global_y = y;



	for (int glyph_y = 0; glyph_y < draw_height; glyph_y++)
	{
		for (int glyph_x = draw_x_start; glyph_x < draw_width; glyph_x++)
		{
			if constexpr (use_gamma_correction)
			{
				// __asm volatile("# LLVM-MCA-BEGIN draw_glyph_pixel_gamma_corrected");
			}
			else
			{
				// __asm volatile("# LLVM-MCA-BEGIN draw_glyph_pixel");
			}


			u8 pixel = line[glyph_x];
			s32 global_x = x + glyph_x;

			if (pixel && fast_mask_buffer_pixel(mask_buffer, mask_buffer_bytes_per_line, global_x, global_y))
			{

				u32 alpha = pixel * u32(a) / 255;

				if constexpr (use_gamma_correction)
				{
					alpha = gamma_correct(alpha);
				}

				u32 inverse_alpha = 255 - alpha;


				// Set pixel blended checked and gamma-corrected
				{
					{
						u8* ptr = framebuffer + (global_y * framebuffer_width * fast_channels_count) + (global_x * fast_channels_count);

						u32 draw_r = ((u32(r) * alpha) + (inverse_alpha * ptr[0])) / 255;
						u32 draw_g = ((u32(g) * alpha) + (inverse_alpha * ptr[1])) / 255;
						u32 draw_b = ((u32(b) * alpha) + (inverse_alpha * ptr[2])) / 255;

						u8 channels[4];

						channels[0] = fast_clamp(draw_r, 255);
						channels[1] = fast_clamp(draw_g, 255);
						channels[2] = fast_clamp(draw_b, 255);
						channels[3] = fast_clamp(a + ptr[3], 255);

						memcpy(ptr, channels, sizeof(channels));
					}
				}
			}

			// __asm volatile("# LLVM-MCA-END");
		}

		line -= glyph_width;
		global_y += 1;
	}
}

template void fast_draw_glyph<true>(u8* framebuffer, u8* mask_buffer, u32 mask_buffer_bytes_per_line, u32 framebuffer_width, u32 framebuffer_height, u8* glyph_data, u32 glyph_width, u32 glyph_height,   s32 x, s32 y,   u8 r, u8 g, u8 b, u8 a);
template void fast_draw_glyph<false>(u8* framebuffer, u8* mask_buffer, u32 mask_buffer_bytes_per_line, u32 framebuffer_width, u32 framebuffer_height, u8* glyph_data, u32 glyph_width, u32 glyph_height,   s32 x, s32 y,   u8 r, u8 g, u8 b, u8 a);
