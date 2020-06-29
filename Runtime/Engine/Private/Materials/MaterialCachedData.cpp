// Copyright Epic Games, Inc. All Rights Reserved.

#include "MaterialCachedData.h"
#include "Materials/MaterialExpressionBreakMaterialAttributes.h"
#include "Materials/MaterialExpressionShadingModel.h"
#include "Materials/MaterialExpressionReroute.h"
#include "Materials/MaterialExpressionSingleLayerWaterMaterialOutput.h"
#include "Materials/MaterialExpressionCollectionParameter.h"
#include "Materials/MaterialExpressionCustomOutput.h"
#include "Materials/MaterialExpressionDynamicParameter.h"
#include "Materials/MaterialExpressionFontSampleParameter.h"
#include "Materials/MaterialExpressionQualitySwitch.h"
#include "Materials/MaterialExpressionFeatureLevelSwitch.h"
#include "Materials/MaterialExpressionShadingPathSwitch.h"
#include "Materials/MaterialExpressionShaderStageSwitch.h"
#include "Materials/MaterialExpressionMakeMaterialAttributes.h"
#include "Materials/MaterialExpressionParameter.h"
#include "Materials/MaterialExpressionRuntimeVirtualTextureSampleParameter.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionStaticBoolParameter.h"
#include "Materials/MaterialExpressionStaticComponentMaskParameter.h"
#include "Materials/MaterialExpressionStaticSwitchParameter.h"
#include "Materials/MaterialExpressionTextureBase.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionTextureSampleParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionVertexInterpolator.h"
#include "Materials/MaterialExpressionSceneColor.h"
#include "Materials/MaterialExpressionRuntimeVirtualTextureOutput.h"
#include "Materials/MaterialExpressionLandscapeGrassOutput.h"
#include "Materials/MaterialExpressionCurveAtlasRowParameter.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialFunctionInstance.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialParameterCollection.h"
#include "LandscapeGrassType.h"
#include "ProfilingDebugging/LoadTimeTracker.h"
#include "Curves/CurveLinearColor.h"
#include "Curves/CurveLinearColorAtlas.h"

void FMaterialCachedExpressionData::Reset()
{
	Parameters.Reset();
	ReferencedTextures.Reset();
	FunctionInfos.Reset();
	ParameterCollectionInfos.Reset();
	GrassTypes.Reset();
	DynamicParameterNames.Reset();
	QualityLevelsUsed.Reset();
	QualityLevelsUsed.AddDefaulted(EMaterialQualityLevel::Num);
	bHasRuntimeVirtualTextureOutput = false;
	bHasSceneColor = false;
}

static int32 FindParameterLowerBoundIndex(const FMaterialCachedParameterEntry& Entry, const FHashedMaterialParameterInfo& HashedParameterInfo)
{
	// Parameters are first sorted by name hash
	const uint64 NameHash = HashedParameterInfo.Name.GetHash();
	const int32 LowerIndex = Algo::LowerBound(Entry.NameHashes, NameHash);
	if (LowerIndex < Entry.NameHashes.Num())
	{
		const TConstArrayView<uint64> NameHashesUpper = MakeArrayView(&Entry.NameHashes[LowerIndex], Entry.NameHashes.Num() - LowerIndex);
		const int32 UpperIndex = LowerIndex + Algo::UpperBound(NameHashesUpper, NameHash);
		if (UpperIndex - LowerIndex > 0)
		{
			// more than 1 entry with the same name, next sort by Association/Index
			auto ProjectionFunc = [](const FMaterialParameterInfo& InParameterInfo)
			{
				return FHashedMaterialParameterInfo(FHashedName(), InParameterInfo.Association, InParameterInfo.Index);
			};
			auto CompareFunc = [](const FHashedMaterialParameterInfo& Lhs, const FHashedMaterialParameterInfo& Rhs)
			{
				if (Lhs.Association != Rhs.Association) return Lhs.Association < Rhs.Association;
				return Lhs.Index < Rhs.Index;
			};

			const TConstArrayView<FMaterialParameterInfo> ParameterInfos = MakeArrayView(&Entry.ParameterInfos[LowerIndex], UpperIndex - LowerIndex);
			return LowerIndex + Algo::LowerBoundBy(ParameterInfos, HashedParameterInfo, ProjectionFunc, CompareFunc);
		}
	}
	return LowerIndex;
}

