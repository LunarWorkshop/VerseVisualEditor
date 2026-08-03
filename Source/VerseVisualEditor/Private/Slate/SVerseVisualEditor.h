#pragma once

#include "CoreMinimal.h"
#include "Input/Reply.h"
#include "SolBuildResults.h"
#include "Semantics/VerseCompilation.h"
#include "VerseDocumentRevision.h"
#include "Document/VerseEditorFileTree.h"
#include "VisualModel/VerseOutliner.h"
#include "Semantics/VerseSemanticCandidates.h"
#include "Semantics/VerseSemanticWorkspace.h"
#include "VerseVisualEditorSettings.h"
#include "Slate/SVerseTile.h"
#include "Editing/VerseExpressionActions.h"
#include "Slate/VerseGraphCoordinates.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STreeView.h"

struct FFileChangeData;
struct FOpenVerseDocument;
enum class EVerseSyntaxControlKind : uint8;

enum class EVerseProjectBuildState : uint8
{
	Unbuilt,
	Building,
	Success,
	Warnings,
	Errors,
};
struct FVerseVisualTile;
class SBorder;
class SBox;
class SDockTab;
class SHorizontalBox;
class SOverlay;
class SSearchBox;
class SVerticalBox;
class SVerseVisualEditor final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SVerseVisualEditor) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SVerseVisualEditor() override;
	virtual void Tick(
		const FGeometry& AllottedGeometry,
		const double InCurrentTime,
		const float InDeltaTime) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	void SaveActiveDocumentFromMenu();
	void SaveActiveDocumentAs();
	void SaveAllDocuments();
	void SaveAllFromMainFrame();
	bool CanSaveAllFromMainFrame() const;
	void RevertActiveDocument();
	void CloseActiveDocument();
	bool CanSaveActiveDocument() const;
	bool HasActiveDocument() const;

