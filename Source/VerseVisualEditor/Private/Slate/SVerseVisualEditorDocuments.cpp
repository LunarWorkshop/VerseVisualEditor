#include "Slate/SVerseVisualEditor.h"

#include "Slate/SVerseLiteralEditor.h"

#include "Slate/SVerseFunctionCanvas.h"
#include "Slate/SVerseFunctionGraphLayout.h"
#include "Slate/SVerseFileCanvas.h"
#include "Slate/SVerseTile.h"

#include "Algo/AllOf.h"
#include "Async/Async.h"
#include "DirectoryWatcherModule.h"
#include "DesktopPlatformModule.h"
#include "EdGraph/EdGraphSchema.h"
#include "Editor/UnrealEdEngine.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "FileHelpers.h"
#include "GraphEditorSettings.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "IDirectoryWatcher.h"
#include "IDesktopPlatform.h"
#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "ISourceControlState.h"
#include "ISolarisEditorModule.h"
#include "SolarisLoadCompilerModule.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "SGraphActionMenu.h"
#include "SGraphPalette.h"
#include "UnrealEdGlobals.h"
#include "VerseDocument.h"
#include "VerseParseSnapshotBuilder.h"
#include "Editing/VerseClauseEditing.h"
#include "Editing/VerseIntrinsicPresentation.h"
#include "Document/VerseDocumentSession.h"
#include "Slate/VerseDefinitionIcon.h"
#include "Editing/VerseExpressionActions.h"
#include "Document/VerseExternalChange.h"
#include "VisualModel/VerseFunctionNavigation.h"
#include "Editing/VerseIdentifier.h"
#include "Editing/VerseProvisionalState.h"
#include "Semantics/VerseSemanticCandidates.h"
#include "Semantics/VerseSemanticWorkspace.h"
#include "Editing/VerseSpecifier.h"
#include "VisualModel/VerseTileProperties.h"
#include "VisualModel/VerseVisualTile.h"
#include "VerseVisualEditorSettings.h"
#include "Slate/VerseVisualEditorStyle.h"
#include "Infrastructure/VerseVisualEditorLifetimeDiagnostics.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SSearchableComboBox.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Text/SMultiLineEditableText.h"
#include "Widgets/Text/STextBlock.h"

#include "Slate/SVerseVisualEditorInternal.h"

#define LOCTEXT_NAMESPACE "SVerseVisualEditor"

using namespace VerseVisualEditorPrivate;

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

	bool FindOutlinerItemByRange(
		TConstArrayView<TSharedPtr<FVerseOutlinerItem>> Items,
		FVerseTextRange Range,
		TArray<TSharedPtr<FVerseOutlinerItem>>& Ancestors,
		TSharedPtr<FVerseOutlinerItem>& OutItem)
	{
		for (const TSharedPtr<FVerseOutlinerItem>& Item : Items)
		{
			if (Item->TileRange == Range)
			{
				OutItem = Item;
				return true;
			}
			Ancestors.Add(Item);
			if (FindOutlinerItemByRange(Item->Children, Range, Ancestors, OutItem))
			{
				return true;
			}
			Ancestors.Pop();
		}
		return false;
	}
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

void SVerseVisualEditor::RefreshOutliner()
{
	OutlinerRootItems.Reset();
	if (ActiveDocument.IsValid() && ActiveDocument->Session.IsValid())
	{
		OutlinerRootItems = FVerseOutlinerBuilder::Build(
			ActiveDocument->Session->GetTiles(),
			ActiveDocument->Session->GetParseSnapshot());
	}
	if (OutlinerTree.IsValid())
	{
		OutlinerTree->RequestTreeRefresh();
		SynchronizeOutlinerSelection(
			ActiveDocument.IsValid() && ActiveDocument->SelectedTile.IsSet()
				? TOptional<FVerseTextRange>(ActiveDocument->SelectedTile->Range)
				: TOptional<FVerseTextRange>());
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

TSharedRef<ITableRow> SVerseVisualEditor::GenerateOutlinerRow(
	TSharedPtr<FVerseOutlinerItem> Item,
	const TSharedRef<STableViewBase>& OwnerTable) const
{
	return SNew(STableRow<TSharedPtr<FVerseOutlinerItem>>, OwnerTable)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, 5.0f, 0.0f)
		[
			SNew(SImage)
			.Image(FAppStyle::GetBrush(GetVerseDefinitionIconName(Item->DefinitionKind)))
			.DesiredSizeOverride(FVector2D(16.0f, 16.0f))
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(FText::FromString(Item->Label))
			.ToolTipText(FText::FromName(Item->DefinitionKind))
		]
	];
}