#if WITH_EDITOR
static int32 TryAddParameter(FMaterialCachedParameters& CachedParameters, EMaterialParameterType Type, const FMaterialParameterInfo& ParameterInfo, const FGuid& ExpressionGuid, bool bOverride = false)
{
	FMaterialCachedParameterEntry& Entry = CachedParameters.Entries[(int32)Type];
	const FHashedMaterialParameterInfo HashedParameterInfo(ParameterInfo);
	int32 Index = FindParameterLowerBoundIndex(Entry, HashedParameterInfo);

	if (Index >= Entry.NameHashes.Num() || Entry.ParameterInfos[Index] != ParameterInfo)
	{
		Entry.NameHashes.Insert(HashedParameterInfo.Name.GetHash(), Index);
		Entry.ParameterInfos.Insert(ParameterInfo, Index);
		Entry.ExpressionGuids.Insert(ExpressionGuid, Index);
		Entry.Overrides.Insert(bOverride, Index);
		return Index;
	}

	if (Entry.Overrides[Index] && !Entry.ExpressionGuids[Index].IsValid())
	{
		// If Parameter was set by a function override, update to a valid expression guid
		Entry.ExpressionGuids[Index] = ExpressionGuid;
	}
	
	return INDEX_NONE;
}

bool FMaterialCachedExpressionData::UpdateForFunction(const FMaterialCachedExpressionContext& Context, UMaterialFunctionInterface* Function, EMaterialParameterAssociation Association, int32 ParameterIndex)
{
	if (!Function)
	{
		return true;
	}

	UMaterialFunctionInstance* FunctionInstance = Cast<UMaterialFunctionInstance>(Function);
	if (FunctionInstance)
	{
		for (const FScalarParameterValue& Param : FunctionInstance->ScalarParameterValues)
		{
			const FMaterialParameterInfo ParameterInfo(Param.ParameterInfo.Name, Association, ParameterIndex);
			const int32 Index = TryAddParameter(Parameters, EMaterialParameterType::Scalar, ParameterInfo, FGuid(), true);
			if (Index != INDEX_NONE)
			{
				Parameters.ScalarValues.Insert(Param.ParameterValue, Index);
				Parameters.ScalarMinMaxValues.Insert(FVector2D(), Index);
				if (Param.AtlasData.bIsUsedAsAtlasPosition)
				{
					Parameters.ScalarCurveValues.Insert(Param.AtlasData.Curve.Get(), Index);
					Parameters.ScalarCurveAtlasValues.Insert(Param.AtlasData.Atlas.Get(), Index);
				}
				else
				{
					Parameters.ScalarCurveValues.Insert(nullptr, Index);
					Parameters.ScalarCurveAtlasValues.Insert(nullptr, Index);
				}
			}
		}

		for (const FVectorParameterValue& Param : FunctionInstance->VectorParameterValues)
		{
			const FMaterialParameterInfo ParameterInfo(Param.ParameterInfo.Name, Association, ParameterIndex);
			const int32 Index = TryAddParameter(Parameters, EMaterialParameterType::Vector, ParameterInfo, FGuid(), true);
			if (Index != INDEX_NONE)
			{
				Parameters.VectorValues.Insert(Param.ParameterValue, Index);
				Parameters.VectorChannelNameValues.Insert(FParameterChannelNames(), Index);
				Parameters.VectorUsedAsChannelMaskValues.Insert(false, Index);
			}
		}

		for (const FTextureParameterValue& Param : FunctionInstance->TextureParameterValues)
		{
			const FMaterialParameterInfo ParameterInfo(Param.ParameterInfo.Name, Association, ParameterIndex);
			const int32 Index = TryAddParameter(Parameters, EMaterialParameterType::Texture, ParameterInfo, FGuid(), true);
			if (Index != INDEX_NONE)
			{
				Parameters.TextureValues.Insert(Param.ParameterValue, Index);
				Parameters.TextureChannelNameValues.Insert(FParameterChannelNames(), Index);
			}
		}

		for (const FRuntimeVirtualTextureParameterValue& Param : FunctionInstance->RuntimeVirtualTextureParameterValues)
		{
			const FMaterialParameterInfo ParameterInfo(Param.ParameterInfo.Name, Association, ParameterIndex);
			const int32 Index = TryAddParameter(Parameters, EMaterialParameterType::RuntimeVirtualTexture, ParameterInfo, FGuid(), true);
			if (Index != INDEX_NONE)
			{
				Parameters.RuntimeVirtualTextureValues.Insert(Param.ParameterValue, Index);
			}
		}

		for (const FFontParameterValue& Param : FunctionInstance->FontParameterValues)
		{
			const FMaterialParameterInfo ParameterInfo(Param.ParameterInfo.Name, Association, ParameterIndex);
			const int32 Index = TryAddParameter(Parameters, EMaterialParameterType::Font, ParameterInfo, FGuid(), true);
			if (Index != INDEX_NONE)
			{
				Parameters.FontValues.Insert(Param.FontValue, Index);
				Parameters.FontPageValues.Insert(Param.FontPage, Index);
			}
		}
	}

	bool bResult = true;

	// Update expressions for all dependent functions first, before processing the remaining expressions in this function
	// This is important so we add parameters in the proper order (parameter values are latched the first time a given parameter name is encountered)
	FMaterialCachedExpressionContext LocalContext(Context);
	LocalContext.bUpdateFunctionExpressions = false; // we update functions explicitly
	{
		FMaterialCachedExpressionData* Self = this;
		auto DependentFunctionLamba = [Self, &LocalContext, Association, ParameterIndex, &bResult](UMaterialFunctionInterface* DepFunction) -> bool
		{
			const TArray<UMaterialExpression*>* FunctionExpressions = DepFunction->GetFunctionExpressions();
			if (FunctionExpressions)
			{
				if (!Self->UpdateForExpressions(LocalContext, *FunctionExpressions, Association, ParameterIndex))
				{
					bResult = false;
				}
			}
			return true;
		};
		Function->IterateDependentFunctions(MoveTemp(DependentFunctionLamba));
	}

	const TArray<UMaterialExpression*>* FunctionExpressions = Function->GetFunctionExpressions();
	if (FunctionExpressions)
	{
		if (!UpdateForExpressions(LocalContext, *FunctionExpressions, Association, ParameterIndex))
		{
			bResult = false;
		}
	}

	FMaterialFunctionInfo NewFunctionInfo;
	NewFunctionInfo.Function = Function;
	NewFunctionInfo.StateId = Function->StateId;
	FunctionInfos.Add(NewFunctionInfo);

	return bResult;
}

