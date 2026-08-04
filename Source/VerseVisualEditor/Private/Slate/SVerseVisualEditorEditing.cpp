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
#include "Semantics/VerseSemanticCandidates.h"
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
	FVerseVisualTile* FindTileByRange(
		TArray<FVerseVisualTile>& Tiles,
		FVerseTextRange Range)
	{
		for (FVerseVisualTile& Tile : Tiles)
		{
			if (Tile.Range == Range)
			{
				return &Tile;
			}
			if (FVerseVisualTile* Nested = FindTileByRange(Tile.Children, Range))
			{
				return Nested;
			}
		}
		return nullptr;
	}

	FVerseVisualTile* FindInsertedControlTile(
		TArray<FVerseVisualTile>& Tiles,
		FVerseTextRange InsertedRange)
	{
		if (FVerseVisualTile* Exact = FindTileByRange(Tiles, InsertedRange);
			Exact != nullptr
			&& Exact->ExpressionKind == EVerseExpressionKind::Control)
		{
			return Exact;
		}
		for (FVerseVisualTile& Tile : Tiles)
		{
			// A multiline control's parser range need not share the source edit's
			// closing boundary. Its revision and opening byte still identify the
			// expression without accidentally selecting an enclosing control.
			if (Tile.ExpressionKind == EVerseExpressionKind::Control
				&& Tile.Range.Revision == InsertedRange.Revision
				&& Tile.Range.BeginByte == InsertedRange.BeginByte)
			{
				return &Tile;
			}
			if (FVerseVisualTile* Nested =
				FindInsertedControlTile(Tile.Children, InsertedRange))
			{
				return Nested;
			}
		}
		return nullptr;
	}

	bool RecordGeneratedContentAsProvisional(
		FOpenVerseDocument& Document,
		FVerseTextRange InsertedControlRange,
		EVerseProvisionalContentTarget Target)
	{
		for (FOpenVerseFunctionTab& Tab : Document.FunctionTabs)
		{
			FVerseVisualTile* ControlTile =
				FindInsertedControlTile(Tab.GraphTiles, InsertedControlRange);
			if (ControlTile == nullptr
				|| ControlTile->ExpressionKind != EVerseExpressionKind::Control)
			{
				continue;
			}
			FVerseVisualTile* Condition = ControlTile->Children.FindByPredicate(
				[](const FVerseVisualTile& Child)
				{
					return Child.Kind == EVerseVisualTileKind::FailableBlock;
				});
			if (Condition == nullptr || Condition->Children.IsEmpty())
			{
				return false;
			}
			const FVerseTextRange PredicateRange = Condition->Children[0].Range;
			Document.ProvisionalTiles.Add(
				PredicateRange,
				Document.Session->GetParseSnapshot().GetDocument()->GetOriginalUtf8View());
			Condition->Children[0].bIsProvisional = true;
			if (Target == EVerseProvisionalContentTarget::FirstConditionAndBodyExpressions)
			{
				const auto* Body = ControlTile->ControlRegions.FindByPredicate(
					[](const auto& Region)
					{
						return Region.Kind == EVerseControlRegionKind::Body;
					});
				if (Body != nullptr && Body->OperandCount > 0
					&& ControlTile->Children.IsValidIndex(Body->FirstOperandIndex))
				{
					FVerseVisualTile& BodyPlaceholder =
						ControlTile->Children[Body->FirstOperandIndex];
					Document.ProvisionalTiles.Add(
						BodyPlaceholder.Range,
						Document.Session->GetParseSnapshot().GetDocument()->GetOriginalUtf8View());
					BodyPlaceholder.bIsProvisional = true;
				}
			}
			return true;
		}
		return false;
	}

	void AdoptProvisionalTile(
		FOpenVerseDocument& Document,
		FVerseTextRange Range)
	{
		Document.ProvisionalTiles.Adopt(Range);
	}

	DECLARE_DELEGATE_OneParam(FOnVerseExpressionChosen, TSharedPtr<FVerseExpressionAction>);
	enum class EVerseExpressionGrouping : uint8
	{
		Category,
		Module,
	};

	EVerseExpressionGrouping LastExpressionGrouping =
		EVerseExpressionGrouping::Category;

	struct FVerseExpressionSchemaAction final : FEdGraphSchemaAction
	{
		static FName StaticGetTypeId()
		{
			static const FName TypeId(TEXT("FVerseExpressionSchemaAction"));
			return TypeId;
		}

		virtual FName GetTypeId() const override { return StaticGetTypeId(); }

		FVerseExpressionSchemaAction(
			TSharedPtr<FVerseExpressionAction> InAction,
			const FText& Grouping)
			: FEdGraphSchemaAction(
				Grouping,
				InAction->DisplayName,
				FText::GetEmpty(),
				0,
				BuildVerseExpressionActionSearchKeywords(*InAction))
			, ExpressionAction(MoveTemp(InAction))
		{
		}

		TSharedPtr<FVerseExpressionAction> ExpressionAction;
	};

	class SVerseExpressionSearch final : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SVerseExpressionSearch) {}
			SLATE_ARGUMENT(TArray<TSharedPtr<FVerseExpressionAction>>, Actions)
			SLATE_ARGUMENT(FText, ContextDescription)
			SLATE_ARGUMENT(FLinearColor, ContextTypeColor)
			SLATE_ARGUMENT(const FSlateBrush*, ContextTypeIcon)
			SLATE_EVENT(FOnVerseExpressionChosen, OnChosen)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			AllActions = InArgs._Actions;
			OnChosen = InArgs._OnChosen;
			Grouping = LastExpressionGrouping;
			ChildSlot
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("Menu.Background"))
				.Padding(6.0f)
				[
					SNew(SBox)
					.WidthOverride(330.0f)
					.HeightOverride(360.0f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().Padding(2.0f, 2.0f, 2.0f, 5.0f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 5.0f, 0.0f)
							[
								SNew(SImage)
								.Image(InArgs._ContextTypeIcon)
								.ColorAndOpacity(InArgs._ContextTypeColor)
							]
							+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(InArgs._ContextDescription)
								.Font(FAppStyle::GetFontStyle("BlueprintEditor.ActionMenu.ContextDescriptionFont"))
								.AutoWrapText(true)
							]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.0f, 0.0f, 0.0f, 0.0f)
							[
								SNew(SSegmentedControl<EVerseExpressionGrouping>)
								.Value(Grouping)
								.OnValueChanged(this, &SVerseExpressionSearch::HandleGroupingChanged)
								+ SSegmentedControl<EVerseExpressionGrouping>::Slot(EVerseExpressionGrouping::Category)
								.Text(LOCTEXT("GroupExpressionsByCategory", "Category"))
								+ SSegmentedControl<EVerseExpressionGrouping>::Slot(EVerseExpressionGrouping::Module)
								.Text(LOCTEXT("GroupExpressionsByModule", "Module"))
							]
						]
						+ SVerticalBox::Slot().FillHeight(1.0f)
						[
							SAssignNew(ActionMenu, SGraphActionMenu)
							.OnCollectAllActions(this, &SVerseExpressionSearch::CollectActions)
							.OnActionSelected(this, &SVerseExpressionSearch::HandleActionSelected)
							.OnActionDoubleClicked(this, &SVerseExpressionSearch::HandleActionDoubleClicked)
							.OnCreateWidgetForAction(this, &SVerseExpressionSearch::CreateActionWidget)
							.AlphaSortItems(true)
							.SortItemsRecursively(true)
						]
					]
				]
			];
			RegisterActiveTimer(
				0.0f,
				FWidgetActiveTimerDelegate::CreateSP(
					this, &SVerseExpressionSearch::FocusSearchBox));
		}

		TSharedPtr<SWidget> GetWidgetToFocus() const
		{
			if (ActionMenu.IsValid())
			{
				return ActionMenu->GetFilterTextBox();
			}
			return nullptr;
		}

	private:
		EActiveTimerReturnType FocusSearchBox(double, float)
		{
			if (!ActionMenu.IsValid())
			{
				return EActiveTimerReturnType::Continue;
			}
			FSlateApplication::Get().SetKeyboardFocus(
				ActionMenu->GetFilterTextBox(), EFocusCause::SetDirectly);
			return EActiveTimerReturnType::Stop;
		}

		void CollectActions(FGraphActionListBuilderBase& OutActions) const
		{
			for (const TSharedPtr<FVerseExpressionAction>& Action : AllActions)
			{
				const FText& ActionGrouping =
					Grouping == EVerseExpressionGrouping::Module
						? Action->ModuleCategory
						: Action->Category;
				OutActions.AddAction(MakeShared<FVerseExpressionSchemaAction>(
					Action, ActionGrouping));
			}
		}

		void HandleGroupingChanged(EVerseExpressionGrouping NewGrouping)
		{
			Grouping = NewGrouping;
			LastExpressionGrouping = NewGrouping;
			if (ActionMenu.IsValid())
			{
				ActionMenu->RefreshAllActions(false, false);
			}
		}

		TSharedRef<SWidget> CreateActionWidget(
			FCreateWidgetForActionData* const CreateData) const
		{
			const TSharedPtr<FEdGraphSchemaAction> SchemaAction = CreateData->Action;
			const bool bIsVerseAction = SchemaAction.IsValid()
				&& SchemaAction->GetTypeId()
					== FVerseExpressionSchemaAction::StaticGetTypeId();
			const TSharedPtr<FVerseExpressionAction> ExpressionAction = bIsVerseAction
				? StaticCastSharedPtr<FVerseExpressionSchemaAction>(SchemaAction)->ExpressionAction
				: nullptr;
			const bool bValueAction = ExpressionAction.IsValid()
				&& (ExpressionAction->SourceForm
						== EVerseExpressionSourceForm::IdentifierReference
					|| ExpressionAction->SourceForm
						== EVerseExpressionSourceForm::Literal
					|| ExpressionAction->SourceForm
						== EVerseExpressionSourceForm::Definition);
			const FSlateBrush* Icon = FAppStyle::GetBrush(bValueAction
				? TEXT("Kismet.AllClasses.VariableIcon")
				: TEXT("Kismet.AllClasses.FunctionIcon"));
			const FLinearColor IconColor = bValueAction
				? GetBlueprintPinColor(ExpressionAction->ResultTypeName)
				: GetDefault<UGraphEditorSettings>()->PureFunctionCallNodeTitleColor;

			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SBox)
					.WidthOverride(16.0f)
					.HeightOverride(16.0f)
					[
						SNew(SImage)
						.Image(Icon)
						.ColorAndOpacity(IconColor)
					]
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(3.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(SchemaAction.IsValid()
						? SchemaAction->GetMenuDescription()
						: FText::GetEmpty())
					.HighlightText(CreateData->HighlightText)
					.ToolTipText(SchemaAction.IsValid()
						? SchemaAction->GetTooltipDescription()
						: FText::GetEmpty())
				];
		}

		void HandleActionSelected(
			const TArray<TSharedPtr<FEdGraphSchemaAction>>& SelectedActions,
			ESelectInfo::Type SelectInfo)
		{
			if (SelectInfo == ESelectInfo::OnKeyPress && SelectedActions.Num() == 1)
			{
				Choose(SelectedActions[0]);
			}
		}

		void HandleActionDoubleClicked(
			const TArray<TSharedPtr<FEdGraphSchemaAction>>& SelectedActions)
		{
			if (SelectedActions.Num() == 1)
			{
				Choose(SelectedActions[0]);
			}
		}

		void Choose(const TSharedPtr<FEdGraphSchemaAction>& SchemaAction)
		{
			if (!SchemaAction.IsValid()
				|| SchemaAction->GetTypeId() != FVerseExpressionSchemaAction::StaticGetTypeId())
			{
				return;
			}
			const TSharedPtr<FVerseExpressionAction> Action =
				StaticCastSharedPtr<FVerseExpressionSchemaAction>(SchemaAction)->ExpressionAction;
			if (Action.IsValid() && OnChosen.IsBound())
			{
				OnChosen.Execute(Action);
			}
		}

		TArray<TSharedPtr<FVerseExpressionAction>> AllActions;
		TSharedPtr<SGraphActionMenu> ActionMenu;
		FOnVerseExpressionChosen OnChosen;
		EVerseExpressionGrouping Grouping = EVerseExpressionGrouping::Category;
	};

	FString GetActionMenuTypeName(const FString& VerseType)
	{
		const FString Trimmed = VerseType.TrimStartAndEnd();
		const FString Lower = Trimmed.ToLower();
		if (Lower == TEXT("int"))
		{
			return TEXT("integer");
		}
		if (Lower == TEXT("logic"))
		{
			return TEXT("boolean");
		}
		if (Lower == TEXT("char"))
		{
			return TEXT("character");
		}
		if (Lower == TEXT("actor"))
		{
			return TEXT("actor object reference");
		}
		return Lower.IsEmpty() ? TEXT("unknown") : Lower;
	}


	const FVerseVisualTile* FindTileById(
		TConstArrayView<FVerseVisualTile> Tiles,
		FVerseVisualTileId TileId)
	{
		for (const FVerseVisualTile& Tile : Tiles)
		{
			if (Tile.Id == TileId)
			{
				return &Tile;
			}
			if (const FVerseVisualTile* Child = FindTileById(Tile.Children, TileId))
			{
				return Child;
			}
		}
		return nullptr;
	}

	bool FindTileAncestry(
		TConstArrayView<FVerseVisualTile> Tiles,
		FVerseVisualTileId TileId,
		TArray<const FVerseVisualTile*>& OutAncestry)
	{
		for (const FVerseVisualTile& Tile : Tiles)
		{
			OutAncestry.Add(&Tile);
			if (Tile.Id == TileId
				|| FindTileAncestry(Tile.Children, TileId, OutAncestry))
			{
				return true;
			}
			OutAncestry.Pop(EAllowShrinking::No);
		}
		return false;
	}


}

