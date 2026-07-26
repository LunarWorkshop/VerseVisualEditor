#pragma once

#include "CoreMinimal.h"
#include "Input/Reply.h"
#include "VerseEditorFileTree.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STreeView.h"

struct FFileChangeData;
struct FOpenVerseDocument;
class SBox;
class SHorizontalBox;

class SVerseVisualEditor final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SVerseVisualEditor) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SVerseVisualEditor() override;

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
	void RebuildDocumentTabs();
	void RefreshActiveDocument();

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

	FString WatchedDirectory;
	FDelegateHandle DirectoryWatcherHandle;
};
