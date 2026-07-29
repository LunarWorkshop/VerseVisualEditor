#include "SVerseVisualEditor.h"

#include "SVerseFunctionCanvas.h"
#include "SVerseFileCanvas.h"
#include "SVerseTile.h"

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
#include "VerseDocumentSession.h"
#include "VerseDefinitionIcon.h"
#include "VerseExternalChange.h"
#include "VerseFunctionNavigation.h"
#include "VerseIdentifier.h"
#include "VerseSemanticWorkspace.h"
#include "VerseSpecifier.h"
#include "VerseTileProperties.h"
#include "VerseVisualTile.h"
#include "VerseVisualEditorSettings.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SVerseVisualEditor"

struct FOpenVerseFunctionTab
{
	FString Name;
	TArray<FString> ScopePath;
	FVerseTextRange FunctionRange;
	FVerseTextRange DeclarationRange;
	FVerseTextRange BodyRange;
	FVerseTextRange ReturnTypeRange;
	TArray<FVerseFunctionNavigationParameter> Parameters;
	TArray<FVerseVisualTile> GraphTiles;
	int32 FirstDeclarationLine = INDEX_NONE;
	int32 LastDeclarationLine = INDEX_NONE;
	FVerseCanvasViewState ViewState;
	TSharedPtr<SVerseFunctionCanvas> FunctionCanvas;
	bool bHasViewState = false;
};

struct FOpenVerseDocument
{
	FString FilePath;
	TSharedPtr<FVerseDocumentSession> Session;
	TArray<uint8> LastKnownDiskBytes;
	FText LoadError;
	FText PropertyValidationMessage;
	TOptional<FString> PendingRenameText;
	TOptional<FString> PendingSpecifierText;
	bool bIsTemporary = false;
	FVerseCanvasViewState ViewState;
	TSharedPtr<SVerseFileCanvas> FileCanvas;
	TOptional<FVerseVisualTile> SelectedTile;
	TArray<FOpenVerseFunctionTab> FunctionTabs;
	int32 ActiveFunctionTabIndex = INDEX_NONE;
	FVerseCompilationResult CompilationResult;
	bool bHasCompilationResult = false;
	bool bCompilationPending = false;
	bool bCompilationInFlight = false;
	double CompileAfterSeconds = 0.0;
	uint64 CompilationRequestId = 0;
	FVerseDocumentRevision SemanticCompilationRevision;
	TArray<FVerseSemanticDiagnostic> SemanticCompilationDiagnostics;
	bool bSemanticCompilationPending = false;
	bool bHasSemanticCompilationResult = false;
};

namespace
{
	constexpr TCHAR SessionSection[] = TEXT("VerseVisualEditor.Session");

