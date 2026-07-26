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
			"Engine",
			"InputCore",
			"Projects",
			"Slate",
			"SlateCore",
			"SourceControl",
			"UnrealEd",
			"Verse",
			"VerseCompiler",
			"uLangCore"
		});
	}
}
