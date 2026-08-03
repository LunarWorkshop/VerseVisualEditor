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
	bool FindVisualTileParent(
		TConstArrayView<FVerseVisualTile> Tiles,
		FVerseVisualTileId ChildId,
		const FVerseVisualTile*& OutParent,
		int32& OutChildIndex)
	{
		for (const FVerseVisualTile& Tile : Tiles)
		{
			for (int32 Index = 0; Index < Tile.Children.Num(); ++Index)
			{
				if (Tile.Children[Index].Id == ChildId)
				{
					OutParent = &Tile;
					OutChildIndex = Index;
					return true;
				}
			}
			if (FindVisualTileParent(Tile.Children, ChildId, OutParent, OutChildIndex))
			{
				return true;
			}
		}
		return false;
	}

	void ReportRetainedSnapshot(
		const TSharedPtr<const FVerseSemanticSnapshot>& Snapshot,
		const FOpenVerseDocument& Document,
		TSet<const FVerseSemanticSnapshot*>& ReportedSnapshots)
	{
		if (!Snapshot.IsValid() || ReportedSnapshots.Contains(Snapshot.Get()))
		{
			return;
		}
		ReportedSnapshots.Add(Snapshot.Get());
		const FString Label = FString::Printf(
			TEXT("retained-by-document=%s shared-refs=%d"),
			*Document.FilePath,
			Snapshot.GetSharedReferenceCount());
		VerseVisualEditorLifetimeDiagnostics::Update(
			Snapshot.Get(),
			TEXT("SemanticSnapshot"),
			*Label);
		VerseVisualEditorLifetimeDiagnostics::Event(
			TEXT("SemanticSnapshot.RetainedByDocument"),
			Snapshot.Get(),
			&Document);
	}

	void ReportExpressionSnapshots(
		const FVerseVisualExpressionDescriptor& Expression,
		const FOpenVerseDocument& Document,
		TSet<const FVerseSemanticSnapshot*>& ReportedSnapshots)
	{
		ReportRetainedSnapshot(Expression.SemanticSnapshot, Document, ReportedSnapshots);
		for (const FVerseVisualExpressionDescriptor& Operand : Expression.Operands)
		{
			ReportExpressionSnapshots(Operand, Document, ReportedSnapshots);
		}
	}

	void ReportTileSnapshots(
		TConstArrayView<FVerseVisualTile> Tiles,
		const FOpenVerseDocument& Document,
		TSet<const FVerseSemanticSnapshot*>& ReportedSnapshots)
	{
		for (const FVerseVisualTile& Tile : Tiles)
		{
			ReportRetainedSnapshot(Tile.SemanticSnapshot, Document, ReportedSnapshots);
			for (const FVerseVisualClauseItemDescriptor& Item : Tile.BodyClause.Items)
			{
				ReportExpressionSnapshots(Item.Expression, Document, ReportedSnapshots);
			}
			ReportTileSnapshots(Tile.Children, Document, ReportedSnapshots);
		}
	}
}