	DECLARE_DELEGATE_OneParam(FOnVerseExpressionChosen, TSharedPtr<FVerseExpressionAction>);
	FLinearColor GetBlueprintPinColor(const FString& VerseType);

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
				FText::FromString(InAction->SourceSpelling))
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
			const bool bIdentifier = ExpressionAction.IsValid()
				&& ExpressionAction->SourceForm
					== EVerseExpressionSourceForm::IdentifierReference;
			const FSlateBrush* Icon = FAppStyle::GetBrush(bIdentifier
				? TEXT("Kismet.AllClasses.VariableIcon")
				: TEXT("Kismet.AllClasses.FunctionIcon"));
			const FLinearColor IconColor = bIdentifier
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

	FText FormatSourceLines(int32 FirstLine, int32 LastLine)
	{
		if (FirstLine == INDEX_NONE || LastLine == INDEX_NONE)
		{
			return FText::GetEmpty();
		}
		return FText::FromString(FirstLine == LastLine
			? FString::Printf(TEXT("L%d"), FirstLine)
			: FString::Printf(TEXT("L%d-%d"), FirstLine, LastLine));
	}

	FLinearColor GetBlueprintPinColor(const FString& VerseType)
	{
		const UGraphEditorSettings* Settings = GetDefault<UGraphEditorSettings>();
		FString Type = VerseType.TrimStartAndEnd().ToLower();
		while (Type.RemoveFromStart(TEXT("?")) || Type.RemoveFromStart(TEXT("[]")))
		{
		}
		if (Type == TEXT("logic"))
		{
			return Settings->BooleanPinTypeColor;
		}
		if (Type == TEXT("int"))
		{
			return Settings->IntPinTypeColor;
		}
		if (Type == TEXT("float"))
		{
			return Settings->FloatPinTypeColor;
		}
		if (Type == TEXT("string"))
		{
			return Settings->StringPinTypeColor;
		}
		if (Type == TEXT("message"))
		{
			return Settings->TextPinTypeColor;
		}
		if (Type == TEXT("char"))
		{
			return Settings->BytePinTypeColor;
		}
		if (Type == TEXT("type"))
		{
			return Settings->ClassPinTypeColor;
		}
		return Settings->ObjectPinTypeColor;
	}

	const FSlateBrush* GetBlueprintPinBrush(const FString& VerseType)
	{
		return FAppStyle::GetBrush(
			VerseType.TrimStartAndEnd().StartsWith(TEXT("[]"))
				? "Graph.ArrayPin.Disconnected"
				: "Graph.Pin.Disconnected");
	}

	TSharedRef<SVerseTile> BuildFunctionGraphTile(
		const FVerseVisualTile& Tile,
		TSharedRef<const FVerseDocument> Document,
		FOnVerseSocketDragStarted OnSocketDragStarted)
	{
		const bool bExpression = Tile.Kind == EVerseVisualTileKind::Expression;
		const bool bFunctionBoundary = Tile.Kind == EVerseVisualTileKind::FunctionEntry
			|| Tile.Kind == EVerseVisualTileKind::FunctionReturn;
		const bool bIdentifier = bExpression
			&& Tile.ExpressionKind == EVerseExpressionKind::Identifier;
		const FLinearColor TileColor = bFunctionBoundary
			? FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("6a3083")))
			: bIdentifier
				? FLinearColor(0.025f, 0.025f, 0.035f, 1.0f)
				: FLinearColor(0.16f, 0.18f, 0.21f, 1.0f);
		TSharedRef<SWidget> Body = SNullWidget::NullWidget;
		if (bExpression && !bIdentifier)
		{
			Body = SNew(SBorder)
				.BorderImage(nullptr)
				.Padding(FMargin(10.0f, 7.0f))
				[
					SNew(STextBlock)
					.Text(FText::FromString(Document->DecodeOriginalRange(Tile.Range)))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
				];
		}
		return SNew(SVerseTile)
			.Tile(Tile)
			.Document(Document)
			.TileColor(TileColor)
			.UnselectedOutlineColor(FLinearColor::Black)
			.HeaderPadding(Tile.Kind == EVerseVisualTileKind::FunctionEntry
				? FMargin(10.0f, 7.0f, 10.0f, 8.0f)
				: FMargin(0.0f, 6.0f, 8.0f, 6.0f))
			.ShowBody(bExpression && !bIdentifier)
			.OnSocketDragStarted(OnSocketDragStarted)
			.BodyContent()
			[
				Body
			];
	}

	FString GetVisualTypeName(
		const FVerseTextRange& TypeRange,
		FName IntrinsicTypeName,
		const FVerseDocument& Document)
	{
		return TypeRange.IsSet()
			? Document.DecodeOriginalRange(TypeRange).TrimStartAndEnd()
			: IntrinsicTypeName.ToString();
	}

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

	struct FBuiltFunctionGraphRow
	{
		TSharedRef<SWidget> Widget;
		TSharedRef<SVerseTile> RootTile;
		TArray<FVerseGraphConnection> Connections;
	};

	FBuiltFunctionGraphRow BuildFunctionGraphRow(
		const FVerseVisualTile& Tile,
		TSharedRef<const FVerseDocument> Document,
		FOnVerseSocketDragStarted OnSocketDragStarted)
	{
		constexpr float OperandColumnWidth = 190.0f;
		constexpr float OperandWireSpace = 72.0f;
		const TSharedRef<SVerseTile> RootTile = BuildFunctionGraphTile(Tile, Document, OnSocketDragStarted);
		if (Tile.ExpressionKind != EVerseExpressionKind::Addition || Tile.Children.Num() != 2)
		{
			return {
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SBox).WidthOverride(OperandColumnWidth + OperandWireSpace)
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					RootTile
				],
				RootTile,
				{}};
		}

		const TSharedRef<SVerseTile> LeftOperand = BuildFunctionGraphTile(Tile.Children[0], Document, OnSocketDragStarted);
		const TSharedRef<SVerseTile> RightOperand = BuildFunctionGraphTile(Tile.Children[1], Document, OnSocketDragStarted);
		TSharedRef<SWidget> Subtree =
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)
			[
				SNew(SBox)
				.WidthOverride(OperandColumnWidth)
				.HAlign(HAlign_Right)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
					[
						LeftOperand
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 18.0f, 0.0f, 0.0f).HAlign(HAlign_Right)
					[
						RightOperand
					]
				]
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBox).WidthOverride(OperandWireSpace)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)
			[
				RootTile
			];

		const FString TypeName = GetVisualTypeName(Tile.TypeRange, Tile.IntrinsicTypeName, *Document);
		const FLinearColor WireColor = GetBlueprintPinColor(TypeName);
		TArray<FVerseGraphConnection> Connections;
		Connections.Add({LeftOperand->GetValueOutputAnchor(0), RootTile->GetValueInputAnchor(0),
			EVerseGraphConnectionAxis::Horizontal, WireColor, 2.0f, 0});
		Connections.Add({RightOperand->GetValueOutputAnchor(0), RootTile->GetValueInputAnchor(1),
			EVerseGraphConnectionAxis::Horizontal, WireColor, 2.0f, 0});
		return {Subtree, RootTile, MoveTemp(Connections)};
	}

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

	bool DiagnosticMatchesFile(const FSolDiagnostic& Diagnostic, const FString& FilePath)
	{
		FString DiagnosticPath = Diagnostic.Location.FilePath;
		FString DocumentPath = FilePath;
		FPaths::NormalizeFilename(DiagnosticPath);
		FPaths::NormalizeFilename(DocumentPath);
		if (DiagnosticPath.Equals(DocumentPath, ESearchCase::IgnoreCase))
		{
			return true;
		}

		DiagnosticPath.RemoveFromStart(TEXT("./"));
		return !DiagnosticPath.IsEmpty()
			&& DocumentPath.EndsWith(
				TEXT("/") + DiagnosticPath,
				ESearchCase::IgnoreCase);
	}

	const FVerseVisualTile* FindReplacementTile(
		TConstArrayView<FVerseVisualTile> Tiles,
		const FVerseVisualTile& PreviousTile)
	{
		for (const FVerseVisualTile& Tile : Tiles)
		{
			if (Tile.Kind == PreviousTile.Kind
				&& Tile.DefinitionKind == PreviousTile.DefinitionKind
				&& Tile.NameRange.IsSet()
				&& Tile.NameRange.BeginByte == PreviousTile.NameRange.BeginByte)
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

	const FVerseVisualTile* FindTileByRange(
		TConstArrayView<FVerseVisualTile> Tiles,
		FVerseTextRange Range)
	{
		for (const FVerseVisualTile& Tile : Tiles)
		{
			if (Tile.Range == Range)
			{
				return &Tile;
			}
			if (const FVerseVisualTile* Nested = FindTileByRange(Tile.Children, Range))
			{
				return Nested;
			}
		}
		return nullptr;
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

	const FVerseFunctionNavigationItem* FindFunctionNavigationItem(
		TConstArrayView<FVerseFunctionNavigationItem> Items,
		const FOpenVerseFunctionTab& Tab)
	{
		if (const FVerseFunctionNavigationItem* ByPath = Items.FindByPredicate(
			[&Tab](const FVerseFunctionNavigationItem& Item)
			{
				return Item.ScopePath == Tab.ScopePath;
			}))
		{
			return ByPath;
		}
		return Items.FindByPredicate([&Tab](const FVerseFunctionNavigationItem& Item)
		{
			return Item.FunctionRange.BeginByte == Tab.FunctionRange.BeginByte;
		});
	}

	void ReconcileFunctionTabs(FOpenVerseDocument& Document)
	{
		if (!Document.Session.IsValid())
		{
			Document.FunctionTabs.Reset();
			Document.ActiveFunctionTabIndex = INDEX_NONE;
			return;
		}

		const TArray<FVerseFunctionNavigationItem> Items = FVerseFunctionNavigationBuilder::Build(
			Document.Session->GetTiles(),
			Document.Session->GetParseSnapshot());
		for (int32 Index = Document.FunctionTabs.Num() - 1; Index >= 0; --Index)
		{
			FOpenVerseFunctionTab& Tab = Document.FunctionTabs[Index];
			const FVerseFunctionNavigationItem* Item = FindFunctionNavigationItem(Items, Tab);
			if (!Item)
			{
				Document.FunctionTabs.RemoveAt(Index);
				if (Document.ActiveFunctionTabIndex == Index)
				{
					Document.ActiveFunctionTabIndex = INDEX_NONE;
				}
				else if (Document.ActiveFunctionTabIndex > Index)
				{
					--Document.ActiveFunctionTabIndex;
				}
				continue;
			}
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
		}
	}
}

void SVerseVisualEditor::Construct(const FArguments& InArgs)
{
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
									SNew(STextBlock)
									.Text(this, &SVerseVisualEditor::GetLocalCompileDiagnosticsText)
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

void SVerseVisualEditor::Tick(
	const FGeometry& AllottedGeometry,
	const double InCurrentTime,
	const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	if (SemanticWorkspace)
	{
		SemanticWorkspace->Tick(FPlatformTime::Seconds());
		PublishCompletedSemanticCompilations();
	}
	const EVerseCompilationMode PreferredMode =
		GetDefault<UVerseVisualEditorSettings>()->CompilationMode;
	if (PreferredMode != CompilationMode)
	{
		SetCompilationMode(PreferredMode);
	}
	for (const TSharedPtr<FOpenVerseDocument>& OpenDocument : OpenDocuments)
	{
		if (OpenDocument.IsValid()
			&& OpenDocument->bCompilationPending
			&& InCurrentTime >= OpenDocument->CompileAfterSeconds)
		{
			StartCompilation(OpenDocument);
		}
	}
}

TArray<FVerseSemanticDocumentInput> SVerseVisualEditor::CollectSemanticDocumentInputs(
	bool bOnlyCleanDocuments) const
{
	TArray<FVerseSemanticDocumentInput> Documents;
	Documents.Reserve(OpenDocuments.Num());
	for (const TSharedPtr<FOpenVerseDocument>& OpenDocument : OpenDocuments)
	{
		if (!OpenDocument.IsValid()
			|| !OpenDocument->Session.IsValid()
			|| (bOnlyCleanDocuments && OpenDocument->Session->IsDirty()))
		{
			continue;
		}

		FVerseSemanticDocumentInput& Input = Documents.AddDefaulted_GetRef();
		Input.FilePath = OpenDocument->FilePath;
		Input.Source = OpenDocument->Session->GetCurrentUtf8();
		Input.Revision = OpenDocument->Session->GetRevision();
	}
	return Documents;
}

void SVerseVisualEditor::QueueSemanticAnalysis(bool bDebounce)
{
	if (SemanticWorkspace)
	{
		SemanticWorkspace->RequestAnalysis(
			CollectSemanticDocumentInputs(),
			FPlatformTime::Seconds(),
			bDebounce);
	}
}

void SVerseVisualEditor::RequestSemanticCompilation(
	const TSharedPtr<FOpenVerseDocument>& OpenDocument)
{
	if (!SemanticWorkspace || !OpenDocument.IsValid() || !OpenDocument->Session.IsValid())
	{
		return;
	}

	OpenDocument->SemanticCompilationRevision = OpenDocument->Session->GetRevision();
	OpenDocument->SemanticCompilationDiagnostics.Reset();
	OpenDocument->bSemanticCompilationPending = true;
	OpenDocument->bHasSemanticCompilationResult = false;
	QueueSemanticAnalysis(false);
}

void SVerseVisualEditor::PublishCompletedSemanticCompilations()
{
	if (!SemanticWorkspace
		|| (SemanticWorkspace->GetState() != EVerseSemanticWorkspaceState::Ready
			&& SemanticWorkspace->GetState() != EVerseSemanticWorkspaceState::Failed))
	{
		return;
	}

	for (const TSharedPtr<FOpenVerseDocument>& OpenDocument : OpenDocuments)
	{
		if (!OpenDocument.IsValid()
			|| !OpenDocument->Session.IsValid()
			|| !OpenDocument->bSemanticCompilationPending
			|| OpenDocument->Session->GetRevision() != OpenDocument->SemanticCompilationRevision
			|| !SemanticWorkspace->LatestAnalysisDescribes(
				OpenDocument->FilePath,
				OpenDocument->SemanticCompilationRevision))
		{
			continue;
		}

		OpenDocument->SemanticCompilationDiagnostics.Reset();
		for (const FVerseSemanticDiagnostic& Diagnostic : SemanticWorkspace->GetDiagnostics())
		{
			if (Diagnostic.AppliesToFile(OpenDocument->FilePath))
			{
				OpenDocument->SemanticCompilationDiagnostics.Add(Diagnostic);
			}
		}
		OpenDocument->bSemanticCompilationPending = false;
		OpenDocument->bHasSemanticCompilationResult = true;

		const bool bHasErrors = OpenDocument->SemanticCompilationDiagnostics.ContainsByPredicate(
			[](const FVerseSemanticDiagnostic& Diagnostic)
			{
				return Diagnostic.Severity == ELogVerbosity::Error
					|| Diagnostic.Severity == ELogVerbosity::Fatal;
			});
		if (OpenDocument == ActiveDocument && bHasErrors)
		{
			bLocalCompilePanelOpen = true;
		}
	}
}

bool SVerseVisualEditor::HasLocalCompileDiagnosticsForActiveDocument() const
{
	return ActiveDocument.IsValid()
		&& (!ActiveDocument->LoadError.IsEmpty()
			|| (ActiveDocument->bHasSemanticCompilationResult
				&& !ActiveDocument->SemanticCompilationDiagnostics.IsEmpty()));
}

FText SVerseVisualEditor::GetLocalCompileDiagnosticsText() const
{
	if (!ActiveDocument.IsValid())
	{
		return LOCTEXT("NoLocalCompileErrors", "No local compile errors.");
	}

	TArray<FString> Lines;
	if (!ActiveDocument->LoadError.IsEmpty())
	{
		Lines.Add(FString::Printf(TEXT("Error: %s"), *ActiveDocument->LoadError.ToString()));
	}
	if (ActiveDocument->bHasSemanticCompilationResult)
	{
		for (const FVerseSemanticDiagnostic& Diagnostic : ActiveDocument->SemanticCompilationDiagnostics)
		{
			const TCHAR* Severity = Diagnostic.Severity == ELogVerbosity::Warning
				? TEXT("Warning")
				: Diagnostic.Severity == ELogVerbosity::Error || Diagnostic.Severity == ELogVerbosity::Fatal
					? TEXT("Error")
					: TEXT("Info");
			const FString Location = Diagnostic.RowSpan.X > 0
				? FString::Printf(TEXT(" (L%d)"), Diagnostic.RowSpan.X)
				: FString();
			Lines.Add(FString::Printf(
				TEXT("%s%s: %s"),
				Severity,
				*Location,
				*Diagnostic.Message.ToString()));
		}
	}
	if (Lines.IsEmpty())
	{
		return LOCTEXT("NoLocalCompileErrors", "No local compile errors.");
	}
	return FText::FromString(FString::Join(Lines, TEXT("\n")));
}

FReply SVerseVisualEditor::CloseLocalCompilePanel()
{
	bLocalCompilePanelOpen = false;
	return FReply::Handled();
}

TSharedRef<SWidget> SVerseVisualEditor::BuildToolbar()
{
	FSlimHorizontalToolBarBuilder ToolbarBuilder(nullptr, FMultiBoxCustomization::None);
	ToolbarBuilder.SetStyle(&FAppStyle::Get(), "AssetEditorToolbar");
	ToolbarBuilder.AddToolBarButton(
		FUIAction(
			FExecuteAction::CreateSP(this, &SVerseVisualEditor::SaveActiveDocumentFromMenu),
			FCanExecuteAction::CreateSP(this, &SVerseVisualEditor::CanSaveActiveDocument)),
		NAME_None,
		FText::GetEmpty(),
		LOCTEXT("SaveActiveDocumentTooltip", "Save Active Verse File (Ctrl+S)"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Save"));

	ToolbarBuilder.AddSeparator();
	ToolbarBuilder.BeginStyleOverride("CalloutToolbar");
	ToolbarBuilder.AddToolBarButton(
		FUIAction(
			FExecuteAction::CreateSP(this, &SVerseVisualEditor::CompileVerseProject),
			FCanExecuteAction::CreateSP(this, &SVerseVisualEditor::CanCompileVerseProject)),
		NAME_None,
		LOCTEXT("CompileActiveDocument", "Compile Verse"),
		TAttribute<FText>::Create(
			TAttribute<FText>::FGetter::CreateSP(this, &SVerseVisualEditor::GetCompileVerseTooltip)),
		TAttribute<FSlateIcon>::Create(
			TAttribute<FSlateIcon>::FGetter::CreateSP(this, &SVerseVisualEditor::GetCompileVerseIcon)));
	ToolbarBuilder.EndStyleOverride();

	ToolbarBuilder.AddWidget(
		SNew(SComboButton)
		.ButtonStyle(FAppStyle::Get(), "SimpleButton")
		.OnGetMenuContent(this, &SVerseVisualEditor::BuildCompilationModeMenu)
		.ButtonContent()
		[
			SNew(STextBlock)
			.Text(this, &SVerseVisualEditor::GetCompilationModeText)
		],
		NAME_None,
		false,
		HAlign_Left);
	return ToolbarBuilder.MakeWidget();
}

void SVerseVisualEditor::CompileVerseProject()
{
	// BuildScripts only knows registered Solaris packages. Schedule the same
	// explicit compile result for private and unsaved editor buffers as well.
	for (const TSharedPtr<FOpenVerseDocument>& OpenDocument : OpenDocuments)
	{
		RequestSemanticCompilation(OpenDocument);
	}
	if (ISolarisEditorModule::IsModuleLoaded())
	{
		ISolarisEditorModule::Get().BuildScripts(
			ISolarisEditorModule::EBuildScriptsInstigator::User);
	}
}

bool SVerseVisualEditor::CanCompileVerseProject() const
{
	return ISolarisEditorModule::IsModuleLoaded();
}

FSlateIcon SVerseVisualEditor::GetCompileVerseIcon() const
{
	const TCHAR* IconName = TEXT("SolarisEditor.BuildScripts");
	if (ProjectBuildState == EVerseProjectBuildState::Building
		|| (ActiveDocument.IsValid() && ActiveDocument->bSemanticCompilationPending))
	{
		return FSlateIcon("SolarisEditorStyle", TEXT("SolarisEditor.BuildScriptsLoading"));
	}
	if (ActiveDocument.IsValid()
		&& ActiveDocument->bHasSemanticCompilationResult
		&& ActiveDocument->SemanticCompilationDiagnostics.ContainsByPredicate(
			[](const FVerseSemanticDiagnostic& Diagnostic)
			{
				return Diagnostic.Severity == ELogVerbosity::Error
					|| Diagnostic.Severity == ELogVerbosity::Fatal;
			}))
	{
		return FSlateIcon("SolarisEditorStyle", TEXT("SolarisEditor.BuildScriptsError"));
	}
	switch (ProjectBuildState)
	{
	case EVerseProjectBuildState::Success:
		IconName = TEXT("SolarisEditor.BuildScriptsSuccess");
		break;
	case EVerseProjectBuildState::Warnings:
		IconName = TEXT("SolarisEditor.BuildScriptsWarning");
		break;
	case EVerseProjectBuildState::Errors:
		IconName = TEXT("SolarisEditor.BuildScriptsError");
		break;
	case EVerseProjectBuildState::Unbuilt:
	default:
		break;
	}
	return FSlateIcon("SolarisEditorStyle", IconName);
}

FText SVerseVisualEditor::GetCompileVerseTooltip() const
{
	if (ProjectBuildState == EVerseProjectBuildState::Building
		|| (ActiveDocument.IsValid() && ActiveDocument->bSemanticCompilationPending))
	{
		return LOCTEXT("VerseBuildInProgress", "Build in progress...");
	}
	if (ActiveDocument.IsValid() && ActiveDocument->bHasSemanticCompilationResult)
	{
		int32 LocalErrorCount = 0;
		for (const FVerseSemanticDiagnostic& Diagnostic : ActiveDocument->SemanticCompilationDiagnostics)
		{
			if (Diagnostic.Severity == ELogVerbosity::Error
				|| Diagnostic.Severity == ELogVerbosity::Fatal)
			{
				++LocalErrorCount;
			}
		}
		if (LocalErrorCount > 0)
		{
			return FText::Format(
				LOCTEXT("PrivateVerseBuildErrors", "Built with {0} local {0}|plural(one=error,other=errors)."),
				LocalErrorCount);
		}
	}
	switch (ProjectBuildState)
	{
	case EVerseProjectBuildState::Success:
		return LOCTEXT("VerseBuildSucceeded", "Built successfully.");
	case EVerseProjectBuildState::Warnings:
		return FText::Format(
			LOCTEXT("VerseBuildWarnings", "Built with {0} {0}|plural(one=warning,other=warnings)."),
			ProjectBuildWarningCount);
	case EVerseProjectBuildState::Errors:
		return FText::Format(
			LOCTEXT("VerseBuildErrors", "Built with {0} {0}|plural(one=error,other=errors)."),
			ProjectBuildErrorCount);
	case EVerseProjectBuildState::Unbuilt:
	default:
		return LOCTEXT("CompileVerseProjectTooltip", "Compile all Verse code in project");
	}
}

void SVerseVisualEditor::HandleProjectBuildStarted(
	const TSharedRef<FSolBuildResults>& BuildResults)
{
	ProjectBuildState = EVerseProjectBuildState::Building;
	ProjectBuildWarningCount = 0;
	ProjectBuildErrorCount = 0;
	for (const TSharedPtr<FOpenVerseDocument>& OpenDocument : OpenDocuments)
	{
		if (OpenDocument.IsValid() && OpenDocument->Session.IsValid())
		{
			RequestSemanticCompilation(OpenDocument);
			if (OpenDocument->Session->IsDirty())
			{
				StartCompilation(OpenDocument);
			}
		}
	}
}

void SVerseVisualEditor::HandleProjectBuildComplete(
	const TSharedRef<FSolBuildResults>& BuildResults)
{
	ProjectBuildWarningCount = 0;
	ProjectBuildErrorCount = 0;
	auto CountDiagnostics = [this](TConstArrayView<FSolDiagnostic> Diagnostics)
	{
		for (const FSolDiagnostic& Diagnostic : Diagnostics)
		{
			ProjectBuildWarningCount += Diagnostic.Info.Severity == ELogVerbosity::Warning ? 1 : 0;
			ProjectBuildErrorCount += Diagnostic.IsBuildFailure() ? 1 : 0;
		}
	};
	CountDiagnostics(BuildResults->BuildDiagnostics);
	CountDiagnostics(BuildResults->BuildAssetsDigestDiagnostics);
	ProjectBuildState = ProjectBuildErrorCount > 0
		? EVerseProjectBuildState::Errors
		: ProjectBuildWarningCount > 0
			? EVerseProjectBuildState::Warnings
			: EVerseProjectBuildState::Success;
	if (SemanticWorkspace)
	{
		// A failed user-package build can still leave Solaris with a useful
		// program containing compiled dependencies, native APIs, and intrinsics.
		// Only a successful build may describe exact on-disk document revisions.
		SemanticWorkspace->RefreshCompiledBaseline(
			ProjectBuildErrorCount == 0
				? CollectSemanticDocumentInputs(true)
				: TArray<FVerseSemanticDocumentInput>());
		QueueSemanticAnalysis(false);
	}

	for (const TSharedPtr<FOpenVerseDocument>& OpenDocument : OpenDocuments)
	{
		ApplyProjectDiagnostics(OpenDocument, BuildResults->BuildDiagnostics);
	}
}

void SVerseVisualEditor::ApplyProjectDiagnostics(
	const TSharedPtr<FOpenVerseDocument>& OpenDocument,
	TConstArrayView<FSolDiagnostic> ProjectDiagnostics)
{
	if (!OpenDocument.IsValid()
		|| !OpenDocument->Session.IsValid()
		|| OpenDocument->Session->IsDirty())
	{
		return;
	}

	TArray<FSolDiagnostic> FileDiagnostics;
	for (const FSolDiagnostic& Diagnostic : ProjectDiagnostics)
	{
		if (DiagnosticMatchesFile(Diagnostic, OpenDocument->FilePath))
		{
			FileDiagnostics.Add(Diagnostic);
		}
	}

	FVerseCompilationResult ProjectResult = VerseCompilation::FromProjectBuildDiagnostics(
		FUtf8StringView(OpenDocument->Session->GetCurrentUtf8()),
		OpenDocument->Session->GetRevision(),
		FileDiagnostics);
	FVerseCompilationResult AcceptedResult;
	if (!VerseCompilation::TryAcceptResult(
		MoveTemp(ProjectResult),
		OpenDocument->Session->GetRevision(),
		OpenDocument->Session->GetTiles(),
		AcceptedResult))
	{
		return;
	}

	OpenDocument->CompilationResult = MoveTemp(AcceptedResult);
	OpenDocument->bHasCompilationResult = true;
	if (OpenDocument == ActiveDocument)
	{
		RefreshActiveDocument();
	}
}

TSharedRef<SWidget> SVerseVisualEditor::BuildCompilationModeMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);
	auto AddMode = [this, &MenuBuilder](
		EVerseCompilationMode Mode,
		const FText& Label,
		const FText& ToolTip)
	{
		MenuBuilder.AddMenuEntry(
			Label,
			ToolTip,
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SVerseVisualEditor::SetCompilationMode, Mode),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([this, Mode]()
				{
					return CompilationMode == Mode;
				})),
			NAME_None,
			EUserInterfaceActionType::RadioButton);
	};

	AddMode(
		EVerseCompilationMode::Continuous,
		LOCTEXT("ContinuousCompilationMode", "Continuous"),
		LOCTEXT("ContinuousCompilationModeTooltip", "Compile shortly after each source edit."));
	AddMode(
		EVerseCompilationMode::OnSave,
		LOCTEXT("OnSaveCompilationMode", "Compile on Save"),
		LOCTEXT("OnSaveCompilationModeTooltip", "Compile after a Verse file is saved."));
	AddMode(
		EVerseCompilationMode::Manual,
		LOCTEXT("ManualCompilationMode", "Manual"),
		LOCTEXT("ManualCompilationModeTooltip", "Compile only when the Compile button is pressed."));
	return MenuBuilder.MakeWidget();
}

void SVerseVisualEditor::SetCompilationMode(EVerseCompilationMode Mode)
{
	if (CompilationMode == Mode)
	{
		return;
	}

	CompilationMode = Mode;
	UVerseVisualEditorSettings* Settings = GetMutableDefault<UVerseVisualEditorSettings>();
	Settings->CompilationMode = Mode;
	Settings->SaveConfig();
	for (const TSharedPtr<FOpenVerseDocument>& OpenDocument : OpenDocuments)
	{
		if (OpenDocument.IsValid())
		{
			OpenDocument->bCompilationPending = false;
			if (Mode == EVerseCompilationMode::Continuous)
			{
				QueueCompilation(OpenDocument, true);
			}
		}
	}
}

FText SVerseVisualEditor::GetCompilationModeText() const
{
	switch (CompilationMode)
	{
	case EVerseCompilationMode::Continuous:
		return LOCTEXT("ContinuousCompilationModeButton", "Continuous");
	case EVerseCompilationMode::OnSave:
		return LOCTEXT("OnSaveCompilationModeButton", "On Save");
	case EVerseCompilationMode::Manual:
	default:
		return LOCTEXT("ManualCompilationModeButton", "Manual");
	}
}

void SVerseVisualEditor::QueueCompilation(
	const TSharedPtr<FOpenVerseDocument>& OpenDocument,
	bool bDebounce)
{
	if (!OpenDocument.IsValid() || !OpenDocument->Session.IsValid())
	{
		return;
	}
	if (!bDebounce)
	{
		StartCompilation(OpenDocument);
		return;
	}

	OpenDocument->bCompilationPending = true;
	OpenDocument->CompileAfterSeconds = FPlatformTime::Seconds() + 0.35;
}

void SVerseVisualEditor::StartCompilation(const TSharedPtr<FOpenVerseDocument>& OpenDocument)
{
	if (!OpenDocument.IsValid() || !OpenDocument->Session.IsValid())
	{
		return;
	}
	RequestSemanticCompilation(OpenDocument);

	OpenDocument->bCompilationPending = false;
	OpenDocument->bCompilationInFlight = true;
	const uint64 RequestId = ++OpenDocument->CompilationRequestId;
	const FVerseDocumentRevision Revision = OpenDocument->Session->GetRevision();
	FUtf8String Source = OpenDocument->Session->GetCurrentUtf8();
	FString SourcePath = OpenDocument->FilePath;
	const TWeakPtr<SVerseVisualEditor> WeakEditor = SharedThis(this);
	const TWeakPtr<FOpenVerseDocument> WeakDocument = OpenDocument;

	(void)Async(EAsyncExecution::ThreadPool,
		[WeakEditor,
		 WeakDocument,
		 RequestId,
		 Revision,
		 Source = MoveTemp(Source),
		 SourcePath = MoveTemp(SourcePath)]() mutable
		{
			FVerseCompilationResult Result = VerseCompilation::Compile(
				MoveTemp(Source),
				Revision,
				MoveTemp(SourcePath));
			AsyncTask(ENamedThreads::GameThread,
				[WeakEditor, WeakDocument, RequestId, Result = MoveTemp(Result)]() mutable
				{
					const TSharedPtr<SVerseVisualEditor> Editor = WeakEditor.Pin();
					const TSharedPtr<FOpenVerseDocument> Document = WeakDocument.Pin();
					if (Editor.IsValid() && Document.IsValid())
					{
						Editor->ApplyCompilationResult(Document, RequestId, MoveTemp(Result));
					}
				});
		});
}

void SVerseVisualEditor::ApplyCompilationResult(
	const TSharedPtr<FOpenVerseDocument>& OpenDocument,
	uint64 RequestId,
	FVerseCompilationResult Result)
{
	if (!OpenDocument.IsValid()
		|| !OpenDocument->Session.IsValid()
		|| !OpenDocuments.Contains(OpenDocument)
		|| OpenDocument->CompilationRequestId != RequestId)
	{
		return;
	}

	OpenDocument->bCompilationInFlight = false;
	FVerseCompilationResult AcceptedResult;
	if (!VerseCompilation::TryAcceptResult(
		MoveTemp(Result),
		OpenDocument->Session->GetRevision(),
		OpenDocument->Session->GetTiles(),
		AcceptedResult))
	{
		return;
	}

	OpenDocument->CompilationResult = MoveTemp(AcceptedResult);
	OpenDocument->bHasCompilationResult = true;
	if (OpenDocument == ActiveDocument)
	{
		RefreshActiveDocument();
	}
}

void SVerseVisualEditor::InvalidateCompilationResult(
	const TSharedPtr<FOpenVerseDocument>& OpenDocument)
{
	if (!OpenDocument.IsValid())
	{
		return;
	}
	++OpenDocument->CompilationRequestId;
	OpenDocument->CompilationResult = {};
	OpenDocument->bHasCompilationResult = false;
	OpenDocument->bCompilationInFlight = false;
	OpenDocument->SemanticCompilationDiagnostics.Reset();
	OpenDocument->bSemanticCompilationPending = false;
	OpenDocument->bHasSemanticCompilationResult = false;
}

FReply SVerseVisualEditor::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
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

FReply SVerseVisualEditor::BeginSocketDrag(const FVerseSocketDragStart& DragStart)
{
	if (!DragStart.Anchor.IsValid() || !ActiveDocument.IsValid()
		|| !ActiveDocument->FunctionTabs.IsValidIndex(ActiveDocument->ActiveFunctionTabIndex))
	{
		return FReply::Unhandled();
	}
	FinishExpressionSearch();
	SocketDrag = DragStart;
	FOpenVerseFunctionTab& Tab =
		ActiveDocument->FunctionTabs[ActiveDocument->ActiveFunctionTabIndex];
	return Tab.FunctionCanvas.IsValid()
		? Tab.FunctionCanvas->BeginConnectionDrag(DragStart)
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
	ExpressionActions = FVerseExpressionActionQuery::Build(
		Tab.Parameters,
		SocketDrag->Socket,
		SocketDrag->bOutput,
		Document,
		SocketDrag->Tile.Range,
		ActiveDocument->FilePath,
		SemanticSnapshots);
	const FString SocketType = GetVisualTypeName(
		SocketDrag->Socket.TypeRange,
		SocketDrag->Socket.IntrinsicTypeName,
		Document);
	const FText ContextDescription = FText::Format(
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
		.ContextTypeColor(GetBlueprintPinColor(SocketType))
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
	if (!TryApplyVerseExpressionAction(
		*ActiveDocument->Session,
		SocketDrag->Tile.Range,
		*Action,
		Error))
	{
		ActiveDocument->LoadError = Error;
		bLocalCompilePanelOpen = true;
		return;
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
	ReconcileFunctionTabs(*ActiveDocument);
	RebuildDocumentTabs();
	RefreshActiveDocument();
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

void SVerseVisualEditor::RefreshActiveDocument()
{
	RefreshOutliner();
	if (!ActiveDocumentBox.IsValid())
	{
		return;
	}
	if (!ActiveDocument.IsValid())
	{
		CaptureActiveCanvasView();
		ScopeBreadcrumbBox.Reset();
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

	ReconcileFunctionTabs(*ActiveDocument);
	const bool bShowingFunction =
		ActiveDocument->FunctionTabs.IsValidIndex(ActiveDocument->ActiveFunctionTabIndex);
	const bool bCanReuseCanvas = bShowingFunction
		? ActiveDocument->FunctionTabs[ActiveDocument->ActiveFunctionTabIndex].FunctionCanvas.IsValid()
		: ActiveDocument->FileCanvas.IsValid();
	if (!bCanReuseCanvas)
	{
		CaptureActiveCanvasView();
	}
	const TOptional<FVerseTextRange> InitialSelectedRange = ActiveDocument->SelectedTile.IsSet()
		? TOptional<FVerseTextRange>(ActiveDocument->SelectedTile->Range)
		: TOptional<FVerseTextRange>();
	TSharedRef<SWidget> ActiveView = SNullWidget::NullWidget;
	if (ActiveDocument->FunctionTabs.IsValidIndex(ActiveDocument->ActiveFunctionTabIndex))
	{
		FOpenVerseFunctionTab& FunctionTab =
			ActiveDocument->FunctionTabs[ActiveDocument->ActiveFunctionTabIndex];
		ActiveDocument->FileCanvas.Reset();
		TSharedPtr<SWidget> FunctionEntryAnchor;
		const TSharedRef<const FVerseDocument> SourceDocument =
			ActiveDocument->Session->GetParseSnapshot().GetDocument();
		TSharedRef<SVerticalBox> FunctionContent = SNew(SVerticalBox);
		TArray<FVerseGraphConnection> GraphConnections;
		TArray<TSharedPtr<SVerseTile>> RootTiles;
		TSharedPtr<SVerseTile> ImplicitReturnSourceTile;
		TSharedPtr<SVerseTile> ReturnTile;
		for (int32 Index = 0; Index < FunctionTab.GraphTiles.Num(); ++Index)
		{
			const FVerseVisualTile& Tile = FunctionTab.GraphTiles[Index];
			if (Index > 0 && FunctionTab.GraphTiles[Index - 1].ExtraBlankLineCount > 0)
			{
				FunctionContent->AddSlot()
				.AutoHeight()
				[
					SNew(SBox).HeightOverride(
						FunctionTab.GraphTiles[Index - 1].ExtraBlankLineCount * 24.0f)
				];
			}
			const FBuiltFunctionGraphRow GraphRow = BuildFunctionGraphRow(
				Tile,
				SourceDocument,
				FOnVerseSocketDragStarted::CreateSP(this, &SVerseVisualEditor::BeginSocketDrag));
			FunctionContent->AddSlot()
			.AutoHeight()
			.HAlign(HAlign_Left)
			[
				GraphRow.Widget
			];
			GraphConnections.Append(GraphRow.Connections);
			RootTiles.Add(GraphRow.RootTile);
			if (Tile.Kind == EVerseVisualTileKind::FunctionEntry)
			{
				FunctionEntryAnchor = GraphRow.RootTile;
			}
			if (Tile.bImplicitReturnValue)
			{
				ImplicitReturnSourceTile = GraphRow.RootTile;
			}
			else if (Tile.Kind == EVerseVisualTileKind::FunctionReturn)
			{
				ReturnTile = GraphRow.RootTile;
			}
		}
		for (int32 Index = 1; Index < RootTiles.Num(); ++Index)
		{
			const TSharedPtr<SWidget> Source = RootTiles[Index - 1]->GetExecutionOutputAnchor();
			const TSharedPtr<SWidget> Target = RootTiles[Index]->GetExecutionInputAnchor();
			if (Source.IsValid() && Target.IsValid())
			{
				GraphConnections.Add({Source, Target, EVerseGraphConnectionAxis::Vertical,
					FLinearColor::White, 2.5f,
					FunctionTab.GraphTiles[Index - 1].ExtraBlankLineCount,
					FVector2D(0.5f, 8.0f / 48.0f),
					FVector2D(0.5f, 24.0f / 32.0f)});
			}
		}
		if (ImplicitReturnSourceTile.IsValid() && ReturnTile.IsValid())
		{
			const FString ReturnType = GetVisualTypeName(
				FunctionTab.GraphTiles.Last().TypeRange,
				FunctionTab.GraphTiles.Last().IntrinsicTypeName,
				*SourceDocument);
			GraphConnections.Add({ImplicitReturnSourceTile->GetFirstValueOutputAnchor(),
				ReturnTile->GetFirstValueInputAnchor(), EVerseGraphConnectionAxis::Horizontal,
				GetBlueprintPinColor(ReturnType), 2.0f, 0});
		}

		if (FunctionTab.FunctionCanvas.IsValid())
		{
			FunctionTab.FunctionCanvas->RefreshContent(
				FunctionContent,
				MoveTemp(GraphConnections),
				FunctionEntryAnchor);
			ActiveView = FunctionTab.FunctionCanvas.ToSharedRef();
		}
		else
		{
			ActiveView = SAssignNew(
				FunctionTab.FunctionCanvas,
				SVerseFunctionCanvas,
				FunctionTab.ViewState,
				!FunctionTab.bHasViewState)
				.InitialAnchor(FunctionEntryAnchor)
				.Connections(GraphConnections)
				.OnConnectionDropped(FOnVerseGraphConnectionDropped::CreateSP(
					this, &SVerseVisualEditor::HandleConnectionDropped))
				.OnConnectionCancelled(FSimpleDelegate::CreateSP(
					this, &SVerseVisualEditor::HandleConnectionCancelled))
				[
					FunctionContent
				];
		}
	}
	else
	{
		TArray<FVerseCompilationDiagnostic> Diagnostics = ActiveDocument->bHasCompilationResult
			? ActiveDocument->CompilationResult.Diagnostics
			: TArray<FVerseCompilationDiagnostic>();
		if (ActiveDocument->FileCanvas.IsValid())
		{
			ActiveDocument->FileCanvas->RefreshContent(
				ActiveDocument->Session.ToSharedRef(),
				InitialSelectedRange,
				MoveTemp(Diagnostics));
			ActiveView = ActiveDocument->FileCanvas.ToSharedRef();
		}
		else
		{
			ActiveView = SAssignNew(
				ActiveDocument->FileCanvas,
				SVerseFileCanvas,
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
				.Diagnostics(Diagnostics)
				.OnFunctionOpened(FOnVerseFunctionOpened::CreateSP(
					this,
					&SVerseVisualEditor::OpenFunctionView,
					ActiveDocument));
		}
	}
	ActiveDocumentBox->SetContent(
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(0.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew(ScopeBreadcrumbBox, SBox)
				[
					BuildScopeBreadcrumb(ActiveDocument)
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(30.0f, 0.0f, 0.0f, 0.0f)
			[
				BuildFunctionTabBar(ActiveDocument)
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			.Padding(8.0f, 0.0f, 8.0f, 8.0f)
			[
				ActiveView
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
			if (Property.EditKind == EVerseTilePropertyEditKind::AccessSpecifiers
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