bool FMaterialCachedExpressionData::UpdateForLayerFunctions(const FMaterialCachedExpressionContext& Context, const FMaterialLayersFunctions& LayerFunctions)
{
	bool bResult = true;
	for (int32 LayerIndex = 0; LayerIndex < LayerFunctions.Layers.Num(); ++LayerIndex)
	{
		if (!UpdateForFunction(Context, LayerFunctions.Layers[LayerIndex], LayerParameter, LayerIndex))
		{
			bResult = false;
		}
	}

	for (int32 BlendIndex = 0; BlendIndex < LayerFunctions.Blends.Num(); ++BlendIndex)
	{
		if (!UpdateForFunction(Context, LayerFunctions.Blends[BlendIndex], BlendParameter, BlendIndex))
		{
			bResult = false;
		}
	}

	return bResult;
}

// Remap the 'Index' of ParameterInfo from 'MaterialLayers' to be relative to 'LocalMaterialLayers'
static bool GetLocalLayerParameterInfo(const FMaterialLayersFunctions& MaterialLayers, const FMaterialParameterInfo& ParameterInfo, const FMaterialLayersFunctions& LocalMaterialLayers, FMaterialParameterInfo& OutLocalParameterInfo)
{
	int32 SrcLayerIndex = ParameterInfo.Index;
	switch (ParameterInfo.Association)
	{
	case GlobalParameter: return false;
	case LayerParameter: break;
	case BlendParameter: ++SrcLayerIndex; break; // Blends are offset by 1
	default: checkNoEntry(); break;
	}

	// Guid of the layer
	const FGuid& LayerGuid = MaterialLayers.LayerGuids[SrcLayerIndex];
	// Find local layer index that's parented to that guid
	int32 LocalLayerIndex = LocalMaterialLayers.ParentLayerGuids.Find(LayerGuid);
	if (LocalLayerIndex != INDEX_NONE)
	{
		if (ParameterInfo.Association == BlendParameter)
		{
			check(LocalLayerIndex > 0);
			--LocalLayerIndex;
		}
		OutLocalParameterInfo = ParameterInfo;
		OutLocalParameterInfo.Index = LocalLayerIndex;
		return true;
	}

	return false;
}

