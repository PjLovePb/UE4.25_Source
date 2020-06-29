// Copyright Epic Games, Inc. All Rights Reserved.

#include "PostProcess/PostProcessVisualizeHDR.h"
#include "PostProcess/PostProcessTonemap.h"
#include "Curves/CurveFloat.h"

extern bool IsExtendLuminanceRangeEnabled();

class FVisualizeHDRPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FVisualizeHDRPS);
	SHADER_USE_PARAMETER_STRUCT(FVisualizeHDRPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT(FEyeAdaptationParameters, EyeAdaptation)
		SHADER_PARAMETER_STRUCT_INCLUDE(FMobileFilmTonemapParameters, MobileTonemap)
		SHADER_PARAMETER_STRUCT_INCLUDE(FTonemapperOutputDeviceParameters, OutputDevice)
		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Input)
		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Output)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HDRSceneColorTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColorTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HistogramTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, EyeAdaptationTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, HDRSceneColorSampler)
		SHADER_PARAMETER_SAMPLER(SamplerState, SceneColorSampler)
		SHADER_PARAMETER_TEXTURE(Texture2D, MiniFontTexture)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("USE_COLOR_MATRIX"), 1);
		OutEnvironment.SetDefine(TEXT("USE_SHADOW_TINT"), 1);
		OutEnvironment.SetDefine(TEXT("USE_CONTRAST"), 1);
		OutEnvironment.SetDefine(TEXT("USE_APPROXIMATE_SRGB"), (uint32)0);
	}
};

IMPLEMENT_GLOBAL_SHADER(FVisualizeHDRPS, "/Engine/Private/PostProcessVisualizeHDR.usf", "MainPS", SF_Pixel);