void SVerseVisualEditor::GetOutlinerChildren(
	TSharedPtr<FVerseOutlinerItem> Item,
	TArray<TSharedPtr<FVerseOutlinerItem>>& OutChildren) const
{
	OutChildren = Item->Children;
}

void SVerseVisualEditor::HandleOutlinerSelectionChanged(
	TSharedPtr<FVerseOutlinerItem> Item,
	ESelectInfo::Type SelectInfo)
{
	if (bSynchronizingOutlinerSelection || !ActiveDocument.IsValid())
	{
		return;
	}

	TGuardValue<bool> SynchronizingGuard(bSynchronizingOutlinerSelection, true);
	if (!Item.IsValid())
	{
		if (ActiveDocument->FileCanvas.IsValid())
		{
			ActiveDocument->FileCanvas->ClearTileSelection();
		}
		else
		{
			HandleTileSelectionCleared(ActiveDocument);
		}
		return;
	}

	if (!ActiveDocument->FileCanvas.IsValid())
	{
		ActiveDocument->ActiveFunctionTabIndex = INDEX_NONE;
		RefreshActiveDocument();
	}
	if (const FVerseVisualTile* Tile = FindTileByRange(
		ActiveDocument->Session->GetTiles(),
		Item->TileRange))
	{
		if (ActiveDocument->FileCanvas.IsValid())
		{
			ActiveDocument->FileCanvas->SelectTile(*Tile);
		}
		else
		{
			HandleTileSelected(*Tile, ActiveDocument);
		}
	}
}

void SVerseVisualEditor::SynchronizeOutlinerSelection(TOptional<FVerseTextRange> TileRange)
{
	if (!OutlinerTree.IsValid())
	{
		return;
	}

	TGuardValue<bool> SynchronizingGuard(bSynchronizingOutlinerSelection, true);
	if (!TileRange.IsSet())
	{
		OutlinerTree->ClearSelection();
		return;
	}

	TArray<TSharedPtr<FVerseOutlinerItem>> Ancestors;
	TSharedPtr<FVerseOutlinerItem> Item;
	if (!FindOutlinerItemByRange(OutlinerRootItems, TileRange.GetValue(), Ancestors, Item))
	{
		OutlinerTree->ClearSelection();
		return;
	}
	for (const TSharedPtr<FVerseOutlinerItem>& Ancestor : Ancestors)
	{
		OutlinerTree->SetItemExpansion(Ancestor, true);
	}
	OutlinerTree->SetSelection(Item, ESelectInfo::Direct);
	OutlinerTree->RequestScrollIntoView(Item);
}

void SVerseVisualEditor::HandleOutlinerItemDoubleClicked(TSharedPtr<FVerseOutlinerItem> Item)
{
	if (!Item.IsValid()
		|| !ActiveDocument.IsValid()
		|| !ActiveDocument->Session.IsValid())
	{
		return;
	}

	if (const FVerseVisualTile* Tile = FindTileByRange(
		ActiveDocument->Session->GetTiles(),
		Item->TileRange))
	{
		if (Tile->DefinitionKind == VerseSyntaxKind::Function)
		{
			if (ActiveDocument->FileCanvas.IsValid())
			{
				ActiveDocument->FileCanvas->FocusTile(*Tile);
			}
			else
			{
				HandleTileSelected(*Tile, ActiveDocument);
			}
			OpenFunctionView(*Tile, ActiveDocument);
			return;
		}

		if (!ActiveDocument->FileCanvas.IsValid())
		{
			ActiveDocument->ActiveFunctionTabIndex = INDEX_NONE;
			RefreshActiveDocument();
		}
		if (ActiveDocument->FileCanvas.IsValid())
		{
			ActiveDocument->FileCanvas->FocusTile(*Tile);
		}
	}
}