void SVerseVisualEditor::Construct(const FArguments& InArgs)
{
	VerseVisualEditorLifetimeDiagnostics::Track(
		this,
		TEXT("EditorWidget"));
	SemanticWorkspace = MakeUnique<FVerseSemanticWorkspace>();
	// Capture whatever semantic program Solaris already owns. Even if a later
	// private overlay fails, its compiled dependencies remain useful for search.
	SemanticWorkspace->RefreshCompiledBaseline(
		TConstArrayView<FVerseSemanticDocumentInput>());
	RefreshFileTree();

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			BuildToolbar()
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SSplitter)
			+ SSplitter::Slot()
			.Value(0.22f)
			[
				SNew(SSplitter)
				.Orientation(Orient_Vertical)
				+ SSplitter::Slot()
				.Value(0.55f)
				.MinSize(100.0f)
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
							.Text(LOCTEXT("ExplorerHeading", "Explorer"))
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
				.Value(0.45f)
				.MinSize(100.0f)
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
							.Text(LOCTEXT("OutlinerHeading", "Outliner"))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
						]
						+ SVerticalBox::Slot()
						.FillHeight(1.0f)
						[
							SNew(SOverlay)
							+ SOverlay::Slot()
							[
								SAssignNew(OutlinerTree, STreeView<TSharedPtr<FVerseOutlinerItem>>)
								.TreeItemsSource(&OutlinerRootItems)
								.OnGenerateRow(this, &SVerseVisualEditor::GenerateOutlinerRow)
								.OnGetChildren(this, &SVerseVisualEditor::GetOutlinerChildren)
								.OnSelectionChanged(this, &SVerseVisualEditor::HandleOutlinerSelectionChanged)
								.OnMouseButtonDoubleClick(this, &SVerseVisualEditor::HandleOutlinerItemDoubleClicked)
								.SelectionMode(ESelectionMode::Single)
							]
							+ SOverlay::Slot()
							.HAlign(HAlign_Center)
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("EmptyOutliner", "No definitions in the active file."))
								.AutoWrapText(true)
								.Justification(ETextJustify::Center)
								.Visibility_Lambda([this]()
								{
									return OutlinerRootItems.IsEmpty()
										? EVisibility::Visible
										: EVisibility::Collapsed;
								})
							]
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
					SNew(SSplitter)
					.Orientation(Orient_Vertical)
					+ SSplitter::Slot()
					.Value(0.82f)
					.MinSize(100.0f)
					[
						SAssignNew(ActiveDocumentBox, SBox)
					]
					+ SSplitter::Slot()
					.Value(0.18f)
					.MinSize(64.0f)
					[
						SAssignNew(LocalCompilePanel, SBorder)
						.Visibility_Lambda([this]()
						{
							return bLocalCompilePanelOpen
								? EVisibility::Visible
								: EVisibility::Collapsed;
						})
						.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
						.Padding(4.0f)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot()
								.FillWidth(1.0f)
								.VAlign(VAlign_Center)
								.Padding(4.0f, 1.0f)
								[
									SNew(STextBlock)
									.Text(LOCTEXT("LocalCompilePanelTitle", "Local Compile Errors"))
									.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
								]
								+ SHorizontalBox::Slot()
								.AutoWidth()
								[
									SNew(SButton)
									.ButtonStyle(FAppStyle::Get(), "SimpleButton")
									.ContentPadding(FMargin(5.0f, 1.0f))
									.ToolTipText(LOCTEXT("CloseLocalCompilePanelTooltip", "Close Local Compile Errors"))
									.OnClicked(this, &SVerseVisualEditor::CloseLocalCompilePanel)
									[
										SNew(STextBlock)
										.Text(FText::FromString(TEXT("\x00D7")))
										.Font(FCoreStyle::GetDefaultFontStyle("Regular", 14))
									]
								]
							]
							+ SVerticalBox::Slot()
							.FillHeight(1.0f)
							.Padding(4.0f, 2.0f)
							[
								SNew(SScrollBox)
								+ SScrollBox::Slot()
								[
									SNew(SMultiLineEditableText)
									.Text(this, &SVerseVisualEditor::GetLocalCompileDiagnosticsText)
									.IsReadOnly(true)
									.AutoWrapText(true)
								]
							]
						]
				]
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
	ISolarisLoadCompilerModule& CompilerModule = ISolarisLoadCompilerModule::Get();
	ProjectBuildStartedHandle = CompilerModule.OnBuildStarted().AddSP(
		this,
		&SVerseVisualEditor::HandleProjectBuildStarted);
	ProjectBuildCompleteHandle = CompilerModule.OnBuildComplete().AddSP(
		this,
		&SVerseVisualEditor::HandleProjectBuildComplete);
	QueueSemanticAnalysis(false);
	if (CompilationMode == EVerseCompilationMode::Continuous)
	{
		for (const TSharedPtr<FOpenVerseDocument>& OpenDocument : OpenDocuments)
		{
			QueueCompilation(OpenDocument, true);
		}
	}
}

SVerseVisualEditor::~SVerseVisualEditor()
{
	VerseVisualEditorLifetimeDiagnostics::Event(
		TEXT("EditorWidget.Destroy.Begin"),
		this,
		SemanticWorkspace.Get());
	TSet<const FVerseSemanticSnapshot*> ReportedSnapshots;
	for (const TSharedPtr<FOpenVerseDocument>& Document : OpenDocuments)
	{
		if (!Document.IsValid())
		{
			continue;
		}
		if (Document->Session.IsValid())
		{
			ReportTileSnapshots(
				Document->Session->GetTiles(),
				*Document,
				ReportedSnapshots);
		}
		for (const FOpenVerseFunctionTab& FunctionTab : Document->FunctionTabs)
		{
			ReportTileSnapshots(
				FunctionTab.GraphTiles,
				*Document,
				ReportedSnapshots);
		}
	}
	VerseVisualEditorLifetimeDiagnostics::Dump(
		TEXT("SVerseVisualEditor destructor begin"));
	if (ISolarisLoadCompilerModule::IsLoaded())
	{
		ISolarisLoadCompilerModule& CompilerModule = ISolarisLoadCompilerModule::Get();
		CompilerModule.OnBuildStarted().Remove(ProjectBuildStartedHandle);
		CompilerModule.OnBuildComplete().Remove(ProjectBuildCompleteHandle);
		ProjectBuildStartedHandle.Reset();
		ProjectBuildCompleteHandle.Reset();
	}
	SaveSession();
	UnregisterDirectoryWatcher();
	VerseVisualEditorLifetimeDiagnostics::Event(
		TEXT("EditorWidget.Destroy.BodyEnd"),
		this,
		SemanticWorkspace.Get());
	VerseVisualEditorLifetimeDiagnostics::Untrack(
		this,
		TEXT("EditorWidget"));
}


