#include "SVerseVisualEditor.h"

#include "SVerseTileCanvas.h"

#include "Async/Async.h"
#include "DirectoryWatcherModule.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "IDirectoryWatcher.h"
#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "ISourceControlState.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "VerseDocument.h"
#include "VerseDocumentSession.h"
#include "VerseExternalChange.h"
#include "VerseIdentifier.h"
#include "VerseTileProperties.h"
#include "VerseVisualTile.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SVerseVisualEditor"

struct FOpenVerseDocument
{
	FString FilePath;
	TSharedPtr<FVerseDocumentSession> Session;
	TArray<uint8> LastKnownDiskBytes;
	FText LoadError;
	FText RenameValidationMessage;
	bool bIsTemporary = false;
	FVerseCanvasViewState ViewState;
	TSharedPtr<SVerseTileCanvas> TileCanvas;
	TOptional<FVerseVisualTile> SelectedTile;
};

namespace
{
	constexpr TCHAR SessionSection[] = TEXT("VerseVisualEditor.Session");

	FText GetSourceControlStatus(const FString& FilePath)
	{
		if (!ISourceControlModule::Get().IsEnabled()
			|| !ISourceControlModule::Get().GetProvider().IsAvailable())
		{
			return LOCTEXT("SourceControlUnavailable", "Source control: unavailable");
		}

		const FSourceControlStatePtr State =
			ISourceControlModule::Get().GetProvider().GetState(FilePath, EStateCacheUsage::Use);
		return State.IsValid()
			? FText::Format(LOCTEXT("SourceControlStatus", "Source control: {0}"), State->GetDisplayName())
			: LOCTEXT("SourceControlUnknown", "Source control: unknown");
	}

	bool ByteArraysEqual(TConstArrayView<uint8> Left, TConstArrayView<uint8> Right)
	{
		return Left.Num() == Right.Num()
			&& (Left.IsEmpty() || FMemory::Memcmp(Left.GetData(), Right.GetData(), Left.Num()) == 0);
	}
}

void SVerseVisualEditor::Construct(const FArguments& InArgs)
{
	RefreshFileTree();

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(4.0f, 2.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.ContentPadding(4.0f)
					.ToolTipText(LOCTEXT("SaveActiveDocumentTooltip", "Save Active Verse File (Ctrl+S)"))
					.IsEnabled_Lambda([this]()
					{
						return CanSaveActiveDocument();
					})
					.OnClicked(this, &SVerseVisualEditor::SaveActiveDocument)
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush("Icons.Save"))
					]
				]
			]
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SSplitter)
			+ SSplitter::Slot()
			.Value(0.22f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.Padding(6.0f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(2.0f, 2.0f, 2.0f, 6.0f)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("VerseFilesHeading", "Verse Files"))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
					]
					+ SVerticalBox::Slot()
					.FillHeight(1.0f)
					[
						SNew(SOverlay)
						+ SOverlay::Slot()
						[
							SAssignNew(FileTree, STreeView<TSharedPtr<FVerseFileTreeItem>>)
							.TreeItemsSource(&RootItems)
							.OnGenerateRow(this, &SVerseVisualEditor::GenerateTreeRow)
							.OnGetChildren(this, &SVerseVisualEditor::GetTreeChildren)
							.OnSelectionChanged(this, &SVerseVisualEditor::HandleTreeSelectionChanged)
							.OnMouseButtonDoubleClick(this, &SVerseVisualEditor::HandleTreeItemDoubleClicked)
							.OnContextMenuOpening(this, &SVerseVisualEditor::MakeTreeContextMenu)
							.SelectionMode(ESelectionMode::Single)
						]
						+ SOverlay::Slot()
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("NoVerseRoots", "No project Verse source folders were found."))
							.AutoWrapText(true)
							.Justification(ETextJustify::Center)
							.Visibility_Lambda([this]()
							{
								return RootItems.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed;
							})
						]
					]
				]
			]
			+ SSplitter::Slot()
			.Value(0.58f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SAssignNew(DocumentTabBar, SHorizontalBox)
				]
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					SAssignNew(ActiveDocumentBox, SBox)
				]
			]
			+ SSplitter::Slot()
			.Value(0.20f)
			[
				SAssignNew(DetailsPanelHost, SBox)
			]
		]
	];

	OpenDetailsTab();

	LoadSession();
	RebuildDocumentTabs();
	RefreshActiveDocument();
	RebuildProperties();
	RevealActiveDocumentInTree();
	RegisterDirectoryWatcher();
}

SVerseVisualEditor::~SVerseVisualEditor()
{
	SaveSession();
	UnregisterDirectoryWatcher();
}

void SVerseVisualEditor::RefreshFileTree()
{
	VerseVisualEditor::DiscoverProjectVerseRoots(SourceRoots);
	RootItems = VerseVisualEditor::BuildVerseFileTree(SourceRoots);
	if (FileTree.IsValid())
	{
		FileTree->RequestTreeRefresh();
		for (const TSharedPtr<FVerseFileTreeItem>& Root : RootItems)
		{
			FileTree->SetItemExpansion(Root, true);
		}
		RevealActiveDocumentInTree();
	}
}