void SVerseVisualEditor::OpenFunctionView(
	const FVerseVisualTile& FunctionTile,
	TSharedPtr<FOpenVerseDocument> OpenDocument)
{
	if (!OpenDocument.IsValid()
		|| !OpenDocument->Session.IsValid()
		|| FunctionTile.DefinitionKind != VerseSyntaxKind::Function)
	{
		return;
	}

	const TArray<FVerseFunctionNavigationItem> Items = FVerseFunctionNavigationBuilder::Build(
		OpenDocument->Session->GetTiles(),
		OpenDocument->Session->GetParseSnapshot());
	const FVerseFunctionNavigationItem* Item = Items.FindByPredicate(
		[&FunctionTile](const FVerseFunctionNavigationItem& Candidate)
		{
			return Candidate.FunctionRange == FunctionTile.Range;
		});
	if (!Item)
	{
		return;
	}
	if (OpenDocument == ActiveDocument)
	{
		CaptureActiveCanvasView();
	}

	const int32 ExistingIndex = OpenDocument->FunctionTabs.IndexOfByPredicate(
		[Item](const FOpenVerseFunctionTab& Tab)
		{
			return Tab.ScopePath == Item->ScopePath;
		});
	if (ExistingIndex != INDEX_NONE)
	{
		OpenDocument->ActiveFunctionTabIndex = ExistingIndex;
	}
	else
	{
		FOpenVerseFunctionTab& Tab = OpenDocument->FunctionTabs.AddDefaulted_GetRef();
		Tab.Name = Item->Name;
		Tab.ScopePath = Item->ScopePath;
		Tab.FunctionRange = Item->FunctionRange;
		Tab.DeclarationRange = Item->DeclarationRange;
		Tab.BodyRange = Item->BodyRange;
		Tab.ReturnTypeRange = Item->ReturnTypeRange;
		Tab.Parameters = Item->Parameters;
		Tab.GraphTiles = Item->GraphTiles;
		Tab.FirstDeclarationLine = Item->FirstDeclarationLine;
		Tab.LastDeclarationLine = Item->LastDeclarationLine;
		OpenDocument->ActiveFunctionTabIndex = OpenDocument->FunctionTabs.Num() - 1;
	}
	if (OpenDocument == ActiveDocument)
	{
		RefreshActiveDocument();
	}
}

FReply SVerseVisualEditor::ActivateGlobalView(TSharedPtr<FOpenVerseDocument> OpenDocument)
{
	FinishExpressionSearch();
	if (OpenDocument.IsValid())
	{
		if (OpenDocument == ActiveDocument)
		{
			CaptureActiveCanvasView();
		}
		OpenDocument->ActiveFunctionTabIndex = INDEX_NONE;
		if (OpenDocument == ActiveDocument)
		{
			RefreshActiveDocument();
		}
	}
	return FReply::Handled();
}

FReply SVerseVisualEditor::ActivateFunctionView(
	TSharedPtr<FOpenVerseDocument> OpenDocument,
	int32 FunctionTabIndex)
{
	FinishExpressionSearch();
	if (OpenDocument.IsValid() && OpenDocument->FunctionTabs.IsValidIndex(FunctionTabIndex))
	{
		if (OpenDocument == ActiveDocument)
		{
			CaptureActiveCanvasView();
		}
		OpenDocument->ActiveFunctionTabIndex = FunctionTabIndex;
		if (OpenDocument == ActiveDocument)
		{
			RefreshActiveDocument();
		}
	}
	return FReply::Handled();
}

FReply SVerseVisualEditor::CloseFunctionView(
	TSharedPtr<FOpenVerseDocument> OpenDocument,
	int32 FunctionTabIndex)
{
	FinishExpressionSearch();
	if (!OpenDocument.IsValid() || !OpenDocument->FunctionTabs.IsValidIndex(FunctionTabIndex))
	{
		return FReply::Handled();
	}
	if (OpenDocument == ActiveDocument)
	{
		CaptureActiveCanvasView();
	}

	OpenDocument->FunctionTabs.RemoveAt(FunctionTabIndex);
	if (OpenDocument->ActiveFunctionTabIndex == FunctionTabIndex)
	{
		OpenDocument->ActiveFunctionTabIndex = INDEX_NONE;
	}
	else if (OpenDocument->ActiveFunctionTabIndex > FunctionTabIndex)
	{
		--OpenDocument->ActiveFunctionTabIndex;
	}
	if (OpenDocument == ActiveDocument)
	{
		RefreshActiveDocument();
	}
	return FReply::Handled();
}

