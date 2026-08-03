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
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SExpandableArea.h"
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
#include "Editing/VerseFormattingEdit.h"
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
	const FVerseVisualTile* FindVisualTileById(
		TConstArrayView<FVerseVisualTile> Tiles,
		FVerseVisualTileId Id)
	{
		for (const FVerseVisualTile& Tile : Tiles)
		{
			if (Tile.Id == Id)
			{
				return &Tile;
			}
			if (const FVerseVisualTile* Child = FindVisualTileById(Tile.Children, Id))
			{
				return Child;
			}
		}
		return nullptr;
	}

	FString FormatOperatorSignature(const FVerseVisualTile& Tile)
	{
		TArray<FString> Inputs;
		for (const FVerseVisualSocket& Socket : Tile.GetValueInputs())
		{
			Inputs.Add(!Socket.SemanticTypeName.IsEmpty()
				? Socket.SemanticTypeName
				: Socket.IntrinsicTypeName.ToString());
		}
		const EVerseIntrinsicCallableForm Form = Inputs.Num() == 1
			? EVerseIntrinsicCallableForm::PrefixOperator
			: EVerseIntrinsicCallableForm::InfixOperator;
		const FVerseIntrinsicPresentationDescriptor* Presentation =
			FindVerseIntrinsicOperatorPresentation(Form, Tile.OperatorSpelling);
		if (Presentation != nullptr && Presentation->bOmitResultInSignaturePicker)
		{
			return FString::Join(Inputs, TEXT(" x "));
		}
		const FString Result = !Tile.SemanticTypeName.IsEmpty()
			? Tile.SemanticTypeName : Tile.IntrinsicTypeName.ToString();
		return FString::Printf(
			TEXT("%s -> %s"), *FString::Join(Inputs, TEXT(" x ")), *Result);
	}

	const FVerseVisualTile* FindReplacementTile(
		TConstArrayView<FVerseVisualTile> Tiles,
		const FVerseVisualTile& PreviousTile)
	{
		for (const FVerseVisualTile& Tile : Tiles)
		{
			if ((Tile.Kind == PreviousTile.Kind
				&& Tile.DefinitionKind == PreviousTile.DefinitionKind
				&& Tile.NameRange.IsSet()
				&& Tile.NameRange.BeginByte == PreviousTile.NameRange.BeginByte)
				|| (Tile.Kind == PreviousTile.Kind
					&& Tile.ExpressionKind == PreviousTile.ExpressionKind
					&& Tile.ControlKind == PreviousTile.ControlKind
					&& Tile.OperatorSpelling == PreviousTile.OperatorSpelling
					&& Tile.Range.BeginByte == PreviousTile.Range.BeginByte))
			{
				return &Tile;
			}
			if (const FVerseVisualTile* Nested = FindReplacementTile(Tile.Children, PreviousTile))
			{
				return Nested;
			}
		}
		return nullptr;
	}
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
	OpenDocument->PropertyValidationMessage = FText::GetEmpty();
	OpenDocument->PendingRenameText.Reset();
	OpenDocument->PendingSpecifierText.Reset();
	if (OpenDocument == ActiveDocument)
	{
		SynchronizeOutlinerSelection(Tile.Range);
		if (ScopeBreadcrumbBox.IsValid())
		{
			ScopeBreadcrumbBox->SetContent(BuildScopeBreadcrumb(OpenDocument));
		}
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
	OpenDocument->PropertyValidationMessage = FText::GetEmpty();
	OpenDocument->PendingRenameText.Reset();
	OpenDocument->PendingSpecifierText.Reset();
	if (OpenDocument == ActiveDocument)
	{
		SynchronizeOutlinerSelection({});
		if (ScopeBreadcrumbBox.IsValid())
		{
			ScopeBreadcrumbBox->SetContent(BuildScopeBreadcrumb(OpenDocument));
		}
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
	OpenDocument->PropertyValidationMessage = ValidateVerseIdentifier(NewName);
	if (!OpenDocument->PropertyValidationMessage.IsEmpty())
	{
		OpenDocument->PendingRenameText = NewName;
		if (OpenDocument == ActiveDocument)
		{
			RebuildProperties();
		}
		return;
	}
	OpenDocument->PendingRenameText.Reset();
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
	FText EditError;
	if (!TryReplaceWithValidatedVerseIdentifier(
		*OpenDocument->Session,
		NameRange,
		NewName,
		EditError))
	{
		OpenDocument->PropertyValidationMessage = EditError;
		if (OpenDocument == ActiveDocument)
		{
			RebuildProperties();
		}
		return;
	}
	OpenDocument->ProvisionalTiles.AdoptContaining(NameRange);

	OpenDocument->bIsTemporary = false;
	QueueSemanticAnalysis(true);
	InvalidateCompilationResult(OpenDocument);
	if (CompilationMode == EVerseCompilationMode::Continuous)
	{
		QueueCompilation(OpenDocument, true);
	}
	OpenDocument->SelectedTile.Reset();
	if (PreviousSelection.IsSet())
	{
		const FVerseVisualTile& PreviousTile = PreviousSelection.GetValue();
		if (const FVerseVisualTile* ReplacementTile = FindReplacementTile(
			OpenDocument->Session->GetTiles(),
			PreviousTile))
		{
			OpenDocument->SelectedTile = *ReplacementTile;
		}
		else
		{
			ReconcileFunctionTabs(
				*OpenDocument,
				FindExactSemanticSnapshot(SemanticWorkspace.Get(), *OpenDocument));
			for (const FOpenVerseFunctionTab& Tab : OpenDocument->FunctionTabs)
			{
				if (const FVerseVisualTile* FunctionGraphReplacement = FindReplacementTile(
					Tab.GraphTiles, PreviousTile))
				{
					OpenDocument->SelectedTile = *FunctionGraphReplacement;
					break;
				}
			}
		}
	}

	RebuildDocumentTabs();
	if (OpenDocument == ActiveDocument)
	{
		RefreshActiveDocument();
	}
}

void SVerseVisualEditor::HandleTypeSelected(
	TSharedPtr<FString> NewType,
	ESelectInfo::Type SelectInfo,
	TSharedPtr<FOpenVerseDocument> OpenDocument,
	FVerseVisualTile DefinitionTile)
{
	if (!NewType.IsValid()
		|| SelectInfo == ESelectInfo::Direct
		|| !OpenDocument.IsValid()
		|| !OpenDocument->Session.IsValid())
	{
		return;
	}
	const FString CurrentType = OpenDocument->Session->GetParseSnapshot()
		.GetDocument()->DecodeOriginalRange(DefinitionTile.TypeRange);
	if (CurrentType == *NewType)
	{
		return;
	}

	const TOptional<FVerseVisualTile> PreviousSelection = OpenDocument->SelectedTile;
	FText EditError;
	TArray<FVerseDocumentEdit> Edits;
	Edits.Add({DefinitionTile.TypeRange, FUtf8String(*NewType)});
	TOptional<FString> AutomaticallySelectedOperatorSignature;
	FString AutomaticallySelectedOperatorSpelling;
	int32 AutomaticallySelectedOperatorBeginByte = INDEX_NONE;

	// An inline literal is part of the definition's editable value. Keep its
	// syntax in lockstep with a primitive annotation change so the rebuilt tile
	// immediately exposes the correct editor (for example float 0.0, not int 0).
	if (DefinitionTile.GetValueInputs().Num() == 1
		&& DefinitionTile.GetValueInputs()[0].InlineLiteralRange.IsSet())
	{
		if (const TOptional<FString> DefaultSource =
			GetDefaultVerseLiteralSourceForType(*NewType))
		{
			Edits.Add({
				DefinitionTile.GetValueInputs()[0].InlineLiteralRange,
				FUtf8String(DefaultSource.GetValue())});
		}
	}
	else if (DefinitionTile.Children.Num() == 1
		&& IsVerseOperatorExpression(DefinitionTile.Children[0].ExpressionKind))
	{
		const FVerseVisualTile& Operator = DefinitionTile.Children[0];
		const bool bEveryOperandIsInline = !Operator.GetValueInputs().IsEmpty()
			&& Algo::AllOf(
				Operator.GetValueInputs(),
				[](const FVerseVisualSocket& Input)
				{
					return Input.InlineLiteralRange.IsSet();
				});
		if (bEveryOperandIsInline)
		{
			FVerseVisualSocket ProspectiveConsumer;
			ProspectiveConsumer.SemanticTypeName = *NewType;
			const TArray<const FVerseVisualSocket*> OutputConsumers = {
				&ProspectiveConsumer};
			const TArray<FVerseOperatorSignature> CompatibleSignatures =
				FVerseSemanticCandidateProvider::BuildOperatorSignatures(
					SemanticWorkspace
						? SemanticWorkspace->GetCandidateSnapshots()
						: TArray<TSharedPtr<const FVerseSemanticSnapshot>>(),
					OpenDocument->FilePath,
					Operator.Range.BeginByte,
					*OpenDocument->Session->GetParseSnapshot().GetDocument(),
					Operator.OperatorSpelling,
					Operator.GetValueInputs().Num(),
					{},
					OutputConsumers);
			const FString CurrentSignature = FormatOperatorSignature(Operator);
			const FVerseOperatorSignature* SelectedSignature =
				CompatibleSignatures.FindByPredicate(
					[&CurrentSignature](const FVerseOperatorSignature& Signature)
					{
						return Signature.DisplayText == CurrentSignature;
					});
			if (SelectedSignature == nullptr && !CompatibleSignatures.IsEmpty())
			{
				SelectedSignature = &CompatibleSignatures[0];
			}
			if (SelectedSignature != nullptr
				&& SelectedSignature->DisplayText != CurrentSignature
				&& SelectedSignature->OperandTypeNames.Num()
					== Operator.GetValueInputs().Num())
			{
				for (int32 Index = 0; Index < Operator.GetValueInputs().Num(); ++Index)
				{
					const TOptional<FString> Default = GetDefaultVerseLiteralSourceForType(
						SelectedSignature->OperandTypeNames[Index]);
					Edits.Add({
						Operator.GetValueInputs()[Index].InlineLiteralRange,
						FUtf8String(Default.Get(TEXT("0"))) });
				}
				AutomaticallySelectedOperatorSignature = SelectedSignature->DisplayText;
				AutomaticallySelectedOperatorSpelling = Operator.OperatorSpelling;
				const FTCHARToUTF8 NewTypeUtf8(**NewType);
				AutomaticallySelectedOperatorBeginByte = Operator.Range.BeginByte
					+ NewTypeUtf8.Length() - DefinitionTile.TypeRange.NumBytes;
			}
		}
	}

	if (!OpenDocument->Session->ReplaceMany(Edits, EditError))
	{
		OpenDocument->PropertyValidationMessage = EditError;
		if (OpenDocument == ActiveDocument)
		{
			RebuildProperties();
		}
		return;
	}
	if (AutomaticallySelectedOperatorSignature.IsSet())
	{
		OpenDocument->PendingOperatorSignatureText =
			AutomaticallySelectedOperatorSignature.GetValue();
		OpenDocument->PendingOperatorSpelling = AutomaticallySelectedOperatorSpelling;
		OpenDocument->PendingOperatorSignatureBeginByte =
			AutomaticallySelectedOperatorBeginByte;
	}
	OpenDocument->ProvisionalTiles.AdoptContaining(DefinitionTile.Range);

	OpenDocument->PropertyValidationMessage = FText::GetEmpty();
	OpenDocument->bIsTemporary = false;
	QueueSemanticAnalysis(true);
	InvalidateCompilationResult(OpenDocument);
	if (CompilationMode == EVerseCompilationMode::Continuous)
	{
		QueueCompilation(OpenDocument, true);
	}
	OpenDocument->SelectedTile.Reset();
	if (PreviousSelection.IsSet())
	{
		const FVerseVisualTile& PreviousTile = PreviousSelection.GetValue();
		if (const FVerseVisualTile* ReplacementTile = FindReplacementTile(
			OpenDocument->Session->GetTiles(), PreviousTile))
		{
			OpenDocument->SelectedTile = *ReplacementTile;
		}
		else
		{
			ReconcileFunctionTabs(
				*OpenDocument,
				FindExactSemanticSnapshot(SemanticWorkspace.Get(), *OpenDocument));
			for (const FOpenVerseFunctionTab& Tab : OpenDocument->FunctionTabs)
			{
				if (const FVerseVisualTile* FunctionGraphReplacement = FindReplacementTile(
					Tab.GraphTiles, PreviousTile))
				{
					OpenDocument->SelectedTile = *FunctionGraphReplacement;
					break;
				}
			}
		}
	}
	RebuildDocumentTabs();
	if (OpenDocument == ActiveDocument)
	{
		RefreshActiveDocument();
	}
}

void SVerseVisualEditor::HandleOperatorSignatureSelected(
	TSharedPtr<FString> NewSignature,
	ESelectInfo::Type SelectInfo,
	TSharedPtr<FOpenVerseDocument> OpenDocument,
	FVerseVisualTile OperatorTile)
{
	if (!NewSignature.IsValid()
		|| SelectInfo == ESelectInfo::Direct
		|| !OpenDocument.IsValid()
		|| !OpenDocument->Session.IsValid())
	{
		return;
	}
	const FVerseOperatorSignature* Signature = OperatorSignatures.FindByPredicate(
		[&NewSignature](const FVerseOperatorSignature& Candidate)
		{
			return Candidate.DisplayText == *NewSignature;
		});
	if (Signature == nullptr
		|| Signature->OperandTypeNames.Num() != OperatorTile.GetValueInputs().Num())
	{
		return;
	}

	TArray<FVerseDocumentEdit> Edits;
	for (int32 Index = 0; Index < OperatorTile.GetValueInputs().Num(); ++Index)
	{
		const FVerseVisualSocket& Input = OperatorTile.GetValueInputs()[Index];
		if (!Input.InlineLiteralRange.IsSet())
		{
			// A connected operand constrains the selected signature and retains its
			// source. Only the remaining inline defaults are rewritten.
			continue;
		}
		const TOptional<FString> TypedDefault =
			GetDefaultVerseLiteralSourceForType(Signature->OperandTypeNames[Index]);
		Edits.Add({
			Input.InlineLiteralRange,
			FUtf8String(TypedDefault.Get(TEXT("0"))) });
	}
	if (Edits.IsEmpty())
	{
		return;
	}

	const TOptional<FVerseVisualTile> PreviousSelection = OpenDocument->SelectedTile;
	FText EditError;
	if (!OpenDocument->Session->ReplaceMany(Edits, EditError))
	{
		OpenDocument->PropertyValidationMessage = EditError;
		RebuildProperties();
		return;
	}
	OpenDocument->PendingOperatorSignatureText = Signature->DisplayText;
	OpenDocument->PendingOperatorSpelling = OperatorTile.OperatorSpelling;
	OpenDocument->PendingOperatorSignatureBeginByte = OperatorTile.Range.BeginByte;
	OpenDocument->ProvisionalTiles.AdoptContaining(OperatorTile.Range);
	OpenDocument->PropertyValidationMessage = FText::GetEmpty();
	OpenDocument->bIsTemporary = false;
	QueueSemanticAnalysis(true);
	InvalidateCompilationResult(OpenDocument);
	if (CompilationMode == EVerseCompilationMode::Continuous)
	{
		QueueCompilation(OpenDocument, true);
	}
	OpenDocument->SelectedTile.Reset();
	if (PreviousSelection.IsSet())
	{
		ReconcileFunctionTabs(
			*OpenDocument,
			FindExactSemanticSnapshot(SemanticWorkspace.Get(), *OpenDocument));
		for (const FOpenVerseFunctionTab& Tab : OpenDocument->FunctionTabs)
		{
			if (const FVerseVisualTile* Replacement = FindReplacementTile(
				Tab.GraphTiles, PreviousSelection.GetValue()))
			{
				OpenDocument->SelectedTile = *Replacement;
				break;
			}
		}
	}
	RebuildDocumentTabs();
	if (OpenDocument == ActiveDocument)
	{
		RefreshActiveDocument();
	}
}

void SVerseVisualEditor::HandleSyntaxControlSelected(
	TSharedPtr<FString> NewValue,
	ESelectInfo::Type SelectInfo,
	TSharedPtr<FOpenVerseDocument> OpenDocument,
	FVerseVisualTile Tile,
	EVerseSyntaxControlKind Control,
	int32 ControlRegionIndex)
{
	if (!NewValue.IsValid()
		|| SelectInfo == ESelectInfo::Direct
		|| !OpenDocument.IsValid()
		|| !OpenDocument->Session.IsValid())
	{
		return;
	}

	FText EditError;
	if (!FVerseFormattingEditService::Apply(
		*OpenDocument->Session, Tile, Control, *NewValue, EditError, ControlRegionIndex))
	{
		OpenDocument->PropertyValidationMessage = EditError;
		if (OpenDocument == ActiveDocument)
		{
			RebuildProperties();
		}
		return;
	}

	OpenDocument->ProvisionalTiles.AdoptContaining(Tile.Range);
	OpenDocument->PropertyValidationMessage = FText::GetEmpty();
	OpenDocument->bIsTemporary = false;
	QueueSemanticAnalysis(true);
	InvalidateCompilationResult(OpenDocument);
	if (CompilationMode == EVerseCompilationMode::Continuous)
	{
		QueueCompilation(OpenDocument, true);
	}

	OpenDocument->SelectedTile.Reset();
	if (const FVerseVisualTile* Replacement = FindReplacementTile(
		OpenDocument->Session->GetTiles(), Tile))
	{
		OpenDocument->SelectedTile = *Replacement;
	}
	else
	{
		ReconcileFunctionTabs(
			*OpenDocument,
			FindExactSemanticSnapshot(SemanticWorkspace.Get(), *OpenDocument));
		for (const FOpenVerseFunctionTab& Tab : OpenDocument->FunctionTabs)
		{
			if (const FVerseVisualTile* GraphReplacement = FindReplacementTile(
				Tab.GraphTiles, Tile))
			{
				OpenDocument->SelectedTile = *GraphReplacement;
				break;
			}
		}
	}
	RebuildDocumentTabs();
	if (OpenDocument == ActiveDocument)
	{
		RefreshActiveDocument();
	}
}

void SVerseVisualEditor::HandleSpecifiersCommitted(
	const FText& NewText,
	ETextCommit::Type CommitType,
	TSharedPtr<FOpenVerseDocument> OpenDocument,
	FVerseVisualTile Tile,
	bool bEffects)
{
	if (CommitType == ETextCommit::OnCleared
		|| !OpenDocument.IsValid()
		|| !OpenDocument->Session.IsValid())
	{
		return;
	}

	FVerseTextRange ReplacementRange(
		OpenDocument->Session->GetRevision(),
		FVerseByteRange::FromBounds(Tile.NameRange.EndByte(), Tile.NameRange.EndByte()));
	const TArray<FVerseTextRange>& SpecifierRanges = bEffects
		? Tile.FunctionEffectSpecifierRanges
		: Tile.FunctionAccessSpecifierRanges;
	if (!SpecifierRanges.IsEmpty())
	{
		const FUtf8StringView Source = OpenDocument->Session->GetParseSnapshot()
			.GetDocument()->GetOriginalUtf8View();
		const int32 Begin = SpecifierRanges[0].BeginByte - 1;
		const int32 End = SpecifierRanges.Last().EndByte() + 1;
		if (Begin < 0
			|| End > Source.Len()
			|| Source[Begin] != static_cast<UTF8CHAR>('<')
			|| Source[End - 1] != static_cast<UTF8CHAR>('>'))
		{
			OpenDocument->PropertyValidationMessage = LOCTEXT(
				"InvalidExistingSpecifierRange",
				"The existing specifier source range is invalid. Source was not changed.");
			RebuildProperties();
			return;
		}
		ReplacementRange = FVerseTextRange(
			OpenDocument->Session->GetRevision(),
			FVerseByteRange::FromBounds(Begin, End));
	}

	const FString ProposedText = NewText.ToString();
	FString NormalizedText;
	OpenDocument->PropertyValidationMessage = NormalizeVerseSpecifiers(ProposedText, NormalizedText);
	if (!OpenDocument->PropertyValidationMessage.IsEmpty())
	{
		OpenDocument->PendingSpecifierText = ProposedText;
		if (OpenDocument == ActiveDocument)
		{
			RebuildProperties();
		}
		return;
	}
	OpenDocument->PendingSpecifierText.Reset();

	FText EditError;
	if (!TryReplaceWithValidatedVerseSpecifiers(
		*OpenDocument->Session,
		ReplacementRange,
		NormalizedText,
		EditError))
	{
		OpenDocument->PropertyValidationMessage = EditError;
		if (OpenDocument == ActiveDocument)
		{
			RebuildProperties();
		}
		return;
	}
	OpenDocument->ProvisionalTiles.AdoptContaining(Tile.Range);

	OpenDocument->bIsTemporary = false;
	QueueSemanticAnalysis(true);
	InvalidateCompilationResult(OpenDocument);
	if (CompilationMode == EVerseCompilationMode::Continuous)
	{
		QueueCompilation(OpenDocument, true);
	}
	OpenDocument->SelectedTile.Reset();
	if (const FVerseVisualTile* ReplacementTile = FindReplacementTile(
		OpenDocument->Session->GetTiles(),
		Tile))
	{
		OpenDocument->SelectedTile = *ReplacementTile;
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
	if (ActiveDocument.IsValid() && !ActiveDocument->PropertyValidationMessage.IsEmpty())
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
				.Text(ActiveDocument->PropertyValidationMessage)
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
	TypeOptions.Reset();
	OperatorSignatureOptions.Reset();
	OperatorSignatures.Reset();
	SyntaxOptionSets.Reset();
	int32 VisiblePropertyCount = 0;
	int32 VisibleSyntaxPropertyCount = 0;
	TSharedRef<SVerticalBox> SyntaxRows = SNew(SVerticalBox);
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
			if (Property.EditKind == EVerseTilePropertyEditKind::Type)
			{
				const TArray<FString> VisibleTypes =
					FVerseSemanticCandidateProvider::BuildVisibleTypeNames(
						SemanticWorkspace
							? SemanticWorkspace->GetCandidateSnapshots()
							: TArray<TSharedPtr<const FVerseSemanticSnapshot>>(),
						ActiveDocument->FilePath,
						ActiveDocument->SelectedTile->Range.BeginByte,
						*ActiveDocument->Session->GetParseSnapshot().GetDocument());
				for (const FString& TypeName : VisibleTypes)
				{
					TypeOptions.Add(MakeShared<FString>(TypeName));
				}
				const TSharedPtr<FString>* SelectedOption = TypeOptions.FindByPredicate(
					[&Property](const TSharedPtr<FString>& Option)
					{
						return Option.IsValid() && *Option == Property.Value;
					});
				const TSharedPtr<FString> InitiallySelected = SelectedOption != nullptr
					? *SelectedOption
					: nullptr;
				ValueWidget = SNew(SSearchableComboBox)
					.OptionsSource(&TypeOptions)
					.InitiallySelectedItem(InitiallySelected)
					.OnGenerateWidget_Lambda([](TSharedPtr<FString> Option)
					{
						return SNew(STextBlock)
							.Text(Option.IsValid()
								? FText::FromString(*Option)
								: FText::GetEmpty());
					})
					.OnSelectionChanged(
						this,
						&SVerseVisualEditor::HandleTypeSelected,
						ActiveDocument,
						ActiveDocument->SelectedTile.GetValue())
					[
						SNew(STextBlock).Text(FText::FromString(Property.Value))
					];
			}
			else if (Property.EditKind == EVerseTilePropertyEditKind::OperatorSignature)
			{
				const FVerseVisualTile& SelectedTile =
					ActiveDocument->SelectedTile.GetValue();
				FVerseOperatorConnectionConstraints Constraints;
				for (const FOpenVerseFunctionTab& Tab : ActiveDocument->FunctionTabs)
				{
					if (FindVisualTileById(Tab.GraphTiles, SelectedTile.Id) == nullptr)
					{
						continue;
					}
					Constraints =
						FVerseVisualTileBuilder::BuildOperatorConnectionConstraints(
							Tab.GraphTiles, SelectedTile,
							*ActiveDocument->Session->GetParseSnapshot().GetDocument());
					break;
				}
				const TArray<const FVerseVisualSocket*> ConnectedOperands =
					Constraints.GetConnectedOperandPointers();
				const TArray<const FVerseVisualSocket*> OutputConsumers =
					Constraints.GetOutputConsumerPointers();
				int32 ConnectedOperandCount = 0;
				for (const FVerseVisualSocket* Socket : ConnectedOperands)
				{
					ConnectedOperandCount += Socket != nullptr ? 1 : 0;
				}
				OperatorSignatures =
					FVerseSemanticCandidateProvider::BuildOperatorSignatures(
						SemanticWorkspace
							? SemanticWorkspace->GetCandidateSnapshots()
							: TArray<TSharedPtr<const FVerseSemanticSnapshot>>(),
						ActiveDocument->FilePath,
						SelectedTile.Range.BeginByte,
						*ActiveDocument->Session->GetParseSnapshot().GetDocument(),
						SelectedTile.OperatorSpelling,
						SelectedTile.GetValueInputs().Num(),
						ConnectedOperands,
						OutputConsumers);
				for (const FVerseOperatorSignature& Signature : OperatorSignatures)
				{
					OperatorSignatureOptions.Add(
						MakeShared<FString>(Signature.DisplayText));
				}
				const FString InferredSignature = FormatOperatorSignature(SelectedTile);
				if (ActiveDocument->PendingOperatorSignatureText.IsSet()
					&& ActiveDocument->PendingOperatorSpelling == SelectedTile.OperatorSpelling
					&& ActiveDocument->PendingOperatorSignatureBeginByte
						== SelectedTile.Range.BeginByte
					&& ActiveDocument->PendingOperatorSignatureText.GetValue()
						== InferredSignature)
				{
					ActiveDocument->PendingOperatorSignatureText.Reset();
				}
				const FString CurrentSignature =
					ActiveDocument->PendingOperatorSignatureText.IsSet()
					&& ActiveDocument->PendingOperatorSpelling == SelectedTile.OperatorSpelling
					&& ActiveDocument->PendingOperatorSignatureBeginByte
						== SelectedTile.Range.BeginByte
						? ActiveDocument->PendingOperatorSignatureText.GetValue()
						: InferredSignature;
				const TSharedPtr<FString>* SelectedOption =
					OperatorSignatureOptions.FindByPredicate(
						[&CurrentSignature](const TSharedPtr<FString>& Option)
						{
							return Option.IsValid()
								&& *Option == CurrentSignature;
						});
				const bool bHasAlternativeSignature =
					OperatorSignatureOptions.Num() > 1
					|| (OperatorSignatureOptions.Num() == 1
						&& OperatorSignatureOptions[0].IsValid()
						&& *OperatorSignatureOptions[0] != CurrentSignature);
				ValueWidget = SNew(SSearchableComboBox)
					.IsEnabled(bHasAlternativeSignature
						&& ConnectedOperandCount < ConnectedOperands.Num())
					.OptionsSource(&OperatorSignatureOptions)
					.InitiallySelectedItem(SelectedOption ? *SelectedOption : nullptr)
					.OnGenerateWidget_Lambda([](TSharedPtr<FString> Option)
					{
						return SNew(STextBlock).Text(Option.IsValid()
							? FText::FromString(*Option) : FText::GetEmpty());
					})
					.OnSelectionChanged(
						this,
						&SVerseVisualEditor::HandleOperatorSignatureSelected,
						ActiveDocument,
						SelectedTile)
					[
						SNew(STextBlock).Text(FText::FromString(CurrentSignature))
					];
			}
			else if (Property.EditKind == EVerseTilePropertyEditKind::Syntax)
			{
				if (Property.SyntaxControl == EVerseSyntaxControlKind::GroupingLayers)
				{
					ValueWidget = SNew(SCheckBox)
						.IsChecked(Property.Value == TEXT("1")
							? ECheckBoxState::Checked
							: ECheckBoxState::Unchecked)
						.OnCheckStateChanged_Lambda(
							[this,
							 OpenDocument = ActiveDocument,
							 Tile = ActiveDocument->SelectedTile.GetValue(),
							 Control = Property.SyntaxControl,
							 RegionIndex = Property.SyntaxRegionIndex](ECheckBoxState NewState)
							{
								HandleSyntaxControlSelected(
									MakeShared<FString>(NewState == ECheckBoxState::Checked
										? TEXT("1") : TEXT("0")),
									ESelectInfo::OnMouseClick,
									OpenDocument,
									Tile,
									Control,
									RegionIndex);
							});
				}
				else
				{
					const TSharedRef<TArray<TSharedPtr<FString>>> Options =
						MakeShared<TArray<TSharedPtr<FString>>>();
					for (const FString& Option : Property.Options)
					{
						Options->Add(MakeShared<FString>(Option));
					}
					SyntaxOptionSets.Add(Options);
					const TSharedPtr<FString>* Selected = Options->FindByPredicate(
						[&Property](const TSharedPtr<FString>& Option)
						{
							return Option.IsValid() && *Option == Property.Value;
						});
					ValueWidget = SNew(SSearchableComboBox)
						.OptionsSource(&Options.Get())
						.InitiallySelectedItem(Selected ? *Selected : nullptr)
						.OnGenerateWidget_Lambda([](TSharedPtr<FString> Option)
						{
							return SNew(STextBlock).Text(Option.IsValid()
								? FText::FromString(*Option) : FText::GetEmpty());
						})
						.OnSelectionChanged(
							this,
							&SVerseVisualEditor::HandleSyntaxControlSelected,
							ActiveDocument,
							ActiveDocument->SelectedTile.GetValue(),
							Property.SyntaxControl,
							Property.SyntaxRegionIndex)
						[
							SNew(STextBlock).Text(FText::FromString(Property.Value))
						];
				}
			}
			else if (Property.EditKind == EVerseTilePropertyEditKind::Literal)
			{
				ValueWidget = SNew(SVerseLiteralEditor)
					.LiteralKind(Property.LiteralKind)
					.LiteralRange(Property.EditRange)
					.SourceText(Property.Value)
					.OnSourceCommitted(FOnVerseLiteralSourceCommitted::CreateSP(
						this,
						&SVerseVisualEditor::HandleInlineLiteralCommitted,
						ActiveDocument));
			}
			else if (Property.EditKind == EVerseTilePropertyEditKind::AccessSpecifiers
				|| Property.EditKind == EVerseTilePropertyEditKind::EffectSpecifiers)
			{
				const FString EditableValue = ActiveDocument->PendingSpecifierText.Get(Property.Value);
				ValueWidget = SNew(SEditableTextBox)
					.Text(FText::FromString(EditableValue))
					.SelectAllTextWhenFocused(true)
					.OnTextCommitted(
						this,
						&SVerseVisualEditor::HandleSpecifiersCommitted,
						ActiveDocument,
						ActiveDocument->SelectedTile.GetValue(),
						Property.EditKind == EVerseTilePropertyEditKind::EffectSpecifiers);
			}
			else
			{
				const FString EditableValue = ActiveDocument->PendingRenameText.Get(Property.Value);
				ValueWidget = SNew(SEditableTextBox)
					.Text(FText::FromString(EditableValue))
					.SelectAllTextWhenFocused(true)
					.OnTextCommitted(
						this,
						&SVerseVisualEditor::HandleRenameCommitted,
						ActiveDocument,
						ActiveDocument->SelectedTile->NameRange);
			}
		}
		const TSharedRef<SVerticalBox> TargetRows = Property.EditKind == EVerseTilePropertyEditKind::Syntax
			? SyntaxRows
			: PropertyRows.ToSharedRef();
		if (Property.EditKind == EVerseTilePropertyEditKind::Syntax)
		{
			++VisibleSyntaxPropertyCount;
		}
		TSharedRef<SVerticalBox> PropertyContent = SNew(SVerticalBox)
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
			];
		if (!Property.Example.IsEmpty())
		{
			PropertyContent->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 5.0f, 0.0f, 0.0f)
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
						.Text(LOCTEXT("SyntaxExampleLabel", "Example"))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 2.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(FText::FromString(Property.Example))
						.Font(FCoreStyle::GetDefaultFontStyle("Mono", 9))
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					]
				]
			];
		}

		TargetRows->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(6.0f, 4.0f)
			[
				PropertyContent
			]
		];
	}

	if (VisibleSyntaxPropertyCount > 0)
	{
		PropertyRows->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 2.0f, 0.0f, 4.0f)
		[
			SNew(SExpandableArea)
			.AreaTitle(LOCTEXT("WhitespaceDetailsSection", "Whitespace and Syntax"))
			.InitiallyCollapsed(PropertyFilterText.IsEmpty()
				? !bWhitespaceDetailsExpanded
				: false)
			.OnAreaExpansionChanged_Lambda([this](bool bExpanded)
			{
				bWhitespaceDetailsExpanded = bExpanded;
			})
			.BodyContent()
			[
				SyntaxRows
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


#undef LOCTEXT_NAMESPACE
