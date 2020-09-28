#include "Tracy_Header.h"

#include "Video_Memory_Allocator.h"

#include "Renderer_Vulkan.h"
#include "b_lib/Log.h"

void Vulkan_Memory_Allocator::init()
{
	pools = make_array<Vulkan_Memory_Pool>(32, c_allocator);

	update_memory_usage_information();
}


void Vulkan_Memory_Allocator::update_memory_usage_information()
{
	device_memory_budget.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT;

	VkPhysicalDeviceMemoryProperties2 mem_properties = 
	{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2,
		.pNext = &device_memory_budget,
	};

#if OS_DARWIN
	vkGetPhysicalDeviceMemoryProperties2KHR(vk.physical_device, &mem_properties);
#else
	vkGetPhysicalDeviceMemoryProperties2(vk.physical_device, &mem_properties);
#endif

	device_memory_properties = mem_properties.memoryProperties;
}



s32 Vulkan_Memory_Allocator::find_memory_type(u32 supported_memory_types, Vulkan_Memory_Allocation_Flags allocation_flags)
{
	if constexpr (DEBUG)
	{
		if (allocation_flags & VULKAN_MEMORY_ONLY_DEVICE_MEMORY)
		{
			assert((allocation_flags & VULKAN_MEMORY_PREFER_HOST_MEMORY) == 0);
		}
	}


	s32 memory_type_index = -1;

	for (int i = 0; i < device_memory_properties.memoryTypeCount; i++)
	{
		if ((supported_memory_types & (1 << i)) == 0) continue; 

		VkMemoryType memory_type = device_memory_properties.memoryTypes[i];

		if (allocation_flags & VULKAN_MEMORY_SHOULD_BE_MAPPABLE)
		{
			if (!(memory_type.propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) ||
				!(memory_type.propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) continue;						
		}

		bool is_memory_on_gpu = (memory_type.propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) ? true : false; 

		if (allocation_flags & VULKAN_MEMORY_ONLY_DEVICE_MEMORY)
		{
			if (!is_memory_on_gpu) continue;
		}

		if (allocation_flags & VULKAN_MEMORY_PREFER_HOST_MEMORY)
		{
			if (is_memory_on_gpu) continue;
		}

		memory_type_index = i;
		break;
	}


	// If we intended to use host's RAM memory, but didn't found any, fallback to GPU memory. 
	if ((allocation_flags & VULKAN_MEMORY_PREFER_HOST_MEMORY) && memory_type_index == -1)
	{
		for (int i = 0; i < device_memory_properties.memoryTypeCount; i++)
		{
			VkMemoryType memory_type = device_memory_properties.memoryTypes[i];

			if (allocation_flags & VULKAN_MEMORY_SHOULD_BE_MAPPABLE)
			{
				if (!(memory_type.propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) ||
					!(memory_type.propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) continue;						
			}

			memory_type_index = i;
			break;
		}
	}

	return memory_type_index;
}




Vulkan_Memory_Allocation Vulkan_Memory_Allocator::allocate_and_bind(VkImage image, Vulkan_Memory_Allocation_Flags allocation_flags, Code_Location code_location)
{
	VkImageMemoryRequirementsInfo2 i = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,
		.pNext = NULL,
		.image = image,
	};

	VkMemoryDedicatedRequirementsKHR dedication_requirements = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS_KHR,
		.pNext = NULL,
	};

	VkMemoryRequirements2 mem_requirements_2 = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
		.pNext = &dedication_requirements,
	};

#if OS_DARWIN
	vkGetImageMemoryRequirements2KHR(vk.device, &i, &mem_requirements_2);
#else
	vkGetImageMemoryRequirements2(vk.device, &i, &mem_requirements_2);
#endif

	VkMemoryRequirements mem_requirements = mem_requirements_2.memoryRequirements;

	u32 memory_type_index = find_memory_type(mem_requirements.memoryTypeBits, allocation_flags);
	if (memory_type_index == -1)
		abort_the_mission(U"Failed to find memory type for image");


	if (dedication_requirements.prefersDedicatedAllocation)
	{
		Vulkan_Memory_Allocation allocation;
		
		if (allocate(memory_type_index, mem_requirements.size, mem_requirements.alignment, &allocation, image, VK_NULL_HANDLE, code_location))
		{
			vkBindImageMemory(vk.device, image, allocation.device_memory, allocation.offset);
			return allocation;
		}
	}

	if (dedication_requirements.requiresDedicatedAllocation)
		abort_the_mission(U"Required dedicated video memory allocation for VkImage has failed");


	Vulkan_Memory_Allocation allocation;

	if (allocate(memory_type_index, mem_requirements.size, mem_requirements.alignment, &allocation, VK_NULL_HANDLE, VK_NULL_HANDLE, code_location))
	{
		vkBindImageMemory(vk.device, image, allocation.device_memory, allocation.offset);
		return allocation;
	}

	abort_the_mission(U"Video memory allocation has failed");

	return allocation;
}

