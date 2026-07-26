#include "SVerseVisualEditor.h"

#include "Async/Async.h"
#include "DirectoryWatcherModule.h"
#include "HAL/FileManager.h"
#include "IDirectoryWatcher.h"
#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "ISourceControlState.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "VerseDocument.h"
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
	FText LoadError;
};

namespace
{
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

	RebuildDocumentTabs();
	RefreshActiveDocument();
	RegisterDirectoryWatcher();
}

SVerseVisualEditor::~SVerseVisualEditor()
{
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
	if (Item.IsValid() && !Item->bIsDirectory)
	{
		OpenDocument(Item->FullPath);
	}
}

void SVerseVisualEditor::OpenDocument(const FString& FilePath)
{
	FString NormalizedPath = FPaths::ConvertRelativePathToFull(FilePath);
	FPaths::NormalizeFilename(NormalizedPath);
	if (const TSharedPtr<FOpenVerseDocument>* Existing = OpenDocuments.FindByPredicate(
		[&NormalizedPath](const TSharedPtr<FOpenVerseDocument>& Candidate)
		{
			return Candidate->FilePath.Equals(NormalizedPath, ESearchCase::IgnoreCase);
		}))
	{
		ActiveDocument = *Existing;
		RebuildDocumentTabs();
		RefreshActiveDocument();
		return;
	}

	TSharedPtr<FOpenVerseDocument> NewDocument = MakeShared<FOpenVerseDocument>();
	NewDocument->FilePath = MoveTemp(NormalizedPath);
	if (!ReloadDocument(NewDocument))
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			NewDocument->LoadError,
			LOCTEXT("OpenVerseFileFailedTitle", "Unable to Open Verse File"));
		return;
	}

	OpenDocuments.Add(NewDocument);
	ActiveDocument = MoveTemp(NewDocument);
	RebuildDocumentTabs();
	RefreshActiveDocument();
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

	OpenDocument->Document = MoveTemp(LoadedDocument);
	OpenDocument->LoadError = FText::GetEmpty();
	return true;
}

FReply SVerseVisualEditor::ActivateDocument(TSharedPtr<FOpenVerseDocument> OpenDocument)
{
	ActiveDocument = MoveTemp(OpenDocument);
	RebuildDocumentTabs();
	RefreshActiveDocument();
	return FReply::Handled();
}

FReply SVerseVisualEditor::CloseDocument(TSharedPtr<FOpenVerseDocument> OpenDocument)
{
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
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
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
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SNew(SMultiLineEditableText)
					.Text(FText::FromString(SourceText))
					.IsReadOnly(true)
				]
			]
		]);
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
