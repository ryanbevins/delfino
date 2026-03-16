// Copyright ryana. All Rights Reserved.

using UnrealBuildTool;

public class SMSLevelImporter : ModuleRules
{
    public SMSLevelImporter(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "InputCore"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "Slate",
            "SlateCore",
            "UnrealEd",
            "AssetTools",
            "LevelEditor",
            "ToolMenus",
            "MeshDescription",
            "StaticMeshDescription",
            "RawMesh",
            "EditorFramework",
            "AssetRegistry",
            "KismetCompiler",
            "Kismet",
            "BlueprintGraph",
            "PhysicsCore",
            "RenderCore",
            "DesktopPlatform",
            "SkeletalMeshDescription",
            "AnimationDataController",
            "AnimationCore",
            "MeshBuilder",
            "TargetPlatform"
        });
    }
}
