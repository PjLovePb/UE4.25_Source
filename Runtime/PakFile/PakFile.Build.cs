// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class PakFile : ModuleRules
{
	public PakFile(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.Add("Core");
		PublicDependencyModuleNames.Add("RSA");
	}
}
