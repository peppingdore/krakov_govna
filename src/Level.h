#pragma once

#include "b_lib/Math.h"

#include "Entities_Info.h"

struct Entities_Storage
{
	Hash_Map<Entity_Id, Entity*> entities;

	Entity_Id next_entity_id = 0;

	inline static Entities_Storage make()
	{
		Entities_Storage storage = {};
		make_hash_map(&storage.entities, 512, c_allocator);
	
		return storage;
	}


	inline Entity* create_entity(Reflection::Struct_Type* entity_type)
	{
		defer { next_entity_id += 1; };
		Entity* e = (Entity*) c_allocator.alloc(entity_type->size, code_location());
		memset(e, 0, entity_type->size); // You should apply ZII principle in the code.
		
		entities.put(next_entity_id, e);

		return e;
	}

	template <typename T>
	inline T* create_entity()
	{
		auto entity_type = (Struct_Type*) Reflection::type_of<T>();
		
		assert(entity_type->base_type == Reflection::type_of<Entity>());
		
		return create_entity(entity_type);
	}

	// @TODO: implement destroy_entity
};


struct Level
{
	Entities_Storage entities_storage;


};