TSharedRef<ITableRow> SVerseVisualEditor::GenerateTreeRow(
	TSharedPtr<FVerseFileTreeItem> Item,
	const TSharedRef<STableViewBase>& OwnerTable) const
{
	const FName IconName = Item->bIsDirectory ? "ContentBrowser.AssetTreeFolderClosed" : "Icons.Documentation";
	return SNew(STableRow<TSharedPtr<FVerseFileTreeItem>>, OwnerTable)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, 5.0f, 0.0f)
		[
			SNew(SImage)
			.Image(FAppStyle::GetBrush(IconName))
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(FText::FromString(Item->Name))
			.ToolTipText(FText::FromString(Item->FullPath))
		]
	];
}

void SVerseVisualEditor::GetTreeChildren(
	TSharedPtr<FVerseFileTreeItem> Item,
	TArray<TSharedPtr<FVerseFileTreeItem>>& OutChildren) const
{
	OutChildren = Item->Children;
}

void SVerseVisualEditor::HandleTreeSelectionChanged(
	TSharedPtr<FVerseFileTreeItem> Item,
	ESelectInfo::Type SelectInfo)
{
	if (SelectInfo != ESelectInfo::Direct && Item.IsValid() && !Item->bIsDirectory)
	{
		OpenDocument(Item->FullPath, true);
	}
}