FReply SVerseVisualEditor::BeginSocketDrag(const FVerseSocketDragStart& DragStart)
{
	if (!DragStart.Anchor.IsValid() || !ActiveDocument.IsValid()
		|| !ActiveDocument->FunctionTabs.IsValidIndex(ActiveDocument->ActiveFunctionTabIndex))
	{
		return FReply::Unhandled();
	}

	FinishExpressionSearch();
	FVerseSocketDragStart EffectiveDrag = DragStart;
	if (EffectiveDrag.bAdoptsProvisionalTile)
	{
		AdoptProvisionalTile(*ActiveDocument, EffectiveDrag.TileRange);
	}
	if (EffectiveDrag.Purpose == FVerseSocketDragStart::EPurpose::ClauseInsertion
		&& EffectiveDrag.Clause.IsSet()
		&& EffectiveDrag.Clause->Items.IsValidIndex(EffectiveDrag.ClauseInsertionIndex))
	{
		const FVerseTextRange ExistingRange =
			EffectiveDrag.Clause->Items[EffectiveDrag.ClauseInsertionIndex].Expression.Range;
		if (ActiveDocument->ProvisionalTiles.Contains(ExistingRange))
		{
			// A drag from the execution anchor immediately before a provisional item
			// means replace that item, not insert another item ahead of it.
			EffectiveDrag.ProvisionalReplacementRange = ExistingRange;
		}
	}
	SocketDrag = EffectiveDrag;
	FOpenVerseFunctionTab& Tab =
		ActiveDocument->FunctionTabs[ActiveDocument->ActiveFunctionTabIndex];
	if (EffectiveDrag.Purpose == FVerseSocketDragStart::EPurpose::ValueConnection)
	{
		TArray<const FVerseVisualTile*> Ancestry;
		if (FindTileAncestry(
			Tab.GraphTiles, EffectiveDrag.Endpoint.Tile, Ancestry))
		{
			for (int32 Index = Ancestry.Num() - 1; Index >= 0; --Index)
			{
				if (Ancestry[Index]->bStatementLevel)
				{
					EffectiveDrag.SemanticScopeRange = Ancestry[Index]->Range;
					break;
				}
			}
		}
		if (!EffectiveDrag.SemanticScopeRange.IsSet())
		{
			EffectiveDrag.SemanticScopeRange = EffectiveDrag.TileRange;
		}
		SocketDrag = EffectiveDrag;
	}
	return Tab.FunctionCanvas.IsValid()
		? Tab.FunctionCanvas->BeginConnectionDrag(EffectiveDrag)
		: FReply::Unhandled();
}

