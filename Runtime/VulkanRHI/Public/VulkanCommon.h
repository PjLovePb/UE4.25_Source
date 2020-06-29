// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VulkanCommon.h: Common definitions used for both runtime and compiling shaders.
=============================================================================*/

#pragma once

#include "RHIDefinitions.h"

#ifndef VULKAN_SUPPORTS_GEOMETRY_SHADERS
	#define VULKAN_SUPPORTS_GEOMETRY_SHADERS					(!PLATFORM_ANDROID || PLATFORM_LUMIN) && PLATFORM_SUPPORTS_GEOMETRY_SHADERS
#endif

// This defines controls shader generation (so will cause a format rebuild)
// be careful wrt cooker/target platform not matching define-wise!!!
#define VULKAN_ENABLE_SHADER_DEBUG_NAMES		1

namespace ShaderStage
{
	enum EStage
	{
		// Adjusting these requires a full shader rebuild (ie modify the guid on VulkanCommon.usf)
		// Keep the values in sync with EShaderFrequency
		Vertex			= 0,
		Pixel			= 1,

#if VULKAN_SUPPORTS_GEOMETRY_SHADERS
		Geometry		= 2,
#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
		Hull			= 3,
		Domain			= 4,
#endif
		NumStages,

		MaxNumSets		= 8,
#else
		NumStages		= 2,

		MaxNumSets		= 4,
#endif

		// Compute is its own pipeline, so it can all live as set 0
		Compute			= 0,

		Invalid			= -1,
	};

	inline EStage GetStageForFrequency(EShaderFrequency Stage)
	{
		switch (Stage)
		{
		case SF_Vertex:		return Vertex;
#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
		case SF_Hull:		return Hull;
		case SF_Domain:		return Domain;
#endif
		case SF_Pixel:		return Pixel;
#if VULKAN_SUPPORTS_GEOMETRY_SHADERS
		case SF_Geometry:	return Geometry;
#endif
		case SF_Compute:	return Compute;
		default:
			checkf(0, TEXT("Invalid shader Stage %d"), (int32)Stage);
			break;
		}

		return Invalid;
	}

	inline EShaderFrequency GetFrequencyForGfxStage(EStage Stage)
	{
		switch (Stage)
		{
		case EStage::Vertex:	return SF_Vertex;
#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
		case EStage::Hull:		return SF_Hull;
		case EStage::Domain:	return SF_Domain;
#endif
		case EStage::Pixel:		return SF_Pixel;
#if VULKAN_SUPPORTS_GEOMETRY_SHADERS
		case EStage::Geometry:	return SF_Geometry;
#endif
		default:
			checkf(0, TEXT("Invalid shader Stage %d"), (int32)Stage);
			break;
		}

		return SF_NumFrequencies;
	}
};

namespace EVulkanBindingType
{
	enum EType : uint8
	{
		PackedUniformBuffer,	//VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
		UniformBuffer,			//VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER

		CombinedImageSampler,	//VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
		Sampler,				//VK_DESCRIPTOR_TYPE_SAMPLER
		Image,					//VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE

		UniformTexelBuffer,		//VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER	Buffer<>

		//A storage image (VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) is a descriptor type that is used for load, store, and atomic operations on image memory from within shaders bound to pipelines.
		StorageImage,			//VK_DESCRIPTOR_TYPE_STORAGE_IMAGE		RWTexture

		//RWBuffer/RWTexture?
		//A storage texel buffer (VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER) represents a tightly packed array of homogeneous formatted data that is stored in a buffer and is made accessible to shaders. Storage texel buffers differ from uniform texel buffers in that they support stores and atomic operations in shaders, may support a different maximum length, and may have different performance characteristics.
		StorageTexelBuffer,

		// UAV/RWBuffer
		//A storage buffer(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) is a region of structured storage that supports both read and write access for shaders.In addition to general read and write operations, some members of storage buffers can be used as the target of atomic operations.In general, atomic operations are only supported on members that have unsigned integer formats.
		StorageBuffer,

		InputAttachment,

		Count,
	};

	static inline char GetBindingTypeChar(EType Type)
	{
		// Make sure these do NOT alias EPackedTypeName*
		switch (Type)
		{
		case UniformBuffer:			return 'b';
		case CombinedImageSampler:	return 'c';
		case Sampler:				return 'p';
		case Image:					return 'w';
		case UniformTexelBuffer:	return 'x';
		case StorageImage:			return 'y';
		case StorageTexelBuffer:	return 'z';
		case StorageBuffer:			return 'v';
		case InputAttachment:		return 'a';
		default:
			check(0);
			break;
		}

		return 0;
	}
}

DECLARE_LOG_CATEGORY_EXTERN(LogVulkan, Display, All);

template< class T >
static FORCEINLINE void ZeroVulkanStruct(T& Struct, int32 VkStructureType)
{
	static_assert(!TIsPointer<T>::Value, "Don't use a pointer!");
	static_assert(STRUCT_OFFSET(T, sType) == 0, "Assumes sType is the first member in the Vulkan type!");
	static_assert(sizeof(T::sType) == sizeof(int32), "Assumed sType is compatible with int32!");
	// Horrible way to coerce the compiler to not have to know what T::sType is so we can have this header not have to include vulkan.h
	(int32&)Struct.sType = VkStructureType;
	FMemory::Memzero(((uint8*)&Struct) + sizeof(VkStructureType), sizeof(T) - sizeof(VkStructureType));
}