void FMaterialCachedParameters_UpdateForLayerParameters(FMaterialCachedParameters& Parameters, const FMaterialCachedExpressionContext& Context, UMaterialInstance* ParentMaterialInstance, const FStaticMaterialLayersParameter& LayerParameters)
{
	const FStaticParameterSet& StaticParameters = ParentMaterialInstance->GetStaticParameters();
	const FMaterialLayersFunctions* ParentMaterialLayers = nullptr;
	for (const FStaticMaterialLayersParameter& Param : StaticParameters.MaterialLayersParameters)
	{
		if (Param.ParameterInfo == LayerParameters.ParameterInfo)
		{
			ParentMaterialLayers = &Param.Value;
			break;
		}
	}

	if (ParentMaterialLayers)
	{
		for (const FScalarParameterValue& Param : ParentMaterialInstance->ScalarParameterValues)
		{
			FMaterialParameterInfo ParameterInfo;
			if (GetLocalLayerParameterInfo(*ParentMaterialLayers, Param.ParameterInfo, LayerParameters.Value, ParameterInfo))
			{
				const int32 Index = TryAddParameter(Parameters, EMaterialParameterType::Scalar, ParameterInfo, FGuid(), true);
				if (Index != INDEX_NONE)
				{
					Parameters.ScalarValues.Insert(Param.ParameterValue, Index);
					Parameters.ScalarMinMaxValues.Insert(FVector2D(), Index);
					if (Param.AtlasData.bIsUsedAsAtlasPosition)
					{
						Parameters.ScalarCurveValues.Insert(Param.AtlasData.Curve.Get(), Index);
						Parameters.ScalarCurveAtlasValues.Insert(Param.AtlasData.Atlas.Get(), Index);
					}
					else
					{
						Parameters.ScalarCurveValues.Insert(nullptr, Index);
						Parameters.ScalarCurveAtlasValues.Insert(nullptr, Index);
					}
				}
			}
		}

		for (const FVectorParameterValue& Param : ParentMaterialInstance->VectorParameterValues)
		{
			FMaterialParameterInfo ParameterInfo;
			if (GetLocalLayerParameterInfo(*ParentMaterialLayers, Param.ParameterInfo, LayerParameters.Value, ParameterInfo))
			{
				const int32 Index = TryAddParameter(Parameters, EMaterialParameterType::Vector, ParameterInfo, FGuid(), true);
				if (Index != INDEX_NONE)
				{
					Parameters.VectorValues.Insert(Param.ParameterValue, Index);
					Parameters.VectorChannelNameValues.Insert(FParameterChannelNames(), Index);
					Parameters.VectorUsedAsChannelMaskValues.Insert(false, Index);
				}
			}
		}

		for (const FTextureParameterValue& Param : ParentMaterialInstance->TextureParameterValues)
		{
			FMaterialParameterInfo ParameterInfo;
			if (GetLocalLayerParameterInfo(*ParentMaterialLayers, Param.ParameterInfo, LayerParameters.Value, ParameterInfo))
			{
				const int32 Index = TryAddParameter(Parameters, EMaterialParameterType::Texture, ParameterInfo, FGuid(), true);
				if (Index != INDEX_NONE)
				{
					Parameters.TextureValues.Insert(Param.ParameterValue, Index);
					Parameters.TextureChannelNameValues.Insert(FParameterChannelNames(), Index);
				}
			}
		}

		for (const FRuntimeVirtualTextureParameterValue& Param : ParentMaterialInstance->RuntimeVirtualTextureParameterValues)
		{
			FMaterialParameterInfo ParameterInfo;
			if (GetLocalLayerParameterInfo(*ParentMaterialLayers, Param.ParameterInfo, LayerParameters.Value, ParameterInfo))
			{
				const int32 Index = TryAddParameter(Parameters, EMaterialParameterType::RuntimeVirtualTexture, ParameterInfo, FGuid(), true);
				if (Index != INDEX_NONE)
				{
					Parameters.RuntimeVirtualTextureValues.Insert(Param.ParameterValue, Index);
				}
			}
		}

		for (const FFontParameterValue& Param : ParentMaterialInstance->FontParameterValues)
		{
			FMaterialParameterInfo ParameterInfo;
			if (GetLocalLayerParameterInfo(*ParentMaterialLayers, Param.ParameterInfo, LayerParameters.Value, ParameterInfo))
			{
				const int32 Index = TryAddParameter(Parameters, EMaterialParameterType::Font, ParameterInfo, FGuid(), true);
				if (Index != INDEX_NONE)
				{
					Parameters.FontValues.Insert(Param.FontValue, Index);
					Parameters.FontPageValues.Insert(Param.FontPage, Index);
				}
			}
		}
	}
}