void SVerseVisualEditor::HandleConnectionDropped(
	const FVerseSocketDragStart& DragStart,
	FVerseDesktopPoint DesktopPosition)
{
	SocketDrag = DragStart;
	OpenExpressionSearch(DesktopPosition);
}

void SVerseVisualEditor::HandleConnectionCancelled()
{
	SocketDrag.Reset();
}

void SVerseVisualEditor::OpenExpressionSearch(FVerseDesktopPoint DesktopPosition)
{
	if (!SocketDrag.IsSet()
		|| !ActiveDocument.IsValid()
		|| !ActiveDocument->FunctionTabs.IsValidIndex(ActiveDocument->ActiveFunctionTabIndex))
	{
		FinishExpressionSearch();
		return;
	}
	const FOpenVerseFunctionTab& Tab =
		ActiveDocument->FunctionTabs[ActiveDocument->ActiveFunctionTabIndex];
	const FVerseDocument& Document =
		*ActiveDocument->Session->GetParseSnapshot().GetDocument();
	// Candidate discovery is read-only and degrades to the last successful local
	// snapshot plus the Solaris baseline when this revision fails analysis.
	// ApplyExpressionAction requires an exact snapshot only for actions that make
	// semantic claims; editor-supported structural actions (currently Add and
	// scoped identifiers) rely on current-range and prospective VST validation.
	const TArray<TSharedPtr<const FVerseSemanticSnapshot>> SemanticSnapshots =
		SemanticWorkspace
			? SemanticWorkspace->GetCandidateSnapshots()
			: TArray<TSharedPtr<const FVerseSemanticSnapshot>>();
	const bool bClauseInsertion = SocketDrag->Purpose
		== FVerseSocketDragStart::EPurpose::ClauseInsertion;
	FVerseTextRange SearchScopeRange = SocketDrag->TileRange;
	if (bClauseInsertion && SocketDrag->Clause.IsSet())
	{
		const FVerseVisualClauseDescriptor& Clause = SocketDrag->Clause.GetValue();
		if (SocketDrag->InsertionKind
			== EVerseVisualSocketInsertionKind::MissingElseClause)
		{
			SearchScopeRange = SocketDrag->InsertionOwnerRange;
		}
		else if (!Clause.Items.IsEmpty())
		{
			SearchScopeRange = Clause.Items[FMath::Clamp(
				SocketDrag->ClauseInsertionIndex,
				0,
				Clause.Items.Num() - 1)].Expression.Range;
		}
		else
		{
			SearchScopeRange = Clause.InteriorRange;
		}
	}
	const FVerseVisualTile* SourceTile = bClauseInsertion
		? nullptr
		: FindTileById(Tab.GraphTiles, SocketDrag->Endpoint.Tile);
	const FVerseVisualSocket* SourceSocket = SourceTile != nullptr
		? SourceTile->FindSocket(SocketDrag->Endpoint.Socket)
		: nullptr;
	if (!bClauseInsertion && SourceSocket == nullptr)
	{
		FinishExpressionSearch();
		return;
	}
	ExpressionActions = bClauseInsertion
		? FVerseExpressionActionQuery::BuildAll(
			Tab.Parameters, Document, SearchScopeRange,
			ActiveDocument->FilePath, SemanticSnapshots)
		: FVerseExpressionActionQuery::Build(
			Tab.Parameters, *SourceSocket, SocketDrag->bOutput,
			Document,
			SocketDrag->SemanticScopeRange.IsSet()
				? SocketDrag->SemanticScopeRange
				: SocketDrag->TileRange,
			ActiveDocument->FilePath, SemanticSnapshots);
	bool bCompatibleOperatorOperandSearch = false;
	TArray<FString> CompatibleOperandTypes;
	if (!bClauseInsertion
		&& !SocketDrag->bOutput
		&& SourceTile != nullptr
		&& IsVerseOperatorExpression(SourceTile->ExpressionKind)
		&& SourceTile->GetValueInputs().IsValidIndex(SocketDrag->Endpoint.Socket.Index)
		&& Algo::AllOf(
			SourceTile->GetValueInputs(),
			[](const FVerseVisualSocket& Input)
			{
				return Input.InlineLiteralRange.IsSet();
			}))
	{
		const int32 OperandIndex = SocketDrag->Endpoint.Socket.Index;
		const FVerseOperatorConnectionConstraints Constraints =
			FVerseVisualTileBuilder::BuildOperatorConnectionConstraints(
				Tab.GraphTiles, *SourceTile, Document);
		const TArray<const FVerseVisualSocket*> OutputConsumers =
			Constraints.GetOutputConsumerPointers();
		const TArray<FVerseOperatorSignature> Signatures =
			FVerseSemanticCandidateProvider::BuildOperatorSignatures(
				SemanticSnapshots, ActiveDocument->FilePath,
				SourceTile->Range.BeginByte, Document,
				SourceTile->OperatorSpelling,
				SourceTile->GetValueInputs().Num(), {}, OutputConsumers);

		struct FRankedAction
		{
			TSharedPtr<FVerseExpressionAction> Action;
			int32 HomogeneousPenalty = 0;
			int32 LiteralChangeCount = 0;
			int32 MissingDefaultCount = 0;
			FString SignatureText;
		};
		TMap<FString, FRankedAction> RankedActions;
		for (const FVerseOperatorSignature& Signature : Signatures)
		{
			if (!Signature.OperandTypeNames.IsValidIndex(OperandIndex)
				|| !Signature.OperandTypes.IsValidIndex(OperandIndex)
				|| Signature.OperandTypes[OperandIndex] == nullptr
				|| !Signature.Snapshot.IsValid())
			{
				continue;
			}
			CompatibleOperandTypes.AddUnique(Signature.OperandTypeNames[OperandIndex]);
			FVerseVisualSocket ConcreteSocket = *SourceSocket;
			ConcreteSocket.SemanticType = Signature.OperandTypes[OperandIndex];
			ConcreteSocket.SemanticSnapshot = Signature.Snapshot;
			ConcreteSocket.SemanticTypeName = Signature.OperandTypeNames[OperandIndex];
			ConcreteSocket.InlineLiteralKind = EVerseLiteralKind::None;
			TArray<TSharedPtr<FVerseExpressionAction>> SignatureActions =
				FVerseExpressionActionQuery::Build(
					Tab.Parameters, ConcreteSocket, false, Document,
					SocketDrag->SemanticScopeRange.IsSet()
						? SocketDrag->SemanticScopeRange
						: SocketDrag->TileRange,
					ActiveDocument->FilePath, SemanticSnapshots);

			FVerseOperatorRetargetRecipe Recipe;
			Recipe.OperatorRange = SourceTile->Range;
			Recipe.OperatorSpelling = SourceTile->OperatorSpelling;
			Recipe.OperandCount = SourceTile->GetValueInputs().Num();
			Recipe.ReplacedOperandIndex = OperandIndex;
			Recipe.SignatureDisplayText = Signature.DisplayText;
			Recipe.OperandTypeNames = Signature.OperandTypeNames;
			for (const FVerseVisualSocket& Input : SourceTile->GetValueInputs())
			{
				Recipe.InlineLiteralRanges.Add(
					Input.InlineLiteralRange);
			}

			const bool bHomogeneous = Signature.OperandTypeNames.IsEmpty()
				|| Algo::AllOf(
					Signature.OperandTypeNames,
					[&Signature](const FString& Type)
					{
						return Type == Signature.OperandTypeNames[0];
					});
			int32 ChangeCount = 0;
			int32 MissingDefaults = 0;
			for (int32 Index = 0; Index < Recipe.OperandCount; ++Index)
			{
				if (Index == OperandIndex)
				{
					continue;
				}
				const TOptional<FString> Default =
					GetDefaultVerseLiteralSourceForType(
						Signature.OperandTypeNames[Index]);
				const FString ExistingSource = Document.DecodeOriginalRange(
					Recipe.InlineLiteralRanges[Index]).TrimStartAndEnd();
				ChangeCount += ExistingSource == Default.Get(TEXT("0")) ? 0 : 1;
				MissingDefaults += Default.IsSet() ? 0 : 1;
			}
			for (TSharedPtr<FVerseExpressionAction>& Action : SignatureActions)
			{
				Action->OperatorRetarget = Recipe;
				const FString Key = FString::Printf(
					TEXT("%d|%s|%s|%s|%s|%d"),
					static_cast<int32>(Action->SourceForm),
					*Action->SourceSpelling, *Action->ResultTypeName,
					*FString::Join(Action->InputTypeNames, TEXT(",")),
					*Action->ModuleCategory.ToString(), Action->BoundInputIndex);
				FRankedAction Candidate{
					Action, bHomogeneous ? 0 : 1, ChangeCount,
					MissingDefaults, Signature.DisplayText};
				FRankedAction* Existing = RankedActions.Find(Key);
				const bool bBetter = Existing == nullptr
					|| Candidate.HomogeneousPenalty < Existing->HomogeneousPenalty
					|| (Candidate.HomogeneousPenalty == Existing->HomogeneousPenalty
						&& Candidate.LiteralChangeCount < Existing->LiteralChangeCount)
					|| (Candidate.HomogeneousPenalty == Existing->HomogeneousPenalty
						&& Candidate.LiteralChangeCount == Existing->LiteralChangeCount
						&& Candidate.MissingDefaultCount < Existing->MissingDefaultCount)
					|| (Candidate.HomogeneousPenalty == Existing->HomogeneousPenalty
						&& Candidate.LiteralChangeCount == Existing->LiteralChangeCount
						&& Candidate.MissingDefaultCount == Existing->MissingDefaultCount
						&& Candidate.SignatureText < Existing->SignatureText);
				if (bBetter)
				{
					RankedActions.Add(Key, MoveTemp(Candidate));
				}
			}
		}
		if (!RankedActions.IsEmpty())
		{
			ExpressionActions.Reset();
			for (TPair<FString, FRankedAction>& Pair : RankedActions)
			{
				ExpressionActions.Add(MoveTemp(Pair.Value.Action));
			}
			bCompatibleOperatorOperandSearch = true;
		}
	}
	const FString SocketType = bCompatibleOperatorOperandSearch
		&& CompatibleOperandTypes.Num() == 1
		? CompatibleOperandTypes[0]
		: bClauseInsertion
		? FString()
		: GetVisualTypeName(
			SourceSocket->TypeRange,
			SourceSocket->IntrinsicTypeName,
			Document,
			SourceSocket->SemanticTypeName);
	const FText ContextDescription = bClauseInsertion
		? LOCTEXT("ExpressionInsertionContext", "All expressions")
		: bCompatibleOperatorOperandSearch && CompatibleOperandTypes.Num() > 1
			? LOCTEXT(
				"ExpressionCompatibleOperandContext",
				"Actions providing a compatible operand")
		: FText::Format(
			SocketDrag->bOutput
				? LOCTEXT("ExpressionConsumerTypeContext", "Actions taking {0}")
				: LOCTEXT("ExpressionProducerTypeContext", "Actions providing {0}"),
			FText::FromString(GetActionMenuTypeName(SocketType)));
	const FSlateBrush* ContextTypeIcon = FAppStyle::GetBrush(
		SocketType.TrimStartAndEnd().StartsWith(TEXT("[]"))
			? TEXT("Graph.ArrayPin.Connected")
			: TEXT("Graph.Pin.Connected"));
	TSharedRef<SVerseExpressionSearch> Search = SNew(SVerseExpressionSearch)
		.Actions(ExpressionActions)
		.ContextDescription(ContextDescription)
		.ContextTypeColor(bClauseInsertion
			|| (bCompatibleOperatorOperandSearch
				&& CompatibleOperandTypes.Num() > 1)
			? FLinearColor::White
			: GetBlueprintPinColor(SocketType))
		.ContextTypeIcon(ContextTypeIcon)
		.OnChosen(FOnVerseExpressionChosen::CreateSP(this, &SVerseVisualEditor::ApplyExpressionAction));
	ExpressionMenu = FSlateApplication::Get().PushMenu(
		AsShared(), FWidgetPath(), Search, DesktopPosition.Value,
		FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
	if (!ExpressionMenu.IsValid())
	{
		FinishExpressionSearch();
		return;
	}
	if (ExpressionMenu->GetOwnedWindow().IsValid())
	{
		ExpressionMenu->GetOwnedWindow()->SetWidgetToFocusOnActivate(Search->GetWidgetToFocus());
	}
	ExpressionMenu->GetOnMenuDismissed().AddLambda(
		[WeakThis = TWeakPtr<SVerseVisualEditor>(SharedThis(this))](TSharedRef<IMenu>)
		{
			if (const TSharedPtr<SVerseVisualEditor> Pinned = WeakThis.Pin())
			{
				Pinned->FinishExpressionSearch();
			}
		});
}

void SVerseVisualEditor::FinishExpressionSearch()
{
	if (ActiveDocument.IsValid()
		&& ActiveDocument->FunctionTabs.IsValidIndex(ActiveDocument->ActiveFunctionTabIndex))
	{
		FOpenVerseFunctionTab& Tab =
			ActiveDocument->FunctionTabs[ActiveDocument->ActiveFunctionTabIndex];
		if (Tab.FunctionCanvas.IsValid())
		{
			Tab.FunctionCanvas->EndConnectionPreview();
		}
	}
	SocketDrag.Reset();
	ExpressionActions.Reset();
	ExpressionMenu.Reset();
}

void SVerseVisualEditor::ApplyExpressionAction(TSharedPtr<FVerseExpressionAction> Action)
{
	if (!Action.IsValid() || !SocketDrag.IsSet() || !ActiveDocument.IsValid())
	{
		return;
	}
	FText Error;
	const bool bClauseInsertion = SocketDrag->Purpose
		== FVerseSocketDragStart::EPurpose::ClauseInsertion
		&& SocketDrag->Clause.IsSet();
	const bool bValueInsertion = SocketDrag->Purpose
		== FVerseSocketDragStart::EPurpose::ValueConnection
		&& SocketDrag->bOutput
		&& SocketDrag->Clause.IsSet();
	const bool bReplaceProvisional = bClauseInsertion
		&& SocketDrag->ProvisionalReplacementRange.IsSet()
		&& SocketDrag->Clause->Items.IsValidIndex(SocketDrag->ClauseInsertionIndex)
		&& SocketDrag->Clause->Items[SocketDrag->ClauseInsertionIndex].Expression.Range
			== SocketDrag->ProvisionalReplacementRange.GetValue();
	FVerseTextRange AppliedExpressionRange;
	bool bApplied = false;
	FString BoundExpressionSource;
	if (SocketDrag->BoundSourceRange.IsSet())
	{
		BoundExpressionSource = ActiveDocument->Session->GetParseSnapshot().GetDocument()
			->DecodeOriginalRange(SocketDrag->BoundSourceRange).TrimStartAndEnd();
	}
	if (bClauseInsertion
		&& SocketDrag->InsertionKind
			== EVerseVisualSocketInsertionKind::MissingElseClause)
	{
		bApplied = FVerseClauseEditing::AddElseExpression(
			*ActiveDocument->Session,
			SocketDrag->InsertionOwnerRange,
			SocketDrag->Clause->Syntax.Delimiter,
			*Action,
			Error,
			&AppliedExpressionRange,
			BoundExpressionSource);
	}
	else if (bReplaceProvisional)
	{
		bApplied = FVerseClauseEditing::ReplaceExpression(
			*ActiveDocument->Session,
			SocketDrag->Clause.GetValue(),
			SocketDrag->ClauseInsertionIndex,
			*Action,
			Error,
			&AppliedExpressionRange);
	}
	else if (bClauseInsertion)
	{
		bApplied = FVerseClauseEditing::InsertExpression(
			*ActiveDocument->Session,
			SocketDrag->Clause.GetValue(),
			SocketDrag->ClauseInsertionIndex,
			*Action,
			Error,
			&AppliedExpressionRange,
			BoundExpressionSource);
	}
	else if (bValueInsertion)
	{
		bApplied = FVerseClauseEditing::InsertExpression(
			*ActiveDocument->Session,
			SocketDrag->Clause.GetValue(),
			SocketDrag->ClauseInsertionIndex,
			*Action,
			Error,
			&AppliedExpressionRange,
			BoundExpressionSource);
	}
	else if (!SocketDrag->bOutput
		&& !SocketDrag->MaterializedInputName.IsEmpty())
	{
		bApplied = TryMaterializeVerseNamedInput(
			*ActiveDocument->Session,
			SocketDrag->TileRange,
			SocketDrag->MaterializedInputName,
			*Action,
			Error);
	}
	else if (Action->OperatorRetarget.IsSet())
	{
		bApplied = TryApplyVerseOperatorOperandAction(
			*ActiveDocument->Session, *Action, Error);
	}
	else
	{
		FVerseBoundExpressionSyntax BoundSyntax;
		const FVerseBoundExpressionSyntax* BoundSyntaxPtr = nullptr;
		if (SocketDrag->bOutput && SocketDrag->BoundSourceRange.IsSet())
		{
			BoundSyntax.Kind = SocketDrag->BoundExpressionKind;
			BoundSyntax.OperatorSpelling = SocketDrag->BoundOperatorSpelling;
			BoundSyntax.bExplicitlyGrouped =
				SocketDrag->bBoundExpressionExplicitlyGrouped;
			BoundSyntaxPtr = &BoundSyntax;
		}
		bApplied = TryApplyVerseExpressionAction(
			*ActiveDocument->Session,
			SocketDrag->TileRange,
			*Action,
			Error,
			BoundSyntaxPtr);
	}
	if (!bApplied)
	{
		ActiveDocument->LoadError = Error;
		bLocalCompilePanelOpen = true;
		return;
	}
	if (Action->OperatorRetarget.IsSet())
	{
		const FVerseOperatorRetargetRecipe& Recipe =
			Action->OperatorRetarget.GetValue();
		ActiveDocument->PendingOperatorSignatureText =
			Recipe.SignatureDisplayText;
		ActiveDocument->PendingOperatorSpelling =
			Recipe.OperatorSpelling;
		ActiveDocument->PendingOperatorSignatureBeginByte =
			Recipe.OperatorRange.BeginByte;
		ActiveDocument->ProvisionalTiles.AdoptContaining(
			Recipe.OperatorRange);
	}
	if (bReplaceProvisional)
	{
		AdoptProvisionalTile(
			*ActiveDocument, SocketDrag->ProvisionalReplacementRange.GetValue());
	}
	ActiveDocument->LoadError = FText::GetEmpty();
	ActiveDocument->bIsTemporary = false;
	QueueSemanticAnalysis(true);
	InvalidateCompilationResult(ActiveDocument);
	if (CompilationMode == EVerseCompilationMode::Continuous)
	{
		QueueCompilation(ActiveDocument, true);
	}
	if (ExpressionMenu.IsValid())
	{
		ExpressionMenu->Dismiss();
	}
	ReconcileFunctionTabs(
		*ActiveDocument,
		FindExactSemanticSnapshot(SemanticWorkspace.Get(), *ActiveDocument));
	const bool bCreatedProvisionalContent = bClauseInsertion
		&& Action->ProvisionalContentTarget != EVerseProvisionalContentTarget::None;
	if (bCreatedProvisionalContent && AppliedExpressionRange.IsSet())
	{
		RecordGeneratedContentAsProvisional(
			*ActiveDocument,
			AppliedExpressionRange,
			Action->ProvisionalContentTarget);
	}
	if (Action->SourceForm == EVerseExpressionSourceForm::Definition
		&& AppliedExpressionRange.IsSet())
	{
		for (FOpenVerseFunctionTab& Tab : ActiveDocument->FunctionTabs)
		{
			if (FVerseVisualTile* CreatedTile = FindTileByRange(
				Tab.GraphTiles, AppliedExpressionRange))
			{
				ActiveDocument->SelectedTile = *CreatedTile;
				OpenDetailsTab();
				break;
			}
		}
	}
	RebuildDocumentTabs();
	RefreshActiveDocument();
}

void SVerseVisualEditor::HandleInlineLiteralCommitted(
	FVerseTextRange LiteralRange,
	FText NewSourceText,
	TSharedPtr<FOpenVerseDocument> OpenDocument)
{
	if (!OpenDocument.IsValid() || !OpenDocument->Session.IsValid())
	{
		return;
	}

	const FString Replacement = NewSourceText.ToString();
	const FTCHARToUTF8 ReplacementUtf8(*Replacement);
	FText Error;
	if (!OpenDocument->Session->Replace(
		LiteralRange,
		FUtf8StringView(
			reinterpret_cast<const UTF8CHAR*>(ReplacementUtf8.Get()),
			ReplacementUtf8.Length()),
		Error))
	{
		OpenDocument->LoadError = Error;
		if (OpenDocument == ActiveDocument)
		{
			bLocalCompilePanelOpen = true;
			RefreshActiveDocument(false);
		}
		return;
	}
	OpenDocument->ProvisionalTiles.AdoptContaining(LiteralRange);

	OpenDocument->LoadError = FText::GetEmpty();
	OpenDocument->bIsTemporary = false;
	QueueSemanticAnalysis(true);
	InvalidateCompilationResult(OpenDocument);
	if (CompilationMode == EVerseCompilationMode::Continuous)
	{
		QueueCompilation(OpenDocument, true);
	}
	ReconcileFunctionTabs(
		*OpenDocument,
		FindExactSemanticSnapshot(SemanticWorkspace.Get(), *OpenDocument));
	RebuildDocumentTabs();
	if (OpenDocument == ActiveDocument)
	{
		RefreshActiveDocument(false);
	}
}

FReply SVerseVisualEditor::HandleClauseReordered(
	const FVerseVisualClauseDescriptor& Clause,
	int32 FromIndex,
	int32 ToIndex)
{
	if (!ActiveDocument.IsValid() || !ActiveDocument->Session.IsValid())
	{
		return FReply::Unhandled();
	}
	FText Error;
	if (!FVerseClauseEditing::ReorderExpression(
		*ActiveDocument->Session, Clause, FromIndex, ToIndex, Error))
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


#undef LOCTEXT_NAMESPACE
