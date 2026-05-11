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
			"CROSSxWebkitSdkUnrealPlugin"
		});

		// HTTP brings in FGenericPlatformHttp::UrlEncode, used by
		// OnClickUseWebkit() to build the demo Webkit URL when the user leaves
		// the URL field blank. Header is in Engine/Source/Runtime/Online/HTTP/
		// Public/GenericPlatform/GenericPlatformHttp.h, but the symbol body
		// only ships in the HTTP module, hence this dep.
		PrivateDependencyModuleNames.AddRange(new string[] { "HTTP" });

		// Allow sub-directory-prefixed includes (e.g. "UI/DappTestPanelBase.h",
		// "Localization/DappLocalizationSubsystem.h"). We don't use the
		// Public/Private split, so the module root must be on the include path.
		PrivateIncludePaths.Add(ModuleDirectory);
	}
}