bool FMaterialCachedExpressionData::UpdateForExpressions(const FMaterialCachedExpressionContext& Context, const TArray<UMaterialExpression*>& Expressions, EMaterialParameterAssociation Association, int32 ParameterIndex)
{
	bool bResult = true;
	for (UMaterialExpression* Expression : Expressions)
	{
		if (!Expression)
		{
			bResult = false;
			continue;
		}

		UObject* ReferencedTexture = Expression->GetReferencedTexture();
		checkf(!ReferencedTexture || Expression->CanReferenceTexture(), TEXT("This expression type missing an override for CanReferenceTexture?"));
		if (Expression->CanReferenceTexture())
		{
			ReferencedTextures.AddUnique(ReferencedTexture);
		}

		if (UMaterialExpressionScalarParameter* ExpressionScalarParameter = Cast<UMaterialExpressionScalarParameter>(Expression))
		{
			const FMaterialParameterInfo ParameterInfo(ExpressionScalarParameter->GetParameterName(), Association, ParameterIndex);
			const int32 Index = TryAddParameter(Parameters, EMaterialParameterType::Scalar, ParameterInfo, ExpressionScalarParameter->ExpressionGUID);
			if (Index != INDEX_NONE)
			{
				float Value = ExpressionScalarParameter->DefaultValue;
				if (Context.Parent)
				{
					Context.Parent->GetScalarParameterDefaultValue(ParameterInfo, Value, false, true);
				}
				Parameters.ScalarValues.Insert(Value, Index);
				Parameters.ScalarMinMaxValues.Insert(FVector2D(ExpressionScalarParameter->SliderMin, ExpressionScalarParameter->SliderMax), Index);
				if (ExpressionScalarParameter->IsUsedAsAtlasPosition())
				{
					UMaterialExpressionCurveAtlasRowParameter* ExpressionAtlasParameter = Cast<UMaterialExpressionCurveAtlasRowParameter>(ExpressionScalarParameter);
					Parameters.ScalarCurveValues.Insert(ExpressionAtlasParameter->Curve, Index);
					Parameters.ScalarCurveAtlasValues.Insert(ExpressionAtlasParameter->Atlas, Index);
				}
				else
				{
					Parameters.ScalarCurveValues.Insert(nullptr, Index);
					Parameters.ScalarCurveAtlasValues.Insert(nullptr, Index);
				}
			}
		}
		else if (UMaterialExpressionVectorParameter* ExpressionVectorParameter = Cast<UMaterialExpressionVectorParameter>(Expression))
		{
			const FMaterialParameterInfo ParameterInfo(ExpressionVectorParameter->GetParameterName(), Association, ParameterIndex);
			const int32 Index = TryAddParameter(Parameters, EMaterialParameterType::Vector, ParameterInfo, ExpressionVectorParameter->ExpressionGUID);
			if (Index != INDEX_NONE)
			{
				FLinearColor Value = ExpressionVectorParameter->DefaultValue;
				if (Context.Parent)
				{
					Context.Parent->GetVectorParameterDefaultValue(ParameterInfo, Value, false, true);
				}
				Parameters.VectorValues.Insert(Value, Index);
				Parameters.VectorChannelNameValues.Insert(ExpressionVectorParameter->ChannelNames, Index);
				Parameters.VectorUsedAsChannelMaskValues.Insert(ExpressionVectorParameter->IsUsedAsChannelMask(), Index);
			}
		}
		else if (UMaterialExpressionTextureSampleParameter* ExpressionTextureParameter = Cast<UMaterialExpressionTextureSampleParameter>(Expression))
		{
			const FMaterialParameterInfo ParameterInfo(ExpressionTextureParameter->GetParameterName(), Association, ParameterIndex);
			const int32 Index = TryAddParameter(Parameters, EMaterialParameterType::Texture, ParameterInfo, ExpressionTextureParameter->ExpressionGUID);
			if (Index != INDEX_NONE)
			{
				UTexture* Value = ExpressionTextureParameter->Texture;
				if (Context.Parent)
				{
					Context.Parent->GetTextureParameterDefaultValue(ParameterInfo, Value, true);
				}
				Parameters.TextureValues.Insert(Value, Index);
				Parameters.TextureChannelNameValues.Insert(ExpressionTextureParameter->ChannelNames, Index);
			}
		}
		else if (UMaterialExpressionFontSampleParameter* ExpressionFontParameter = Cast<UMaterialExpressionFontSampleParameter>(Expression))
		{
			const FMaterialParameterInfo ParameterInfo(ExpressionFontParameter->GetParameterName(), Association, ParameterIndex);
			const int32 Index = TryAddParameter(Parameters, EMaterialParameterType::Font, ParameterInfo, ExpressionFontParameter->ExpressionGUID);
			if (Index != INDEX_NONE)
			{
				UFont* Font = ExpressionFontParameter->Font;
				int32 FontTexturePage = ExpressionFontParameter->FontTexturePage;
				if (Context.Parent)
				{
					Context.Parent->GetFontParameterDefaultValue(ParameterInfo, Font, FontTexturePage, true);
				}
				Parameters.FontValues.Insert(Font, Index);
				Parameters.FontPageValues.Insert(FontTexturePage, Index);
			}
		}
		else if (UMaterialExpressionRuntimeVirtualTextureSampleParameter* ExpressionRTVParameter = Cast<UMaterialExpressionRuntimeVirtualTextureSampleParameter>(Expression))
		{
			const FMaterialParameterInfo ParameterInfo(ExpressionRTVParameter->GetParameterName(), Association, ParameterIndex);
			const int32 Index = TryAddParameter(Parameters, EMaterialParameterType::RuntimeVirtualTexture, ParameterInfo, ExpressionRTVParameter->ExpressionGUID);
			if (Index != INDEX_NONE)
			{
				URuntimeVirtualTexture* Value = ExpressionRTVParameter->VirtualTexture;
				if (Context.Parent)
				{
					Context.Parent->GetRuntimeVirtualTextureParameterDefaultValue(ParameterInfo, Value, true);
				}
				Parameters.RuntimeVirtualTextureValues.Insert(Value, Index);
			}
		}
		else if (UMaterialExpressionCollectionParameter* ExpressionCollectionParameter = Cast<UMaterialExpressionCollectionParameter>(Expression))
		{
			UMaterialParameterCollection* Collection = ExpressionCollectionParameter->Collection;
			if (Collection)
			{
				FMaterialParameterCollectionInfo NewInfo;
				NewInfo.ParameterCollection = Collection;
				NewInfo.StateId = Collection->StateId;
				ParameterCollectionInfos.AddUnique(NewInfo);
			}
		}
		else if (UMaterialExpressionDynamicParameter* ExpressionDynamicParameter = Cast< UMaterialExpressionDynamicParameter>(Expression))
		{
			DynamicParameterNames.Empty(ExpressionDynamicParameter->ParamNames.Num());
			for (const FString& Name : ExpressionDynamicParameter->ParamNames)
			{
				DynamicParameterNames.Add(*Name);
			}
		}
		else if (UMaterialExpressionLandscapeGrassOutput* ExpressionGrassOutput = Cast<UMaterialExpressionLandscapeGrassOutput>(Expression))
		{
			for (const auto& Type : ExpressionGrassOutput->GrassTypes)
			{
				GrassTypes.AddUnique(Type.GrassType);
			}
		}
		else if (UMaterialExpressionQualitySwitch* QualitySwitchNode = Cast<UMaterialExpressionQualitySwitch>(Expression))
		{
			for (int32 InputIndex = 0; InputIndex < EMaterialQualityLevel::Num; InputIndex++)
			{
				if (QualitySwitchNode->Inputs[InputIndex].IsConnected())
				{
					QualityLevelsUsed[InputIndex] = true;
				}
			}

			if (QualitySwitchNode->Default.IsConnected())
			{
				QualityLevelsUsed[EMaterialQualityLevel::High] = true;
			}
		}
		else if (Expression->IsA(UMaterialExpressionRuntimeVirtualTextureOutput::StaticClass()))
		{
			bHasRuntimeVirtualTextureOutput = true;
		}
		else if (Expression->IsA(UMaterialExpressionSceneColor::StaticClass()))
		{
			bHasSceneColor = true;
		}
		else if (Context.bUpdateFunctionExpressions)
		{
			if (UMaterialExpressionMaterialFunctionCall* FunctionCall = Cast<UMaterialExpressionMaterialFunctionCall>(Expression))
			{
				if (!UpdateForFunction(Context, FunctionCall->MaterialFunction, GlobalParameter, -1))
				{
					bResult = false;
				}

				// Update the function call node, so it can relink inputs and outputs as needed
				// Update even if MaterialFunctionNode->MaterialFunction is NULL, because we need to remove the invalid inputs in that case
				FunctionCall->UpdateFromFunctionResource();
			}
			else if (UMaterialExpressionMaterialAttributeLayers* LayersExpression = Cast<UMaterialExpressionMaterialAttributeLayers>(Expression))
			{
				checkf(Association == GlobalParameter, TEXT("UMaterialExpressionMaterialAttributeLayers can't be nested"));
				if (!UpdateForLayerFunctions(Context, LayersExpression->DefaultLayers))
				{
					bResult = false;
				}

				DefaultLayers = LayersExpression->DefaultLayers.Layers;
				DefaultLayerBlends = LayersExpression->DefaultLayers.Blends;

				LayersExpression->RebuildLayerGraph(false);
			}
		}
	}

	return bResult;
}
#endif // WITH_EDITOR

