using UnrealBuildTool;

public class VerseVisualEditor : ModuleRules
{
	public VerseVisualEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"Projects",
			"Slate",
			"SlateCore",
			"UnrealEd",
			"Verse"
		});
	}
}
