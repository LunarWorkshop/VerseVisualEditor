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
	DECLARE_DELEGATE_RetVal_OneParam(
		FReply, FOnVerseFunctionGraphTileSelected, const FVerseVisualTile&);
	DECLARE_DELEGATE_RetVal_OneParam(
		bool, FIsVerseFunctionGraphTileSelected, FVerseTextRange);

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

	const FSlateBrush* GetBlueprintPinBrush(const FString& VerseType)
	{
		return FAppStyle::GetBrush(
			VerseType.TrimStartAndEnd().StartsWith(TEXT("[]"))
				? "Graph.ArrayPin.Disconnected"
				: "Graph.Pin.Disconnected");
	}

	struct FVerseTileWidgetRegistry
	{
		TMap<FVerseVisualTileId, TSharedPtr<SVerseTile>> Tiles;
		TSharedRef<FVerseGraphEndpointRegistry> Endpoints =
			MakeShared<FVerseGraphEndpointRegistry>();

		void Add(FVerseVisualTileId Id, TSharedPtr<SVerseTile> Widget)
		{
			Tiles.Add(Id, MoveTemp(Widget));
		}
		const TSharedPtr<SVerseTile>* Find(FVerseVisualTileId Id) const
		{
			return Tiles.Find(Id);
		}
	};
	TArray<FVerseGraphConnection> ResolveModelConnections(
		TConstArrayView<FVerseVisualConnection> ModelConnections,
		const FVerseTileWidgetRegistry& Widgets,
		TSharedRef<const FVerseDocument> Document,
		FVerseGraphRenderScopeId RenderScope = FVerseGraphRenderScopeId::Root(),
		EVerseFunctionGraphPresentation Presentation =
			EVerseFunctionGraphPresentation::VerticalExecution);

	TSharedRef<SVerseTile> BuildFunctionGraphTile(
		const FVerseVisualTile& Tile,
		TSharedRef<const FVerseDocument> Document,
		FOnVerseSocketDragStarted OnSocketDragStarted,
		FOnVerseInlineLiteralCommitted OnInlineLiteralCommitted,
		TSharedPtr<SWidget> BodyOverride = nullptr,
		bool bCompactExecutionSpacing = false,
		TSharedPtr<SVerseGraphRenderScope> BodyRenderScope = nullptr,
		FOnVerseFunctionGraphTileSelected OnTileSelected = {},
		FIsVerseFunctionGraphTileSelected IsTileSelected = {},
		FOnVerseClauseReordered OnClauseReordered = {},
		TConstArrayView<FVerseVisualConnection> ModelConnections = {},
		FVerseTileWidgetRegistry* WidgetRegistry = nullptr,
		TSharedPtr<SVerseGraphRenderScope> OwningRenderScope = nullptr,
		EVerseFunctionGraphPresentation Presentation =
			EVerseFunctionGraphPresentation::VerticalExecution,
		TOptional<float> ClauseInsertionBodySpineX = {})
	{
		TSet<FVerseVisualSocketId> ConnectedSockets;
		for (const FVerseVisualConnection& Connection : ModelConnections)
		{
			if (Connection.Source.Tile == Tile.Id)
			{
				ConnectedSockets.Add(Connection.Source.Socket);
			}
			if (Connection.Target.Tile == Tile.Id)
			{
				ConnectedSockets.Add(Connection.Target.Socket);
			}
		}
		const bool bExpression = Tile.Kind == EVerseVisualTileKind::Expression;
		const bool bFailableBlock = Tile.Kind == EVerseVisualTileKind::FailableBlock;
		const bool bIdentifier = bExpression
			&& Tile.ExpressionKind == EVerseExpressionKind::Identifier;
		const bool bControl = bExpression
			&& Tile.ExpressionKind == EVerseExpressionKind::Control;
		const FLinearColor TileColor = VerseVisualEditorStyle::GetTileTitleColor(Tile);
		TSharedRef<SWidget> Body = BodyOverride.IsValid()
			? BodyOverride.ToSharedRef()
			: SNullWidget::NullWidget;
		if (!BodyOverride.IsValid() && bExpression && !bIdentifier && !bControl)
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
		TArray<FText> ExecutionOutputLabels;
		if (bControl && Tile.ControlKind == EVerseControlKind::If)
		{
			ExecutionOutputLabels = {
				LOCTEXT("IfCompletedOutput", "Completed"),
				LOCTEXT("IfTrueOutput", "True"),
				LOCTEXT("IfFalseOutput", "False")};
		}
		FMargin TileHeaderPadding(0.0f, 6.0f, 8.0f, 6.0f);
		if (Tile.Kind == EVerseVisualTileKind::FunctionEntry)
		{
			TileHeaderPadding = FMargin(10.0f, 7.0f, 10.0f, 8.0f);
		}
		else if (bIdentifier)
		{
			TileHeaderPadding = FMargin(6.0f, 6.0f, 8.0f, 6.0f);
		}
		TSharedRef<SVerseTile> Result = SNew(SVerseTile)
			.Tile(Tile)
			.Document(Document)
			.TileColor(TileColor)
			.UnselectedOutlineColor(FLinearColor::Black)
			.HeaderPadding(TileHeaderPadding)
			.ArrowPadding(bFailableBlock
				? FMargin(8.0f, 4.0f, 3.0f, 0.0f)
				: FMargin(8.0f, 14.0f, 3.0f, 0.0f))
			.ShowBody(BodyOverride.IsValid()
				|| bFailableBlock
				|| (bExpression && !bIdentifier && !bControl))
			.CompactExecutionSpacing(bCompactExecutionSpacing)
			.FunctionGraphPresentation(Presentation)
			.ExecutionOutputLabels(MoveTemp(ExecutionOutputLabels))
			.ConnectedSockets(MoveTemp(ConnectedSockets))
			.IsSelected_Lambda([Range = Tile.Range, IsTileSelected]()
			{
				return IsTileSelected.IsBound()
					&& IsTileSelected.Execute(Range);
			})
			.OnSelected(FOnClicked::CreateLambda([Tile, OnTileSelected]()
			{
				return OnTileSelected.IsBound()
					? OnTileSelected.Execute(Tile)
					: FReply::Unhandled();
			}))
			.OnSocketDragStarted(OnSocketDragStarted)
			.OnInlineLiteralCommitted(OnInlineLiteralCommitted)
			.OnClauseReordered(OnClauseReordered)
			.BodyRenderScope(BodyRenderScope)
			.OwningRenderScope(OwningRenderScope)
			.ClauseInsertionBodySpineX(ClauseInsertionBodySpineX)
			.BodyContent()
			[
				Body
			];
		if (WidgetRegistry != nullptr)
		{
			WidgetRegistry->Add(Tile.Id, Result);
		}
		return Result;
	}


	EVerseExpressionOutcome GetFirstOutputOutcome(const FVerseVisualTile& Tile)
	{
		return !Tile.GetValueOutputs().IsEmpty()
			? Tile.GetValueOutputs()[0].Outcome
			: Tile.Outcome;
	}


	TArray<FVerseGraphConnection> ResolveModelConnections(
		TConstArrayView<FVerseVisualConnection> ModelConnections,
		const FVerseTileWidgetRegistry& Widgets,
		TSharedRef<const FVerseDocument> Document,
		FVerseGraphRenderScopeId RenderScope,
		EVerseFunctionGraphPresentation Presentation)
	{
		TArray<FVerseGraphConnection> Result;
		for (const FVerseVisualConnection& Model : ModelConnections)
		{
			if (!(Model.RenderScope == RenderScope))
			{
				continue;
			}
			const TSharedPtr<SVerseTile>* SourceWidget = Widgets.Find(Model.Source.Tile);
			const TSharedPtr<SVerseTile>* TargetWidget = Widgets.Find(Model.Target.Tile);
			if (SourceWidget == nullptr || TargetWidget == nullptr
				|| !SourceWidget->IsValid() || !TargetWidget->IsValid())
			{
				continue;
			}
			const TSharedPtr<SWidget> SourceAnchor =
				(*SourceWidget)->GetSocketAnchor(Model.Source.Socket);
			const TSharedPtr<SWidget> TargetAnchor =
				(*TargetWidget)->GetSocketAnchor(Model.Target.Socket);
			if (!SourceAnchor.IsValid() || !TargetAnchor.IsValid())
			{
				continue;
			}
			const FVerseVisualSocket* SourceSocket =
				(*SourceWidget)->GetTile().FindSocket(Model.Source.Socket);
			const FVerseVisualSocket* TargetSocket =
				(*TargetWidget)->GetTile().FindSocket(Model.Target.Socket);
			const FVerseVisualSocket* TypedSocket = SourceSocket != nullptr
				&& (!SourceSocket->SemanticTypeName.IsEmpty()
					|| SourceSocket->TypeRange.IsSet()
					|| !SourceSocket->IntrinsicTypeName.IsNone())
				? SourceSocket : TargetSocket;
			FLinearColor Color = FLinearColor::White;
			if (Model.Outcome == EVerseExpressionOutcome::FailureOnly
				|| Model.Source.Socket.Role == EVerseVisualSocketRole::FailureContext)
			{
				Color = GetVerseFailureDecorationColor();
			}
			else if (TypedSocket != nullptr
				&& Model.Source.Socket.Role != EVerseVisualSocketRole::Execution
				&& Model.Source.Socket.Role != EVerseVisualSocketRole::ClauseInsertion)
			{
				Color = GetBlueprintPinColor(GetVisualTypeName(
					TypedSocket->TypeRange,
					TypedSocket->IntrinsicTypeName,
					*Document,
					TypedSocket->SemanticTypeName));
			}
			FVerseGraphConnection& Connection = Result.AddDefaulted_GetRef();
			Connection.Source = Model.Source;
			Connection.Target = Model.Target;
			Connection.EndpointRegistry = Widgets.Endpoints;
			Connection.Axis = GetVersePresentedConnectionAxis(
				Model.Axis,
				Model.Source.Socket.Role,
				Presentation);
			Connection.Color = Color;
			Connection.Thickness =
				Model.Source.Socket.Role == EVerseVisualSocketRole::Execution
				|| Model.Source.Socket.Role == EVerseVisualSocketRole::ClauseInsertion
				? GetDefault<UGraphEditorSettings>()->DefaultExecutionWireThickness
				: GetDefault<UGraphEditorSettings>()->DefaultDataWireThickness;
			Connection.ExtraBlankLineMarkers = Model.ExtraBlankLineMarkers;
			const TWeakPtr<SVerseGraphRenderScope> SourceScope =
				(*SourceWidget)->GetSocketRenderScope(Model.Source.Socket);
			const TWeakPtr<SVerseGraphRenderScope> TargetScope =
				(*TargetWidget)->GetSocketRenderScope(Model.Target.Socket);
			Widgets.Endpoints->Register(Model.Source, {
				SourceAnchor,
				(*SourceWidget)->GetSocketAnchorCoordinate(Model.Source.Socket),
				SourceScope,
				(*SourceWidget)->GetMotionTarget(),
				SourceScope.IsValid()});
			Widgets.Endpoints->Register(Model.Target, {
				TargetAnchor,
				(*TargetWidget)->GetSocketAnchorCoordinate(Model.Target.Socket),
				TargetScope,
				(*TargetWidget)->GetMotionTarget(),
				TargetScope.IsValid()});
			Connection.Outcome = Model.Outcome;
		}
		return Result;
	}

	struct FBuiltFunctionGraphRow
	{
		TSharedRef<SWidget> Widget;
		TSharedRef<SVerseTile> RootTile;
		TFunction<FVector2D()> RootPosition;
		TSharedPtr<SVerseGraphMotionWidget> MotionWidget;

		FVector2D GetRootPosition() const
		{
			return RootPosition ? RootPosition() : FVector2D::ZeroVector;
		}
	};

	FBuiltFunctionGraphRow BuildFunctionGraphRow(
		const FVerseVisualTile& Tile,
		TSharedRef<const FVerseDocument> Document,
		FOnVerseSocketDragStarted OnSocketDragStarted,
		FOnVerseInlineLiteralCommitted OnInlineLiteralCommitted,
		bool bCompactOperands = false,
		FOnVerseFunctionGraphTileSelected OnTileSelected = {},
		FIsVerseFunctionGraphTileSelected IsTileSelected = {},
		FOnVerseClauseReordered OnClauseReordered = {},
		TConstArrayView<FVerseVisualConnection> ModelConnections = {},
		FVerseTileWidgetRegistry* WidgetRegistry = nullptr,
		TSharedPtr<SVerseGraphRenderScope> OwningRenderScope = nullptr,
		EVerseFunctionGraphPresentation Presentation =
			EVerseFunctionGraphPresentation::VerticalExecution,
		TSharedPtr<FVerseGraphMotionController> MotionController = nullptr,
		FString ParentMotionKey = FString())
	{
		constexpr float StandardOperandColumnWidth = 190.0f;
		constexpr float StandardOperandWireSpace = 72.0f;
		constexpr float CompactOperandWireSpace = 20.0f;
		const float OperandColumnWidth = StandardOperandColumnWidth;
		const float OperandWireSpace = bCompactOperands
			? CompactOperandWireSpace
			: StandardOperandWireSpace;
		const FString MotionKey = MotionController.IsValid()
			? MotionController->AllocateKey(
				BuildVerseGraphMotionKeyBase(Tile, *Document))
			: FString();
		const bool bIsGraphAnchor = Tile.Kind == EVerseVisualTileKind::FunctionEntry
			&& ParentMotionKey.IsEmpty();
		auto FinishRow = [MotionController, MotionKey, ParentMotionKey, Presentation,
			bIsGraphAnchor](
			FBuiltFunctionGraphRow Row)
		{
			if (!MotionController.IsValid())
			{
				return Row;
			}
			TSharedRef<SVerseGraphMotionWidget> MotionWidget =
				SNew(SVerseGraphMotionWidget)
				.Controller(MotionController)
				.MotionKey(MotionKey)
				.ParentMotionKey(ParentMotionKey)
				.Entrance(Presentation == EVerseFunctionGraphPresentation::VerticalExecution
					? EVerseGraphMotionEntrance::FromRight
					: EVerseGraphMotionEntrance::FromTop)
				.IsGraphAnchor(bIsGraphAnchor)
				[
					Row.Widget
				];
			Row.RootTile->SetMotionTarget(MotionWidget);
			Row.MotionWidget = MotionWidget;
			Row.Widget = MotionWidget;
			return Row;
		};
		if (Tile.Kind == EVerseVisualTileKind::FailableBlock)
		{
			TSharedPtr<SHorizontalBox> HorizontalChain;
			TSharedPtr<SVerseStatementLayoutPanel> AutomaticChain;
			TSharedRef<SWidget> Chain = SNullWidget::NullWidget;
			if (Presentation != EVerseFunctionGraphPresentation::Tracks)
			{
				AutomaticChain = SNew(SVerseStatementLayoutPanel)
					.Presentation(Presentation)
					.StatementGap(16.0f);
				Chain = AutomaticChain.ToSharedRef();
			}
			else
			{
				HorizontalChain = SNew(SHorizontalBox);
				Chain = HorizontalChain.ToSharedRef();
			}
			TSharedRef<SVerseGraphRenderScope> RenderScope =
				SNew(SVerseGraphRenderScope)
				.Background(EVerseGraphRenderScopeBackground::Failable)
				.ClipToBounds(true);
			for (int32 ChildIndex = 0; ChildIndex < Tile.Children.Num(); ++ChildIndex)
			{
				FBuiltFunctionGraphRow ChildRow = BuildFunctionGraphRow(
					Tile.Children[ChildIndex],
					Document,
					OnSocketDragStarted,
					OnInlineLiteralCommitted,
					true,
					OnTileSelected,
					IsTileSelected,
					OnClauseReordered,
					ModelConnections,
					WidgetRegistry,
					RenderScope,
					Presentation,
					MotionController,
					MotionKey);
				if (AutomaticChain.IsValid())
				{
					AutomaticChain->AddStatement(
						ChildRow.Widget,
						ChildRow.RootTile,
						[ChildRow]() { return ChildRow.GetRootPosition(); },
						0.0f);
				}
				else
				{
					HorizontalChain->AddSlot()
					.AutoWidth()
					.VAlign(VAlign_Top)
					.Padding(ChildIndex == 0 ? 0.0f : 72.0f, 0.0f, 0.0f, 0.0f)
					[
						ChildRow.Widget
					];
				}
			}

			const TSharedRef<SVerseTile> BlockTile = BuildFunctionGraphTile(
				Tile,
				Document,
				OnSocketDragStarted,
				OnInlineLiteralCommitted,
				Chain,
				bCompactOperands,
				RenderScope,
				OnTileSelected,
				IsTileSelected,
				OnClauseReordered,
				ModelConnections,
				WidgetRegistry,
				OwningRenderScope,
				Presentation,
				AutomaticChain.IsValid()
					? TOptional<float>(AutomaticChain->GetExecutionSpinePosition())
					: TOptional<float>());
			RenderScope->SetConnections(
				WidgetRegistry != nullptr
					? ResolveModelConnections(
						ModelConnections,
						*WidgetRegistry,
						Document,
						FVerseGraphRenderScopeId::ForTile(Tile.Id),
						Presentation)
					: TArray<FVerseGraphConnection>());
			if (bCompactOperands
				|| Presentation == EVerseFunctionGraphPresentation::Tracks)
			{
				return FinishRow({BlockTile, BlockTile});
			}
			const float BlockOffset = OperandColumnWidth + OperandWireSpace;
			return FinishRow({
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SBox).WidthOverride(OperandColumnWidth + OperandWireSpace)
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					BlockTile
				],
				BlockTile,
				[BlockOffset]() { return FVector2D(BlockOffset, 0.0f); }});
		}
		if (Presentation != EVerseFunctionGraphPresentation::Tracks
			&& Tile.ExpressionKind == EVerseExpressionKind::Control
			&& Tile.ControlKind == EVerseControlKind::If)
		{
			TSharedRef<SWidget> PredicatePresentation = SNullWidget::NullWidget;
			const FVerseVisualExpressionDescriptor::FControlRegion* ConditionRegion =
				Tile.ControlRegions.FindByPredicate(
					[](const FVerseVisualExpressionDescriptor::FControlRegion& Region)
					{
						return Region.Kind == EVerseControlRegionKind::Condition
							&& Region.OperandCount > 0;
					});
			if (ConditionRegion != nullptr
				&& Tile.Children.IsValidIndex(ConditionRegion->FirstOperandIndex))
			{
				FBuiltFunctionGraphRow PredicateRow = BuildFunctionGraphRow(
					Tile.Children[ConditionRegion->FirstOperandIndex],
					Document,
					OnSocketDragStarted,
					OnInlineLiteralCommitted,
					true,
					OnTileSelected,
					IsTileSelected,
					OnClauseReordered,
					ModelConnections,
					WidgetRegistry,
					OwningRenderScope,
					Presentation,
					MotionController,
					MotionKey);
				PredicatePresentation = PredicateRow.Widget;
			}
			const TSharedRef<SVerseTile> RootTile = BuildFunctionGraphTile(
				Tile,
				Document,
				OnSocketDragStarted,
				OnInlineLiteralCommitted,
				nullptr,
				bCompactOperands,
				nullptr,
				OnTileSelected,
				IsTileSelected,
				OnClauseReordered,
				ModelConnections,
				WidgetRegistry,
				OwningRenderScope,
				Presentation);
			auto BuildExecutionBranch = [&](EVerseControlRegionKind RegionKind)
			{
				TSharedRef<SVerseStatementLayoutPanel> Branch =
					SNew(SVerseStatementLayoutPanel)
					.Presentation(Presentation)
					.StatementGap(16.0f);
				const FVerseVisualExpressionDescriptor::FControlRegion* Region =
					Tile.ControlRegions.FindByPredicate(
						[RegionKind](const FVerseVisualExpressionDescriptor::FControlRegion& Candidate)
						{
							return Candidate.Kind == RegionKind;
						});
				if (Region != nullptr)
				{
					for (int32 Offset = 0; Offset < Region->OperandCount; ++Offset)
					{
						const int32 ChildIndex = Region->FirstOperandIndex + Offset;
						if (!Tile.Children.IsValidIndex(ChildIndex))
						{
							continue;
						}
						FBuiltFunctionGraphRow ChildRow = BuildFunctionGraphRow(
							Tile.Children[ChildIndex],
							Document,
							OnSocketDragStarted,
							OnInlineLiteralCommitted,
							bCompactOperands,
							OnTileSelected,
							IsTileSelected,
							OnClauseReordered,
							ModelConnections,
							WidgetRegistry,
							OwningRenderScope,
							Presentation,
							MotionController,
							MotionKey);
						Branch->AddStatement(
							ChildRow.Widget,
							ChildRow.RootTile,
							[ChildRow]() { return ChildRow.GetRootPosition(); });
					}
				}
				return Branch;
			};

			const TSharedRef<SVerseStatementLayoutPanel> TrueBranch =
				BuildExecutionBranch(EVerseControlRegionKind::Body);
			const TSharedRef<SVerseStatementLayoutPanel> FalseBranch =
				BuildExecutionBranch(EVerseControlRegionKind::Else);
			PredicatePresentation->SlatePrepass();
			RootTile->SlatePrepass();
			const float PredicateColumnWidth = FMath::Max(
				OperandColumnWidth,
				PredicatePresentation->GetDesiredSize().X);
			const float ExecutionSpineOffset = PredicateColumnWidth + OperandWireSpace;
			const float BranchLeftPadding =
				ExecutionSpineOffset + RootTile->GetDesiredSize().X + 24.0f;
			return FinishRow({
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SBox)
							.MinDesiredWidth(OperandColumnWidth)
							.HAlign(HAlign_Right)
							[
								PredicatePresentation
							]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SBox).WidthOverride(OperandWireSpace)
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						RootTile
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 18.0f, 0.0f, 0.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SBox).WidthOverride(BranchLeftPadding)
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)
					[
						TrueBranch
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SBox).WidthOverride(48.0f)
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)
					[
						FalseBranch
					]
				],
				RootTile,
				[ExecutionSpineOffset]()
				{
					return FVector2D(ExecutionSpineOffset, 0.0f);
				}});
		}
		const TSharedRef<SVerseTile> RootTile = BuildFunctionGraphTile(
			Tile,
			Document,
			OnSocketDragStarted,
			OnInlineLiteralCommitted,
			nullptr,
			bCompactOperands,
			nullptr,
			OnTileSelected,
			IsTileSelected,
			OnClauseReordered,
			ModelConnections,
			WidgetRegistry,
			OwningRenderScope,
			Presentation);
		if (Presentation == EVerseFunctionGraphPresentation::Tracks
			&& Tile.ExpressionKind == EVerseExpressionKind::Control)
		{
			return FinishRow({RootTile, RootTile});
		}
		if (Tile.ExpressionKind == EVerseExpressionKind::Control)
		{
			TSharedRef<SHorizontalBox> RegionRow = SNew(SHorizontalBox);
			for (const FVerseVisualExpressionDescriptor::FControlRegion& Region :
				Tile.ControlRegions)
			{
				FText RegionName;
				switch (Region.Kind)
				{
				case EVerseControlRegionKind::Condition:
					RegionName = LOCTEXT("ControlConditionRegion", "Condition");
					break;
				case EVerseControlRegionKind::Else:
					RegionName = LOCTEXT("ControlElseRegion", "Else");
					break;
				default:
					RegionName = LOCTEXT("ControlBodyRegion", "Body");
					break;
				}
				const FText StyleName = Region.PunctuationStyle
					== EVerseClausePunctuationStyle::Braces
					? LOCTEXT("BracesBodyStyle", "Braces")
					: Region.PunctuationStyle
						== EVerseClausePunctuationStyle::ColonOrIndentation
						? LOCTEXT("IndentedBodyStyle", "Indented")
						: FText::GetEmpty();
				TSharedRef<SVerticalBox> RegionContent = SNew(SVerticalBox);
				bool bHasRegionTile = false;
				for (int32 Offset = 0; Offset < Region.OperandCount; ++Offset)
				{
					const int32 ChildIndex = Region.FirstOperandIndex + Offset;
					if (!Tile.Children.IsValidIndex(ChildIndex))
					{
						continue;
					}
					FBuiltFunctionGraphRow ChildRow = BuildFunctionGraphRow(
					Tile.Children[ChildIndex],
						Document,
						OnSocketDragStarted,
						OnInlineLiteralCommitted,
						bCompactOperands,
						OnTileSelected,
						IsTileSelected,
						OnClauseReordered,
						ModelConnections,
						WidgetRegistry,
						OwningRenderScope,
						Presentation,
						MotionController,
						MotionKey);
					RegionContent->AddSlot()
					.AutoHeight()
					.Padding(8.0f, Offset == 0 ? 8.0f : 16.0f, 8.0f, 0.0f)
					[
						ChildRow.Widget
					];
					bHasRegionTile = true;
				}
				if (!bHasRegionTile)
				{
					RegionContent->AddSlot().AutoHeight().Padding(8.0f)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("EmptyControlRegion", "Empty"))
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					];
				}
				RegionRow->AddSlot()
				.AutoWidth()
				.VAlign(VAlign_Top)
				.Padding(0.0f, 0.0f, 12.0f, 0.0f)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					.Padding(6.0f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(STextBlock)
							.Text(StyleName.IsEmpty()
								? RegionName
								: FText::Format(LOCTEXT("ControlRegionWithStyle", "{0} · {1}"),
									RegionName, StyleName))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							RegionContent
						]
					]
				];
			}
			const TSharedRef<SVerticalBox> ControlPresentation =
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					RootTile
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 12.0f, 0.0f, 0.0f)
				[
					RegionRow
				];
			return FinishRow({
				ControlPresentation,
				RootTile,
				[ControlPresentation, RootTile]()
				{
					ControlPresentation->SlatePrepass();
					RootTile->SlatePrepass();
					return FVector2D(
						(ControlPresentation->GetDesiredSize().X
							- RootTile->GetDesiredSize().X) * 0.5f,
						0.0f);
				}});
		}
		const bool bHasOperandLayout = IsVerseOperatorExpression(Tile.ExpressionKind)
			|| Tile.ExpressionKind == EVerseExpressionKind::Call
			|| Tile.ExpressionKind == EVerseExpressionKind::Definition;
		if (!bHasOperandLayout || Tile.Children.IsEmpty())
		{
			if (bCompactOperands)
			{
				return FinishRow({RootTile, RootTile});
			}
			return FinishRow({
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
				[RootOffset = OperandColumnWidth + OperandWireSpace]()
				{
					return FVector2D(RootOffset, 0.0f);
				}});
		}

		TSharedRef<SVerseExpressionLayoutPanel> ExpressionLayout =
			SNew(SVerseExpressionLayoutPanel)
			.HorizontalGap(OperandWireSpace)
			.VerticalGap(18.0f);
		ExpressionLayout->SetRoot(RootTile);
		for (int32 Index = 0; Index < Tile.Children.Num(); ++Index)
		{
			if (Tile.Children[Index].LiteralKind != EVerseLiteralKind::None)
			{
				continue;
			}
			FBuiltFunctionGraphRow OperandRow = BuildFunctionGraphRow(
				Tile.Children[Index],
				Document,
				OnSocketDragStarted,
				OnInlineLiteralCommitted,
				true,
				OnTileSelected,
				IsTileSelected,
				OnClauseReordered,
				ModelConnections,
				WidgetRegistry,
				OwningRenderScope,
				Presentation,
				MotionController,
				MotionKey);
			ExpressionLayout->AddOperand(
				OperandRow.Widget,
				OperandRow.RootTile,
				[OperandRow]() { return OperandRow.GetRootPosition(); },
				Index);
		}

		return FinishRow({
			ExpressionLayout,
			RootTile,
			[ExpressionLayout]() { return ExpressionLayout->GetRootPosition(); }});
	}
}

