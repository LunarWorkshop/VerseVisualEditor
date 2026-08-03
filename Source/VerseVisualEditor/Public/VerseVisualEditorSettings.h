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

UENUM()
enum class EVerseFormattingIndentation : uint8
{
	Spaces UMETA(DisplayName = "Spaces"),
	Tabs UMETA(DisplayName = "Tabs"),
};

UENUM()
enum class EVerseFormattingLineEnding : uint8
{
	Lf UMETA(DisplayName = "LF"),
	CrLf UMETA(DisplayName = "CRLF"),
	Cr UMETA(DisplayName = "CR"),
};

UENUM()
enum class EVerseFormattingBodySyntax : uint8
{
	Colon UMETA(DisplayName = "Colon-indented"),
	Braces UMETA(DisplayName = "Braces"),
	BareIndentation UMETA(DisplayName = "Bare indentation"),
	Dot UMETA(DisplayName = "Dot body"),
};

UENUM()
enum class EVerseFormattingStatementSeparator : uint8
{
	Newline UMETA(DisplayName = "Newline"),
	SemicolonSpace UMETA(DisplayName = "Semicolon + space"),
	SemicolonNewline UMETA(DisplayName = "Semicolon + newline"),
};

UENUM()
enum class EVerseFormattingBracePlacement : uint8
{
	SameLine UMETA(DisplayName = "Same line"),
	NextLine UMETA(DisplayName = "Next line"),
};

/** Shared defaults used only when source and neighboring clauses provide no convention. */
UCLASS(config = Editor, defaultconfig, meta = (DisplayName = "Verse Visual Editor"))
class VERSEVISUALEDITOR_API UVerseVisualEditorProjectSettings final : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetContainerName() const override { return TEXT("Project"); }
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
	virtual FName GetSectionName() const override { return TEXT("VerseVisualEditor"); }
	virtual FText GetSectionText() const override;
	virtual FText GetSectionDescription() const override;

	UPROPERTY(config, EditAnywhere, Category = General)
	EVerseFormattingIndentation Indentation = EVerseFormattingIndentation::Spaces;

	UPROPERTY(config, EditAnywhere, Category = General, meta = (ClampMin = "1", ClampMax = "8"))
	int32 IndentationWidth = 4;

	UPROPERTY(config, EditAnywhere, Category = General)
	EVerseFormattingLineEnding LineEnding = EVerseFormattingLineEnding::Lf;

	UPROPERTY(config, EditAnywhere, Category = General)
	EVerseFormattingBodySyntax BodySyntax = EVerseFormattingBodySyntax::Colon;

	UPROPERTY(config, EditAnywhere, Category = General)
	EVerseFormattingStatementSeparator StatementSeparator =
		EVerseFormattingStatementSeparator::Newline;

	UPROPERTY(config, EditAnywhere, Category = General)
	EVerseFormattingStatementSeparator FailurePredicateSeparator =
		EVerseFormattingStatementSeparator::SemicolonSpace;

	UPROPERTY(config, EditAnywhere, Category = General)
	EVerseFormattingBracePlacement BracePlacement =
		EVerseFormattingBracePlacement::SameLine;

	UPROPERTY(config, EditAnywhere, Category = Spacing)
	bool bSpaceAroundOperators = true;

	UPROPERTY(config, EditAnywhere, Category = Spacing)
	bool bSpaceAfterComma = true;

	UPROPERTY(config, EditAnywhere, Category = Spacing)
	bool bSpaceAfterSemicolon = true;

	UPROPERTY(config, EditAnywhere, Category = Spacing)
	bool bSpaceInsideParentheses = false;
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

	UPROPERTY(config, EditAnywhere, Category = FormattingOverrides,
		meta = (DisplayName = "Override Project Formatting"))
	bool bOverrideProjectFormatting = false;

	UPROPERTY(config, EditAnywhere, Category = FormattingOverrides,
		meta = (EditCondition = "bOverrideProjectFormatting"))
	EVerseFormattingIndentation IndentationOverride = EVerseFormattingIndentation::Spaces;

	UPROPERTY(config, EditAnywhere, Category = FormattingOverrides,
		meta = (EditCondition = "bOverrideProjectFormatting", ClampMin = "1", ClampMax = "8"))
	int32 IndentationWidthOverride = 4;

	UPROPERTY(config, EditAnywhere, Category = FormattingOverrides,
		meta = (EditCondition = "bOverrideProjectFormatting"))
	EVerseFormattingLineEnding LineEndingOverride = EVerseFormattingLineEnding::Lf;

	UPROPERTY(config, EditAnywhere, Category = FormattingOverrides,
		meta = (EditCondition = "bOverrideProjectFormatting"))
	EVerseFormattingBodySyntax BodySyntaxOverride = EVerseFormattingBodySyntax::Colon;

	UPROPERTY(config, EditAnywhere, Category = FormattingOverrides,
		meta = (EditCondition = "bOverrideProjectFormatting"))
	EVerseFormattingStatementSeparator StatementSeparatorOverride =
		EVerseFormattingStatementSeparator::Newline;

	UPROPERTY(config, EditAnywhere, Category = FormattingOverrides,
		meta = (EditCondition = "bOverrideProjectFormatting"))
	EVerseFormattingBracePlacement BracePlacementOverride =
		EVerseFormattingBracePlacement::SameLine;

};
