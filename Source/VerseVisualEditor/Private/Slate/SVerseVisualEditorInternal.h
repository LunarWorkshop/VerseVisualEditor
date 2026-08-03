#pragma once

#include "Slate/SVerseVisualEditor.h"

#include "Document/VerseDocumentSession.h"
#include "Editing/VerseProvisionalState.h"
#include "Infrastructure/VerseVisualEditorLifetimeDiagnostics.h"
#include "VisualModel/VerseFunctionNavigation.h"

class FVerseDocument;
class FVerseGraphMotionController;
class SVerseFileCanvas;
class SVerseFunctionCanvas;

/** Per-function transient editor state shared by the split Slate implementation files. */
struct FOpenVerseFunctionTab
{
	FString Name;
	TArray<FString> ScopePath;
	FVerseTextRange FunctionRange;
	FVerseTextRange DeclarationRange;
	FVerseTextRange BodyRange;
	FVerseTextRange ReturnTypeRange;
	TArray<FVerseFunctionNavigationParameter> Parameters;
	TArray<FVerseVisualTile> GraphTiles;
	int32 FirstDeclarationLine = INDEX_NONE;
	int32 LastDeclarationLine = INDEX_NONE;
	FVerseCanvasViewState ViewState;
	TSharedPtr<SVerseFunctionCanvas> FunctionCanvas;
	TSharedPtr<FVerseGraphMotionController> MotionController;
	FVerseDocumentRevision GraphRevision;
	bool bGraphUsesExactSemanticSnapshot = false;
	bool bHasViewState = false;
};

/** Per-file transient editor state shared by the split Slate implementation files. */
struct FOpenVerseDocument
{
	FOpenVerseDocument()
	{
		VerseVisualEditorLifetimeDiagnostics::Track(this, TEXT("OpenDocument"));
	}

	~FOpenVerseDocument()
	{
		VerseVisualEditorLifetimeDiagnostics::Untrack(this, TEXT("OpenDocument"));
	}

	FString FilePath;
	TSharedPtr<FVerseDocumentSession> Session;
	TArray<uint8> LastKnownDiskBytes;
	FText LoadError;
	FText PropertyValidationMessage;
	TOptional<FString> PendingRenameText;
	TOptional<FString> PendingSpecifierText;
	TOptional<FString> PendingOperatorSignatureText;
	FString PendingOperatorSpelling;
	int32 PendingOperatorSignatureBeginByte = INDEX_NONE;
	bool bIsTemporary = false;
	FVerseCanvasViewState ViewState;
	TSharedPtr<SVerseFileCanvas> FileCanvas;
	TOptional<FVerseVisualTile> SelectedTile;
	FVerseProvisionalState ProvisionalTiles;
	TArray<FOpenVerseFunctionTab> FunctionTabs;
	int32 ActiveFunctionTabIndex = INDEX_NONE;
	FVerseCompilationResult CompilationResult;
	bool bHasCompilationResult = false;
	bool bCompilationPending = false;
	bool bCompilationInFlight = false;
	double CompileAfterSeconds = 0.0;
	uint64 CompilationRequestId = 0;
	FVerseDocumentRevision SemanticCompilationRevision;
	TArray<FVerseSemanticDiagnostic> SemanticCompilationDiagnostics;
	bool bSemanticCompilationPending = false;
	bool bHasSemanticCompilationResult = false;
};

namespace VerseVisualEditorPrivate
{
	FLinearColor GetBlueprintPinColor(const FString& VerseType);
	FString GetVisualTypeName(
		const FVerseTextRange& TypeRange,
		FName IntrinsicTypeName,
		const FVerseDocument& Document,
		FStringView SemanticTypeName = {});
	const FVerseVisualTile* FindTileByRange(
		TConstArrayView<FVerseVisualTile> Tiles,
		FVerseTextRange Range);
	void ApplyProvisionalState(
		TArray<FVerseVisualTile>& Tiles,
		const FVerseProvisionalState& ProvisionalTiles);
	TSharedPtr<const FVerseSemanticSnapshot> FindExactSemanticSnapshot(
		const FVerseSemanticWorkspace* Workspace,
		const FOpenVerseDocument& Document);
	void BindGraphTiles(
		FOpenVerseDocument& Document,
		TArray<FVerseVisualTile>& GraphTiles,
		const TSharedPtr<const FVerseSemanticSnapshot>& Snapshot);
	void ReconcileFunctionTabs(
		FOpenVerseDocument& Document,
		const TSharedPtr<const FVerseSemanticSnapshot>& SemanticSnapshot);
}