FReply SVerseVisualEditor::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.IsControlDown() && InKeyEvent.GetKey() == EKeys::S)
	{
		if (InKeyEvent.IsShiftDown())
		{
			SaveAllDocuments();
			return FReply::Handled();
		}
		return SaveActiveDocument();
	}
	if (InKeyEvent.IsControlDown() && InKeyEvent.GetKey() == EKeys::W)
	{
		CloseActiveDocument();
		return FReply::Handled();
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

void SVerseVisualEditor::HandleTreeItemDoubleClicked(TSharedPtr<FVerseFileTreeItem> Item)
{
	if (Item.IsValid() && !Item->bIsDirectory)
	{
		OpenDocument(Item->FullPath, false);
	}
}

bool SVerseVisualEditor::FindTreeItemByPath(
	TConstArrayView<TSharedPtr<FVerseFileTreeItem>> Items,
	const FString& FilePath,
	TSharedPtr<FVerseFileTreeItem>& OutItem)
{
	for (const TSharedPtr<FVerseFileTreeItem>& Item : Items)
	{
		if (Item->FullPath.Equals(FilePath, ESearchCase::IgnoreCase))
		{
			OutItem = Item;
			return true;
		}

		if (Item->bIsDirectory && FindTreeItemByPath(Item->Children, FilePath, OutItem))
		{
			FileTree->SetItemExpansion(Item, true);
			return true;
		}
	}

	return false;
}

void SVerseVisualEditor::RevealActiveDocumentInTree()
{
	if (!FileTree.IsValid() || !ActiveDocument.IsValid())
	{
		return;
	}

	TSharedPtr<FVerseFileTreeItem> TreeItem;
	if (!FindTreeItemByPath(RootItems, ActiveDocument->FilePath, TreeItem))
	{
		return;
	}

	FileTree->SetSelection(TreeItem, ESelectInfo::Direct);
	FileTree->RequestScrollIntoView(TreeItem);
}

TSharedPtr<SWidget> SVerseVisualEditor::MakeTreeContextMenu()
{
	if (!FileTree.IsValid())
	{
		return nullptr;
	}

	const TArray<TSharedPtr<FVerseFileTreeItem>> SelectedItems = FileTree->GetSelectedItems();
	if (SelectedItems.Num() != 1 || !SelectedItems[0].IsValid())
	{
		return nullptr;
	}

	const TSharedPtr<FVerseFileTreeItem>& SelectedItem = SelectedItems[0];
	if (!SelectedItem->bIsDirectory)
	{
		OpenDocument(SelectedItem->FullPath, false);
	}
	return MakeRevealContextMenu(SelectedItem->FullPath);
}

TSharedPtr<SWidget> SVerseVisualEditor::MakeRevealContextMenu(FString Path)
{
	FMenuBuilder MenuBuilder(true, nullptr);
	MenuBuilder.AddMenuEntry(
		LOCTEXT("RevealInFileExplorer", "Reveal in File Explorer"),
		LOCTEXT("RevealInFileExplorerTooltip", "Open File Explorer and reveal this item."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.FolderOpen"),
		FUIAction(FExecuteAction::CreateSP(this, &SVerseVisualEditor::RevealInFileExplorer, MoveTemp(Path))));
	return MenuBuilder.MakeWidget();
}

void SVerseVisualEditor::RevealInFileExplorer(FString Path)
{
	FPlatformProcess::ExploreFolder(*Path);
}

FReply SVerseVisualEditor::HandleTabMouseButtonUp(
	const FGeometry& Geometry,
	const FPointerEvent& PointerEvent,
	TSharedPtr<FOpenVerseDocument> OpenDocument)
{
	if (PointerEvent.GetEffectingButton() != EKeys::RightMouseButton)
	{
		return FReply::Unhandled();
	}

	PinDocument(OpenDocument);
	FSlateApplication::Get().PushMenu(
		SharedThis(this),
		FWidgetPath(),
		MakeRevealContextMenu(OpenDocument->FilePath).ToSharedRef(),
		PointerEvent.GetScreenSpacePosition(),
		FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
	return FReply::Handled();
}

FReply SVerseVisualEditor::HandleTabMouseButtonDoubleClick(
	const FGeometry& Geometry,
	const FPointerEvent& PointerEvent,
	TSharedPtr<FOpenVerseDocument> OpenDocument)
{
	if (PointerEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}

	PinDocument(OpenDocument);
	return FReply::Handled();
}

void SVerseVisualEditor::OpenDocument(const FString& FilePath, bool bTemporary)
{
	FString NormalizedPath = FPaths::ConvertRelativePathToFull(FilePath);
	FPaths::NormalizeFilename(NormalizedPath);
	if (const TSharedPtr<FOpenVerseDocument>* Existing = OpenDocuments.FindByPredicate(
		[&NormalizedPath](const TSharedPtr<FOpenVerseDocument>& Candidate)
		{
			return Candidate->FilePath.Equals(NormalizedPath, ESearchCase::IgnoreCase);
		}))
	{
		CaptureActiveCanvasView();
		ActiveDocument = *Existing;
		if (!bTemporary)
		{
			PinDocument(ActiveDocument);
		}
		RebuildDocumentTabs();
		RefreshActiveDocument();
		return;
	}

	TSharedPtr<FOpenVerseDocument> NewDocument = MakeShared<FOpenVerseDocument>();
	NewDocument->FilePath = MoveTemp(NormalizedPath);
	NewDocument->bIsTemporary = bTemporary;
	if (!ReloadDocument(NewDocument))
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			NewDocument->LoadError,
			LOCTEXT("OpenVerseFileFailedTitle", "Unable to Open Verse File"));
		return;
	}

	if (bTemporary)
	{
		OpenDocuments.RemoveAll([](const TSharedPtr<FOpenVerseDocument>& Candidate)
		{
			return Candidate.IsValid() && Candidate->bIsTemporary;
		});
	}
	CaptureActiveCanvasView();
	OpenDocuments.Add(NewDocument);
	ActiveDocument = MoveTemp(NewDocument);
	RebuildDocumentTabs();
	RefreshActiveDocument();
}

void SVerseVisualEditor::PinDocument(const TSharedPtr<FOpenVerseDocument>& OpenDocument)
{
	if (OpenDocument.IsValid())
	{
		OpenDocument->bIsTemporary = false;
	}
}

bool SVerseVisualEditor::ReloadDocument(const TSharedPtr<FOpenVerseDocument>& OpenDocument)
{
	TArray<uint8> DiskBytes;
	if (!FFileHelper::LoadFileToArray(DiskBytes, *OpenDocument->FilePath))
	{
		OpenDocument->LoadError = FText::Format(
			LOCTEXT("ReloadReadFailed", "Could not read Verse file: {0}"),
			FText::FromString(OpenDocument->FilePath));
		return false;
	}

	FText Error;
	TSharedPtr<FVerseDocument> LoadedDocument = FVerseDocument::CreateFromBytes(DiskBytes, Error);
	if (!LoadedDocument.IsValid())
	{
		OpenDocument->LoadError = Error;
		return false;
	}

	if (OpenDocument->Session.IsValid())
	{
		OpenDocument->Session->Reload(LoadedDocument.ToSharedRef());
	}
	else
	{
		OpenDocument->Session = MakeShared<FVerseDocumentSession>(LoadedDocument.ToSharedRef());
	}
	OpenDocument->LastKnownDiskBytes = MoveTemp(DiskBytes);
	OpenDocument->LoadError = FText::GetEmpty();
	OpenDocument->RenameValidationMessage = FText::GetEmpty();
	OpenDocument->SelectedTile.Reset();
	return true;
}

FReply SVerseVisualEditor::ActivateDocument(TSharedPtr<FOpenVerseDocument> OpenDocument)
{
	CaptureActiveCanvasView();
	ActiveDocument = MoveTemp(OpenDocument);
	RebuildDocumentTabs();
	RefreshActiveDocument();
	RevealActiveDocumentInTree();
	return FReply::Handled();
}

FReply SVerseVisualEditor::CloseDocument(TSharedPtr<FOpenVerseDocument> OpenDocument)
{
	if (OpenDocument.IsValid() && OpenDocument->Session.IsValid() && OpenDocument->Session->IsDirty())
	{
		const EAppReturnType::Type Choice = FMessageDialog::Open(
			EAppMsgType::YesNoCancel,
			FText::Format(
				LOCTEXT("SaveBeforeClose", "Save changes to {0} before closing?\n\nYes: Save\nNo: Discard\nCancel: Keep the tab open"),
				FText::FromString(FPaths::GetCleanFilename(OpenDocument->FilePath))),
			LOCTEXT("UnsavedVerseFileTitle", "Unsaved Verse File"));
		if (Choice == EAppReturnType::Cancel
			|| (Choice == EAppReturnType::Yes && !SaveDocument(OpenDocument)))
		{
			return FReply::Handled();
		}
	}

	if (ActiveDocument == OpenDocument)
	{
		CaptureActiveCanvasView();
	}
	const int32 RemovedIndex = OpenDocuments.IndexOfByKey(OpenDocument);
	OpenDocuments.Remove(OpenDocument);
	if (ActiveDocument == OpenDocument)
	{
		ActiveDocument = OpenDocuments.IsEmpty()
			? nullptr
			: OpenDocuments[FMath::Clamp(RemovedIndex - 1, 0, OpenDocuments.Num() - 1)];
	}
	RebuildDocumentTabs();
	RefreshActiveDocument();
	RevealActiveDocumentInTree();
	return FReply::Handled();
}

FReply SVerseVisualEditor::SaveActiveDocument()
{
	SaveDocument(ActiveDocument);
	return FReply::Handled();
}

bool SVerseVisualEditor::SaveDocument(const TSharedPtr<FOpenVerseDocument>& OpenDocument)
{
	if (!OpenDocument.IsValid() || !OpenDocument->Session.IsValid())
	{
		return false;
	}
	if (!OpenDocument->Session->IsDirty())
	{
		return true;
	}

	FText Error;
	if (!OpenDocument->Session->SaveToFile(OpenDocument->FilePath, Error))
	{
		OpenDocument->LoadError = Error;
		FMessageDialog::Open(
			EAppMsgType::Ok,
			Error,
			LOCTEXT("SaveVerseFileFailedTitle", "Unable to Save Verse File"));
		return false;
	}

	OpenDocument->LastKnownDiskBytes = OpenDocument->Session->BuildCurrentFileBytes();
	OpenDocument->LoadError = FText::GetEmpty();
	RebuildDocumentTabs();
	return true;
}

void SVerseVisualEditor::SaveActiveDocumentFromMenu()
{
	SaveDocument(ActiveDocument);
}

void SVerseVisualEditor::SaveAllDocuments()
{
	for (const TSharedPtr<FOpenVerseDocument>& OpenDocument : OpenDocuments)
	{
		if (OpenDocument.IsValid()
			&& OpenDocument->Session.IsValid()
			&& OpenDocument->Session->IsDirty()
			&& !SaveDocument(OpenDocument))
		{
			break;
		}
	}
}

void SVerseVisualEditor::RevertActiveDocument()
{
	if (!HasActiveDocument())
	{
		return;
	}
	if (ActiveDocument->Session->IsDirty()
		&& FMessageDialog::Open(
			EAppMsgType::YesNo,
			FText::Format(
				LOCTEXT("ConfirmRevert", "Revert {0} and discard all unsaved changes?"),
				FText::FromString(FPaths::GetCleanFilename(ActiveDocument->FilePath))),
			LOCTEXT("ConfirmRevertTitle", "Revert Verse File")) != EAppReturnType::Yes)
	{
		return;
	}

	ReloadDocument(ActiveDocument);
	RebuildDocumentTabs();
	RefreshActiveDocument();
}

void SVerseVisualEditor::CloseActiveDocument()
{
	if (ActiveDocument.IsValid())
	{
		CloseDocument(ActiveDocument);
	}
}

bool SVerseVisualEditor::CanSaveActiveDocument() const
{
	return ActiveDocument.IsValid()
		&& ActiveDocument->Session.IsValid()
		&& ActiveDocument->Session->IsDirty();
}

bool SVerseVisualEditor::CanSaveAnyDocument() const
{
	return OpenDocuments.ContainsByPredicate([](const TSharedPtr<FOpenVerseDocument>& OpenDocument)
	{
		return OpenDocument.IsValid()
			&& OpenDocument->Session.IsValid()
			&& OpenDocument->Session->IsDirty();
	});
}

bool SVerseVisualEditor::HasActiveDocument() const
{
	return ActiveDocument.IsValid() && ActiveDocument->Session.IsValid();
}

void SVerseVisualEditor::RebuildDocumentTabs()
{
	if (!DocumentTabBar.IsValid())
	{
		return;
	}

	DocumentTabBar->ClearChildren();
	for (const TSharedPtr<FOpenVerseDocument>& OpenDocument : OpenDocuments)
	{
		const TWeakPtr<FOpenVerseDocument> WeakDocument = OpenDocument;
		DocumentTabBar->AddSlot()
		.AutoWidth()
		.Padding(3.0f, 1.0f)
		[
			SNew(SBorder)
			.OnMouseButtonUp(this, &SVerseVisualEditor::HandleTabMouseButtonUp, OpenDocument)
			.OnMouseDoubleClick(this, &SVerseVisualEditor::HandleTabMouseButtonDoubleClick, OpenDocument)
			.BorderImage(FAppStyle::GetBrush(ActiveDocument == OpenDocument
				? "DetailsView.CategoryTop"
				: "ToolPanel.GroupBorder"))
			.Padding(1.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.OnClicked(this, &SVerseVisualEditor::ActivateDocument, OpenDocument)
					.ToolTipText_Lambda([WeakDocument]()
					{
						const TSharedPtr<FOpenVerseDocument> Document = WeakDocument.Pin();
						return Document.IsValid()
							? FText::Format(
								LOCTEXT("DocumentTabTooltip", "{0}\n{1}"),
								FText::FromString(Document->FilePath),
								GetSourceControlStatus(Document->FilePath))
							: FText::GetEmpty();
					})
					[
					SNew(STextBlock)
						.Text_Lambda([WeakDocument]()
						{
							const TSharedPtr<FOpenVerseDocument> Document = WeakDocument.Pin();
							if (!Document.IsValid())
							{
								return FText::GetEmpty();
							}
							const FString Name = FPaths::GetCleanFilename(Document->FilePath)
								+ (Document->Session.IsValid() && Document->Session->IsDirty() ? TEXT("*") : TEXT(""));
							return FText::FromString(Name);
						})
						.Font_Lambda([WeakDocument]()
						{
							const TSharedPtr<FOpenVerseDocument> Document = WeakDocument.Pin();
							return FCoreStyle::GetDefaultFontStyle(
								Document.IsValid() && Document->Session.IsValid() && Document->Session->IsDirty()
									? (Document->bIsTemporary ? "BoldItalic" : "Bold")
									: (Document.IsValid() && Document->bIsTemporary ? "Italic" : "Regular"),
								10);
						})
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.ContentPadding(FMargin(3.0f, 1.0f))
					.OnClicked(this, &SVerseVisualEditor::CloseDocument, OpenDocument)
					.ToolTipText(LOCTEXT("CloseDocumentTab", "Close"))
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("×")))
					]
				]
			]
		];
	}
}

