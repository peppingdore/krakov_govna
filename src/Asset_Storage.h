#pragma once

#include "b_lib/Hash_Map.h"

#include "Renderer.h"



struct Asset_Storage
{
	Hash_Map<Unicode_String, Texture> textures;
	Hash_Map<Unicode_String, Mesh>    meshes;


	void init();

	Texture* load_texture(Unicode_String path);
	Mesh*    load_obj_mesh(Unicode_String path);


	inline Texture* find_texture(Unicode_String name)
	{
		return textures.get(name); 
	}

	inline Mesh* find_mesh(Unicode_String name)
	{
		return meshes.get(name); 
	}
};

inline Asset_Storage asset_storage;