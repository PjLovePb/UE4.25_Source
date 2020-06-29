// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class SlateNullRenderer : ModuleRules
{
	public SlateNullRenderer(ReadOnlyTargetRules Target) : base(Target)
	{
        PrivateIncludePaths.Add("Runtime/SlateNullRenderer/Private");

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"SlateCore"
			}
		);

		if (Target.bCompileAgainstEngine)
		{
			PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Engine",
				"RenderCore",
				"RHI"
			}
		);
		}
	}
}