void SVerseVisualEditor::RefreshActiveDocument(
	bool bAnimateGraphChanges,
	bool bRebuildDocumentChrome)
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

	ReconcileFunctionTabs(
		*ActiveDocument,
		FindExactSemanticSnapshot(SemanticWorkspace.Get(), *ActiveDocument));
	const bool bShowingFunction =
		ActiveDocument->FunctionTabs.IsValidIndex(ActiveDocument->ActiveFunctionTabIndex);
	const bool bCanReuseCanvas = bShowingFunction
		? ActiveDocument->FunctionTabs[ActiveDocument->ActiveFunctionTabIndex].FunctionCanvas.IsValid()
		: ActiveDocument->FileCanvas.IsValid();
	const bool bMustRebuildDocumentChrome = bRebuildDocumentChrome || !bCanReuseCanvas;
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
		if (!FunctionTab.MotionController.IsValid())
		{
			FunctionTab.MotionController = MakeShared<FVerseGraphMotionController>();
		}
		FunctionTab.MotionController->BeginBuild(
			bAnimateGraphChanges && FunctionTab.FunctionCanvas.IsValid());
		ActiveDocument->FileCanvas.Reset();
		TSharedPtr<SWidget> FunctionEntryAnchor;
		const TSharedRef<const FVerseDocument> SourceDocument =
			ActiveDocument->Session->GetParseSnapshot().GetDocument();
		TSharedRef<SVerticalBox> FunctionContent = SNew(SVerticalBox);
		TSharedPtr<SHorizontalBox> RootExecutionTrack;
		TSharedPtr<SVerseStatementLayoutPanel> AutomaticLayout;
		if (FunctionGraphPresentation == EVerseFunctionGraphPresentation::Tracks)
		{
			RootExecutionTrack = SNew(SHorizontalBox);
			TSharedRef<SWidget> RootTrackWidget = RootExecutionTrack.ToSharedRef();
			RootTrackWidget =
					SNew(SBox)
					.Padding(8.0f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)
						.Padding(0.0f, 4.0f, 14.0f, 0.0f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("MainExecutionTrack", "Main"))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
							.ColorAndOpacity(FSlateColor::UseSubduedForeground())
						]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							RootExecutionTrack.ToSharedRef()
						]
					];
			FunctionContent->AddSlot().AutoHeight()
			[
				RootTrackWidget
			];
		}
		else
		{
			AutomaticLayout = SNew(SVerseStatementLayoutPanel)
				.Presentation(FunctionGraphPresentation)
				.StatementGap(FunctionGraphPresentation
					== EVerseFunctionGraphPresentation::VerticalExecution
					? 12.0f : 72.0f);
			FunctionContent->AddSlot().AutoHeight()
			[
				AutomaticLayout.ToSharedRef()
			];
		}
		auto AddRootPresentation = [
			&FunctionContent, &RootExecutionTrack, &AutomaticLayout](
			const FBuiltFunctionGraphRow& Row,
			float LeadingSpace = 0.0f)
		{
			if (RootExecutionTrack.IsValid())
			{
				RootExecutionTrack->AddSlot()
				.AutoWidth()
				.VAlign(VAlign_Top)
				.Padding(LeadingSpace, 0.0f, 72.0f, 0.0f)
				[
					Row.Widget
				];
			}
			else if (AutomaticLayout.IsValid())
			{
				AutomaticLayout->AddStatement(
					Row.Widget,
					Row.RootTile,
					[Row]() { return Row.GetRootPosition(); },
					LeadingSpace);
			}
			else
			{
				FunctionContent->AddSlot()
				.AutoHeight()
				.HAlign(HAlign_Left)
				.Padding(0.0f, LeadingSpace, 0.0f, 0.0f)
				[
					Row.Widget
				];
			}
		};
		const TArray<FVerseVisualConnection> ModelConnections =
			FVerseVisualTileBuilder::BuildConnections(FunctionTab.GraphTiles);
		FVerseTileWidgetRegistry WidgetRegistry;
		const FOnVerseFunctionGraphTileSelected OnFunctionTileSelected =
			FOnVerseFunctionGraphTileSelected::CreateLambda(
				[this, OpenDocument = ActiveDocument](const FVerseVisualTile& Tile)
				{
					HandleTileSelected(Tile, OpenDocument);
					return FReply::Handled();
				});
		const FIsVerseFunctionGraphTileSelected IsFunctionTileSelected =
			FIsVerseFunctionGraphTileSelected::CreateLambda(
				[OpenDocument = ActiveDocument](FVerseTextRange Range)
				{
					return OpenDocument.IsValid()
						&& OpenDocument->SelectedTile.IsSet()
						&& OpenDocument->SelectedTile->Range == Range;
				});
		for (int32 Index = 0; Index < FunctionTab.GraphTiles.Num(); ++Index)
		{
			const FVerseVisualTile& Tile = FunctionTab.GraphTiles[Index];
			const float LeadingStatementSpace = Index > 0
				? FunctionTab.GraphTiles[Index - 1].ExtraBlankLineCount * 24.0f
				: 0.0f;
			const FBuiltFunctionGraphRow GraphRow = BuildFunctionGraphRow(
				Tile,
				SourceDocument,
				FOnVerseSocketDragStarted::CreateSP(this, &SVerseVisualEditor::BeginSocketDrag),
				FOnVerseInlineLiteralCommitted::CreateSP(
					this,
					&SVerseVisualEditor::HandleInlineLiteralCommitted,
					ActiveDocument),
				false,
				OnFunctionTileSelected,
				IsFunctionTileSelected,
				FOnVerseClauseReordered::CreateSP(
					this, &SVerseVisualEditor::HandleClauseReordered),
				ModelConnections,
				&WidgetRegistry,
				nullptr,
				FunctionGraphPresentation,
				FunctionTab.MotionController);
			const bool bPairWithImplicitReturn = Tile.bImplicitReturnValue
				&& FunctionTab.GraphTiles.IsValidIndex(Index + 1)
				&& FunctionTab.GraphTiles[Index + 1].Kind
					== EVerseVisualTileKind::FunctionReturn;
			if (bPairWithImplicitReturn)
			{
				FVerseVisualTile ReturnDisplayTile =
					FunctionTab.GraphTiles[Index + 1];
				const EVerseExpressionOutcome ReturnOutcome =
					GetFirstOutputOutcome(Tile);
				ReturnDisplayTile.Outcome = ReturnOutcome;
				const TSharedRef<SVerseTile> ReturnRoot = BuildFunctionGraphTile(
					ReturnDisplayTile,
					SourceDocument,
					FOnVerseSocketDragStarted::CreateSP(
						this, &SVerseVisualEditor::BeginSocketDrag),
					FOnVerseInlineLiteralCommitted::CreateSP(
						this,
						&SVerseVisualEditor::HandleInlineLiteralCommitted,
						ActiveDocument),
					nullptr,
					false,
					nullptr,
					OnFunctionTileSelected,
					IsFunctionTileSelected,
					FOnVerseClauseReordered::CreateSP(
						this, &SVerseVisualEditor::HandleClauseReordered),
					ModelConnections,
					&WidgetRegistry,
					nullptr,
					FunctionGraphPresentation);
				const FString ReturnMotionKey = FunctionTab.MotionController->AllocateKey(
					BuildVerseGraphMotionKeyBase(ReturnDisplayTile, *SourceDocument));
				const TSharedRef<SVerseGraphMotionWidget> ReturnMotion =
					SNew(SVerseGraphMotionWidget)
					.Controller(FunctionTab.MotionController)
					.MotionKey(ReturnMotionKey)
					.Entrance(FunctionGraphPresentation
						== EVerseFunctionGraphPresentation::VerticalExecution
						? EVerseGraphMotionEntrance::FromRight
						: EVerseGraphMotionEntrance::FromTop)
					[
						ReturnRoot
					];
				ReturnRoot->SetMotionTarget(ReturnMotion);
				GraphRow.RootTile->SlatePrepass();
				ReturnRoot->SlatePrepass();
				const float ReturnTopPadding =
					GraphRow.RootTile->GetValueSocketCenterY(0, true)
					- ReturnRoot->GetValueSocketCenterY(0, false);
				const TSharedRef<SWidget> PairWidget =
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)
					[
						GraphRow.Widget
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SBox).WidthOverride(96.0f)
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)
					[
						SNew(SBox)
						.Padding(FMargin(0.0f, ReturnTopPadding, 0.0f, 0.0f))
						[
							ReturnMotion
						]
					];
				AddRootPresentation(
					{PairWidget, GraphRow.RootTile, GraphRow.RootPosition},
					LeadingStatementSpace);
				++Index;
				continue;
			}
			AddRootPresentation(GraphRow, LeadingStatementSpace);
			if (Tile.Kind == EVerseVisualTileKind::FunctionEntry)
			{
				// Motion positions are measured from the outer motion wrapper, so the
				// wrapper itself must define graph-space zero. Anchoring to the inner
				// tile introduces its padding as a false function-entry displacement.
				FunctionEntryAnchor = GraphRow.MotionWidget.IsValid()
					? StaticCastSharedPtr<SWidget>(GraphRow.MotionWidget)
					: StaticCastSharedRef<SWidget>(GraphRow.RootTile).ToSharedPtr();
			}
		}

		if (FunctionGraphPresentation == EVerseFunctionGraphPresentation::Tracks)
		{
			TFunction<void(const FVerseVisualTile&, int32,
				TArray<TWeakPtr<SVerseTile>>)> AddControlTracks;
			AddControlTracks = [&, this](
				const FVerseVisualTile& ControlTile,
				int32 Depth,
				TArray<TWeakPtr<SVerseTile>> VisibilityOwners)
			{
				if (ControlTile.ExpressionKind != EVerseExpressionKind::Control)
				{
					return;
				}
				if (const TSharedPtr<SVerseTile>* ControlWidget =
					WidgetRegistry.Find(ControlTile.Id))
				{
					VisibilityOwners.Add(*ControlWidget);
				}
				for (const FVerseVisualExpressionDescriptor::FControlRegion& Region :
					ControlTile.ControlRegions)
				{
					TArray<const FVerseVisualTile*> NestedControls;
					FText TrackName = LOCTEXT("BodyExecutionTrack", "Body");
					if (Region.Kind == EVerseControlRegionKind::Condition)
					{
						TrackName = LOCTEXT("ConditionExecutionTrack", "Condition");
					}
					else if (Region.Kind == EVerseControlRegionKind::Else)
					{
						TrackName = LOCTEXT("FalseExecutionTrack", "False");
					}
					else if (ControlTile.ControlKind == EVerseControlKind::If)
					{
						TrackName = LOCTEXT("TrueExecutionTrack", "True");
					}

					const auto GetTrackVisibility = [VisibilityOwners]()
					{
						for (const TWeakPtr<SVerseTile>& Owner : VisibilityOwners)
						{
							const TSharedPtr<SVerseTile> Pinned = Owner.Pin();
							if (!Pinned.IsValid() || !Pinned->IsExpanded())
							{
								return EVisibility::Collapsed;
							}
						}
						return EVisibility::Visible;
					};
					const TAttribute<EVisibility> TrackVisibility =
						TAttribute<EVisibility>::CreateLambda(GetTrackVisibility);
					TSharedRef<SHorizontalBox> Track = SNew(SHorizontalBox);
					TSharedRef<SVerseGraphRenderScope> TrackRenderScope =
						SNew(SVerseGraphRenderScope)
						.Background(EVerseGraphRenderScopeBackground::Root)
						.ClipToBounds(false)
						.VisibilityGuardOnly(true)
						.Visibility(TrackVisibility);
					for (int32 Offset = 0; Offset < Region.OperandCount; ++Offset)
					{
						const int32 ChildIndex = Region.FirstOperandIndex + Offset;
						if (!ControlTile.Children.IsValidIndex(ChildIndex))
						{
							continue;
						}
						const FVerseVisualTile& Child = ControlTile.Children[ChildIndex];
						const FBuiltFunctionGraphRow ChildRow = BuildFunctionGraphRow(
							Child,
							SourceDocument,
							FOnVerseSocketDragStarted::CreateSP(
								this, &SVerseVisualEditor::BeginSocketDrag),
							FOnVerseInlineLiteralCommitted::CreateSP(
								this,
								&SVerseVisualEditor::HandleInlineLiteralCommitted,
								ActiveDocument),
							false,
							OnFunctionTileSelected,
							IsFunctionTileSelected,
							FOnVerseClauseReordered::CreateSP(
								this, &SVerseVisualEditor::HandleClauseReordered),
							ModelConnections,
							&WidgetRegistry,
							TrackRenderScope,
							FunctionGraphPresentation,
							FunctionTab.MotionController);
						Track->AddSlot().AutoWidth().VAlign(VAlign_Top)
						.Padding(Offset == 0 ? 0.0f : 72.0f, 0.0f, 0.0f, 0.0f)
						[
							ChildRow.Widget
						];
						if (Child.ExpressionKind == EVerseExpressionKind::Control)
						{
							NestedControls.Add(&Child);
						}
					}
					TrackRenderScope->SetContent(Track);
					FunctionContent->AddSlot().AutoHeight()
					.Padding(0.0f, 8.0f, 0.0f, 0.0f)
					[
						SNew(SVerticalBox)
						.Visibility(TrackVisibility)
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(SSeparator)
							.SeparatorImage(FAppStyle::GetBrush("Menu.Separator"))
							.Thickness(1.0f)
							.ColorAndOpacity(FLinearColor(0.42f, 0.44f, 0.48f, 0.8f))
						]
						+ SVerticalBox::Slot().AutoHeight()
						.Padding(Depth * 36.0f + 8.0f, 8.0f, 8.0f, 8.0f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)
							.Padding(0.0f, 4.0f, 14.0f, 0.0f)
							[
								SNew(STextBlock)
								.Text(TrackName)
								.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
								.ColorAndOpacity(FSlateColor::UseSubduedForeground())
							]
							+ SHorizontalBox::Slot().AutoWidth()
							[
								TrackRenderScope
							]
						]
					];
					for (const FVerseVisualTile* NestedControl : NestedControls)
					{
						AddControlTracks(
							*NestedControl, Depth + 1, VisibilityOwners);
					}
				}
			};
			for (const FVerseVisualTile& Tile : FunctionTab.GraphTiles)
			{
				AddControlTracks(Tile, 1, {});
			}
		}

		// Rendering consumes only immutable model endpoints. The positional layout
		// code above may arrange widgets, but it cannot invent sockets or wires.
		TArray<FVerseGraphConnection> GraphConnections = ResolveModelConnections(
			ModelConnections,
			WidgetRegistry,
			SourceDocument,
			FVerseGraphRenderScopeId::Root(),
			FunctionGraphPresentation);
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
				.MotionController(FunctionTab.MotionController)
				.OnConnectionDropped(FOnVerseGraphConnectionDropped::CreateSP(
					this, &SVerseVisualEditor::HandleConnectionDropped))
				.OnConnectionCancelled(FSimpleDelegate::CreateSP(
					this, &SVerseVisualEditor::HandleConnectionCancelled))
				.OnBackgroundClicked(FSimpleDelegate::CreateSP(
					this,
					&SVerseVisualEditor::HandleTileSelectionCleared,
					ActiveDocument))
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
	if (bMustRebuildDocumentChrome)
	{
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
	}
	RebuildProperties();
}


#undef LOCTEXT_NAMESPACE