void SVerseVisualEditor::RefreshActiveDocument()
{
	if (!ActiveDocumentBox.IsValid())
	{
		return;
	}
	CaptureActiveCanvasView();

	if (!ActiveDocument.IsValid())
	{
		RebuildProperties();
		ActiveDocumentBox->SetContent(
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("NoOpenDocument", "Select a Verse file to open it."))
			]);
		return;
	}

	const TWeakPtr<FOpenVerseDocument> WeakDocument = ActiveDocument;
	const TOptional<FVerseTextRange> InitialSelectedRange = ActiveDocument->SelectedTile.IsSet()
		? TOptional<FVerseTextRange>(ActiveDocument->SelectedTile->Range)
		: TOptional<FVerseTextRange>();
	ActiveDocumentBox->SetContent(
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(8.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
					.Text_Lambda([WeakDocument]()
					{
						const TSharedPtr<FOpenVerseDocument> Document = WeakDocument.Pin();
						return Document.IsValid() ? Document->LoadError : FText::GetEmpty();
					})
				.ColorAndOpacity(FLinearColor(1.0f, 0.55f, 0.0f))
				.Visibility_Lambda([WeakDocument]()
				{
					const TSharedPtr<FOpenVerseDocument> Document = WeakDocument.Pin();
						return Document.IsValid() && !Document->LoadError.IsEmpty()
						? EVisibility::Visible
						: EVisibility::Collapsed;
				})
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SAssignNew(
					ActiveDocument->TileCanvas,
					SVerseTileCanvas,
					ActiveDocument->Session.ToSharedRef(),
					ActiveDocument->ViewState,
					InitialSelectedRange,
					FOnVerseTileSelected::CreateSP(
						this,
						&SVerseVisualEditor::HandleTileSelected,
						ActiveDocument),
					FSimpleDelegate::CreateSP(
						this,
						&SVerseVisualEditor::HandleTileSelectionCleared,
						ActiveDocument))
			]
		]);
	RebuildProperties();
}

