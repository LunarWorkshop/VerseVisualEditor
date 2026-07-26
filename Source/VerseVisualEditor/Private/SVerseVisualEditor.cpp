#include "SVerseVisualEditor.h"

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
#include "VerseParseSnapshotBuilder.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Text/SMultiLineEditableText.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SVerseVisualEditor"

struct FOpenVerseDocument
{
	FString FilePath;
	TSharedPtr<FVerseDocument> Document;
	TOptional<FVerseParseSnapshot> ParseSnapshot;
	FText LoadError;
	bool bIsTemporary = false;
	float ScrollOffset = 0.0f;
	TSharedPtr<SScrollBox> ScrollBox;
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

	bool MatchesOriginalFile(const FVerseDocument& Document, TConstArrayView<uint8> DiskBytes)
	{
		constexpr uint8 Utf8Bom[] = {0xEF, 0xBB, 0xBF};
		const FUtf8StringView OriginalText = Document.GetOriginalUtf8View();
		const int32 BomLength = Document.HasUtf8Bom() ? UE_ARRAY_COUNT(Utf8Bom) : 0;
		if (DiskBytes.Num() != BomLength + OriginalText.Len())
		{
			return false;
		}

		if (BomLength > 0 && FMemory::Memcmp(DiskBytes.GetData(), Utf8Bom, BomLength) != 0)
		{
			return false;
		}

		return OriginalText.IsEmpty()
			|| FMemory::Memcmp(
				DiskBytes.GetData() + BomLength,
				OriginalText.GetData(),
				OriginalText.Len()) == 0;
	}
}

void SVerseVisualEditor::Construct(const FArguments& InArgs)
{
	RefreshFileTree();

	ChildSlot
	[
		SNew(SSplitter)
		+ SSplitter::Slot()
		.Value(0.25f)
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
		.Value(0.75f)
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
	];

	LoadSession();
	RebuildDocumentTabs();
	RefreshActiveDocument();
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
		CaptureActiveScrollOffset();
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
	CaptureActiveScrollOffset();
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
	FText Error;
	TSharedPtr<FVerseDocument> LoadedDocument = FVerseDocument::LoadFromFile(OpenDocument->FilePath, Error);
	if (!LoadedDocument.IsValid())
	{
		OpenDocument->LoadError = Error;
		return false;
	}

	OpenDocument->ParseSnapshot = FVerseParseSnapshotBuilder::Build(LoadedDocument.ToSharedRef());
	OpenDocument->Document = MoveTemp(LoadedDocument);
	OpenDocument->LoadError = FText::GetEmpty();
	return true;
}

FReply SVerseVisualEditor::ActivateDocument(TSharedPtr<FOpenVerseDocument> OpenDocument)
{
	CaptureActiveScrollOffset();
	ActiveDocument = MoveTemp(OpenDocument);
	RebuildDocumentTabs();
	RefreshActiveDocument();
	RevealActiveDocumentInTree();
	return FReply::Handled();
}

FReply SVerseVisualEditor::CloseDocument(TSharedPtr<FOpenVerseDocument> OpenDocument)
{
	if (ActiveDocument == OpenDocument)
	{
		CaptureActiveScrollOffset();
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
						.Text(FText::FromString(FPaths::GetCleanFilename(OpenDocument->FilePath)))
						.Font_Lambda([WeakDocument]()
						{
							const TSharedPtr<FOpenVerseDocument> Document = WeakDocument.Pin();
							return FCoreStyle::GetDefaultFontStyle(
								Document.IsValid() && Document->bIsTemporary ? "Italic" : "Regular",
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
	CaptureActiveScrollOffset();

	if (!ActiveDocument.IsValid())
	{
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
	const FString SourceText = ActiveDocument->Document.IsValid()
		? ActiveDocument->Document->DecodeOriginalRange(ActiveDocument->Document->GetWholeOriginalRange())
		: FString();
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
				.Text(FText::FromString(ActiveDocument->FilePath))
				.ToolTipText(FText::FromString(ActiveDocument->FilePath))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 2.0f, 0.0f, 6.0f)
			[
				SNew(STextBlock)
				.Text_Lambda([WeakDocument]()
				{
					const TSharedPtr<FOpenVerseDocument> Document = WeakDocument.Pin();
					return Document.IsValid() ? GetSourceControlStatus(Document->FilePath) : FText::GetEmpty();
				})
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]
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
				SAssignNew(ActiveDocument->ScrollBox, SScrollBox)
				+ SScrollBox::Slot()
				[
					SNew(SMultiLineEditableText)
					.Text(FText::FromString(SourceText))
					.IsReadOnly(true)
				]
			]
		]);
	ActiveDocument->ScrollBox->SetScrollOffset(ActiveDocument->ScrollOffset);
}

void SVerseVisualEditor::CaptureActiveScrollOffset()
{
	if (ActiveDocument.IsValid() && ActiveDocument->ScrollBox.IsValid())
	{
		ActiveDocument->ScrollOffset = ActiveDocument->ScrollBox->GetScrollOffset();
		ActiveDocument->ScrollBox.Reset();
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
		GConfig->GetFloat(
			SessionSection,
			*(KeyPrefix + TEXT("ScrollOffset")),
			RestoredDocument->ScrollOffset,
			GEditorPerProjectIni);
		RestoredDocument->ScrollOffset = FMath::Max(0.0f, RestoredDocument->ScrollOffset);

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
	CaptureActiveScrollOffset();
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
			*(KeyPrefix + TEXT("ScrollOffset")),
			OpenDocument->ScrollOffset,
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
		if (OpenDocument->Document.IsValid()
			&& FFileHelper::LoadFileToArray(DiskBytes, *OpenDocument->FilePath)
			&& MatchesOriginalFile(*OpenDocument->Document, DiskBytes))
		{
			continue;
		}

		ReloadDocument(OpenDocument);
	}

	if (bRefreshTree)
	{
		RefreshFileTree();
	}
	RebuildDocumentTabs();
	RefreshActiveDocument();
}

#undef LOCTEXT_NAMESPACE
