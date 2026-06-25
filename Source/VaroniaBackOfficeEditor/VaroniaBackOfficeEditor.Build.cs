// VaroniaBackOfficeEditor.Build.cs
using UnrealBuildTool;

public class VaroniaBackOfficeEditor : ModuleRules
{
    public VaroniaBackOfficeEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core",
            "CoreUObject",
            "Engine",
            "InputCore",
        });

        PrivateDependencyModuleNames.AddRange(new string[] {
            "Slate",
            "SlateCore",
            "UnrealEd",
            "LevelEditor",
            "ToolMenus",
            "RenderCore",
            "RHI",
            "ImageWrapper",
            "VaroniaBackOffice",   // FLBEConfig + enums (modèle GlobalConfig)
            "Json",
            "JsonUtilities",
            "DesktopPlatform",     // dialogue Save As (Ortho)
            "AppFramework",        // SColorPicker / OpenColorPicker
        });
    }
}