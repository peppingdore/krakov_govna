#include "Asset_Storage.h"


#include "Renderer.h"

void Asset_Storage::init()
{
	make_hash_map(&textures, 128, c_allocator);
	make_hash_map(&meshes,   128, c_allocator);



	// Load textures
	{
		auto iter = iterate_files(U"assets/textures", frame_allocator);
		if (iter.succeeded_to_open())
		{
			while (iter.next().is_not_empty())
			{
				Texture* tex = load_texture(path_concat(frame_allocator, iter.directory, iter.current));
				
				if (tex)
					Log(U"Loaded texture: %", tex->name);
				else
					Log(U"Failed to load texture: %", iter.current);
			}
		}
	}

	// Load meshes
	{
		auto iter = iterate_files(U"assets/meshes", frame_allocator);
		if (iter.succeeded_to_open())
		{
			while (iter.next().is_not_empty())
			{
				if (iter.current.ends_with(U".obj"))
				{
					Mesh* mesh = load_obj_mesh(path_concat(frame_allocator, iter.directory, iter.current));
				
					if (mesh)
						Log(U"Loaded mesh: %", mesh->name);
					else
						Log(U"Failed to load mesh: %", iter.current);
				}
			}
		}
	}
}

Texture* Asset_Storage::load_texture(Unicode_String path)
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

Mesh* Asset_Storage::load_obj_mesh(Unicode_String path)
{
	Buffer buffer;

	if (!read_entire_file_to_buffer(c_allocator, path, &buffer))
	{
		return NULL;
	}

	defer{ buffer.free(); };


	String str = String((char*) buffer.data, buffer.occupied);

	auto positions = make_array<Vector3>(128, c_allocator);
	auto normals   = make_array<Vector3>(128, c_allocator);
	auto uvs       = make_array<Vector2>(128, c_allocator);
	defer { positions.free(); };
	defer { normals.free(); };
	defer { uvs.free(); };


	// @MemoryLeak: if we fail to parse, this arrays will not be freed.
	auto vertices = make_array<Vertex>(128, c_allocator);
	auto indices  = make_array<u32>   (128, c_allocator);



	int line = 0;


	auto take_until_whitespace_then_advance_and_skip_whitespace = [](String* str) -> String
	{
		auto str_copy = *str;

		auto result = take_until_whitespace_or_newline(str_copy);
		str_copy.advance(result.length);
		str_copy = skip_whitespace(str_copy);

		*str = str_copy;

		return result;
	};



	while (true)
	{
		if (str.length == 0) break;
		
		if (str[0] == '#')
		{
			str.advance(take_until_inclusive(str, '\n').length);
			continue;
		}

		auto line_start = take_until_whitespace_then_advance_and_skip_whitespace(&str);


		if (line_start == "v")
		{
			auto x_str = take_until_whitespace_then_advance_and_skip_whitespace(&str);
			auto y_str = take_until_whitespace_then_advance_and_skip_whitespace(&str);
			auto z_str = take_until_whitespace_then_advance_and_skip_whitespace(&str);

			Vector3 position;

			bool success = true;

			success &= parse_number(x_str, &position.x);
			success &= parse_number(y_str, &position.y);
			success &= parse_number(z_str, &position.z);

			if (!success)
			{
				Log(U"Failed to parse position at line: %", line);
				return false;
			}


			positions.add(position);
		}
		else if (line_start == "vn")
		{
			auto x_str = take_until_whitespace_then_advance_and_skip_whitespace(&str);
			auto y_str = take_until_whitespace_then_advance_and_skip_whitespace(&str);
			auto z_str = take_until_whitespace_then_advance_and_skip_whitespace(&str);

			Vector3 normal;

			bool success = true;

			success &= parse_number(x_str, &normal.x);
			success &= parse_number(y_str, &normal.y);
			success &= parse_number(z_str, &normal.z);

			if (!success)
			{
				Log(U"Failed to parse normal at line: %", line);
				return false;
			}


			normals.add(normal);
		}
		else if (line_start == "vt")
		{
			auto u_str = take_until_whitespace_then_advance_and_skip_whitespace(&str);
			auto v_str = take_until_whitespace_then_advance_and_skip_whitespace(&str);

			Vector2 uv;

			bool success = true;

			success &= parse_number(u_str, &uv.x);
			success &= parse_number(v_str, &uv.y);

			if (!success)
			{
				Log(U"Failed to parse normal at line: %", line);
				return false;
			}

			uvs.add(uv);
		}
		else if (line_start == "f")
		{
			auto take_until_whitespace_or_slash_and_advance = [](String* str, bool* out_met_slash) -> String
			{
				auto str_copy = *str;

				str_copy = skip_whitespace(str_copy);

				for (int i = 0; i < str_copy.length; i++)
				{
					char c = str_copy[i];

					if (c == ' ' || c == '/' || c == '\n')
					{
						*out_met_slash = c == '/';
						str->advance(i + 1);
						return str_copy.sliced(0, i);
					}
				}

				*out_met_slash = false;
				str->advance(str->length);
				return str_copy;
			};




			auto parse_vertex = [&](Vertex* out_vertex) -> bool
			{
				bool met_slash;

				Vertex vertex = {};

				auto position_index_str = take_until_whitespace_or_slash_and_advance(&str, &met_slash);
				int  position_index;
				if (!parse_number(position_index_str, &position_index))
				{
					Log(U"Failed to parse position index at line: %", line);
					return false;
				}

				vertex.position = *positions[position_index - 1];

				if (met_slash)
				{
					auto uv_index_str = take_until_whitespace_or_slash_and_advance(&str, &met_slash);

					if (uv_index_str.length) // UV may be skipped.
					{
						int  uv_index;
						if (!parse_number(uv_index_str, &uv_index))
						{
							Log(U"Failed to parse uv index at line: %", line);
							return false;
						}

						vertex.uv = *uvs[uv_index - 1];
					}

					if (met_slash)
					{
						auto normal_index_str = take_until_whitespace_or_slash_and_advance(&str, &met_slash);
						int  normal_index;
						if (!parse_number(normal_index_str, &normal_index))
						{
							Log(U"Failed to parse normal index at line: %", line);
							return false;
						}

						vertex.normal = *normals[normal_index - 1];
					}
				}

				*out_vertex = vertex;
				return true;
			};


			Vertex parsed_vertices[3];
			if (!parse_vertex(&parsed_vertices[0]))
			{
				Log(U"Failed to parse vertex[0] at line: %", line);
				return false;
			}

			if (!parse_vertex(&parsed_vertices[1]))
			{
				Log(U"Failed to parse vertex[1] at line: %", line);
				return false;
			}

			if (!parse_vertex(&parsed_vertices[2]))
			{
				Log(U"Failed to parse vertex[2] at line: %", line);
				return false;
			}


			vertices.add_range(parsed_vertices, 3);

			indices.add(vertices.count - 3);
			indices.add(vertices.count - 2);
			indices.add(vertices.count - 1);
		}

		if (str[0] == '\n')
			str.advance(1);

		line += 1;
	}


	Mesh mesh = {};

	mesh.vertices = vertices;
	mesh.indices  = indices;

	mesh.name = get_file_name_without_extension(path).copy_with(c_allocator);

	return meshes.put(mesh.name, mesh);
}
