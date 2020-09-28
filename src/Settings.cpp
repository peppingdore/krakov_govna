#include "Settings.h"

#include "b_lib/Reflection.h"
#include "b_lib/File.h"
#include "b_lib/Tokenizer.h"
#include "b_lib/Collection.h"

#include "Main.h"
#include "UI.h"
#include "Input.h"
#include "Ease_Functions.h"


bool save_settings()
{
	ZoneScoped;

	assert(threading.is_main_thread());

	Unicode_String path = path_concat(frame_allocator, executable_directory, Unicode_String(U"settings.txt"));
	File file = open_file(frame_allocator, path, FILE_WRITE | FILE_CREATE_NEW);

	if (!file.succeeded_to_open())
	{
		log(ctx.logger, U"Failed to open settings file to save settings");
		return false;
	}

	defer { file.close(); };

	using namespace Reflection;

	{
		Struct_Type* settings_type_info = (Struct_Type*) Reflection::type_of<Settings>();
		for (auto iter = settings_type_info->iterate_members(frame_allocator); Struct_Member* member = iter.next();)
		{
			file.write(":");
			file.write(member->name);
			file.write(" ");
			
			file.write(write_thing(member->type, add_bytes_to_pointer(&settings, member->offset), frame_allocator));
		
			file.write("\n");
		}
	}

	// log(ctx.logger, U"Successfully saved settings");
	return true;
}

bool load_settings()
{
	ZoneScoped;

	assert(threading.is_main_thread());


	Unicode_String path = path_concat(frame_allocator, executable_directory, Unicode_String(U"settings.txt"));
	File file = open_file(frame_allocator, path, FILE_READ);

	if (!file.succeeded_to_open())
	{
		return false;
	}

	defer{
		file.close();
		save_settings();
	};

	using namespace Reflection;

	{
		Struct_Type* settings_type_info = (Struct_Type*) Reflection::type_of<Settings>();

		// @MemoryLeak: nothing is properly freed here.
		Tokenizer tokenizer = tokenize(&file, frame_allocator);
		tokenizer.key_characters.add(':');


		String token;
		while (true)
		{
			if (tokenizer.peek_token().is_empty()) break;

			if (!tokenizer.expect_token(":"))
			{
				log(ctx.logger, U"Expected ':' at line: %", tokenizer.line);
				return false;
			}

			token = tokenizer.peek_token();

			Struct_Member* found_member = NULL;
			for (auto iter = settings_type_info->iterate_members(frame_allocator); Struct_Member* member = iter.next();)
			{
				if (member->name == token)
				{
					found_member = member;
					break;
				}
			}

			if (!found_member)
			{
				log(ctx.logger, U"Field '%' wasn't found", token);
				return false;
			}

			if (!read_thing(&tokenizer, found_member->type, add_bytes_to_pointer(&settings, found_member->offset), frame_allocator, c_allocator))
			{
				log(ctx.logger, U"Failed to parse field '%'", found_member->name);
				return false;
			}
		}
	}

	// Validating loaded settings.
	{
		// @TODO
	}

	return true;
}