FReply SVerseVisualEditor::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Delete
		&& ActiveDocument.IsValid()
		&& ActiveDocument->SelectedTile.IsSet())
	{
		const FVerseVisualTile Selected = ActiveDocument->SelectedTile.GetValue();
		if (Selected.EditableClause.IsSet()
			&& Selected.ClauseItemIndex != INDEX_NONE)
		{
			FText Error;
			FVerseTextRange ProvisionalReplacementRange;
			if (!FVerseClauseEditing::DeleteExpression(
				*ActiveDocument->Session,
				Selected.EditableClause.GetValue(),
				Selected.ClauseItemIndex,
				Error,
				&ProvisionalReplacementRange))
			{
				ActiveDocument->LoadError = Error;
				bLocalCompilePanelOpen = true;
				return FReply::Handled();
			}
			if (ProvisionalReplacementRange.IsSet())
			{
				ActiveDocument->ProvisionalTiles.Add(
					ProvisionalReplacementRange,
					ActiveDocument->Session->GetParseSnapshot().GetDocument()
						->GetOriginalUtf8View());
			}
			ActiveDocument->SelectedTile.Reset();
			ActiveDocument->LoadError = FText::GetEmpty();
			ActiveDocument->bIsTemporary = false;
			QueueSemanticAnalysis(true);
			InvalidateCompilationResult(ActiveDocument);
			if (CompilationMode == EVerseCompilationMode::Continuous)
			{
				QueueCompilation(ActiveDocument, true);
			}
			ReconcileFunctionTabs(
				*ActiveDocument,
				FindExactSemanticSnapshot(SemanticWorkspace.Get(), *ActiveDocument));
			RebuildDocumentTabs();
			RefreshActiveDocument();
			return FReply::Handled();
		}

		// A nested value expression cannot be removed as raw text without making
		// its parent syntactically incomplete. Delete replaces that one operand
		// with a source-safe default; the compiler may still report a semantic
		// mismatch for types which have no literal default.
		const FVerseVisualTile* Parent = nullptr;
		int32 ChildIndex = INDEX_NONE;
		for (const FOpenVerseFunctionTab& Tab : ActiveDocument->FunctionTabs)
		{
			if (FindVisualTileParent(Tab.GraphTiles, Selected.Id, Parent, ChildIndex))
			{
				break;
			}
		}
		if (Parent != nullptr
			&& ChildIndex != INDEX_NONE
			&& Selected.Range.IsSet())
		{
			const FVerseVisualSocket* ParentInput = Parent->FindSocket({
				EVerseVisualSocketDirection::Input,
				EVerseVisualSocketRole::Value,
				ChildIndex});
			FString ExpectedType;
			if (Parent->Kind == EVerseVisualTileKind::Definition
				&& Parent->TypeRange.IsSet())
			{
				ExpectedType = ActiveDocument->Session->GetParseSnapshot()
					.GetDocument()->DecodeOriginalRange(Parent->TypeRange);
			}
			else if (ParentInput != nullptr)
			{
				ExpectedType = !ParentInput->SemanticTypeName.IsEmpty()
					? ParentInput->SemanticTypeName
					: ParentInput->TypeRange.IsSet()
						? ActiveDocument->Session->GetParseSnapshot().GetDocument()
							->DecodeOriginalRange(ParentInput->TypeRange)
						: ParentInput->IntrinsicTypeName.ToString();
			}
			const FString Replacement =
				GetDefaultVerseLiteralSourceForType(ExpectedType).Get(TEXT("0"));
			const FTCHARToUTF8 ReplacementUtf8(*Replacement);
			FText Error;
			if (!ActiveDocument->Session->Replace(
				Selected.Range,
				FUtf8StringView(
					reinterpret_cast<const UTF8CHAR*>(ReplacementUtf8.Get()),
					ReplacementUtf8.Length()),
				Error))
			{
				ActiveDocument->LoadError = Error;
				bLocalCompilePanelOpen = true;
				return FReply::Handled();
			}
			ActiveDocument->SelectedTile.Reset();
			ActiveDocument->LoadError = FText::GetEmpty();
			ActiveDocument->bIsTemporary = false;
			QueueSemanticAnalysis(true);
			InvalidateCompilationResult(ActiveDocument);
			if (CompilationMode == EVerseCompilationMode::Continuous)
			{
				QueueCompilation(ActiveDocument, true);
			}
			ReconcileFunctionTabs(
				*ActiveDocument,
				FindExactSemanticSnapshot(SemanticWorkspace.Get(), *ActiveDocument));
			RebuildDocumentTabs();
			RefreshActiveDocument();
			return FReply::Handled();
		}
	}
	if (InKeyEvent.IsControlDown() && InKeyEvent.GetKey() == EKeys::S)
	{
		if (InKeyEvent.IsAltDown())
		{
			SaveActiveDocumentAs();
			return FReply::Handled();
		}
		if (InKeyEvent.IsShiftDown())
		{
			SaveAllFromMainFrame();
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


#undef LOCTEXT_NAMESPACE
