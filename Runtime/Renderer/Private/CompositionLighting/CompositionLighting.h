// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	CompositionLighting.h: The center for all deferred lighting activities.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "SceneRendering.h"

/**
 * The center for all screen space processing activities (e.g. G-buffer manipulation, lighting).
 */
class FCompositionLighting
{
public:
	void ProcessBeforeBasePass(FRHICommandListImmediate& RHICmdList, FViewInfo& View, bool bDBuffer, uint32 SSAOLevels);

	void ProcessAfterBasePass(FRHICommandListImmediate& RHICmdList,  FViewInfo& View);

	// only call if LPV is enabled
	void ProcessLpvIndirect(FRHICommandListImmediate& RHICmdList, FViewInfo& View);

	bool CanProcessAsyncSSAO(TArray<FViewInfo>& Views);
	void ProcessAsyncSSAO(FRHICommandListImmediate& RHICmdList, TArray<FViewInfo>& Views);
	void GfxWaitForAsyncSSAO(FRHICommandListImmediate& RHICmdList);

private:
	void PrepareAsyncSSAO(FRHICommandListImmediate& RHICmdList, TArray<FViewInfo>& Views);
	void FinishAsyncSSAO(FRHICommandListImmediate& RHICmdList);
	FComputeFenceRHIRef AsyncSSAOFence;
};

/** The global used for deferred lighting. */
extern FCompositionLighting GCompositionLighting;

extern bool ShouldRenderScreenSpaceAmbientOcclusion(const FViewInfo& View);