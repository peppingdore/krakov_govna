#pragma once

#include "b_lib/Basic.h"
#include "b_lib/Dynamic_Array.h"

#include "vulkan/vulkan.h"

struct Vulkan_Pool_Region
{
	u64 size;
	bool allocated;
};

struct Vulkan_Memory_Pool
{
	VkDeviceMemory device_memory;
	u32 memory_type_index;

	Dynamic_Array<Vulkan_Pool_Region> regions;
};

struct Vulkan_Memory_Allocation
{
	s32 pool_index; // If this is -1, allocation is dedicated and is not in pools array.

	VkDeviceMemory device_memory;
	u64 offset;
	u64 size;
};


enum Vulkan_Memory_Allocation_Flags: u32
{
	VULKAN_MEMORY_ONLY_DEVICE_MEMORY = 1,
	VULKAN_MEMORY_PREFER_HOST_MEMORY = 1 << 1,
	VULKAN_MEMORY_SHOULD_BE_MAPPABLE = 1 << 3,
};


struct Vulkan_Memory_Allocator
{
	// Already allocated pools must not be released
	//  indices should stay the same
	Dynamic_Array<Vulkan_Memory_Pool> pools;

	const u64 default_pool_size = megabytes(2);


	VkPhysicalDeviceMemoryProperties          device_memory_properties;
	VkPhysicalDeviceMemoryBudgetPropertiesEXT device_memory_budget;


	
	// This functions will decide for you which type of allocation to do.
	// If allocation fails, application terminates.
	Vulkan_Memory_Allocation allocate_and_bind(VkImage image, Vulkan_Memory_Allocation_Flags allocation_flags, Code_Location code_location);
	Vulkan_Memory_Allocation allocate_and_bind(VkBuffer buffer, Vulkan_Memory_Allocation_Flags allocation_flags, Code_Location code_location);

	void free(Vulkan_Memory_Allocation allocation);



	u64 device_local_memory_size();
	u64 device_local_memory_used();
	u64 device_local_free_memory_size();




	// For allocator's internal usage.
	// Returns -1 in case of failure.
	s32 find_memory_type(u32 supported_memory_types, Vulkan_Memory_Allocation_Flags allocation_flags);

	// For allocator's internal usage.
	bool allocate(u32 memory_type_index, u64 size, u64 alignment, Vulkan_Memory_Allocation* result, VkImage 
		dedication_image = VK_NULL_HANDLE, VkBuffer dedication_buffer = VK_NULL_HANDLE, Code_Location code_location = code_location());



	void update_memory_usage_information();
	void init();

	void dump_allocations();
};

inline Vulkan_Memory_Allocator vulkan_memory_allocator;