Vulkan_Memory_Allocation Vulkan_Memory_Allocator::allocate_and_bind(VkBuffer buffer, Vulkan_Memory_Allocation_Flags allocation_flags, Code_Location code_location)
{
	VkBufferMemoryRequirementsInfo2 i = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2,
		.pNext = NULL,
		.buffer = buffer,
	};

	VkMemoryDedicatedRequirementsKHR dedication_requirements = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS_KHR,
		.pNext = NULL,
	};

	VkMemoryRequirements2 mem_requirements_2 = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
		.pNext = &dedication_requirements,
	};

#if OS_DARWIN
	vkGetBufferMemoryRequirements2KHR(vk.device, &i, &mem_requirements_2);
#else
	vkGetBufferMemoryRequirements2(vk.device, &i, &mem_requirements_2);
#endif

	VkMemoryRequirements mem_requirements = mem_requirements_2.memoryRequirements;

	u32 memory_type_index = find_memory_type(mem_requirements.memoryTypeBits, allocation_flags);
	if (memory_type_index == -1)
		abort_the_mission(U"Failed to find memory type for image");


	if (dedication_requirements.prefersDedicatedAllocation)
	{
		Vulkan_Memory_Allocation allocation;
		
		if (allocate(memory_type_index, mem_requirements.size, mem_requirements.alignment, &allocation, VK_NULL_HANDLE, buffer, code_location))
		{
			vkBindBufferMemory(vk.device, buffer, allocation.device_memory, allocation.offset);
			return allocation;
		}
	}

	if (dedication_requirements.requiresDedicatedAllocation)
		abort_the_mission(U"Required dedicated video memory allocation for VkBuffer has failed");


	Vulkan_Memory_Allocation allocation;

	if (allocate(memory_type_index, mem_requirements.size, mem_requirements.alignment, &allocation, VK_NULL_HANDLE, VK_NULL_HANDLE, code_location))
	{
		vkBindBufferMemory(vk.device, buffer, allocation.device_memory, allocation.offset);
		return allocation;
	}

	abort_the_mission(U"Video memory allocation has failed");

	return allocation;
}




bool Vulkan_Memory_Allocator::allocate(u32 memory_type_index, u64 size, u64 alignment, Vulkan_Memory_Allocation* result, VkImage dedication_image, VkBuffer dedication_buffer, Code_Location code_location)
{

	// Log(U"Allocation of size: % at: %: %", size_to_string(size, c_allocator), String(code_location.file_name, strlen(code_location.file_name)), code_location.line);

	if (dedication_image)
	{
		assert(!dedication_buffer);
	}

	bool is_dedicated = dedication_image || dedication_buffer;

	if (!is_dedicated)
	{
		assert(size < default_pool_size);

		for (Vulkan_Memory_Pool& pool: pools)
		{
			if (pool.memory_type_index != memory_type_index) continue;

			u64 offset = 0;

			for (Vulkan_Pool_Region& region: pool.regions)
			{
				if (!region.allocated && region.size >= size)
				{
					u64 aligned_offset = align(offset, alignment);
					u64 offsets_offset = aligned_offset - offset;

					if (region.size >= size + offsets_offset)
					{
						u32 region_index = pool.regions.fast_pointer_index(&region);

						Vulkan_Memory_Allocation allocation = {
							.pool_index = pools.fast_pointer_index(&pool),
							.device_memory = pool.device_memory,
							.offset = aligned_offset,
							.size = size,
						};

						*result = allocation;

						s64 right_size = region.size - (size + offsets_offset);
						assert(right_size >= 0);


						region.size = size;
						region.allocated = true;


						if (offsets_offset)
						{
							assert(offsets_offset > 0);

							// We add small region at the left to compensate alignment requirement.
							//  This introduces a little waste of memory. 
							Vulkan_Pool_Region left_region = {
								.size = offsets_offset,
								.allocated = false,
							};	

							pool.regions.add_at_index(region_index, left_region);

							region_index += 1;
						}


						if (right_size)
						{
							assert(right_size > 0);
							// Have some memory on right side.

							Vulkan_Pool_Region right_region = {
								.size = (u64) right_size,
								.allocated = false,
							};

							pool.regions.add_at_index(region_index + 1, right_region);
						}

						return true;
					}
				}

				offset += region.size; 
			}
		}


		{

			u64 new_pool_size = default_pool_size;
			
			// Fight against fragmentation.
			if (size > megabytes(48))
				new_pool_size *= 2;



			VkMemoryAllocateInfo i = {
				.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
				.pNext = NULL,
				.allocationSize = new_pool_size,
				.memoryTypeIndex = memory_type_index,
			};

			VkDeviceMemory new_pool_device_memory;
			
			VkResult allocation_result = vkAllocateMemory(vk.device, &i, vk.host_allocator, &new_pool_device_memory);

			
			if (allocation_result != VK_SUCCESS) return false;

			Vulkan_Memory_Pool new_pool = {
				.device_memory = new_pool_device_memory,
				.memory_type_index = memory_type_index,

				.regions = make_array<Vulkan_Pool_Region>(64, c_allocator),
			};

			{
				// @Maybe: align the allocation size by some reasonable number.

				// :VulkanAlignment
				// As Vulkan specification states, any memory that is returned
				//  by vkAllocateMemory meets all the alignment requirements.
				//  And this is the first region of the allocated pool,
				//    so this rule applies for this region.
				Vulkan_Pool_Region allocated_region = {
					.size = size,
					.allocated = true,
				};

				Vulkan_Pool_Region remaining_region = {
					.size = new_pool_size - size,
					.allocated = false,
				};
				
				new_pool.regions.add(allocated_region);
				new_pool.regions.add(remaining_region);
			}


			pools.add(new_pool);

			Vulkan_Memory_Allocation allocation = {
				.pool_index = pools.count - 1,
				
				.device_memory = new_pool.device_memory,
				.offset = 0,
				.size = size,
			};

			*result = allocation;

			return true;
		}
	}

	assert(is_dedicated);

	{
		VkMemoryDedicatedAllocateInfoKHR dedicated_info = {
			.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO_KHR,
			.pNext = NULL,
			.image = dedication_image,
			.buffer = dedication_buffer,
		};

		// :VulkanAlignment
		VkMemoryAllocateInfo i = {
			.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
			.pNext = &dedicated_info,
			.allocationSize = size,
			.memoryTypeIndex = memory_type_index,
		};



		VkDeviceMemory device_memory;
		VkResult vk_result = vkAllocateMemory(vk.device, &i, vk.host_allocator, &device_memory);
		if (vk_result != VK_SUCCESS)
			return false;

		Vulkan_Memory_Allocation allocation = {
			.pool_index = -1,

			.device_memory = device_memory,
			.offset = 0,
			.size = size,
		};

		*result = allocation;

		return true;
	}
}