TSharedRef<SWidget> SVerseVisualEditor::BuildScopeBreadcrumb(
	TSharedPtr<FOpenVerseDocument> OpenDocument) const
{
	TArray<FString> ScopePath;
	if (OpenDocument.IsValid())
	{
		ScopePath = VerseVisualEditor::BuildVerseModulePath(OpenDocument->FilePath, SourceRoots);
		if (OpenDocument->FunctionTabs.IsValidIndex(OpenDocument->ActiveFunctionTabIndex))
		{
			ScopePath.Append(OpenDocument->FunctionTabs[OpenDocument->ActiveFunctionTabIndex].ScopePath);
		}
		else if (OpenDocument->Session.IsValid() && OpenDocument->SelectedTile.IsSet())
		{
			const FVerseVisualTile& SelectedTile = OpenDocument->SelectedTile.GetValue();
			if (SelectedTile.DefinitionKind == VerseSyntaxKind::Module
				|| SelectedTile.DefinitionKind == VerseSyntaxKind::Class
				|| SelectedTile.DefinitionKind == VerseSyntaxKind::Struct
				|| SelectedTile.DefinitionKind == VerseSyntaxKind::Interface)
			{
				TArray<FString> SelectedPath;
				if (FVerseFunctionNavigationBuilder::FindDefinitionPath(
					OpenDocument->Session->GetTiles(),
					OpenDocument->Session->GetParseSnapshot(),
					SelectedTile.Range,
					SelectedPath))
				{
					ScopePath.Append(MoveTemp(SelectedPath));
				}
			}
		}
	}

	TSharedRef<SHorizontalBox> Breadcrumb = SNew(SHorizontalBox);
	for (int32 Index = 0; Index < ScopePath.Num(); ++Index)
	{
		if (Index > 0)
		{
			Breadcrumb->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(6.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT(">")))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			];
		}
		Breadcrumb->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(FText::FromString(ScopePath[Index]))
			.Font(FCoreStyle::GetDefaultFontStyle(
				Index == ScopePath.Num() - 1 ? "Bold" : "Regular",
				9))
		];
	}

	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(FMargin(8.0f, 5.0f))
		[
			Breadcrumb
		];
}

TSharedRef<SWidget> SVerseVisualEditor::BuildFunctionTabBar(
	TSharedPtr<FOpenVerseDocument> OpenDocument)
{
	TSharedRef<SHorizontalBox> Tabs = SNew(SHorizontalBox);
	const TArray<FString> FileModulePath = VerseVisualEditor::BuildVerseModulePath(
		OpenDocument->FilePath,
		SourceRoots);
	const FText FileModuleTabText = FText::FromString(FString::Printf(
		TEXT("%s >"),
		FileModulePath.IsEmpty() ? TEXT("File") : *FileModulePath.Last()));
	Tabs->AddSlot()
	.AutoWidth()
	.Padding(3.0f, 2.0f)
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush(OpenDocument->ActiveFunctionTabIndex == INDEX_NONE
			? "DetailsView.CategoryTop"
			: "ToolPanel.GroupBorder"))
		.Padding(1.0f)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.OnClicked(this, &SVerseVisualEditor::ActivateGlobalView, OpenDocument)
			.ToolTipText(LOCTEXT("GlobalViewTooltip", "File-level and structural view"))
			[
				SNew(STextBlock)
				.Text(FileModuleTabText)
			]
		]
	];

	for (int32 Index = 0; Index < OpenDocument->FunctionTabs.Num(); ++Index)
	{
		const FOpenVerseFunctionTab& FunctionTab = OpenDocument->FunctionTabs[Index];
		Tabs->AddSlot()
		.AutoWidth()
		.Padding(3.0f, 2.0f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush(OpenDocument->ActiveFunctionTabIndex == Index
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
					.OnClicked(this, &SVerseVisualEditor::ActivateFunctionView, OpenDocument, Index)
					.ToolTipText(FText::FromString(FString::Join(FunctionTab.ScopePath, TEXT(" > "))))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0.0f, 0.0f, 5.0f, 0.0f)
						[
							SNew(SImage)
							.Image(FAppStyle::GetBrush("GraphEditor.Function_16x"))
							.DesiredSizeOverride(FVector2D(16.0f, 16.0f))
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(FText::FromString(FunctionTab.Name))
						]
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.ContentPadding(FMargin(3.0f, 1.0f))
					.OnClicked(this, &SVerseVisualEditor::CloseFunctionView, OpenDocument, Index)
					.ToolTipText(LOCTEXT("CloseFunctionView", "Close function tab"))
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("\u00d7")))
					]
				]
			]
		];
	}
	return Tabs;
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
	FinishExpressionSearch();
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
	VerseVisualEditorLifetimeDiagnostics::Update(
		NewDocument.Get(),
		TEXT("OpenDocument"),
		*NewDocument->FilePath);
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
	QueueSemanticAnalysis(false);
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
		OpenDocument->ProvisionalTiles.Reset();
	}
	else
	{
		OpenDocument->Session = MakeShared<FVerseDocumentSession>(LoadedDocument.ToSharedRef());
	}
	OpenDocument->LastKnownDiskBytes = MoveTemp(DiskBytes);
	OpenDocument->LoadError = FText::GetEmpty();
	OpenDocument->PropertyValidationMessage = FText::GetEmpty();
	OpenDocument->PendingRenameText.Reset();
	OpenDocument->PendingSpecifierText.Reset();
	OpenDocument->SelectedTile.Reset();
	InvalidateCompilationResult(OpenDocument);
	if (CompilationMode == EVerseCompilationMode::Continuous)
	{
		QueueCompilation(OpenDocument, true);
	}
	if (OpenDocuments.Contains(OpenDocument))
	{
		QueueSemanticAnalysis(false);
	}
	return true;
}

