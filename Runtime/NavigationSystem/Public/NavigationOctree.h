// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "AI/Navigation/NavigationTypes.h"
#include "Math/GenericOctreePublic.h"
#include "NavigationSystemTypes.h"
#include "EngineStats.h"
#include "AI/NavigationModifier.h"
#include "AI/Navigation/NavRelevantInterface.h"
#include "Math/GenericOctree.h"

class INavRelevantInterface;
class FNavigationOctree;
typedef FNavigationRelevantDataFilter FNavigationOctreeFilter;

struct NAVIGATIONSYSTEM_API FNavigationOctreeElement
{
	FBoxSphereBounds Bounds;
	TSharedRef<FNavigationRelevantData, ESPMode::ThreadSafe> Data;
	uint32 OwnerUniqueId = INDEX_NONE;	

public:
	explicit FNavigationOctreeElement(UObject& SourceObject)
		: Data(new FNavigationRelevantData(SourceObject))
		, OwnerUniqueId(SourceObject.GetUniqueID())
	{}

	FNavigationOctreeElement(const FNavigationOctreeElement& Other)
		: Bounds(Other.Bounds)
		, Data(Other.Data)
		, OwnerUniqueId(Other.OwnerUniqueId)
	{}

	FNavigationOctreeElement& operator=(const FNavigationOctreeElement& Other)
	{
		new(this) FNavigationOctreeElement(Other);
		return *this;
	}

	FORCEINLINE bool IsEmpty() const
	{
		const FBox BBox = Bounds.GetBox();
		return Data->IsEmpty() && (BBox.IsValid == 0 || BBox.GetSize().IsNearlyZero());
	}

	FORCEINLINE bool IsMatchingFilter(const FNavigationOctreeFilter& Filter) const
	{
		return Data->IsMatchingFilter(Filter);
	}

	/** 
	 *	retrieves Modifier, if it doesn't contain any "Meta Navigation Areas". 
	 *	If it does then retrieves a copy with meta areas substituted with
	 *	appropriate non-meta areas, depending on NavAgent
	 */
	FORCEINLINE FCompositeNavModifier GetModifierForAgent(const struct FNavAgentProperties* NavAgent = NULL) const 
	{ 
		return Data->Modifiers.HasMetaAreas() ? Data->Modifiers.GetInstantiatedMetaModifier(NavAgent, Data->SourceObject) : Data->Modifiers;
	}

	FORCEINLINE bool ShouldUseGeometry(const FNavDataConfig& NavConfig) const
	{ 
		return !Data->ShouldUseGeometryDelegate.IsBound() || Data->ShouldUseGeometryDelegate.Execute(&NavConfig);
	}

	FORCEINLINE int32 GetAllocatedSize() const
	{
		return Data->GetAllocatedSize();
	}

	FORCEINLINE void Shrink()
	{
		Data->Shrink();
	}

	FORCEINLINE void ValidateAndShrink()
	{
		Data->ValidateAndShrink();
	}

	FORCEINLINE UObject* GetOwner(bool bEvenIfPendingKill = false) const { return Data->SourceObject.Get(bEvenIfPendingKill); }
};

struct FNavigationOctreeSemantics
{
	typedef TOctree<FNavigationOctreeElement, FNavigationOctreeSemantics> FOctree;
	enum { MaxElementsPerLeaf = 16 };
	enum { MinInclusiveElementsPerNode = 7 };
	enum { MaxNodeDepth = 12 };

	typedef TInlineAllocator<MaxElementsPerLeaf> ElementAllocator;
	//typedef FDefaultAllocator ElementAllocator;

	FORCEINLINE static const FBoxSphereBounds& GetBoundingBox(const FNavigationOctreeElement& NavData)
	{
		return NavData.Bounds;
	}

	FORCEINLINE static bool AreElementsEqual(const FNavigationOctreeElement& A, const FNavigationOctreeElement& B)
	{
		return A.Data->SourceObject == B.Data->SourceObject;
	}

	FORCEINLINE static void ApplyOffset(FNavigationOctreeElement& Element, const FVector& InOffset)
	{
		ensureMsgf(false, TEXT("Not implemented yet"));
	}

#if NAVSYS_DEBUG
	FORCENOINLINE 
#endif // NAVSYS_DEBUG
	static void SetElementId(FOctree& OctreeOwner, const FNavigationOctreeElement& Element, FOctreeElementId Id);
};

class NAVIGATIONSYSTEM_API FNavigationOctree : public TOctree<FNavigationOctreeElement, FNavigationOctreeSemantics>, public TSharedFromThis<FNavigationOctree, ESPMode::ThreadSafe>
{
public:
	DECLARE_DELEGATE_TwoParams(FNavigableGeometryComponentExportDelegate, UActorComponent*, FNavigationRelevantData&);
	FNavigableGeometryComponentExportDelegate ComponentExportDelegate;

	enum ENavGeometryStoringMode {
		SkipNavGeometry,
		StoreNavGeometry,
	};

	FNavigationOctree(const FVector& Origin, float Radius);
	virtual ~FNavigationOctree();

	/** Add new node and fill it with navigation export data */
	void AddNode(UObject* ElementOb, INavRelevantInterface* NavElement, const FBox& Bounds, FNavigationOctreeElement& Data);

	/** Append new data to existing node */
	void AppendToNode(const FOctreeElementId& Id, INavRelevantInterface* NavElement, const FBox& Bounds, FNavigationOctreeElement& Data);

	/** Updates element bounds remove/add operation */
	void UpdateNode(const FOctreeElementId& Id, const FBox& NewBounds);

	/** Remove node */
	void RemoveNode(const FOctreeElementId& Id);

	void SetNavigableGeometryStoringMode(ENavGeometryStoringMode NavGeometryMode);

	const FNavigationRelevantData* GetDataForID(const FOctreeElementId& Id) const;

	ENavGeometryStoringMode GetNavGeometryStoringMode() const
	{
		return bGatherGeometry ? StoreNavGeometry : SkipNavGeometry;
	}

	void SetDataGatheringMode(ENavDataGatheringModeConfig Mode);
	
	// @hack! TO BE FIXED
	void DemandLazyDataGathering(const FNavigationOctreeElement& Element);
	void DemandLazyDataGathering(FNavigationRelevantData& ElementData);


	FORCEINLINE static uint32 HashObject(const UObject& Object)
	{
		return Object.GetUniqueID();
	}

protected:
	friend struct FNavigationOctreeController;
	friend struct FNavigationOctreeSemantics;

	void SetElementIdImpl(const uint32 OwnerUniqueId, FOctreeElementId Id);

	TMap<uint32, FOctreeElementId> ObjectToOctreeId;
	ENavDataGatheringMode DefaultGeometryGatheringMode;
	uint32 bGatherGeometry : 1;
	uint32 NodesMemory;
};

template<>
FORCEINLINE_DEBUGGABLE void SetOctreeMemoryUsage(TOctree<FNavigationOctreeElement, FNavigationOctreeSemantics>* Octree, int32 NewSize)
{
	{
		DEC_DWORD_STAT_BY( STAT_NavigationMemory, Octree->TotalSizeBytes );
		DEC_DWORD_STAT_BY(STAT_Navigation_CollisionTreeMemory, Octree->TotalSizeBytes);
	}
	Octree->TotalSizeBytes = NewSize;
	{
		INC_DWORD_STAT_BY( STAT_NavigationMemory, NewSize );
		INC_DWORD_STAT_BY(STAT_Navigation_CollisionTreeMemory, NewSize);
	}
}
