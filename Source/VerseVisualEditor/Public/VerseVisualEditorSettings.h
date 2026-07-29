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

};