void Vulkan_Memory_Allocator::free(Vulkan_Memory_Allocation allocation)
{
	if (allocation.pool_index == -1)
	{
		vkFreeMemory(vk.device, allocation.device_memory, vk.host_allocator);
	}
	else
	{
		Vulkan_Memory_Pool* pool = pools[allocation.pool_index];

		u64 offset = 0;
		for (Vulkan_Pool_Region& region: pool->regions)
		{
			if (offset == allocation.offset)
			{
				assert(region.size == allocation.size && region.allocated);

				int region_index = pool->regions.fast_pointer_index(&region);
				

				bool join_with_previous = false;			
				bool join_with_next     = false;


				if (region_index > 0 &&
					!pool->regions[region_index - 1]->allocated)
				{
					join_with_previous = true;
				}

				if (region_index < pool->regions.count - 1 &&
					!pool->regions[region_index + 1]->allocated)
				{
					join_with_next = true;
				}



				if (join_with_previous && join_with_next)
				{
					Vulkan_Pool_Region new_region = {
						.size = 
							pool->regions[region_index - 1]->size + 
							region.size +
							pool->regions[region_index + 1]->size,

						.allocated = false,
					};


					*pool->regions[region_index - 1] = new_region;
					pool->regions.remove_range(region_index, 2);
				}
				else if (join_with_previous)
				{
					Vulkan_Pool_Region new_region = {
						.size = 
							pool->regions[region_index - 1]->size + 
							region.size,

						.allocated = false,
					};


					*pool->regions[region_index - 1] = new_region;
					pool->regions.remove_at_index(region_index);
				}
				else if (join_with_next)
				{
					Vulkan_Pool_Region new_region = {
						.size = 
							region.size + 
							pool->regions[region_index + 1]->size,

						.allocated = false,
					};


					region = new_region;
					pool->regions.remove_at_index(region_index + 1);
				}
				else
				{
					region.allocated = false;
				}

				return;
			}

			offset += region.size;
		}

		assert(false);
	}
}

void Vulkan_Memory_Allocator::dump_allocations()
{
	Log(U"Vulkan memory dump\n------\n");

	for (Vulkan_Memory_Pool& pool: vulkan_memory_allocator.pools)
	{
		size_t pool_size = 0;
		for (Vulkan_Pool_Region& region: pool.regions)
		{
			pool_size += region.size;
		}

		Log(U"\tPool %. size = %", vulkan_memory_allocator.pools.fast_pointer_index(&pool), size_to_string(pool_size, frame_allocator));



		for (Vulkan_Pool_Region& region: pool.regions)
		{
			Log(U"\t\tRegion %. size = %, allocated = %", pool.regions.fast_pointer_index(&region), size_to_string(region.size, frame_allocator), region.allocated);

		}

		Log(U"\n");
	}

	Log(U"------");
}