void SVerseVisualEditor::HandleTileSelected(
	const FVerseVisualTile& Tile,
	TSharedPtr<FOpenVerseDocument> OpenDocument)
{
	if (!OpenDocument.IsValid())
	{
		return;
	}

	OpenDocument->SelectedTile = Tile;
	if (OpenDocument == ActiveDocument)
	{
		OpenDetailsTab();
		RebuildProperties();
	}
}

void SVerseVisualEditor::HandleTileSelectionCleared(TSharedPtr<FOpenVerseDocument> OpenDocument)
{
	if (!OpenDocument.IsValid())
	{
		return;
	}

	OpenDocument->SelectedTile.Reset();
	if (OpenDocument == ActiveDocument)
	{
		RebuildProperties();
	}
}

void SVerseVisualEditor::HandlePropertyFilterChanged(const FText& FilterText)
{
	PropertyFilterText = FilterText.ToString();
	RebuildProperties();
}

void SVerseVisualEditor::HandleRenameCommitted(
	const FText& NewText,
	ETextCommit::Type CommitType,
	TSharedPtr<FOpenVerseDocument> OpenDocument,
	FVerseTextRange NameRange)
{
	if (CommitType == ETextCommit::OnCleared
		|| !OpenDocument.IsValid()
		|| !OpenDocument->Session.IsValid())
	{
		return;
	}

	const FString NewName = NewText.ToString();
	OpenDocument->RenameValidationMessage = ValidateVerseIdentifier(NewName);
	const FString CurrentName = OpenDocument->Session->GetParseSnapshot()
		.GetDocument()->DecodeOriginalRange(NameRange);
	if (CurrentName == NewName)
	{
		if (OpenDocument == ActiveDocument)
		{
			RebuildProperties();
		}
		return;
	}

	const TOptional<FVerseVisualTile> PreviousSelection = OpenDocument->SelectedTile;
	const FTCHARToUTF8 Converted(*NewName, NewName.Len());
	const FUtf8StringView Replacement(
		reinterpret_cast<const UTF8CHAR*>(Converted.Get()),
		Converted.Length());
	FText EditError;
	if (!OpenDocument->Session->Replace(NameRange, Replacement, EditError))
	{
		OpenDocument->RenameValidationMessage = EditError;
		if (OpenDocument == ActiveDocument)
		{
			RebuildProperties();
		}
		return;
	}

	OpenDocument->bIsTemporary = false;
	OpenDocument->SelectedTile.Reset();
	if (PreviousSelection.IsSet())
	{
		const FVerseVisualTile& PreviousTile = PreviousSelection.GetValue();
		if (const FVerseVisualTile* ReplacementTile = OpenDocument->Session->GetTiles().FindByPredicate(
			[&PreviousTile](const FVerseVisualTile& Tile)
			{
				return Tile.Kind == PreviousTile.Kind
					&& Tile.DefinitionKind == PreviousTile.DefinitionKind
					&& Tile.NameRange.IsSet()
					&& Tile.NameRange.BeginByte == PreviousTile.NameRange.BeginByte;
			}))
		{
			OpenDocument->SelectedTile = *ReplacementTile;
		}
	}

	RebuildDocumentTabs();
	if (OpenDocument == ActiveDocument)
	{
		RefreshActiveDocument();
	}
}