void FMaterialCachedParameterEntry::Reset()
{
	NameHashes.Reset();
	ParameterInfos.Reset();
	ExpressionGuids.Reset();
	Overrides.Reset();
}

void FMaterialCachedParameters::Reset()
{
	for (int32 i = 0; i < NumMaterialParameterTypes; ++i)
	{
		Entries[i].Reset();
	}

	ScalarValues.Reset();
	VectorValues.Reset();
	TextureValues.Reset();
	FontValues.Reset();
	FontPageValues.Reset();
	RuntimeVirtualTextureValues.Reset();

#if WITH_EDITORONLY_DATA
	ScalarMinMaxValues.Reset();
	ScalarCurveValues.Reset();
	ScalarCurveAtlasValues.Reset();
	VectorChannelNameValues.Reset();
	VectorUsedAsChannelMaskValues.Reset();
	TextureChannelNameValues.Reset();
#endif // WITH_EDITORONLY_DATA
}


int32 FMaterialCachedParameters::FindParameterIndex(EMaterialParameterType Type, const FHashedMaterialParameterInfo& HashedParameterInfo, bool bOveriddenOnly) const
{
	const int32 Index = FindParameterIndex(Type, HashedParameterInfo);
	if (Index != INDEX_NONE)
	{
		if (IsParameterValid(Type, Index, bOveriddenOnly))
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

int32 FMaterialCachedParameters::FindParameterIndex(EMaterialParameterType Type, const FHashedMaterialParameterInfo& HashedParameterInfo) const
{
	const FMaterialCachedParameterEntry& Entry = Entries[(int32)Type];
	const int32 Index = FindParameterLowerBoundIndex(Entry, HashedParameterInfo);
	if (Index < Entry.NameHashes.Num() &&
		Entry.NameHashes[Index] == HashedParameterInfo.Name.GetHash() &&
		Entry.ParameterInfos[Index].Association == HashedParameterInfo.Association &&
		Entry.ParameterInfos[Index].Index == HashedParameterInfo.Index)
	{
		return Index;
	}
	
	return INDEX_NONE;
}

bool FMaterialCachedParameters::IsParameterValid(EMaterialParameterType Type, int32 Index, bool bOveriddenOnly) const
{
	const FMaterialCachedParameterEntry& Entry = Entries[(int32)Type];
	return !bOveriddenOnly || Entry.Overrides[Index];
}

bool FMaterialCachedParameters::IsDefaultParameterValid(EMaterialParameterType Type, int32 Index, bool bOveriddenOnly, bool bCheckOwnedGlobalOverrides) const
{
	const FMaterialCachedParameterEntry& Entry = Entries[(int32)Type];
	const bool bOveridden = Entry.Overrides[Index];
	if (!bCheckOwnedGlobalOverrides && bOveridden)
	{
		return false;
	}
	if (bOveriddenOnly && !bOveridden)
	{
		return false;
	}
	return true;
}

void FMaterialCachedParameters::GetAllParameterInfoOfType(EMaterialParameterType Type, bool bEmptyOutput, TArray<FMaterialParameterInfo>& OutParameterInfo, TArray<FGuid>& OutParameterIds) const
{
	const FMaterialCachedParameterEntry& Entry = Entries[(int32)Type];
	const int32 NumParameters = Entry.NameHashes.Num();
	if (bEmptyOutput)
	{
		OutParameterInfo.Empty(NumParameters);
		OutParameterIds.Empty(NumParameters);
	}

	for (int32 i = 0; i < NumParameters; ++i)
	{
		OutParameterInfo.Add(Entry.ParameterInfos[i]);
		OutParameterIds.Add(Entry.ExpressionGuids[i]);
	}
}

void FMaterialCachedParameters::GetAllGlobalParameterInfoOfType(EMaterialParameterType Type, bool bEmptyOutput, TArray<FMaterialParameterInfo>& OutParameterInfo, TArray<FGuid>& OutParameterIds) const
{
	const FMaterialCachedParameterEntry& Entry = Entries[(int32)Type];
	const int32 NumParameters = Entry.NameHashes.Num();
	if (bEmptyOutput)
	{
		OutParameterInfo.Empty(NumParameters);
		OutParameterIds.Empty(NumParameters);
	}

	for (int32 i = 0; i < NumParameters; ++i)
	{
		const FMaterialParameterInfo& ParameterInfo = Entry.ParameterInfos[i];
		if (ParameterInfo.Association == GlobalParameter)
		{
			OutParameterInfo.Add(ParameterInfo);
			OutParameterIds.Add(Entry.ExpressionGuids[i]);
		}
	}
}

