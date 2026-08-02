#pragma once

#include "Engine/DeveloperSettings.h"

#include "VerseVisualEditorSettings.generated.h"

UENUM()
enum class EVerseCompilationMode : uint8
{
	Continuous UMETA(DisplayName = "Continuous"),
	OnSave UMETA(DisplayName = "Compile on Save"),
	Manual UMETA(DisplayName = "Manual"),
};

/** Temporary prototype choices for evaluating function-graph presentation. */
UENUM()
enum class EVerseFunctionGraphPresentation : uint8
{
	VerticalExecution UMETA(DisplayName = "Vertical Execution"),
	HorizontalExecution UMETA(DisplayName = "Horizontal Execution"),
	Tracks UMETA(DisplayName = "Tracks"),
};

/** Per-user, per-project preferences for the Verse Visual Editor. */
UCLASS(config = EditorPerProjectUserSettings, meta = (DisplayName = "Verse Visual Editor"))
class VERSEVISUALEDITOR_API UVerseVisualEditorSettings final : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetContainerName() const override { return TEXT("Editor"); }
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
	virtual FName GetSectionName() const override { return TEXT("VerseVisualEditor"); }
	virtual FText GetSectionText() const override;
	virtual FText GetSectionDescription() const override;

	UPROPERTY(config, EditAnywhere, Category = Compilation, meta = (DisplayName = "Compilation Mode"))
	EVerseCompilationMode CompilationMode = EVerseCompilationMode::Continuous;

	UPROPERTY(config, EditAnywhere, Category = Prototype,
		meta = (DisplayName = "Function Graph Presentation", ConfigRestartRequired = true,
			ToolTip = "Temporary prototype selector. Restart the Verse Visual Editor after changing it."))
	EVerseFunctionGraphPresentation FunctionGraphPresentation =
		EVerseFunctionGraphPresentation::VerticalExecution;

	UPROPERTY(config, EditAnywhere, Category = Animation,
		meta = (DisplayName = "Graph Motion Duration", ClampMin = "0.0", UIMin = "0.0",
			UIMax = "1.0", Units = "s",
			ToolTip = "Duration of tile reflow, entrance, and elastic-return animations. Zero disables animation."))
	float GraphMotionDurationSeconds = 0.25f;

};