void SVerseVisualEditor::HandleDetailsTabClosed(TSharedRef<SDockTab> ClosedTab)
{
	if (DetailsTab == ClosedTab)
	{
		DetailsPanelHost->SetContent(SNullWidget::NullWidget);
		DetailsPanelHost->SetVisibility(EVisibility::Collapsed);
		DetailsTab.Reset();
		PropertyFilter.Reset();
		PropertyRows.Reset();
	}
}

void SVerseVisualEditor::OpenDetailsTab()
{
	if (DetailsTab.IsValid() || !DetailsPanelHost.IsValid())
	{
		return;
	}
	DetailsPanelHost->SetVisibility(EVisibility::Visible);

	TSharedRef<SDockTab> NewDetailsTab =
		SAssignNew(DetailsTab, SDockTab)
		.TabRole(ETabRole::PanelTab)
		.Label(LOCTEXT("DetailsTabLabel", "Details"))
		.CanEverClose(true)
		.OnTabClosed(this, &SVerseVisualEditor::HandleDetailsTabClosed);
	NewDetailsTab->SetTabIcon(FAppStyle::GetBrush("LevelEditor.Tabs.Details"));

	DetailsPanelHost->SetContent(
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			NewDetailsTab
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			BuildDetailsPanel()
		]);

	RebuildProperties();
}

TSharedRef<SWidget> SVerseVisualEditor::BuildDetailsPanel()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(8.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SAssignNew(PropertyFilter, SSearchBox)
				.InitialText(FText::FromString(PropertyFilterText))
				.HintText(LOCTEXT("PropertyFilterHint", "Filter properties"))
				.OnTextChanged(this, &SVerseVisualEditor::HandlePropertyFilterChanged)
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SAssignNew(PropertyRows, SVerticalBox)
				]
			]
		];
}

void SVerseVisualEditor::RebuildProperties()
{
	if (!PropertyRows.IsValid())
	{
		return;
	}

	PropertyRows->ClearChildren();
	if (ActiveDocument.IsValid() && !ActiveDocument->RenameValidationMessage.IsEmpty())
	{
		PropertyRows->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 6.0f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.BorderBackgroundColor(FLinearColor(0.35f, 0.04f, 0.02f, 1.0f))
			.Padding(6.0f)
			[
				SNew(STextBlock)
				.Text(ActiveDocument->RenameValidationMessage)
				.AutoWrapText(true)
				.ColorAndOpacity(FLinearColor(1.0f, 0.55f, 0.2f))
			]
		];
	}
	if (!ActiveDocument.IsValid()
		|| !ActiveDocument->SelectedTile.IsSet()
		|| !ActiveDocument->Session.IsValid())
	{
		return;
	}

	const TArray<FVerseTileProperty> Properties = FVerseTileProperties::Build(
		ActiveDocument->SelectedTile.GetValue(),
		ActiveDocument->Session->GetParseSnapshot());
	int32 VisiblePropertyCount = 0;
	for (const FVerseTileProperty& Property : Properties)
	{
		if (!FVerseTileProperties::MatchesFilter(Property, PropertyFilterText))
		{
			continue;
		}

		++VisiblePropertyCount;
		TSharedRef<SWidget> ValueWidget = SNew(STextBlock)
			.Text(FText::FromString(Property.Value))
			.AutoWrapText(true);
		if (Property.bEditable)
		{
			ValueWidget = SNew(SEditableTextBox)
				.Text(FText::FromString(Property.Value))
				.SelectAllTextWhenFocused(true)
				.OnTextCommitted(
					this,
					&SVerseVisualEditor::HandleRenameCommitted,
					ActiveDocument,
					ActiveDocument->SelectedTile->NameRange);
		}
		PropertyRows->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(6.0f, 4.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(FText::FromString(Property.Name))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 2.0f, 0.0f, 0.0f)
				[
					ValueWidget
				]
			]
		];
	}

	if (VisiblePropertyCount == 0)
	{
		PropertyRows->AddSlot()
		.AutoHeight()
		.Padding(4.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NoMatchingProperties", "No matching properties."))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		];
	}
}

