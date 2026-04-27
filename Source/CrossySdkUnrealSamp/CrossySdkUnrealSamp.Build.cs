// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class CrossySdkUnrealSamp : ModuleRules
{
	public CrossySdkUnrealSamp(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"UMG",
			"Slate",
			"SlateCore",
			"Json",
			"JsonUtilities",
			"CROSSxSdkUnrealPlugin",
			"CROSSxRampSdkUnrealPlugin"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });
	}
}