FScreenPassTexture AddVisualizeHDRPass(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FVisualizeHDRInputs& Inputs)
{
	check(Inputs.SceneColor.IsValid());
	check(Inputs.SceneColorBeforeTonemap.IsValid());
	check(Inputs.HistogramTexture);
	check(Inputs.EyeAdaptationTexture);
	check(Inputs.EyeAdaptationParameters);

	FScreenPassRenderTarget Output = Inputs.OverrideOutput;

	if (!Output.IsValid())
	{
		Output = FScreenPassRenderTarget::CreateFromInput(GraphBuilder, Inputs.SceneColor, View.GetOverwriteLoadAction(), TEXT("VisualizeHDR"));
	}

	const FScreenPassTextureViewport InputViewport(Inputs.SceneColor);
	const FScreenPassTextureViewport OutputViewport(Output);

	FRHISamplerState* BilinearClampSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	FVisualizeHDRPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FVisualizeHDRPS::FParameters>();
	PassParameters->RenderTargets[0] = Output.GetRenderTargetBinding();
	PassParameters->View = View.ViewUniformBuffer;
	PassParameters->Input = GetScreenPassTextureViewportParameters(InputViewport);
	PassParameters->Output = GetScreenPassTextureViewportParameters(OutputViewport);
	PassParameters->SceneColorTexture = Inputs.SceneColor.Texture;
	PassParameters->SceneColorSampler = BilinearClampSampler;
	PassParameters->HDRSceneColorTexture = Inputs.SceneColorBeforeTonemap.Texture;
	PassParameters->HDRSceneColorSampler = BilinearClampSampler;
	PassParameters->HistogramTexture = Inputs.HistogramTexture;
	PassParameters->EyeAdaptationTexture = Inputs.EyeAdaptationTexture;
	PassParameters->EyeAdaptation = *Inputs.EyeAdaptationParameters;
	PassParameters->OutputDevice = GetTonemapperOutputDeviceParameters(*View.Family);
	PassParameters->MobileTonemap = GetMobileFilmTonemapParameters(
		View.FinalPostProcessSettings,
		/* UseColorMatrix = */ true,
		/* UseShadowTint = */ true,
		/* UseContrast = */ true);
	PassParameters->MiniFontTexture = GetMiniFontTexture();

	TShaderMapRef<FVisualizeHDRPS> PixelShader(View.ShaderMap);

	RDG_EVENT_SCOPE(GraphBuilder, "VisualizeComplexity");

	AddDrawScreenPass(GraphBuilder, RDG_EVENT_NAME("Visualizer"), View, OutputViewport, InputViewport, PixelShader, PassParameters);

	Output.LoadAction = ERenderTargetLoadAction::ELoad;

	AddDrawCanvasPass(GraphBuilder, RDG_EVENT_NAME("Overlay"), View, Output,
		[Output, &View](FCanvas& Canvas)
	{
		const EAutoExposureMethod AutoExposureMethod = GetAutoExposureMethod(View);
		const bool bExtendedLuminanceRange = IsExtendLuminanceRangeEnabled();

		const float LuminanceMax = LuminanceMaxFromLensAttenuation();

		float X = Output.ViewRect.Min.X + 30;
		float Y = Output.ViewRect.Min.Y + 28;
		const float YStep = 14;
		const float ColumnWidth = 250;

		FString Line;

		Line = FString::Printf(TEXT("HDR Histogram (EV100, max of RGB)"));
		Canvas.DrawShadowedString(X, Y += YStep, *Line, GetStatsFont(), FLinearColor(1, 1, 1));

		Y += 160;

		float MinX = Output.ViewRect.Min.X + 64 + 10;
		float MaxY = Output.ViewRect.Max.Y - 64;
		float SizeX = Output.ViewRect.Size().X - 64 * 2 - 20;

		for (uint32 i = 0; i <= 4; ++i)
		{
			int XAdd = (int)(i * SizeX / 4);
			float HistogramPosition = i / 4.0f;
			float EV100Value = FMath::Lerp(View.FinalPostProcessSettings.HistogramLogMin, View.FinalPostProcessSettings.HistogramLogMax, HistogramPosition);
			if (!bExtendedLuminanceRange)
			{
				// In this case the post process settings are actually Log2 values.
				EV100Value = Log2ToEV100(LuminanceMax,EV100Value);
			}

			Line = FString::Printf(TEXT("%.2g"), EV100Value);
			Canvas.DrawShadowedString(MinX + XAdd - 5, MaxY + YStep, *Line, GetStatsFont(), FLinearColor(1, 0.3f, 0.3f));
		}
		Y += 3 * YStep;
		switch (AutoExposureMethod)
		{
		case EAutoExposureMethod::AEM_Basic:
			Line = FString::Printf(TEXT("Basic"));
			break;
		case EAutoExposureMethod::AEM_Histogram:
			Line = FString::Printf(TEXT("Histogram"));
			break;
		case EAutoExposureMethod::AEM_Manual:
			Line = FString::Printf(TEXT("Manual"));
			break;
		default:
			Line = FString::Printf(TEXT("Unknown"));
			break;
		}
		Canvas.DrawShadowedString(X, Y += YStep, TEXT("Auto Exposure Method:"), GetStatsFont(), FLinearColor(1, 1, 1));
		Canvas.DrawShadowedString(X + ColumnWidth, Y, *Line, GetStatsFont(), FLinearColor(1, 1, 1));

		Line = FString::Printf(TEXT("%g%% .. %g%%"), View.FinalPostProcessSettings.AutoExposureLowPercent, View.FinalPostProcessSettings.AutoExposureHighPercent);
		Canvas.DrawShadowedString(X, Y += YStep, TEXT("Percent Low/High:"), GetStatsFont(), FLinearColor(1, 1, 1));
		Canvas.DrawShadowedString(X + ColumnWidth, Y, *Line, GetStatsFont(), FLinearColor(1, 1, 1));

		if (bExtendedLuminanceRange)
		{
			Line = FString::Printf(TEXT("%.1f .. %.1f"), View.FinalPostProcessSettings.AutoExposureMinBrightness, View.FinalPostProcessSettings.AutoExposureMaxBrightness);
		}
		else
		{
			Line = FString::Printf(TEXT("%.1f .. %.1f"), LuminanceToEV100(LuminanceMax,View.FinalPostProcessSettings.AutoExposureMinBrightness), LuminanceToEV100(LuminanceMax,View.FinalPostProcessSettings.AutoExposureMaxBrightness));
		}
		Canvas.DrawShadowedString(X, Y += YStep, TEXT("EV100 Min/Max"), GetStatsFont(), FLinearColor(1, 1, 1));
		Canvas.DrawShadowedString(X + ColumnWidth, Y, *Line, GetStatsFont(), FLinearColor(0.3f, 0.3f, 1));

		Line = FString::Printf(TEXT("%g / %g"), View.FinalPostProcessSettings.AutoExposureSpeedUp, View.FinalPostProcessSettings.AutoExposureSpeedDown);
		Canvas.DrawShadowedString(X, Y += YStep, TEXT("Speed Up/Down:"), GetStatsFont(), FLinearColor(1, 1, 1));
		Canvas.DrawShadowedString(X + ColumnWidth, Y, *Line, GetStatsFont(), FLinearColor(1, 1, 1));

		float AutoExposureBias = View.FinalPostProcessSettings.AutoExposureBias;
		{
			float AverageSceneLuminance = View.GetLastAverageSceneLuminance();

			float CurveExposureBias = 0.0f;
			float AverageSceneLuminanceEV100 = 0.0f;
			if (AverageSceneLuminance > 0)
			{
				// We need the Log2(0.18) to convert from average luminance to saturation luminance
				AverageSceneLuminanceEV100 = LuminanceToEV100(LuminanceMax, AverageSceneLuminance) + FMath::Log2(1.0f / 0.18f);
				if (View.FinalPostProcessSettings.AutoExposureBiasCurve)
				{
					CurveExposureBias = View.FinalPostProcessSettings.AutoExposureBiasCurve->GetFloatValue(AverageSceneLuminanceEV100);
				}
			}

			Line = FString::Printf(TEXT("%.3g"), AverageSceneLuminanceEV100);
			Canvas.DrawShadowedString(X, Y += YStep, TEXT("Average Scene EV100:"), GetStatsFont(), FLinearColor(1, 1, 1));
			Canvas.DrawShadowedString(X + ColumnWidth, Y, *Line, GetStatsFont(), FLinearColor(1, 1, 1));

			Line = FString::Printf(TEXT("%.3g"), AutoExposureBias);
			Canvas.DrawShadowedString(X, Y += YStep, TEXT("Exposure Compensation (Settings):"), GetStatsFont(), FLinearColor(1, 1, 1));
			Canvas.DrawShadowedString(X + ColumnWidth, Y, *Line, GetStatsFont(), FLinearColor(1, 1, 1));

			Line = FString::Printf(TEXT("%.3g"), CurveExposureBias);
			Canvas.DrawShadowedString(X, Y += YStep, TEXT("Exposure Compensation (Curve):"), GetStatsFont(), FLinearColor(1, 1, 1));
			Canvas.DrawShadowedString(X + ColumnWidth, Y, *Line, GetStatsFont(), FLinearColor(1, 1, 1));

			AutoExposureBias += CurveExposureBias;
		}

		Line = FString::Printf(TEXT("%.3g"), AutoExposureBias);
		Canvas.DrawShadowedString(X, Y += YStep, TEXT("Exposure Compensation (All): "), GetStatsFont(), FLinearColor(1, 1, 1));
		Canvas.DrawShadowedString(X + ColumnWidth, Y, *Line, GetStatsFont(), FLinearColor(1, 0.3f, 0.3f));

		if (bExtendedLuminanceRange)
		{
			Line = FString::Printf(TEXT("%.1f .. %.1f"), View.FinalPostProcessSettings.HistogramLogMin, View.FinalPostProcessSettings.HistogramLogMax);
		}
		else
		{
			Line = FString::Printf(TEXT("%.1f .. %.1f"), Log2ToEV100(LuminanceMax,View.FinalPostProcessSettings.HistogramLogMin), Log2ToEV100(LuminanceMax,View.FinalPostProcessSettings.HistogramLogMax));
		}

		Canvas.DrawShadowedString(X, Y += YStep, TEXT("Histogram EV100 Min/Max:"), GetStatsFont(), FLinearColor(1, 1, 1));
		Canvas.DrawShadowedString(X + ColumnWidth, Y, *Line, GetStatsFont(), FLinearColor(0.3f, 0.3f, 1));

	});

	return MoveTemp(Output);
}