FReply SVerseVisualEditor::ActivateDocument(TSharedPtr<FOpenVerseDocument> OpenDocument)
{
	FinishExpressionSearch();
	CaptureActiveCanvasView();
	ActiveDocument = MoveTemp(OpenDocument);
	RebuildDocumentTabs();
	RefreshActiveDocument();
	RevealActiveDocumentInTree();
	if (HasLocalCompileDiagnosticsForActiveDocument())
	{
		bLocalCompilePanelOpen = true;
	}
	else
	{
		bLocalCompilePanelOpen = false;
	}
	return FReply::Handled();
}

FReply SVerseVisualEditor::CloseDocument(TSharedPtr<FOpenVerseDocument> OpenDocument)
{
	FinishExpressionSearch();
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
	QueueSemanticAnalysis(false);
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
		if (CompilationMode == EVerseCompilationMode::OnSave)
		{
			QueueCompilation(OpenDocument, false);
		}
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
	if (CompilationMode == EVerseCompilationMode::OnSave)
	{
		QueueCompilation(OpenDocument, false);
	}
	return true;
}

void SVerseVisualEditor::SaveActiveDocumentFromMenu()
{
	SaveDocument(ActiveDocument);
}

void SVerseVisualEditor::SaveActiveDocumentAs()
{
	if (!HasActiveDocument())
	{
		return;
	}

	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		return;
	}

	TArray<FString> SelectedFiles;
	if (!DesktopPlatform->SaveFileDialog(
		FSlateApplication::Get().FindBestParentWindowHandleForDialogs(AsShared()),
		LOCTEXT("SaveVerseFileAsTitle", "Save Verse File As").ToString(),
		FPaths::GetPath(ActiveDocument->FilePath),
		FPaths::GetCleanFilename(ActiveDocument->FilePath),
		TEXT("Verse source files (*.verse)|*.verse"),
		EFileDialogFlags::None,
		SelectedFiles)
		|| SelectedFiles.IsEmpty())
	{
		return;
	}

	FString NewFilePath = FPaths::ConvertRelativePathToFull(SelectedFiles[0]);
	if (FPaths::GetExtension(NewFilePath).IsEmpty())
	{
		NewFilePath += TEXT(".verse");
		if (FPaths::FileExists(NewFilePath)
			&& FMessageDialog::Open(
				EAppMsgType::YesNo,
				FText::Format(
					LOCTEXT("ConfirmSaveVerseFileAsOverwrite", "{0} already exists. Replace it?"),
					FText::FromString(NewFilePath)),
				LOCTEXT("ConfirmSaveVerseFileAsOverwriteTitle", "Confirm Save As")) != EAppReturnType::Yes)
		{
			return;
		}
	}
	FPaths::NormalizeFilename(NewFilePath);

	if (OpenDocuments.ContainsByPredicate([&](const TSharedPtr<FOpenVerseDocument>& OpenDocument)
		{
			return OpenDocument.IsValid()
				&& OpenDocument != ActiveDocument
				&& OpenDocument->FilePath.Equals(NewFilePath, ESearchCase::IgnoreCase);
		}))
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			LOCTEXT("SaveVerseFileAsAlreadyOpen", "That Verse file is already open in another tab."),
			LOCTEXT("SaveVerseFileAsAlreadyOpenTitle", "Unable to Save As"));
		return;
	}

	FText Error;
	if (!ActiveDocument->Session->SaveToFile(NewFilePath, Error))
	{
		ActiveDocument->LoadError = Error;
		FMessageDialog::Open(
			EAppMsgType::Ok,
			Error,
			LOCTEXT("SaveVerseFileAsFailedTitle", "Unable to Save Verse File As"));
		return;
	}

	ActiveDocument->FilePath = MoveTemp(NewFilePath);
	VerseVisualEditorLifetimeDiagnostics::Update(
		ActiveDocument.Get(),
		TEXT("OpenDocument"),
		*ActiveDocument->FilePath);
	ActiveDocument->LastKnownDiskBytes = ActiveDocument->Session->BuildCurrentFileBytes();
	ActiveDocument->LoadError = FText::GetEmpty();
	ActiveDocument->bIsTemporary = false;
	QueueSemanticAnalysis(false);
	RefreshFileTree();
	RebuildDocumentTabs();
	RevealActiveDocumentInTree();
	if (CompilationMode == EVerseCompilationMode::OnSave)
	{
		QueueCompilation(ActiveDocument, false);
	}
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

