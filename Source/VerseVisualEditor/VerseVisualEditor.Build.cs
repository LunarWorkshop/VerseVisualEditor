using UnrealBuildTool;

public class VerseVisualEditor : ModuleRules
{
	public VerseVisualEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateIncludePathModuleNames.Add("WorkspaceMenuStructure");

		PrivateDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"DirectoryWatcher",
			"DesktopPlatform",
			"DeveloperSettings",
			"Engine",
			"InputCore",
			"MainFrame",
			"Projects",
			"Slate",
			"SlateCore",
			"Solaris",
			"SolarisEditor",
			"SolarisLoadCompiler",
			"SourceControl",
			"ToolMenus",
			"UnrealEd",
			"Verse",
			"VerseCompiler",
			"uLangCore"
		});
	}
}
