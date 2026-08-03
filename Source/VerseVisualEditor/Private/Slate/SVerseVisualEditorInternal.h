#pragma once

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
	/** Retains an explicit overload choice while a semantically invalid fallback operand exists. */
	TOptional<FString> PendingOperatorSignatureText;
	FString PendingOperatorSpelling;
	int32 PendingOperatorSignatureBeginByte = INDEX_NONE;
	bool bIsTemporary = false;
	FVerseCanvasViewState ViewState;
	TSharedPtr<SVerseFileCanvas> FileCanvas;
	TOptional<FVerseVisualTile> SelectedTile;
	/** Revision-specific, editor-only tile state. Deliberately absent from session persistence. */
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
		FStringView SemanticTypeName = FStringView());

	const FVerseVisualTile* FindTileByRange(
		TConstArrayView<FVerseVisualTile> Tiles,
		FVerseTextRange Range);

	TSharedPtr<const FVerseSemanticSnapshot> FindExactSemanticSnapshot(
		const FVerseSemanticWorkspace* Workspace,
		const FOpenVerseDocument& Document);

	void ReconcileFunctionTabs(
		FOpenVerseDocument& Document,
		const TSharedPtr<const FVerseSemanticSnapshot>& SemanticSnapshot = nullptr);
}