void SVerseVisualEditor::CaptureActiveCanvasView()
{
	if (ActiveDocument.IsValid() && ActiveDocument->TileCanvas.IsValid())
	{
		ActiveDocument->ViewState = ActiveDocument->TileCanvas->GetViewState();
		ActiveDocument->TileCanvas.Reset();
	}
}

void SVerseVisualEditor::LoadSession()
{
	if (!GConfig)
	{
		return;
	}

	int32 TabCount = 0;
	GConfig->GetInt(SessionSection, TEXT("TabCount"), TabCount, GEditorPerProjectIni);

	FString ActiveFilePath;
	GConfig->GetString(SessionSection, TEXT("ActiveFilePath"), ActiveFilePath, GEditorPerProjectIni);
	FPaths::NormalizeFilename(ActiveFilePath);

	for (int32 TabIndex = 0; TabIndex < TabCount; ++TabIndex)
	{
		const FString KeyPrefix = FString::Printf(TEXT("Tab%d."), TabIndex);
		FString FilePath;
		if (!GConfig->GetString(SessionSection, *(KeyPrefix + TEXT("FilePath")), FilePath, GEditorPerProjectIni))
		{
			continue;
		}

		FilePath = FPaths::ConvertRelativePathToFull(FilePath);
		FPaths::NormalizeFilename(FilePath);
		if (!FPaths::FileExists(FilePath))
		{
			continue;
		}

		TSharedPtr<FOpenVerseDocument> RestoredDocument = MakeShared<FOpenVerseDocument>();
		RestoredDocument->FilePath = MoveTemp(FilePath);
		GConfig->GetBool(
			SessionSection,
			*(KeyPrefix + TEXT("Temporary")),
			RestoredDocument->bIsTemporary,
			GEditorPerProjectIni);
		float VerticalScrollOffset = 0.0f;
		if (!GConfig->GetFloat(
			SessionSection,
			*(KeyPrefix + TEXT("VerticalScrollOffset")),
			VerticalScrollOffset,
			GEditorPerProjectIni))
		{
			GConfig->GetFloat(
				SessionSection,
				*(KeyPrefix + TEXT("ScrollOffset")),
				VerticalScrollOffset,
				GEditorPerProjectIni);
		}
		float HorizontalScrollOffset = 0.0f;
		GConfig->GetFloat(
			SessionSection,
			*(KeyPrefix + TEXT("HorizontalScrollOffset")),
			HorizontalScrollOffset,
			GEditorPerProjectIni);
		GConfig->GetFloat(
			SessionSection,
			*(KeyPrefix + TEXT("Zoom")),
			RestoredDocument->ViewState.Zoom,
			GEditorPerProjectIni);
		RestoredDocument->ViewState.ScrollOffset.X = FMath::Max(0.0f, HorizontalScrollOffset);
		RestoredDocument->ViewState.ScrollOffset.Y = FMath::Max(0.0f, VerticalScrollOffset);

		if (!ReloadDocument(RestoredDocument))
		{
			continue;
		}

		OpenDocuments.Add(RestoredDocument);
		if (RestoredDocument->FilePath.Equals(ActiveFilePath, ESearchCase::IgnoreCase))
		{
			ActiveDocument = RestoredDocument;
		}
	}

	if (!ActiveDocument.IsValid() && !OpenDocuments.IsEmpty())
	{
		ActiveDocument = OpenDocuments[0];
	}
}

void SVerseVisualEditor::SaveSession()
{
	CaptureActiveCanvasView();
	if (!GConfig)
	{
		return;
	}

	GConfig->EmptySection(SessionSection, GEditorPerProjectIni);
	GConfig->SetInt(SessionSection, TEXT("TabCount"), OpenDocuments.Num(), GEditorPerProjectIni);
	GConfig->SetString(
		SessionSection,
		TEXT("ActiveFilePath"),
		ActiveDocument.IsValid() ? *ActiveDocument->FilePath : TEXT(""),
		GEditorPerProjectIni);

	for (int32 TabIndex = 0; TabIndex < OpenDocuments.Num(); ++TabIndex)
	{
		const TSharedPtr<FOpenVerseDocument>& OpenDocument = OpenDocuments[TabIndex];
		const FString KeyPrefix = FString::Printf(TEXT("Tab%d."), TabIndex);
		GConfig->SetString(
			SessionSection,
			*(KeyPrefix + TEXT("FilePath")),
			*OpenDocument->FilePath,
			GEditorPerProjectIni);
		GConfig->SetBool(
			SessionSection,
			*(KeyPrefix + TEXT("Temporary")),
			OpenDocument->bIsTemporary,
			GEditorPerProjectIni);
		GConfig->SetFloat(
			SessionSection,
			*(KeyPrefix + TEXT("HorizontalScrollOffset")),
			static_cast<float>(OpenDocument->ViewState.ScrollOffset.X),
			GEditorPerProjectIni);
		GConfig->SetFloat(
			SessionSection,
			*(KeyPrefix + TEXT("VerticalScrollOffset")),
			static_cast<float>(OpenDocument->ViewState.ScrollOffset.Y),
			GEditorPerProjectIni);
		GConfig->SetFloat(
			SessionSection,
			*(KeyPrefix + TEXT("Zoom")),
			OpenDocument->ViewState.Zoom,
			GEditorPerProjectIni);
	}

	GConfig->Flush(false, GEditorPerProjectIni);
}

