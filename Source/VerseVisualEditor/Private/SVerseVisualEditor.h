#pragma once

#include "CoreMinimal.h"
#include "Input/Reply.h"
#include "VerseDocumentRevision.h"
#include "VerseEditorFileTree.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STreeView.h"

struct FFileChangeData;
struct FOpenVerseDocument;
struct FVerseVisualTile;
class SBorder;
class SBox;
class SDockTab;
class SHorizontalBox;
class SSearchBox;
class SVerticalBox;

class SVerseVisualEditor final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SVerseVisualEditor) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SVerseVisualEditor() override;
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
	TSharedRef<ITableRow> GenerateTreeRow(
		TSharedPtr<FVerseFileTreeItem> Item,
		const TSharedRef<STableViewBase>& OwnerTable) const;
	void GetTreeChildren(
		TSharedPtr<FVerseFileTreeItem> Item,
		TArray<TSharedPtr<FVerseFileTreeItem>>& OutChildren) const;
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
	void RefreshActiveDocument();
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
	void HandleDetailsTabClosed(TSharedRef<SDockTab> ClosedTab);
	void OpenDetailsTab();
	TSharedRef<SWidget> BuildDetailsPanel();
	void RebuildProperties();
	void CaptureActiveCanvasView();
	void LoadSession();
	void SaveSession();

	void RegisterDirectoryWatcher();
	void UnregisterDirectoryWatcher();
	void HandleDirectoryChanged(const TArray<FFileChangeData>& FileChanges);
	void ProcessDirectoryChanges(TArray<FFileChangeData> FileChanges);

	TArray<FVerseSourceRoot> SourceRoots;
	TArray<TSharedPtr<FVerseFileTreeItem>> RootItems;
	TSharedPtr<STreeView<TSharedPtr<FVerseFileTreeItem>>> FileTree;

	TArray<TSharedPtr<FOpenVerseDocument>> OpenDocuments;
	TSharedPtr<FOpenVerseDocument> ActiveDocument;
	TSharedPtr<SHorizontalBox> DocumentTabBar;
	TSharedPtr<SBox> ActiveDocumentBox;
	TSharedPtr<SBox> DetailsPanelHost;
	TSharedPtr<SDockTab> DetailsTab;
	TSharedPtr<SSearchBox> PropertyFilter;
	TSharedPtr<SVerticalBox> PropertyRows;
	FString PropertyFilterText;

	FString WatchedDirectory;
	FDelegateHandle DirectoryWatcherHandle;
};
