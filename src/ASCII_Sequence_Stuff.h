#pragma once




// This also will do 8 bit color
inline rgb decode_8_bit_color(u8 color)
{
	// 8 bit color table
	const rgb eight_bit_colors[] = {
		rgb(45,  45,  45),   // Black
		rgb(197, 15,  31),  // Red
		rgb(19,  161, 14),  // Green
		rgb(255, 199, 6),   // Yellow
		rgb(0,   55,  218), // Blue
		rgb(136, 23,  152), // Magenta
		rgb(44,  181, 233), // Cyan
		rgb(204, 204, 204), // White

		rgb(128, 128, 128), // Bright black
		rgb(231, 72,  86),  // Bright red
		rgb(0,   255, 0),   // Bright green
		rgb(234, 236, 35),  // Bright yellow
		rgb(59,  120, 255), // Bright blue
		rgb(255, 85,  255), // Bright magenta
		rgb(0,   255, 255), // Bright cyan
		rgb(255, 255, 255), // Bright white
	};

	if (color <= 15)
	{
		return eight_bit_colors[color];
	}

	if (color >= 232)
	{
		int whiteness = (color - 232) * (255 / (255 - 232));
		return rgb(whiteness, whiteness, whiteness);
	}

	// Calculation based on me looking at
	//   https://en.wikipedia.org/wiki/ANSI_escape_code#SGR

	int columm = (color - 16) % 36;
	int row    = (color - 16) / 36;

	int r = (255 / 6) * (row % 6);
	int g = (255 / 6) * (columm / 6);

	int b = (255 / 6) * (columm % 6);

	return rgb(r, g, b);
}

inline bool parse_color_from_escape_sequence(Dynamic_Array<String> arguments, rgb* value, int* arguments_advance_count)
{
	if (arguments.count == 0) return false;

	int color_format_number;
	if (parse_number(*arguments[0], &color_format_number))
	{
		if (color_format_number == 5)
		{
			if (arguments.count < 2) return false;

			*arguments_advance_count = 2;

			int color_256;
			if (parse_number(*arguments[1], &color_256))
			{
				// 256 color mode.
				rgb color = decode_8_bit_color(color_256);
				value->r = color.r;
				value->g = color.g;
				value->b = color.b;

				return true;
			}
		}
		else if (color_format_number == 2)
		{
			// 24 bit color mode.

			if (arguments.count < 4) return false;

			*arguments_advance_count = 4;

			int r = 0;
			int g = 0;
			int b = 0;

			if ((arguments[1]->length == 0 || parse_number(*arguments[1], &r)) && 
				(arguments[2]->length == 0 || parse_number(*arguments[2], &g)) &&
				(arguments[3]->length == 0 || parse_number(*arguments[3], &b)))

			{
				value->r = r;
				value->g = g;
				value->b = b;

				return true;
			}
		}
	}

	return false;
};