void SVerseVisualEditor::SaveAllFromMainFrame()
{
	FEditorFileUtils::SaveDirtyPackages(
		false,
		true,
		true,
		false,
		false,
		false);
	SaveAllDocuments();
}

bool SVerseVisualEditor::CanSaveAllFromMainFrame() const
{
	return FSlateApplication::Get().IsNormalExecution()
		&& (!GUnrealEd || !GUnrealEd->GetPackageAutoSaver().IsAutoSaving());
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


void SVerseVisualEditor::CaptureActiveCanvasView()
{
	if (!ActiveDocument.IsValid())
	{
		return;
	}
	if (ActiveDocument->FunctionTabs.IsValidIndex(ActiveDocument->ActiveFunctionTabIndex))
	{
		FOpenVerseFunctionTab& FunctionTab =
			ActiveDocument->FunctionTabs[ActiveDocument->ActiveFunctionTabIndex];
		if (FunctionTab.FunctionCanvas.IsValid())
		{
			FunctionTab.ViewState = FunctionTab.FunctionCanvas->GetViewState();
			FunctionTab.bHasViewState = true;
			FunctionTab.FunctionCanvas.Reset();
		}
	}
	else if (ActiveDocument->FileCanvas.IsValid())
	{
		ActiveDocument->ViewState = ActiveDocument->FileCanvas->GetViewState();
		ActiveDocument->FileCanvas.Reset();
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
	CompilationMode = GetDefault<UVerseVisualEditorSettings>()->CompilationMode;
	FunctionGraphPresentation =
		GetDefault<UVerseVisualEditorSettings>()->FunctionGraphPresentation;
	FString StoredPreference;
	const FString SettingsSection = UVerseVisualEditorSettings::StaticClass()->GetPathName();
	if (!GConfig->GetString(
		*SettingsSection,
		TEXT("CompilationMode"),
		StoredPreference,
		GEditorPerProjectIni))
	{
		int32 LegacyCompilationMode = INDEX_NONE;
		if (GConfig->GetInt(
			SessionSection,
			TEXT("CompilationMode"),
			LegacyCompilationMode,
			GEditorPerProjectIni)
			&& LegacyCompilationMode >= static_cast<int32>(EVerseCompilationMode::Continuous)
			&& LegacyCompilationMode <= static_cast<int32>(EVerseCompilationMode::Manual))
		{
			CompilationMode = static_cast<EVerseCompilationMode>(LegacyCompilationMode);
			UVerseVisualEditorSettings* Settings = GetMutableDefault<UVerseVisualEditorSettings>();
			Settings->CompilationMode = CompilationMode;
			Settings->SaveConfig();
		}
	}

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
		VerseVisualEditorLifetimeDiagnostics::Update(
			RestoredDocument.Get(),
			TEXT("OpenDocument"),
			*RestoredDocument->FilePath);
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