void SVerseVisualEditor::RegisterDirectoryWatcher()
{
	WatchedDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	FPaths::NormalizeDirectoryName(WatchedDirectory);
	FDirectoryWatcherModule& Module = FModuleManager::LoadModuleChecked<FDirectoryWatcherModule>("DirectoryWatcher");
	if (IDirectoryWatcher* Watcher = Module.Get())
	{
		Watcher->RegisterDirectoryChangedCallback_Handle(
			WatchedDirectory,
			IDirectoryWatcher::FDirectoryChanged::CreateSP(this, &SVerseVisualEditor::HandleDirectoryChanged),
			DirectoryWatcherHandle,
			IDirectoryWatcher::WatchOptions::IncludeDirectoryChanges);
	}
}

void SVerseVisualEditor::UnregisterDirectoryWatcher()
{
	if (!DirectoryWatcherHandle.IsValid())
	{
		return;
	}

	if (FDirectoryWatcherModule* Module = FModuleManager::GetModulePtr<FDirectoryWatcherModule>("DirectoryWatcher"))
	{
		if (IDirectoryWatcher* Watcher = Module->Get())
		{
			Watcher->UnregisterDirectoryChangedCallback_Handle(WatchedDirectory, DirectoryWatcherHandle);
		}
	}
	DirectoryWatcherHandle.Reset();
}

void SVerseVisualEditor::HandleDirectoryChanged(const TArray<FFileChangeData>& FileChanges)
{
	if (IsInGameThread())
	{
		ProcessDirectoryChanges(FileChanges);
		return;
	}

	const TWeakPtr<SVerseVisualEditor> WeakThis = SharedThis(this);
	AsyncTask(ENamedThreads::GameThread, [WeakThis, FileChanges]()
	{
		if (const TSharedPtr<SVerseVisualEditor> Pinned = WeakThis.Pin())
		{
			Pinned->ProcessDirectoryChanges(FileChanges);
		}
	});
}

void SVerseVisualEditor::ProcessDirectoryChanges(TArray<FFileChangeData> FileChanges)
{
	bool bRefreshTree = false;
	bool bRefreshActiveDocument = false;
	bool bRebuildTabs = false;
	for (const FFileChangeData& Change : FileChanges)
	{
		if (Change.Action == FFileChangeData::FCA_RescanRequired)
		{
			bRefreshTree = true;
			continue;
		}

		if (!Change.Filename.EndsWith(TEXT(".verse"), ESearchCase::IgnoreCase))
		{
			if (Change.Action == FFileChangeData::FCA_Added || Change.Action == FFileChangeData::FCA_Removed)
			{
				bRefreshTree = true;
			}
			continue;
		}

		bRefreshTree = true;
		FString ChangedPath = FPaths::ConvertRelativePathToFull(Change.Filename);
		FPaths::NormalizeFilename(ChangedPath);
		const TSharedPtr<FOpenVerseDocument>* Found = OpenDocuments.FindByPredicate(
			[&ChangedPath](const TSharedPtr<FOpenVerseDocument>& Candidate)
			{
				return Candidate->FilePath.Equals(ChangedPath, ESearchCase::IgnoreCase);
			});
		if (!Found)
		{
			continue;
		}

		const TSharedPtr<FOpenVerseDocument> OpenDocument = *Found;
		TArray<uint8> DiskBytes;
		const bool bReadDisk = FFileHelper::LoadFileToArray(DiskBytes, *OpenDocument->FilePath);
		const EVerseExternalChangeAction ExternalChangeAction = DetermineVerseExternalChangeAction(
			bReadDisk && ByteArraysEqual(OpenDocument->LastKnownDiskBytes, DiskBytes),
			OpenDocument->Session.IsValid() && OpenDocument->Session->IsDirty());
		if (ExternalChangeAction == EVerseExternalChangeAction::Ignore)
		{
			continue;
		}

		if (ExternalChangeAction == EVerseExternalChangeAction::PromptReloadOrKeepLocal)
		{
			const EAppReturnType::Type Choice = FMessageDialog::Open(
				EAppMsgType::YesNo,
				FText::Format(
					LOCTEXT(
						"DirtyExternalChange",
						"{0} changed outside Verse Visual Editor while it has local changes.\n\nYes: Reload and discard local changes\nNo: Keep local changes"),
					FText::FromString(FPaths::GetCleanFilename(OpenDocument->FilePath))),
				LOCTEXT("DirtyExternalChangeTitle", "Verse File Changed Externally"));
			if (Choice == EAppReturnType::No)
			{
				if (bReadDisk)
				{
					OpenDocument->LastKnownDiskBytes = MoveTemp(DiskBytes);
				}
				continue;
			}
		}

		ReloadDocument(OpenDocument);
		bRebuildTabs = true;
		bRefreshActiveDocument |= OpenDocument == ActiveDocument;
	}

	if (bRefreshTree)
	{
		RefreshFileTree();
	}
	if (bRefreshActiveDocument)
	{
		RefreshActiveDocument();
	}
	if (bRebuildTabs)
	{
		RebuildDocumentTabs();
	}
}

#undef LOCTEXT_NAMESPACE
