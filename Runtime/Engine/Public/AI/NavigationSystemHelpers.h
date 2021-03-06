// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EngineDefines.h"
#include "AI/Navigation/NavigationTypes.h"
#include "Chaos/Core.h"

class AActor;
class UBodySetup;
struct FCompositeNavModifier;
struct FKAggregateGeom;
struct FKConvexElem;
struct FNavHeightfieldSamples;

template<typename InElementType> class TNavStatArray;

#if WITH_PHYSX
namespace physx
{
	class PxTriangleMesh;
	class PxConvexMesh;
	class PxHeightField;
}
#endif // WITH_PHYSX

#if WITH_CHAOS
namespace Chaos
{
	template<typename T>
	class THeightField;

	class FTriangleMeshImplicitObject;
}
#endif

struct FNavigableGeometryExport
{
	virtual ~FNavigableGeometryExport() {}
#if WITH_PHYSX
	virtual void ExportPxTriMesh16Bit(physx::PxTriangleMesh const * const TriMesh, const FTransform& LocalToWorld) = 0;
	virtual void ExportPxTriMesh32Bit(physx::PxTriangleMesh const * const TriMesh, const FTransform& LocalToWorld) = 0;
	virtual void ExportPxConvexMesh(physx::PxConvexMesh const * const ConvexMesh, const FTransform& LocalToWorld) = 0;
	virtual void ExportPxHeightField(physx::PxHeightField const * const HeightField, const FTransform& LocalToWorld) = 0;
#endif // WITH_PHYSX
#if WITH_CHAOS
	virtual void ExportChaosTriMesh(const Chaos::FTriangleMeshImplicitObject* const TriMesh, const FTransform& LocalToWorld) = 0;
	virtual void ExportChaosConvexMesh(const FKConvexElem* const Convex, const FTransform& LocalToWorld) = 0;
	virtual void ExportChaosHeightField(const Chaos::THeightField<float>* const Heightfield, const FTransform& LocalToWorld) = 0;
#endif
	virtual void ExportHeightFieldSlice(const FNavHeightfieldSamples& PrefetchedHeightfieldSamples, const int32 NumRows, const int32 NumCols, const FTransform& LocalToWorld, const FBox& SliceBox) = 0;
	virtual void ExportRigidBodySetup(UBodySetup& BodySetup, const FTransform& LocalToWorld) = 0;
	virtual void ExportCustomMesh(const FVector* VertexBuffer, int32 NumVerts, const int32* IndexBuffer, int32 NumIndices, const FTransform& LocalToWorld) = 0;
	
	virtual void AddNavModifiers(const FCompositeNavModifier& Modifiers) = 0;

	// Optional delegate for geometry per instance transforms
	virtual void SetNavDataPerInstanceTransformDelegate(const FNavDataPerInstanceTransformDelegate& InDelegate) = 0;
};