private:
	void RefreshFileTree();
	void RefreshOutliner();
	TSharedRef<ITableRow> GenerateTreeRow(
		TSharedPtr<FVerseFileTreeItem> Item,
		const TSharedRef<STableViewBase>& OwnerTable) const;
	void GetTreeChildren(
		TSharedPtr<FVerseFileTreeItem> Item,
		TArray<TSharedPtr<FVerseFileTreeItem>>& OutChildren) const;
	TSharedRef<ITableRow> GenerateOutlinerRow(
		TSharedPtr<FVerseOutlinerItem> Item,
		const TSharedRef<STableViewBase>& OwnerTable) const;
	void GetOutlinerChildren(
		TSharedPtr<FVerseOutlinerItem> Item,
		TArray<TSharedPtr<FVerseOutlinerItem>>& OutChildren) const;
	void HandleOutlinerSelectionChanged(
		TSharedPtr<FVerseOutlinerItem> Item,
		ESelectInfo::Type SelectInfo);
	void HandleOutlinerItemDoubleClicked(TSharedPtr<FVerseOutlinerItem> Item);
	void SynchronizeOutlinerSelection(TOptional<FVerseTextRange> TileRange);
	void OpenFunctionView(
		const FVerseVisualTile& FunctionTile,
		TSharedPtr<FOpenVerseDocument> OpenDocument);
	FReply ActivateGlobalView(TSharedPtr<FOpenVerseDocument> OpenDocument);
	FReply ActivateFunctionView(TSharedPtr<FOpenVerseDocument> OpenDocument, int32 FunctionTabIndex);
	FReply CloseFunctionView(TSharedPtr<FOpenVerseDocument> OpenDocument, int32 FunctionTabIndex);
	TSharedRef<SWidget> BuildScopeBreadcrumb(TSharedPtr<FOpenVerseDocument> OpenDocument) const;
	TSharedRef<SWidget> BuildFunctionTabBar(TSharedPtr<FOpenVerseDocument> OpenDocument);
	void HandleTreeSelectionChanged(
		TSharedPtr<FVerseFileTreeItem> Item,
		ESelectInfo::Type SelectInfo);
	void HandleTreeItemDoubleClicked(TSharedPtr<FVerseFileTreeItem> Item);
	bool FindTreeItemByPath(
		TConstArrayView<TSharedPtr<FVerseFileTreeItem>> Items,
		const FString& FilePath,
		TSharedPtr<FVerseFileTreeItem>& OutItem);
	void RevealActiveDocumentInTree();
	TSharedPtr<SWidget> MakeTreeContextMenu();
	TSharedPtr<SWidget> MakeRevealContextMenu(FString Path);
	void RevealInFileExplorer(FString Path);
	FReply HandleTabMouseButtonUp(
		const FGeometry& Geometry,
		const FPointerEvent& PointerEvent,
		TSharedPtr<FOpenVerseDocument> OpenDocument);
	FReply HandleTabMouseButtonDoubleClick(
		const FGeometry& Geometry,
		const FPointerEvent& PointerEvent,
		TSharedPtr<FOpenVerseDocument> OpenDocument);

	void OpenDocument(const FString& FilePath, bool bTemporary);
	void PinDocument(const TSharedPtr<FOpenVerseDocument>& OpenDocument);
	bool ReloadDocument(const TSharedPtr<FOpenVerseDocument>& OpenDocument);
	FReply ActivateDocument(TSharedPtr<FOpenVerseDocument> OpenDocument);
	FReply CloseDocument(TSharedPtr<FOpenVerseDocument> OpenDocument);
	FReply SaveActiveDocument();
	bool SaveDocument(const TSharedPtr<FOpenVerseDocument>& OpenDocument);
	void RebuildDocumentTabs();
	void RefreshActiveDocument(
		bool bAnimateGraphChanges = true,
		bool bRebuildDocumentChrome = true);
	void HandleTileSelected(
		const FVerseVisualTile& Tile,
		TSharedPtr<FOpenVerseDocument> OpenDocument);
	void HandleTileSelectionCleared(TSharedPtr<FOpenVerseDocument> OpenDocument);
	void HandlePropertyFilterChanged(const FText& FilterText);
	void HandleRenameCommitted(
		const FText& NewText,
		ETextCommit::Type CommitType,
		TSharedPtr<FOpenVerseDocument> OpenDocument,
		FVerseTextRange NameRange);
	void HandleTypeSelected(
		TSharedPtr<FString> NewType,
		ESelectInfo::Type SelectInfo,
		TSharedPtr<FOpenVerseDocument> OpenDocument,
		FVerseVisualTile DefinitionTile);
	void HandleOperatorSignatureSelected(
		TSharedPtr<FString> NewSignature,
		ESelectInfo::Type SelectInfo,
		TSharedPtr<FOpenVerseDocument> OpenDocument,
		FVerseVisualTile OperatorTile);
	void HandleSyntaxControlSelected(
		TSharedPtr<FString> NewValue,
		ESelectInfo::Type SelectInfo,
		TSharedPtr<FOpenVerseDocument> OpenDocument,
		FVerseVisualTile Tile,
		EVerseSyntaxControlKind Control,
		int32 ControlRegionIndex);
	void HandleSpecifiersCommitted(
		const FText& NewText,
		ETextCommit::Type CommitType,
		TSharedPtr<FOpenVerseDocument> OpenDocument,
		FVerseVisualTile Tile,
		bool bEffects);
	void HandleDetailsTabClosed(TSharedRef<SDockTab> ClosedTab);
	void OpenDetailsTab();
	TSharedRef<SWidget> BuildDetailsPanel();
	void RebuildProperties();
	void CaptureActiveCanvasView();
	void LoadSession();
	void SaveSession();
	TSharedRef<SWidget> BuildToolbar();
	void CompileVerseProject();
	bool CanCompileVerseProject() const;
	FSlateIcon GetCompileVerseIcon() const;
	FText GetCompileVerseTooltip() const;
	void HandleProjectBuildStarted(const TSharedRef<FSolBuildResults>& BuildResults);
	void HandleProjectBuildComplete(const TSharedRef<FSolBuildResults>& BuildResults);
	void ApplyProjectDiagnostics(
		const TSharedPtr<FOpenVerseDocument>& OpenDocument,
		TConstArrayView<FSolDiagnostic> ProjectDiagnostics);
	TSharedRef<SWidget> BuildCompilationModeMenu();
	void SetCompilationMode(EVerseCompilationMode Mode);
	FText GetCompilationModeText() const;
	void QueueCompilation(const TSharedPtr<FOpenVerseDocument>& OpenDocument, bool bDebounce);
	void StartCompilation(
		const TSharedPtr<FOpenVerseDocument>& OpenDocument,
		bool bRequestSemanticAnalysis = true);
	void ApplyCompilationResult(
		const TSharedPtr<FOpenVerseDocument>& OpenDocument,
		uint64 RequestId,
		FVerseCompilationResult Result);
	void InvalidateCompilationResult(const TSharedPtr<FOpenVerseDocument>& OpenDocument);
	FReply BeginSocketDrag(const struct FVerseSocketDragStart& DragStart);
	void HandleConnectionDropped(
		const struct FVerseSocketDragStart& DragStart,
		FVerseDesktopPoint DesktopPosition);
	void HandleConnectionCancelled();
	void OpenExpressionSearch(FVerseDesktopPoint DesktopPosition);
	void FinishExpressionSearch();
	void ApplyExpressionAction(TSharedPtr<struct FVerseExpressionAction> Action);
	void HandleInlineLiteralCommitted(
		FVerseTextRange LiteralRange,
		FText NewSourceText,
		TSharedPtr<FOpenVerseDocument> OpenDocument);
	FReply HandleClauseReordered(
		const FVerseVisualClauseDescriptor& Clause,
		int32 FromIndex,
		int32 ToIndex);
	TArray<struct FVerseSemanticDocumentInput> CollectSemanticDocumentInputs(
		bool bOnlyCleanDocuments = false) const;
	void QueueSemanticAnalysis(bool bDebounce);
	void MarkSemanticCompilationPending(const TSharedPtr<FOpenVerseDocument>& OpenDocument);
	void RequestSemanticCompilation(const TSharedPtr<FOpenVerseDocument>& OpenDocument);
	void PublishCompletedSemanticCompilations();
	bool HasLocalCompileDiagnosticsForActiveDocument() const;
	FText GetLocalCompileDiagnosticsText() const;
	FReply CloseLocalCompilePanel();

	void RegisterDirectoryWatcher();
	void UnregisterDirectoryWatcher();
	void HandleDirectoryChanged(const TArray<FFileChangeData>& FileChanges);
	void ProcessDirectoryChanges(TArray<FFileChangeData> FileChanges);

	TArray<FVerseSourceRoot> SourceRoots;
	TArray<TSharedPtr<FVerseFileTreeItem>> RootItems;
	TSharedPtr<STreeView<TSharedPtr<FVerseFileTreeItem>>> FileTree;
	TArray<TSharedPtr<FVerseOutlinerItem>> OutlinerRootItems;
	TSharedPtr<STreeView<TSharedPtr<FVerseOutlinerItem>>> OutlinerTree;
	bool bSynchronizingOutlinerSelection = false;

	TArray<TSharedPtr<FOpenVerseDocument>> OpenDocuments;
	TSharedPtr<FOpenVerseDocument> ActiveDocument;
	TSharedPtr<SHorizontalBox> DocumentTabBar;
	TSharedPtr<SBox> ActiveDocumentBox;
	TSharedPtr<SWidget> LocalCompilePanel;
	TSharedPtr<SBox> ScopeBreadcrumbBox;
	TSharedPtr<SBox> DetailsPanelHost;
	TSharedPtr<SDockTab> DetailsTab;
	TSharedPtr<SSearchBox> PropertyFilter;
	TSharedPtr<SVerticalBox> PropertyRows;
	bool bWhitespaceDetailsExpanded = false;
	TArray<TSharedPtr<FString>> TypeOptions;
	TArray<TSharedPtr<FString>> OperatorSignatureOptions;
	TArray<FVerseOperatorSignature> OperatorSignatures;
	TArray<TSharedPtr<TArray<TSharedPtr<FString>>>> SyntaxOptionSets;
	FString PropertyFilterText;

	FString WatchedDirectory;
	FDelegateHandle DirectoryWatcherHandle;
	FDelegateHandle ProjectBuildStartedHandle;
	FDelegateHandle ProjectBuildCompleteHandle;
	EVerseProjectBuildState ProjectBuildState = EVerseProjectBuildState::Unbuilt;
	int32 ProjectBuildWarningCount = 0;
	int32 ProjectBuildErrorCount = 0;
	EVerseCompilationMode CompilationMode = EVerseCompilationMode::Continuous;
	EVerseFunctionGraphPresentation FunctionGraphPresentation =
		EVerseFunctionGraphPresentation::VerticalExecution;
	TOptional<struct FVerseSocketDragStart> SocketDrag;
	TArray<TSharedPtr<struct FVerseExpressionAction>> ExpressionActions;
	TSharedPtr<class IMenu> ExpressionMenu;
	TUniquePtr<FVerseSemanticWorkspace> SemanticWorkspace;
	bool bLocalCompilePanelOpen = false;
};
