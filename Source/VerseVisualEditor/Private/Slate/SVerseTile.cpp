#include "Slate/SVerseTile.h"
#include "Slate/SVerseGraphSurface.h"

#include "Slate/SVerseLiteralEditor.h"

#include "Brushes/SlateColorBrush.h"
#include "Input/DragAndDrop.h"
#include "HAL/PlatformTime.h"
#include "Rendering/DrawElements.h"
#include "Settings/EditorStyleSettings.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Slate/VerseDefinitionIcon.h"
#include "VerseDocument.h"
#include "VerseParseSnapshotBuilder.h"
#include "VisualModel/VerseFunctionEffects.h"
#include "Slate/VerseVisualEditorStyle.h"
#include "Slate/VerseGraphMotion.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SVerseTile"

namespace
{
	class FVerseClauseTileDragDropOp final : public FDragDropOperation
	{
	public:
		DRAG_DROP_OPERATOR_TYPE(FVerseClauseTileDragDropOp, FDragDropOperation)

		FVerseVisualClauseDescriptor Clause;
		int32 ItemIndex = INDEX_NONE;
		TWeakPtr<SVerseGraphMotionWidget> MotionTarget;

		virtual void OnDragged(const FDragDropEvent& DragDropEvent) override
		{
			if (const TSharedPtr<SVerseGraphMotionWidget> Target = MotionTarget.Pin())
			{
				Target->UpdateElasticDrag(DragDropEvent.GetScreenSpacePosition());
			}
		}

		virtual void OnDrop(bool bDropWasHandled, const FPointerEvent& MouseEvent) override
		{
			if (const TSharedPtr<SVerseGraphMotionWidget> Target = MotionTarget.Pin())
			{
				Target->EndElasticDrag();
			}
		}

		virtual TSharedPtr<SWidget> GetDefaultDecorator() const override
		{
			return SNullWidget::NullWidget;
		}

		static TSharedRef<FVerseClauseTileDragDropOp> New(
			const TOptional<FVerseVisualClauseDescriptor>& InClause,
			int32 InItemIndex,
			TSharedPtr<SVerseGraphMotionWidget> InMotionTarget,
			FVector2D StartDesktopPosition)
		{
			TSharedRef<FVerseClauseTileDragDropOp> Operation =
				MakeShared<FVerseClauseTileDragDropOp>();
			if (InClause.IsSet())
			{
				Operation->Clause = InClause.GetValue();
			}
			Operation->ItemIndex = InItemIndex;
			Operation->MotionTarget = InMotionTarget;
			if (InMotionTarget.IsValid())
			{
				InMotionTarget->BeginElasticDrag(StartDesktopPosition);
			}
			Operation->Construct();
			return Operation;
		}
	};
}

TArray<FVerseFailablePatternSegment> BuildVerseFailablePatternSegments(FVector2D Size)
{
	TArray<FVerseFailablePatternSegment> Segments;
	if (Size.X <= 0.0f || Size.Y <= 0.0f)
	{
		return Segments;
	}

	constexpr float HalfWidth = 18.0f;
	constexpr float HalfHeight = 24.0f;
	for (float CenterY = 0.0f; CenterY <= Size.Y + HalfHeight; CenterY += HalfHeight)
	{
		const bool bOffsetRow = FMath::RoundToInt(CenterY / HalfHeight) % 2 != 0;
		for (float CenterX = bOffsetRow ? 0.0f : -HalfWidth;
			CenterX <= Size.X + HalfWidth;
			CenterX += HalfWidth * 2.0f)
		{
			const FVector2D Top(CenterX, CenterY - HalfHeight);
			const FVector2D Right(CenterX + HalfWidth, CenterY);
			const FVector2D Bottom(CenterX, CenterY + HalfHeight);
			const FVector2D Left(CenterX - HalfWidth, CenterY);
			Segments.Append({
				{Top, Right},
				{Right, Bottom},
				{Bottom, Left},
				{Left, Top}});
		}
	}
	return Segments;
}

TStaticArray<FVector2D, 4> BuildVerseFailableCornerCenters(FVector2D Size)
{
	return TStaticArray<FVector2D, 4>(
		FVector2D::ZeroVector,
		FVector2D(Size.X, 0.0f),
		FVector2D(0.0f, Size.Y),
		Size);
}

FVector2D GetVerseExecutionPinAnchorCoordinate(
	bool bInput,
	bool bCompact,
	EVerseFunctionGraphPresentation Presentation)
{
	if (Presentation != EVerseFunctionGraphPresentation::VerticalExecution)
	{
		return FVector2D(0.5f, 0.5f);
	}
	if (bInput)
	{
		return FVector2D(0.5f, 24.0f / 32.0f);
	}
	return FVector2D(0.5f, 1.0f);
}

FVector2D GetVerseExecutionPinDesiredSize(
	bool bInput,
	bool bCompact,
	EVerseFunctionGraphPresentation Presentation)
{
	if (Presentation != EVerseFunctionGraphPresentation::VerticalExecution)
	{
		return FVector2D(32.0f, 24.0f);
	}
	return bInput
		? FVector2D(48.0f, 32.0f)
		: FVector2D(24.0f, 12.0f);
}

EVerseVisualConnectionAxis GetVerseExecutionPreviewAxis(
	EVerseFunctionGraphPresentation Presentation)
{
	return Presentation == EVerseFunctionGraphPresentation::VerticalExecution
		? EVerseVisualConnectionAxis::Vertical
		: EVerseVisualConnectionAxis::Horizontal;
}

namespace
{
	float GetVerseGraphMajorGridWidth()
	{
		const float GridSize = static_cast<float>(GetDefault<UEditorStyleSettings>()->GridSnapSize);
		const float RulePeriod = FMath::Max(1.0f, FAppStyle::GetFloat(TEXT("Graph.Panel.GridRulePeriod")));
		return GridSize * RulePeriod;
	}

	FLinearColor GetVerseTilePinColor(const FString& VerseType)
	{
		return VerseVisualEditorStyle::GetTypeColor(VerseType);
	}

	const FSlateBrush* GetVerseTilePinBrush(const FString& VerseType, bool bConnected)
	{
		const bool bArray = VerseType.TrimStartAndEnd().StartsWith(TEXT("[]"));
		return FAppStyle::GetBrush(bArray
			? (bConnected ? "Graph.ArrayPin.Connected" : "Graph.ArrayPin.Disconnected")
			: (bConnected ? "Graph.Pin.Connected" : "Graph.Pin.Disconnected"));
	}

	class SVerseTileExecutionPin final : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SVerseTileExecutionPin)
			: _Input(false)
			, _Connected(false)
			, _Compact(false)
			, _Presentation(EVerseFunctionGraphPresentation::VerticalExecution)
		{}
			SLATE_ARGUMENT(bool, Input)
			SLATE_ARGUMENT(bool, Connected)
			SLATE_ARGUMENT(bool, Compact)
			SLATE_ARGUMENT(EVerseFunctionGraphPresentation, Presentation)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			bInput = InArgs._Input;
			bConnected = InArgs._Connected;
			bCompact = InArgs._Compact;
			Presentation = InArgs._Presentation;
			SetCanTick(false);
		}

		virtual FVector2D ComputeDesiredSize(float) const override
		{
			return GetVerseExecutionPinDesiredSize(bInput, bCompact, Presentation);
		}

		virtual int32 OnPaint(
			const FPaintArgs& Args,
			const FGeometry& AllottedGeometry,
			const FSlateRect& MyCullingRect,
			FSlateWindowElementList& OutDrawElements,
			int32 LayerId,
			const FWidgetStyle& InWidgetStyle,
			bool bParentEnabled) const override
		{
			const FVector2D PinCenter = AllottedGeometry.GetLocalSize()
				* GetVerseExecutionPinAnchorCoordinate(
					bInput, bCompact, Presentation);
			const FSlateBrush* PinBrush = FAppStyle::GetBrush(
				bConnected ? "Graph.ExecPin.Connected" : "Graph.ExecPin.Disconnected");
			const FVector2D PinSize = PinBrush->ImageSize;
			FSlateDrawElement::MakeRotatedBox(
				OutDrawElements,
				LayerId,
				AllottedGeometry.ToPaintGeometry(
					PinSize,
					FSlateLayoutTransform(PinCenter - PinSize * 0.5f)),
				PinBrush,
				ESlateDrawEffect::None,
				Presentation == EVerseFunctionGraphPresentation::VerticalExecution
					? HALF_PI : 0.0f,
				PinSize * 0.5f,
				FSlateDrawElement::RelativeToElement,
				InWidgetStyle.GetColorAndOpacityTint());
			return LayerId;
		}

	private:
		bool bInput = false;
		bool bConnected = false;
		bool bCompact = false;
		EVerseFunctionGraphPresentation Presentation =
			EVerseFunctionGraphPresentation::VerticalExecution;
	};

	class SVerseFailableValuePin final : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SVerseFailableValuePin) {}
			SLATE_ARGUMENT(FLinearColor, Color)
			SLATE_ARGUMENT(bool, Connected)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			Color = InArgs._Color;
			bConnected = InArgs._Connected;
			SetCanTick(false);
		}

		virtual FVector2D ComputeDesiredSize(float) const override
		{
			return FVector2D(11.0f, 11.0f);
		}

		virtual int32 OnPaint(
			const FPaintArgs& Args,
			const FGeometry& AllottedGeometry,
			const FSlateRect& MyCullingRect,
			FSlateWindowElementList& OutDrawElements,
			int32 LayerId,
			const FWidgetStyle& InWidgetStyle,
			bool bParentEnabled) const override
		{
			const FVector2D Size(8.0f, 8.0f);
			const FLinearColor Tint =
				Color * InWidgetStyle.GetColorAndOpacityTint();
			const FVector2D Center = AllottedGeometry.GetLocalSize() * 0.5f;
			if (bConnected)
			{
				static const FSlateColorBrush WhiteBrush(FLinearColor::White);
				FSlateDrawElement::MakeRotatedBox(
					OutDrawElements,
					LayerId,
					AllottedGeometry.ToPaintGeometry(
						Size,
						FSlateLayoutTransform(Center - Size * 0.5f)),
					&WhiteBrush,
					ESlateDrawEffect::None,
					PI * 0.25f,
					Size * 0.5f,
					FSlateDrawElement::RelativeToElement,
					Tint);
			}
			else
			{
				const float Radius = Size.X * 0.5f;
				TArray<FVector2f> Points({
					FVector2f(Center + FVector2D(0.0f, -Radius)),
					FVector2f(Center + FVector2D(Radius, 0.0f)),
					FVector2f(Center + FVector2D(0.0f, Radius)),
					FVector2f(Center + FVector2D(-Radius, 0.0f)),
					FVector2f(Center + FVector2D(0.0f, -Radius))});
				FSlateDrawElement::MakeLines(
					OutDrawElements,
					LayerId,
					AllottedGeometry.ToPaintGeometry(),
					MoveTemp(Points),
					ESlateDrawEffect::None,
					Tint,
					true,
					1.5f);
			}
			return LayerId;
		}

	private:
		FLinearColor Color = FLinearColor::White;
		bool bConnected = false;
	};

	/** A restrained Blueprint-like vertical gloss which leaves rounded corner arcs untouched. */
	class SVerseTileIdentityGradient final : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SVerseTileIdentityGradient)
			: _RoundBottom(false)
		{}
			SLATE_ATTRIBUTE(bool, RoundBottom)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			RoundBottom = InArgs._RoundBottom;
			SetVisibility(EVisibility::HitTestInvisible);
		}

		virtual FVector2D ComputeDesiredSize(float) const override
		{
			return FVector2D::ZeroVector;
		}

		virtual int32 OnPaint(
			const FPaintArgs& Args,
			const FGeometry& AllottedGeometry,
			const FSlateRect& MyCullingRect,
			FSlateWindowElementList& OutDrawElements,
			int32 LayerId,
			const FWidgetStyle& InWidgetStyle,
			bool bParentEnabled) const override
		{
			const FVector2D Size = AllottedGeometry.GetLocalSize();
			const ISlateStyle& VisualStyle = VerseVisualEditorStyle::Get();
			const float Radius = VisualStyle.GetFloat(TEXT("Metric.TileCornerRadius"));
			if (Size.X <= Radius * 2.0f || Size.Y <= 2.0f)
			{
				return LayerId;
			}

			TArray<FSlateGradientStop> Stops({
				FSlateGradientStop(
					FVector2D(0.0f, 0.0f),
					VisualStyle.GetColor(TEXT("Color.HeaderGlossTop"))),
				FSlateGradientStop(
					FVector2D(0.0f, Size.Y * 0.42f),
					VisualStyle.GetColor(TEXT("Color.HeaderGlossMiddle"))),
				FSlateGradientStop(
					FVector2D(0.0f, Size.Y),
					VisualStyle.GetColor(TEXT("Color.HeaderGlossBottom")))});
			const float BottomRadius = RoundBottom.Get(false) ? Radius : 0.0f;
			FSlateDrawElement::MakeGradient(
				OutDrawElements,
				LayerId,
				AllottedGeometry.ToPaintGeometry(),
				MoveTemp(Stops),
				Orient_Horizontal,
				ESlateDrawEffect::None,
				FVector4f(Radius, Radius, BottomRadius, BottomRadius));
			return LayerId;
		}

	private:
		TAttribute<bool> RoundBottom;
	};

	/** Extends the same restrained depth cue through expanded tile bodies. */
	class SVerseTileMainGradient final : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SVerseTileMainGradient) {}
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			SetVisibility(EVisibility::HitTestInvisible);
		}

		virtual FVector2D ComputeDesiredSize(float) const override
		{
			return FVector2D::ZeroVector;
		}

		virtual int32 OnPaint(
			const FPaintArgs& Args,
			const FGeometry& AllottedGeometry,
			const FSlateRect& MyCullingRect,
			FSlateWindowElementList& OutDrawElements,
			int32 LayerId,
			const FWidgetStyle& InWidgetStyle,
			bool bParentEnabled) const override
		{
			const FVector2D Size = AllottedGeometry.GetLocalSize();
			if (Size.X <= 0.0f || Size.Y <= 2.0f)
			{
				return LayerId;
			}

			const ISlateStyle& VisualStyle = VerseVisualEditorStyle::Get();
			const float Radius = VisualStyle.GetFloat(TEXT("Metric.TileCornerRadius"));
			TArray<FSlateGradientStop> Stops({
				FSlateGradientStop(
					FVector2D(0.0f, 0.0f),
					VisualStyle.GetColor(TEXT("Color.BodyGradientTop"))),
				FSlateGradientStop(
					FVector2D(0.0f, Size.Y * 0.42f),
					VisualStyle.GetColor(TEXT("Color.BodyGradientMiddle"))),
				FSlateGradientStop(
					FVector2D(0.0f, Size.Y),
					VisualStyle.GetColor(TEXT("Color.BodyGradientBottom")))});
			FSlateDrawElement::MakeGradient(
				OutDrawElements,
				LayerId,
				AllottedGeometry.ToPaintGeometry(),
				MoveTemp(Stops),
				Orient_Horizontal,
				ESlateDrawEffect::None,
				FVector4f(0.0f, 0.0f, Radius, Radius));
			return LayerId;
		}
	};

	/** Semantic regions and socket docks used to compose one tile. */
	struct FVerseTilePresentationSpec
	{
		bool bHasIdentityBand = false;
		bool bHasMainContent = false;
		bool bHasSourcePreview = false;
		bool bHorizontalExecution = false;
		bool bHorizontalImplicitReturnSource = false;
		bool bHorizontalImplicitReturnTile = false;
		bool bHorizontalFailureTerminal = false;
		TArray<FText> ExecutionOutputLabels;

		static FVerseTilePresentationSpec Build(
			const FVerseVisualTile& Tile,
			bool bCallerHasMainContent,
			bool bCallerHasSourcePreview,
			EVerseFunctionGraphPresentation Presentation)
		{
			FVerseTilePresentationSpec Result;
			const bool bExpression = Tile.Kind == EVerseVisualTileKind::Expression;
			const bool bOperator = bExpression
				&& IsVerseOperatorExpression(Tile.ExpressionKind);
			const bool bIdentifier = bExpression
				&& Tile.ExpressionKind == EVerseExpressionKind::Identifier;
			const bool bLiteral = bExpression
				&& Tile.ExpressionKind == EVerseExpressionKind::Literal;
			const bool bControl = bExpression
				&& Tile.ExpressionKind == EVerseExpressionKind::Control;
			Result.bHasIdentityBand = !(bOperator || bIdentifier || bLiteral
				|| Tile.Kind == EVerseVisualTileKind::Unknown
				|| Tile.Kind == EVerseVisualTileKind::SyncArm);
			Result.bHasMainContent = bCallerHasMainContent || bLiteral
				|| bControl || Tile.Kind == EVerseVisualTileKind::FailableBlock
				|| Tile.Kind == EVerseVisualTileKind::SyncArm
				|| (!Result.bHasIdentityBand
					&& Tile.Kind != EVerseVisualTileKind::Unknown)
				|| !Tile.GetValueInputs().IsEmpty()
				|| !Tile.GetValueOutputs().IsEmpty();
			Result.bHasSourcePreview = bCallerHasSourcePreview;
			Result.bHorizontalExecution = Presentation
				!= EVerseFunctionGraphPresentation::VerticalExecution;
			const bool bHorizontalLayout = Presentation
				== EVerseFunctionGraphPresentation::HorizontalExecution;
			Result.bHorizontalImplicitReturnSource = bHorizontalLayout
				&& Tile.bImplicitReturnValue;
			Result.bHorizontalImplicitReturnTile = bHorizontalLayout
				&& Tile.Kind == EVerseVisualTileKind::FunctionReturn;
			Result.bHorizontalFailureTerminal = bHorizontalLayout
				&& Tile.StatementFailure != EVerseStatementFailureDisposition::None;
			if (bExpression && Tile.ExpressionKind == EVerseExpressionKind::Control
				&& Tile.ControlKind == EVerseControlKind::If)
			{
				Result.ExecutionOutputLabels = {
					LOCTEXT("IfCompletedOutput", "Completed"),
					LOCTEXT("IfTrueOutput", "True"),
					LOCTEXT("IfFalseOutput", "False")};
			}
			return Result;
		}
	};

}

void SVerseTile::Construct(const FArguments& InArgs)
{
	Tile = InArgs._Tile;
	ConnectedSockets = InArgs._ConnectedSockets;
	SetRenderOpacity(Tile.bIsProvisional ? 0.5f : 1.0f);
	Document = InArgs._Document;
	IsSelected = InArgs._IsSelected;
	OnSelected = InArgs._OnSelected;
	OnOpened = InArgs._OnOpened;
	OnSocketDragStarted = InArgs._OnSocketDragStarted;
	OnInlineLiteralCommitted = InArgs._OnInlineLiteralCommitted;
	OnClauseReordered = InArgs._OnClauseReordered;
	EndpointRegistry = InArgs._EndpointRegistry;
	OwningRenderScope = InArgs._OwningRenderScope;
	BodyRenderScope = InArgs._BodyRenderScope;
	UnselectedOutlineColor = InArgs._UnselectedOutlineColor;
	bCompactExecutionSpacing = InArgs._CompactExecutionSpacing;
	FunctionGraphPresentation = InArgs._FunctionGraphPresentation;
	const FVerseTilePresentationSpec Presentation = FVerseTilePresentationSpec::Build(
		Tile,
		InArgs._HasMainContent,
		InArgs._HasSourcePreview,
		FunctionGraphPresentation);
	bHasIdentityBand = Presentation.bHasIdentityBand;
	bHasMainContent = Presentation.bHasMainContent;
	bHasSourcePreview = Presentation.bHasSourcePreview;
	const bool bHorizontalExecution = Presentation.bHorizontalExecution;
	const bool bHorizontalExecutionLayout = FunctionGraphPresentation
		== EVerseFunctionGraphPresentation::HorizontalExecution;
	const bool bHasVerticalExecutionOutputs = !bHorizontalExecution
		&& Tile.FindSocket({
			EVerseVisualSocketDirection::Output,
			EVerseVisualSocketRole::Execution,
			0}) != nullptr;

	TSharedRef<SVerticalBox> TileWithExecution = SNew(SVerticalBox);
	TSharedPtr<SVerseTileExecutionPin> ExecutionInputPin;
	const FVerseVisualSocketId ExecutionInputId{
		EVerseVisualSocketDirection::Input, EVerseVisualSocketRole::Execution, 0};
	const bool bHasExecutionInput = Tile.FindSocket(ExecutionInputId) != nullptr;
	if (bHasExecutionInput)
	{
		ExecutionInputPin =
			SNew(SVerseTileExecutionPin)
			.Input(true)
			.Connected(ConnectedSockets.Contains(ExecutionInputId))
			.Presentation(FunctionGraphPresentation)
			.RenderTransform(bHorizontalExecution
				? FSlateRenderTransform(FVector2D(-16.0f, 0.0f))
				: FSlateRenderTransform());
		SocketAnchors.Add(ExecutionInputId, ExecutionInputPin);
		if (!bHorizontalExecution)
		{
			TileWithExecution->AddSlot()
			.AutoHeight()
			.HAlign(HAlign_Left)
			[
				SNew(SBox)
				.WidthOverride(48.0f)
				.HeightOverride(32.0f)
			];
		}
	}

	const bool bOperatorTile = Tile.Kind == EVerseVisualTileKind::Expression
		&& IsVerseOperatorExpression(Tile.ExpressionKind);
	const bool bIfTile = Tile.Kind == EVerseVisualTileKind::Expression
		&& Tile.ExpressionKind == EVerseExpressionKind::Control
		&& Tile.ControlKind == EVerseControlKind::If;
	const bool bSyncTile = Tile.Kind == EVerseVisualTileKind::Expression
		&& Tile.ExpressionKind == EVerseExpressionKind::Control
		&& Tile.ControlKind == EVerseControlKind::Sync;
	const bool bSuspendingFunction = IsVerseSuspendingFunctionTile(Tile, *Document);
	const bool bHorizontalImplicitReturnSource =
		Presentation.bHorizontalImplicitReturnSource;
	const bool bHorizontalImplicitReturnTile =
		Presentation.bHorizontalImplicitReturnTile;
	const bool bHorizontalFailureTerminal =
		Presentation.bHorizontalFailureTerminal;
	const FText OperatorLines = bOperatorTile ? GetLineText() : FText::GetEmpty();
	TSharedRef<SWidget> MainContent = InArgs._MainContent.Widget;
	TSharedRef<SWidget> SourcePreview = InArgs._SourcePreview.Widget;
	if (Tile.ExpressionKind == EVerseExpressionKind::Literal
		&& Tile.LiteralKind != EVerseLiteralKind::None)
	{
		MainContent = SNew(SBorder)
			.BorderImage(nullptr)
			.Padding(FMargin(8.0f, 6.0f))
			[
				SNew(SVerseLiteralEditor)
				.LiteralKind(Tile.LiteralKind)
				.LiteralRange(Tile.Range)
				.SourceText(Decode(Tile.Range).ToString())
				.OnSourceCommitted(FOnVerseLiteralSourceCommitted::CreateLambda(
					[this](FVerseTextRange Range, FText Source)
					{
						OnInlineLiteralCommitted.ExecuteIfBound(Range, Source);
					}))
			];
	}
	if (Tile.Kind == EVerseVisualTileKind::FailableBlock
		|| Tile.Kind == EVerseVisualTileKind::SyncArm)
	{
		const bool bFailableContext = Tile.Kind == EVerseVisualTileKind::FailableBlock;
		TSharedPtr<SWidget> EntryPinButton;
		const FVerseVisualSocketId ClauseInsertionId{
			EVerseVisualSocketDirection::Output,
			EVerseVisualSocketRole::ClauseInsertion,
			0};
		if (Tile.FindSocket(ClauseInsertionId) != nullptr)
		{
			const FVerseVisualSocketInsertionTarget* InsertionTarget =
				Tile.FindSocketInsertionTarget(ClauseInsertionId);
			TOptional<FVerseVisualClauseDescriptor> InsertionClause;
			EVerseVisualSocketInsertionKind InsertionKind =
				EVerseVisualSocketInsertionKind::Clause;
			FVerseTextRange InsertionOwnerRange;
			int32 InsertionIndex = INDEX_NONE;
			if (InsertionTarget != nullptr)
			{
				InsertionClause = InsertionTarget->Clause;
				InsertionKind = InsertionTarget->Kind;
				InsertionOwnerRange = InsertionTarget->OwnerExpressionRange;
				InsertionIndex = InsertionTarget->InsertIndex;
			}
			const TSharedRef<SVerseTileExecutionPin> EntryPin =
				SNew(SVerseTileExecutionPin)
				.Input(false)
				.Connected(ConnectedSockets.Contains(ClauseInsertionId))
				.Compact(true)
				.Presentation(FunctionGraphPresentation)
				.RenderTransform(FSlateRenderTransform(
					bHorizontalExecution
						? FVector2D(-16.0f, 0.0f)
						: FVector2D(0.0f, -2.0f)));
			SocketAnchors.Add(ClauseInsertionId, EntryPin);
			EntryPinButton =
				SNew(SBorder)
				.BorderImage(nullptr)
				.Padding(0.0f)
				.OnMouseButtonDown(
					this,
					&SVerseTile::HandleClauseInsertionMouseButtonDown,
					TSharedPtr<SWidget>(EntryPin),
					GetVerseExecutionPinAnchorCoordinate(
						false, true, FunctionGraphPresentation),
					ClauseInsertionId,
					InsertionClause,
					InsertionKind,
					InsertionOwnerRange,
					InsertionIndex)
				[
					EntryPin
				];
		}
		TSharedRef<SWidget> FailureChain = SNullWidget::NullWidget;
		if (bHorizontalExecutionLayout)
		{
			constexpr float HorizontalBodyTopPadding = 36.0f;
			const float EntryPinTopPadding = InArgs._ClauseInsertionBodySpineX.IsSet()
				? FMath::Max(0.0f,
					HorizontalBodyTopPadding
					+ InArgs._ClauseInsertionBodySpineX.GetValue()
					- 12.0f)
				: HorizontalBodyTopPadding;
			FailureChain =
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)
				.Padding(FMargin(0.0f, EntryPinTopPadding, 0.0f, 0.0f))
				[
					EntryPinButton.IsValid()
						? EntryPinButton.ToSharedRef()
						: SNullWidget::NullWidget
				]
				+ SHorizontalBox::Slot().AutoWidth()
				.Padding(FMargin(0.0f, HorizontalBodyTopPadding,
					bFailableContext ? 116.0f : 28.0f, 32.0f))
				[
					MainContent
				];
		}
		else if (bHorizontalExecution)
		{
			// Track presentation retains its compact horizontal failable-body layout.
			FailureChain =
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					EntryPinButton.IsValid()
						? EntryPinButton.ToSharedRef()
						: SNullWidget::NullWidget
				]
				+ SHorizontalBox::Slot().AutoWidth()
				.Padding(FMargin(0.0f, 20.0f,
					bFailableContext ? 116.0f : 28.0f, 28.0f))
				[
					MainContent
				];
		}
		else
		{
			MainContent->SlatePrepass();
			const float BodyWidth = MainContent->GetDesiredSize().X;
			// Direct failable statements terminate at this scope's right wall.
			// Keep a marker lane beyond the widest internal tile.
			const float FailureChainWidth = BodyWidth + 40.0f
				+ (bFailableContext ? 96.0f : 0.0f);
			if (EntryPinButton.IsValid())
			{
				EntryPinButton->SlatePrepass();
			}
			const float EntryPinWidth = EntryPinButton.IsValid()
				? EntryPinButton->GetDesiredSize().X : 0.0f;
			const float EntryPinCenterX = InArgs._ClauseInsertionBodySpineX.IsSet()
				? 20.0f + InArgs._ClauseInsertionBodySpineX.GetValue()
				: FailureChainWidth * 0.5f;
			FailureChain =
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)
				.Padding(0.0f, 0.0f, 0.0f, 4.0f)
				[
					SNew(SBox)
					.WidthOverride(FailureChainWidth)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth()
						[
							SNew(SBox).WidthOverride(FMath::Max(
								0.0f, EntryPinCenterX - EntryPinWidth * 0.5f))
						]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							EntryPinButton.IsValid()
								? EntryPinButton.ToSharedRef()
								: SNullWidget::NullWidget
						]
					]
				]
				+ SVerticalBox::Slot().AutoHeight()
				.Padding(FMargin(20.0f, 20.0f, 20.0f, 28.0f))
				[
					MainContent
				];
		}
		TSharedPtr<SVerseGraphRenderScope> LocalBodyRenderScope = InArgs._BodyRenderScope;
		if (!LocalBodyRenderScope.IsValid())
		{
			LocalBodyRenderScope = SNew(SVerseGraphRenderScope)
				.Background(bFailableContext
					? EVerseGraphRenderScopeBackground::Failable
					: EVerseGraphRenderScopeBackground::Synchronization)
				.ClipToBounds(true);
		}
		LocalBodyRenderScope->SetContent(FailureChain);
		MainContent = LocalBodyRenderScope.ToSharedRef();
	}
	TSharedRef<SWidget> FailureContextInputWidget = SNullWidget::NullWidget;
	if (bIfTile)
	{
		const FVerseVisualSocketId FailureInputId{
			EVerseVisualSocketDirection::Input,
			EVerseVisualSocketRole::FailureContext,
			0};
		const TSharedRef<SVerseFailableValuePin> Pin =
			SNew(SVerseFailableValuePin)
				.Color(GetVerseFailureDecorationColor())
				.Connected(ConnectedSockets.Contains(FailureInputId))
				.Visibility(EVisibility::HitTestInvisible)
				.RenderTransform(FSlateRenderTransform(FVector2D(-5.5f, 0.0f)));
		SocketAnchors.Add(FailureInputId, Pin);
		FailureContextInputWidget = Pin;
	}
	TSharedRef<SWidget> FailureContextOutputWidget = SNullWidget::NullWidget;
	const FVerseVisualSocketId FailureOutputId{
		EVerseVisualSocketDirection::Output,
		EVerseVisualSocketRole::FailureContext,
		0};
	const bool bHasFailureOutput = Tile.FindSocket(FailureOutputId) != nullptr;
	if (bHasFailureOutput)
	{
		const TSharedRef<SVerseFailableValuePin> Pin =
			SNew(SVerseFailableValuePin)
				.Color(GetVerseFailureDecorationColor())
				.Connected(ConnectedSockets.Contains(FailureOutputId))
				.Visibility(EVisibility::HitTestInvisible)
				.RenderTransform(FSlateRenderTransform(
					bHorizontalFailureTerminal
						? FVector2D::ZeroVector
						: FVector2D(5.5f, 0.0f)));
		SocketAnchors.Add(FailureOutputId, Pin);
		FailureContextOutputWidget = Pin;
	}
	TSharedRef<SWidget> ValueOutputWidget = BuildSocketColumn(Tile.GetValueOutputs(), true);
	TSharedRef<SWidget> ValueOutputDock = ValueOutputWidget;
	if (bHasFailureOutput && !bHorizontalFailureTerminal)
	{
		ValueOutputDock =
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Right)
			.Padding(0.0f, 2.0f, 0.0f, 2.0f)
			[
				FailureContextOutputWidget
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Right)
			[
				ValueOutputWidget
			];
	}
	if (Tile.StatementFailure != EVerseStatementFailureDisposition::None
		&& !bHorizontalFailureTerminal)
	{
		ValueOutputDock =
			SNew(SBox)
			.Padding(FMargin(0.0f, 22.0f, 0.0f, 0.0f))
			[
				ValueOutputDock
			];
	}
	ValueOutputDockWidget = ValueOutputDock;
	const ISlateStyle& VisualStyle = VerseVisualEditorStyle::Get();

	TSharedPtr<SBox> VerticalExecutionOutputHost;
	TSharedPtr<SBox> SourceExecutionOutputHost;
	TSharedPtr<SBox> HorizontalExecutionOutputHost;
	TSharedRef<SWidget> MainIdentity = bHasIdentityBand
		? SNullWidget::NullWidget
		: BuildMainIdentity(InArgs._Compact);
	TSharedRef<SWidget> MainCenter = bHasMainContent
		? MainContent
		: MainIdentity;
	if (!bHasIdentityBand && bHasMainContent)
	{
		MainCenter = SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[MainIdentity]
			+ SVerticalBox::Slot().AutoHeight()[MainContent];
	}
	if (bSyncTile)
	{
		MainCenter = SNew(SBox)
			.MinDesiredWidth(300.0f)
			.Padding(FMargin(4.0f, 0.0f))
			[MainCenter];
	}

	TSharedRef<SVerticalBox> LeftDock = SNew(SVerticalBox);
	HorizontalExecutionInputDockWidget = LeftDock;
	LeftDock->AddSlot().AutoHeight().HAlign(HAlign_Left)
		[ bHorizontalExecution && ExecutionInputPin.IsValid()
			? ExecutionInputPin.ToSharedRef() : SNullWidget::NullWidget ];
	LeftDock->AddSlot().AutoHeight().HAlign(HAlign_Left)[FailureContextInputWidget];
	LeftDock->AddSlot().AutoHeight().HAlign(HAlign_Left)
		[BuildSocketColumn(Tile.GetValueInputs(), false)];
	TSharedRef<SVerticalBox> RightDock = SNew(SVerticalBox);
	HorizontalExecutionOutputDockWidget = RightDock;
	RightDock->AddSlot().AutoHeight().HAlign(HAlign_Right)
		[ SAssignNew(HorizontalExecutionOutputHost, SBox)
			.Visibility(bHorizontalExecution ? EVisibility::Visible : EVisibility::Collapsed) ];
	RightDock->AddSlot().AutoHeight().HAlign(HAlign_Right)[ValueOutputDock];
	const bool bHasMainSocketDocks =
		Tile.GetValueInputs().Num() > 0
		|| Tile.GetValueOutputs().Num() > 0
		|| (bHorizontalExecution
			&& (Tile.GetOtherInputs().Num() > 0 || Tile.GetOtherOutputs().Num() > 0));
	const bool bHasMainRegion = bHasMainContent || bHasMainSocketDocks;

	TSharedRef<SHorizontalBox> MainSocketRow =
		SAssignNew(MainSocketRowWidget, SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			bSyncTile ? SNullWidget::NullWidget : LeftDock
		]
		+ SHorizontalBox::Slot().FillWidth(1.0f)
		.VAlign(Tile.Kind == EVerseVisualTileKind::FailableBlock
			? VAlign_Fill : VAlign_Center)
		[
			SNew(SBorder)
			.OnMouseButtonDown(this, &SVerseTile::HandleIdentityMouseButtonDown)
			.BorderImage(FCoreStyle::Get().GetBrush("NoBorder"))
			.Padding(Tile.Kind == EVerseVisualTileKind::FailableBlock
				|| Tile.Kind == EVerseVisualTileKind::SyncArm
				|| bSyncTile
				? FMargin(0.0f) : FMargin(8.0f, 6.0f))
			[
				MainCenter
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			bSyncTile ? SNullWidget::NullWidget : RightDock
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			bHasFailureOutput ? SNullWidget::NullWidget : FailureContextOutputWidget
		];

	TSharedRef<SOverlay> IdentitySurface =
		SAssignNew(IdentityBandWidget, SOverlay)
		.Visibility(bHasIdentityBand ? EVisibility::Visible : EVisibility::Collapsed)
		+ SOverlay::Slot()
		[
			SNew(SBorder)
			.BorderImage(VisualStyle.GetBrush(TEXT("Tile.Identity")))
			.BorderBackgroundColor(InArgs._TileColor)
		]
		+ SOverlay::Slot()[SNew(SVerseTileIdentityGradient).RoundBottom(false)]
		+ SOverlay::Slot()
		[
			SNew(SBorder)
			.OnMouseButtonDown(this, &SVerseTile::HandleIdentityMouseButtonDown)
			.BorderImage(FCoreStyle::Get().GetBrush("NoBorder"))
			.Padding(InArgs._Compact
				? FMargin(6.0f, 3.0f)
				: FMargin(9.0f, 6.0f, 9.0f, 7.0f))
			[
				BuildIdentityBand(InArgs._Compact)
			]
		];

	TSharedRef<SOverlay> MainSurface =
		SNew(SOverlay)
		.Visibility(bHasMainRegion ? EVisibility::Visible : EVisibility::Collapsed)
		+ SOverlay::Slot()
		[
			SNew(SBorder)
			.Visibility(bSyncTile ? EVisibility::HitTestInvisible : EVisibility::Collapsed)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(
				VerseVisualEditorStyle::Get().GetColor(TEXT("Color.SynchronizationGlass")))
		]
		+ SOverlay::Slot()[SNew(SVerseTileMainGradient)]
		+ SOverlay::Slot()[MainSocketRow]
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Center)
		[
			bSyncTile ? LeftDock : SNullWidget::NullWidget
		]
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Center)
		[
			bSyncTile ? RightDock : SNullWidget::NullWidget
		];

	TSharedRef<SWidget> Separator =
		SNew(SBox).HeightOverride(1.0f)
		[
			SNew(SImage).Image(VisualStyle.GetBrush(TEXT("Tile.Separator")))
		];
	TSharedRef<SVerticalBox> Regions = SNew(SVerticalBox);
	Regions->AddSlot().AutoHeight()[IdentitySurface];
	Regions->AddSlot().AutoHeight()
		[ SNew(SBox).Visibility(bHasIdentityBand && bHasMainRegion
			? EVisibility::Visible : EVisibility::Collapsed)[Separator] ];
	Regions->AddSlot().AutoHeight()[MainSurface];
	Regions->AddSlot().AutoHeight()
		[ SNew(SBox).Visibility(bHasSourcePreview
			? EVisibility::Visible : EVisibility::Collapsed)[Separator] ];
	Regions->AddSlot().AutoHeight()
		[ SNew(SOverlay)
			.Visibility(bHasSourcePreview ? EVisibility::Visible : EVisibility::Collapsed)
			+ SOverlay::Slot()
			[ SNew(SBorder)
				.BorderImage(VisualStyle.GetBrush(TEXT("Tile.SourcePreview")))
				.Padding(FMargin(9.0f, 5.0f, 9.0f, 6.0f))
				[ SNew(SBox)
					.MaxDesiredWidth(this, &SVerseTile::GetSourcePreviewMaxWidth)
					[SourcePreview] ] ]
			+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom)
			[ SAssignNew(SourceExecutionOutputHost, SBox)
				.Visibility(bHasVerticalExecutionOutputs
					? EVisibility::Visible : EVisibility::Collapsed)
				.VAlign(VAlign_Bottom) ] ];
	Regions->AddSlot().AutoHeight()
		[ SNew(SBorder)
			.Visibility(InArgs._DiagnosticText.IsEmpty()
				? EVisibility::Collapsed : EVisibility::Visible)
			.BorderImage(VisualStyle.GetBrush(TEXT("Tile.Diagnostic")))
			.Padding(FMargin(8.0f, 4.0f))
			[ SNew(STextBlock)
				.Text(InArgs._DiagnosticText)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				.ColorAndOpacity(FLinearColor(1.0f, 0.28f, 0.20f, 1.0f))
				.AutoWrapText(true) ] ];
	Regions->AddSlot().AutoHeight()
		[ SAssignNew(VerticalExecutionOutputHost, SBox)
			.Visibility(bHasVerticalExecutionOutputs && !bHasSourcePreview
				? EVisibility::Visible : EVisibility::Collapsed)
			.VAlign(VAlign_Bottom) ];

	TSharedRef<SBorder> TileSurface =
		SNew(SBorder)
		.OnMouseButtonDown(this, &SVerseTile::HandleTileMouseButtonDown)
		.BorderImage(VisualStyle.GetBrush(TEXT("Tile.Outline")))
		.BorderBackgroundColor(this, &SVerseTile::GetOutlineColor)
		.Padding(Tile.Kind == EVerseVisualTileKind::FailableBlock ? 2.0f : 1.0f)
		[
			SNew(SBorder)
			.BorderImage(VisualStyle.GetBrush(TEXT("Tile.Surface")))
			.Padding(0.0f)
			[
				SNew(SBox)
				.MinDesiredWidth(bIfTile && bHorizontalExecution ? 220.0f : 0.0f)
				[ Regions ]
			]
		]
	;

	TSharedRef<SWidget> ChromeSurface =
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SBorder)
			.Visibility(EVisibility::HitTestInvisible)
			.BorderImage(VisualStyle.GetBrush(TEXT("Tile.Shadow")))
			.BorderBackgroundColor(this, &SVerseTile::GetShadowColor)
			.RenderTransform(FSlateRenderTransform(FVector2D(0.0f, 2.0f)))
		]
		+ SOverlay::Slot()
		[
			TileSurface
		];

	TSharedRef<SOverlay> Decorated = SNew(SOverlay);
	Decorated->AddSlot()[ChromeSurface];
	Decorated->AddSlot()
	.HAlign(HAlign_Right)
	.VAlign(VAlign_Top)
	.Padding(FMargin(0.0f, -8.0f, -8.0f, 0.0f))
	[
		SNew(SImage)
		.Visibility(bSuspendingFunction
			? EVisibility::HitTestInvisible : EVisibility::Collapsed)
		.Image(FAppStyle::GetBrush(TEXT("Graph.Latent.LatentIcon")))
		.DesiredSizeOverride(FVector2D(16.0f, 16.0f))
	];
	const FLinearColor FailureColor = GetVerseFailureDecorationColor();
	if (Tile.Kind == EVerseVisualTileKind::FailableBlock)
	{
		auto AddCorner = [&](EHorizontalAlignment Horizontal, EVerticalAlignment Vertical,
			FVector2D Offset)
		{
			Decorated->AddSlot()
			.HAlign(Horizontal)
			.VAlign(Vertical)
			[
				SNew(SVerseFailableValuePin)
					.Color(FailureColor)
					.Connected(true)
					.Visibility(EVisibility::HitTestInvisible)
					.RenderTransformPivot(FVector2D(0.5f, 0.5f))
					.RenderTransform(FSlateRenderTransform(
						FScale2D(0.82f, 1.18f),
						Offset))
			];
		};
		AddCorner(HAlign_Left, VAlign_Top, FVector2D(-5.5f, -5.5f));
		AddCorner(HAlign_Right, VAlign_Top, FVector2D(5.5f, -5.5f));
		AddCorner(HAlign_Left, VAlign_Bottom, FVector2D(-5.5f, 5.5f));
		AddCorner(HAlign_Right, VAlign_Bottom, FVector2D(5.5f, 5.5f));
	}
	if (Tile.StatementFailure != EVerseStatementFailureDisposition::None)
	{
		Decorated->AddSlot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Top)
		// Straddle the tile boundary so the badge is centered on the
		// top-right corner rather than inset into the header.
		.Padding(FMargin(0.0f, -10.0f, -10.0f, 0.0f))
		[
			SNew(SBox)
			.WidthOverride(20.0f)
			.HeightOverride(20.0f)
			[
				SNew(SVerseFailableValuePin)
					.Color(FailureColor)
					.Connected(true)
					.Visibility(EVisibility::HitTestInvisible)
					.RenderTransformPivot(FVector2D(0.5f, 0.5f))
					.RenderTransform(FSlateRenderTransform(FScale2D(1.8f, 1.8f)))
			]
		];
	}
	TSharedRef<SWidget> DecoratedTileSurface = Decorated;

	TSharedRef<SOverlay> TileAndOutput = SNew(SOverlay);
	TileAndOutput->AddSlot()
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			DecoratedTileSurface
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBox)
			.HeightOverride(0.0f)
		]
	];
	auto AddFloatingValuePin = [this, &TileAndOutput](
		const FVerseVisualSocket& Socket,
		bool bOutput,
		EHorizontalAlignment Horizontal,
		EVerticalAlignment Vertical,
		FVector2D Offset)
	{
		const FString Type = !Socket.SemanticTypeName.IsEmpty()
			? Socket.SemanticTypeName
			: Socket.TypeRange.IsSet()
			? Decode(Socket.TypeRange).ToString()
			: Socket.IntrinsicTypeName.ToString();
		const bool bFailable = Socket.Outcome == EVerseExpressionOutcome::FailableValue
			|| Socket.Outcome == EVerseExpressionOutcome::FailureOnly;
		TSharedPtr<SWidget> PinCore;
		const FLinearColor PinColor = Socket.Outcome == EVerseExpressionOutcome::FailureOnly
			? GetVerseFailureDecorationColor() : GetVerseTilePinColor(Type);
		if (bFailable)
		{
			PinCore = SNew(SVerseFailableValuePin)
				.Color(PinColor)
				.Connected(ConnectedSockets.Contains(Socket.Id))
				.RenderTransformPivot(FVector2D(0.5f, 0.5f))
				.RenderTransform_Lambda([this, SocketId = Socket.Id]()
				{
					return GetSocketDragPinTransform(SocketId);
				})
				.Visibility(EVisibility::HitTestInvisible);
		}
		else
		{
			PinCore = SNew(SImage)
				.Image(GetVerseTilePinBrush(Type, ConnectedSockets.Contains(Socket.Id)))
				.ColorAndOpacity_Lambda([this, SocketId = Socket.Id, PinColor]()
				{
					return GetSocketDragPinColor(SocketId, PinColor);
				})
				.RenderTransformPivot(FVector2D(0.5f, 0.5f))
				.RenderTransform_Lambda([this, SocketId = Socket.Id]()
				{
					return GetSocketDragPinTransform(SocketId);
				})
				.Visibility(EVisibility::HitTestInvisible)
				.DesiredSizeOverride(FVector2D(11.0f, 11.0f));
		}
		TSharedRef<SOverlay> Pin = SNew(SOverlay)
			+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				SNew(SImage)
				.Image(GetVerseTilePinBrush(Type, true))
				.ColorAndOpacity_Lambda([this, SocketId = Socket.Id]()
				{
					return GetSocketDragHaloColor(SocketId);
				})
				.Visibility_Lambda([this, SocketId = Socket.Id]()
				{
					return GetSocketDragHaloVisibility(SocketId);
				})
				.RenderTransformPivot(FVector2D(0.5f, 0.5f))
				.RenderTransform(FSlateRenderTransform(FScale2D(1.45f, 1.45f)))
			]
			+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				PinCore.ToSharedRef()
			];
		SocketAnchors.Add(Socket.Id, Pin);
		TileAndOutput->AddSlot()
		.HAlign(Horizontal)
		.VAlign(Vertical)
		[
			SNew(SBorder)
			.BorderImage(nullptr)
			.Padding(0.0f)
			.ColorAndOpacity_Lambda([this, SocketId = Socket.Id]()
			{
				const float Opacity = GetSocketDragOpacity(SocketId);
				return FLinearColor(1.0f, 1.0f, 1.0f, Opacity);
			})
			.RenderTransform(FSlateRenderTransform(Offset))
			.OnMouseButtonDown(
				this, &SVerseTile::HandleSocketMouseButtonDown,
				TSharedPtr<SWidget>(Pin), Socket, bOutput, 0)
			[
				Pin
			]
		];
	};
	if (bHorizontalImplicitReturnSource && !Tile.GetValueOutputs().IsEmpty())
	{
		AddFloatingValuePin(
			Tile.GetValueOutputs()[0], true,
			HAlign_Center, VAlign_Top, FVector2D(0.0f, -5.5f));
	}
	if (bHorizontalImplicitReturnTile && !Tile.GetValueInputs().IsEmpty())
	{
		AddFloatingValuePin(
			Tile.GetValueInputs()[0], false,
			HAlign_Center, VAlign_Bottom, FVector2D(0.0f, 5.5f));
	}
	if (bHorizontalFailureTerminal && bHasFailureOutput)
	{
		TileAndOutput->AddSlot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Top)
		[
			SNew(SBox)
			.RenderTransform(FSlateRenderTransform(FVector2D(0.0f, -5.5f)))
			[
				FailureContextOutputWidget
			]
		];
	}

	if (!bHorizontalExecution && Tile.FindSocket({EVerseVisualSocketDirection::Output,
		EVerseVisualSocketRole::Execution, 0}) != nullptr)
	{
		const TArray<FText>& OutputLabels = Presentation.ExecutionOutputLabels;
		int32 OutputCount = 0;
		while (Tile.FindSocket({EVerseVisualSocketDirection::Output,
			EVerseVisualSocketRole::Execution, OutputCount}) != nullptr)
		{
			++OutputCount;
		}
		TSharedRef<SHorizontalBox> OutputRow = SNew(SHorizontalBox);
		for (int32 OutputIndex = 0; OutputIndex < OutputCount; ++OutputIndex)
		{
			const float OutputColumnWidth = OutputIndex == 0 ? 72.0f : 64.0f;
			const FVerseVisualSocketId OutputId{
				EVerseVisualSocketDirection::Output,
				EVerseVisualSocketRole::Execution,
				OutputIndex};
			const bool bConnected = ConnectedSockets.Contains(OutputId);
			const FVerseVisualSocketInsertionTarget* InsertionTarget =
				Tile.FindSocketInsertionTarget(OutputId);
			TOptional<FVerseVisualClauseDescriptor> InsertionClause;
			EVerseVisualSocketInsertionKind InsertionKind =
				EVerseVisualSocketInsertionKind::Clause;
			FVerseTextRange InsertionOwnerRange;
			int32 InsertionIndex = INDEX_NONE;
			if (InsertionTarget != nullptr)
			{
				InsertionClause = InsertionTarget->Clause;
				InsertionKind = InsertionTarget->Kind;
				InsertionOwnerRange = InsertionTarget->OwnerExpressionRange;
				InsertionIndex = InsertionTarget->InsertIndex;
			}
			const TSharedRef<SVerseTileExecutionPin> OutputAnchor =
				SNew(SVerseTileExecutionPin)
				.Input(false)
				.Connected(bConnected)
				.Compact(bCompactExecutionSpacing)
				.Presentation(FunctionGraphPresentation);
			OutputRow->AddSlot()
			.AutoWidth()
			[
				SNew(SBox)
				.WidthOverride(OutputColumnWidth)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[
						SNew(STextBlock)
						.Visibility(OutputLabels.IsValidIndex(OutputIndex)
							? EVisibility::Visible
							: EVisibility::Collapsed)
						.Text(OutputLabels.IsValidIndex(OutputIndex)
							? OutputLabels[OutputIndex]
							: FText::GetEmpty())
						.TextStyle(FAppStyle::Get(), "Graph.Node.PinName")
						.ColorAndOpacity(VerseVisualEditorStyle::GetPrimaryTextColor())
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(OutputIndex == 0 ? HAlign_Left : HAlign_Center)
					.Padding(OutputIndex == 0
						? FMargin(12.0f, 4.0f, 0.0f, 0.0f)
						: FMargin(0.0f, 4.0f, 0.0f, 0.0f))
					[
						SNew(SBorder)
						.BorderImage(nullptr)
						.Padding(0.0f)
						.OnMouseButtonDown(
							this,
							&SVerseTile::HandleClauseInsertionMouseButtonDown,
							TSharedPtr<SWidget>(OutputAnchor),
							GetVerseExecutionPinAnchorCoordinate(
								false,
								bCompactExecutionSpacing,
								FunctionGraphPresentation),
							OutputId,
							InsertionClause,
							InsertionKind,
							InsertionOwnerRange,
							InsertionIndex)
						[
							OutputAnchor
						]
					]
				]
			];
			SocketAnchors.Add(OutputId, OutputAnchor);
		}
		if (bHasSourcePreview && SourceExecutionOutputHost.IsValid())
		{
			SourceExecutionOutputHost->SetContent(OutputRow);
		}
		else if (VerticalExecutionOutputHost.IsValid())
		{
			VerticalExecutionOutputHost->SetContent(OutputRow);
		}
		else
		{
			TileAndOutput->AddSlot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Bottom)
			[
				OutputRow
			];
		}
	}
	else if (bHorizontalExecution && Tile.FindSocket({
		EVerseVisualSocketDirection::Output,
		EVerseVisualSocketRole::Execution, 0}) != nullptr)
	{
		const TArray<FText>& OutputLabels = Presentation.ExecutionOutputLabels;
		TSharedRef<SVerticalBox> OutputColumn = SNew(SVerticalBox);
		int32 OutputIndex = 0;
		while (Tile.FindSocket({EVerseVisualSocketDirection::Output,
			EVerseVisualSocketRole::Execution, OutputIndex}) != nullptr)
		{
			const FVerseVisualSocketId OutputId{
				EVerseVisualSocketDirection::Output,
				EVerseVisualSocketRole::Execution,
				OutputIndex};
			const FVerseVisualSocketInsertionTarget* InsertionTarget =
				Tile.FindSocketInsertionTarget(OutputId);
			TOptional<FVerseVisualClauseDescriptor> InsertionClause;
			EVerseVisualSocketInsertionKind InsertionKind =
				EVerseVisualSocketInsertionKind::Clause;
			FVerseTextRange InsertionOwnerRange;
			int32 InsertionIndex = INDEX_NONE;
			if (InsertionTarget != nullptr)
			{
				InsertionClause = InsertionTarget->Clause;
				InsertionKind = InsertionTarget->Kind;
				InsertionOwnerRange = InsertionTarget->OwnerExpressionRange;
				InsertionIndex = InsertionTarget->InsertIndex;
			}
			const TSharedRef<SVerseTileExecutionPin> OutputAnchor =
				SNew(SVerseTileExecutionPin)
				.Input(false)
				.Connected(ConnectedSockets.Contains(OutputId))
				.Compact(bCompactExecutionSpacing)
				.Presentation(FunctionGraphPresentation)
				.RenderTransform(FSlateRenderTransform(FVector2D(16.0f, 0.0f)));
			SocketAnchors.Add(OutputId, OutputAnchor);
			OutputColumn->AddSlot()
			.AutoHeight()
			.HAlign(HAlign_Right)
			.Padding(0.0f, OutputIndex == 0 ? 0.0f : 3.0f, 0.0f, 0.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Visibility(OutputLabels.IsValidIndex(OutputIndex)
						? EVisibility::Visible : EVisibility::Collapsed)
					.Text(OutputLabels.IsValidIndex(OutputIndex)
						? OutputLabels[OutputIndex] : FText::GetEmpty())
					.TextStyle(FAppStyle::Get(), "Graph.Node.PinName")
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SBorder)
					.BorderImage(nullptr)
					.Padding(0.0f)
					.OnMouseButtonDown(
						this,
						&SVerseTile::HandleClauseInsertionMouseButtonDown,
						TSharedPtr<SWidget>(OutputAnchor),
						GetVerseExecutionPinAnchorCoordinate(
							false,
							bCompactExecutionSpacing,
							FunctionGraphPresentation),
						OutputId,
						InsertionClause,
						InsertionKind,
						InsertionOwnerRange,
						InsertionIndex)
					[
						OutputAnchor
					]
				]
			];
			++OutputIndex;
		}
		if (HorizontalExecutionOutputHost.IsValid())
		{
			HorizontalExecutionOutputHost->SetContent(OutputColumn);
		}
	}

	TileWithExecution->AddSlot()
	.AutoHeight()
	.Padding(0.0f, bHasExecutionInput && !bHorizontalExecution ? -8.0f : 0.0f,
		0.0f, 0.0f)
	[
		TileAndOutput
	];
	if (ExecutionInputPin.IsValid() && !bHorizontalExecution)
	{
		ChildSlot
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				TileWithExecution
			]
			+ SOverlay::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Top)
			[
				ExecutionInputPin.ToSharedRef()
			]
		];
	}
	else
	{
		ChildSlot[TileWithExecution];
	}
	if (EndpointRegistry.IsValid())
	{
		for (const TPair<FVerseVisualSocketId, TSharedPtr<SWidget>>& Pair : SocketAnchors)
		{
			if (Pair.Value.IsValid())
			{
				EndpointRegistry->Register({Tile.Id, Pair.Key}, {
					Pair.Value,
					GetSocketAnchorCoordinate(Pair.Key),
					GetSocketRenderScope(Pair.Key),
					MotionTarget,
					GetSocketRenderScope(Pair.Key).IsValid()});
			}
		}
	}
}

float SVerseTile::GetValueSocketCenterY(int32 SocketIndex, bool bOutput) const
{
	// Layout queries happen before arrangement, so derive the row center from
	// the same semantic regions and dock widgets that compose the tile.
	const float ExecutionOffset = Tile.FindSocket({EVerseVisualSocketDirection::Input,
		EVerseVisualSocketRole::Execution, 0}) != nullptr
		&& FunctionGraphPresentation == EVerseFunctionGraphPresentation::VerticalExecution
		? 24.0f : 0.0f;
	const float IdentityHeight = bHasIdentityBand && IdentityBandWidget.IsValid()
		? IdentityBandWidget->GetDesiredSize().Y + 1.0f
		: 0.0f;
	const float MainRowHeight = MainSocketRowWidget.IsValid()
		? MainSocketRowWidget->GetDesiredSize().Y
		: 0.0f;
	const TSharedPtr<SWidget>& Column = bOutput ? ValueOutputColumn : ValueInputColumn;
	const TArray<TSharedPtr<SWidget>>& Rows = bOutput ? ValueOutputRows : ValueInputRows;
	if (!Column.IsValid() || !Rows.IsValidIndex(SocketIndex))
	{
		return ExecutionOffset + 1.0f + IdentityHeight + MainRowHeight * 0.5f;
	}

	float RowCenter = 0.0f;
	for (int32 Index = 0; Index < SocketIndex; ++Index)
	{
		RowCenter += Rows[Index]->GetDesiredSize().Y + 2.0f;
	}
	RowCenter += 1.0f + Rows[SocketIndex]->GetDesiredSize().Y * 0.5f;
	float ColumnTop = (MainRowHeight - Column->GetDesiredSize().Y) * 0.5f;
	if (bOutput && ValueOutputDockWidget.IsValid())
	{
		const float GroupHeight = ValueOutputDockWidget->GetDesiredSize().Y;
		ColumnTop = (MainRowHeight - GroupHeight) * 0.5f
			+ GroupHeight - Column->GetDesiredSize().Y;
	}
	return ExecutionOffset + 1.0f + IdentityHeight + ColumnTop + RowCenter;
}

TSharedRef<SWidget> SVerseTile::BuildIdentityBand(bool bCompact) const
{
	const FText Kind = GetKindText();
	const FText Name = GetNameText();
	const FText Type = GetTypeText();
	const FText Lines = GetLineText();
	const bool bInlineDefinitionType = Tile.Kind == EVerseVisualTileKind::Definition
		&& (Tile.DefinitionKind == VerseSyntaxKind::Variable
			|| Tile.DefinitionKind == VerseSyntaxKind::Constant)
		&& !Name.IsEmpty()
		&& !Type.IsEmpty();
	const FText IdentityName = bInlineDefinitionType
		? FText::Format(LOCTEXT("DefinitionNameAndType", "{0} : {1}"), Name, Type)
		: Name;
	TSharedRef<SVerticalBox> Identity = SNew(SVerticalBox);
	Identity->AddSlot()
	.AutoHeight()
	[
		SNew(SHorizontalBox)
		.Visibility(Kind.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 5.0f, 0.0f)
		[
			SNew(SImage)
			.Visibility(GetIcon() ? EVisibility::Visible : EVisibility::Collapsed)
			.Image(GetIcon())
			.DesiredSizeOverride(FVector2D(16.0f, 16.0f))
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(Kind)
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			.ColorAndOpacity(VerseVisualEditorStyle::GetSecondaryTextColor())
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(bCompact ? 10.0f : 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Visibility(bCompact && !IdentityName.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed)
			.Text(IdentityName)
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
			.ColorAndOpacity(VerseVisualEditorStyle::GetPrimaryTextColor())
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(bCompact ? 8.0f : 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Visibility(bCompact && !bInlineDefinitionType && !Type.IsEmpty()
				? EVisibility::Visible
				: EVisibility::Collapsed)
			.Text(Type.IsEmpty() ? FText::GetEmpty() : FText::Format(LOCTEXT("CompactType", ": {0}"), Type))
			.ColorAndOpacity(VerseVisualEditorStyle::GetSecondaryTextColor())
		]
	];
	if (!bCompact && !IdentityName.IsEmpty())
	{
		Identity->AddSlot().AutoHeight().Padding(0.0f, 2.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(IdentityName)
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
			.ColorAndOpacity(VerseVisualEditorStyle::GetPrimaryTextColor())
		];
	}
	if (!Lines.IsEmpty())
	{
		Identity->AddSlot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(Lines)
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			.ColorAndOpacity(VerseVisualEditorStyle::GetMetadataTextColor())
		];
	}
	if (!bCompact && !bInlineDefinitionType && !Type.IsEmpty()
		&& Tile.Kind == EVerseVisualTileKind::Definition)
	{
		Identity->AddSlot().AutoHeight()
		[
			SNew(STextBlock)
			.Text(FText::Format(LOCTEXT("DefinitionType", "Type: {0}"), Type))
			.ColorAndOpacity(VerseVisualEditorStyle::GetSecondaryTextColor())
		];
	}
	return Identity;
}

float SVerseTile::GetHorizontalExecutionSpineY() const
{
	check(FunctionGraphPresentation !=
		EVerseFunctionGraphPresentation::VerticalExecution);

	const FVerseVisualSocketId InputId{
		EVerseVisualSocketDirection::Input,
		EVerseVisualSocketRole::Execution,
		0};
	const FVerseVisualSocketId OutputId{
		EVerseVisualSocketDirection::Output,
		EVerseVisualSocketRole::Execution,
		0};
	const FVerseVisualSocketId& PrimaryId = Tile.FindSocket(InputId) != nullptr
		? InputId
		: OutputId;
	const TSharedPtr<SWidget> Anchor = GetSocketAnchor(PrimaryId);
	if (!Anchor.IsValid())
	{
		ensureMsgf(false,
			TEXT("Horizontal statement layout requires a real execution socket anchor."));
		return 0.0f;
	}

	Anchor->SlatePrepass();
	const float OutlinePadding = Tile.Kind == EVerseVisualTileKind::FailableBlock
		? 2.0f
		: 1.0f;
	const float IdentityOffset = bHasIdentityBand && IdentityBandWidget.IsValid()
		? IdentityBandWidget->GetDesiredSize().Y
			+ (bHasMainContent ? 1.0f : 0.0f)
		: 0.0f;
	const bool bInput = PrimaryId.Direction == EVerseVisualSocketDirection::Input;
	const TSharedPtr<SWidget>& Dock = bInput
		? HorizontalExecutionInputDockWidget
		: HorizontalExecutionOutputDockWidget;
	const float DockTop = MainSocketRowWidget.IsValid() && Dock.IsValid()
		? FMath::Max(0.0f,
			(MainSocketRowWidget->GetDesiredSize().Y - Dock->GetDesiredSize().Y) * 0.5f)
		: 0.0f;
	return OutlinePadding
		+ IdentityOffset
		+ DockTop
		+ Anchor->GetDesiredSize().Y
			* GetVerseExecutionPinAnchorCoordinate(
				bInput,
				bCompactExecutionSpacing,
				FunctionGraphPresentation).Y;
}

TSharedRef<SWidget> SVerseTile::BuildMainIdentity(bool bCompact) const
{
	const FText Lines = GetLineText();
	const bool bOperator = Tile.Kind == EVerseVisualTileKind::Expression
		&& IsVerseOperatorExpression(Tile.ExpressionKind);
	const bool bIdentifier = Tile.Kind == EVerseVisualTileKind::Expression
		&& Tile.ExpressionKind == EVerseExpressionKind::Identifier;
	const bool bLiteral = Tile.Kind == EVerseVisualTileKind::Expression
		&& Tile.ExpressionKind == EVerseExpressionKind::Literal;
	TSharedRef<SVerticalBox> Main = SNew(SVerticalBox);
	Main->AddSlot().AutoHeight()
	.Padding(Tile.Kind == EVerseVisualTileKind::SyncArm
		? FMargin(4.0f, 3.0f, 4.0f, 0.0f)
		: FMargin(0.0f))
	[
		SNew(STextBlock)
		.Visibility(!Lines.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed)
		.Text(Lines)
		.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
		.ColorAndOpacity(VerseVisualEditorStyle::GetMetadataTextColor())
	];
	if (bOperator)
	{
		Main->AddSlot().AutoHeight()
		[
			SNew(SBox).MinDesiredWidth(72.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Tile.OperatorSpelling))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 22))
				.ColorAndOpacity(VerseVisualEditorStyle::GetPrimaryTextColor())
				.Justification(ETextJustify::Center)
				.Margin(FMargin(0.0f, 2.0f))
			]
		];
	}
	else if (bIdentifier)
	{
		Main->AddSlot().AutoHeight()
		[
			SNew(STextBlock)
			.Text(GetNameText())
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
			.ColorAndOpacity(VerseVisualEditorStyle::GetPrimaryTextColor())
		];
	}
	else if (bLiteral)
	{
		Main->AddSlot().AutoHeight()
		[
			SNew(STextBlock)
			.Text(GetKindText())
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
			.ColorAndOpacity(VerseVisualEditorStyle::GetSecondaryTextColor())
		];
	}
	return Main;
}

FOptionalSize SVerseTile::GetSourcePreviewMaxWidth() const
{
	constexpr float MinimumCoreWidth = 160.0f;
	constexpr float MaximumPreviewGrowth = 100.0f;
	constexpr float PreviewHorizontalPadding = 18.0f;
	const float IdentityWidth = IdentityBandWidget.IsValid()
		? IdentityBandWidget->GetDesiredSize().X : 0.0f;
	const float MainWidth = MainSocketRowWidget.IsValid()
		? MainSocketRowWidget->GetDesiredSize().X : 0.0f;
	const float MeasuredCoreWidth = FMath::Max(IdentityWidth, MainWidth);
	const float CoreWidth = MeasuredCoreWidth > 0.0f
		? MeasuredCoreWidth : MinimumCoreWidth;
	return FOptionalSize(
		CoreWidth + MaximumPreviewGrowth - PreviewHorizontalPadding);
}

TSharedRef<SWidget> SVerseTile::BuildSocketColumn(
	TConstArrayView<FVerseVisualSocket> Sockets,
	bool bOutput)
{
	TSharedRef<SVerticalBox> Column = SNew(SVerticalBox);
	TArray<TSharedPtr<SWidget>>& Rows = bOutput ? ValueOutputRows : ValueInputRows;
	Rows.Reset();
	for (int32 SocketIndex = 0; SocketIndex < Sockets.Num(); ++SocketIndex)
	{
		const FVerseVisualSocket& Socket = Sockets[SocketIndex];
		const bool bHorizontalExecution = FunctionGraphPresentation
			== EVerseFunctionGraphPresentation::HorizontalExecution;
		if (bHorizontalExecution
			&& SocketIndex == 0
			&& ((bOutput && Tile.bImplicitReturnValue)
				|| (!bOutput && Tile.Kind == EVerseVisualTileKind::FunctionReturn)))
		{
			continue;
		}
		const FString Type = !Socket.SemanticTypeName.IsEmpty()
			? Socket.SemanticTypeName
			: Socket.TypeRange.IsSet()
			? Decode(Socket.TypeRange).ToString()
			: Socket.IntrinsicTypeName.ToString();
		const bool bIdentityAlreadyShowsOutputName = bOutput
			&& (Tile.Kind == EVerseVisualTileKind::Definition
				|| (Tile.Kind == EVerseVisualTileKind::Expression
					&& Tile.ExpressionKind == EVerseExpressionKind::Identifier));
		const FText Name = bIdentityAlreadyShowsOutputName
			|| (Tile.Kind == EVerseVisualTileKind::Expression
				&& Tile.OperatorRange.IsSet())
			? FText::GetEmpty()
			: !Socket.SemanticName.IsEmpty()
			? FText::FromString(Socket.SemanticName)
			: Decode(Socket.NameRange);
		TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
		auto AddPin = [&]()
		{
			const bool bFailable =
				Socket.Outcome == EVerseExpressionOutcome::FailableValue
				|| Socket.Outcome == EVerseExpressionOutcome::FailureOnly;
			const FLinearColor PinColor =
				Socket.Outcome == EVerseExpressionOutcome::FailureOnly
					? GetVerseFailureDecorationColor()
					: GetVerseTilePinColor(Type);
			TSharedPtr<SWidget> PinCore;
			if (bFailable)
			{
				PinCore = SNew(SVerseFailableValuePin)
					.Color(PinColor)
					.Connected(ConnectedSockets.Contains(Socket.Id))
					.RenderTransformPivot(FVector2D(0.5f, 0.5f))
					.RenderTransform_Lambda([this, SocketId = Socket.Id]()
					{
						return GetSocketDragPinTransform(SocketId);
					})
					.Visibility(EVisibility::HitTestInvisible);
			}
			else
			{
				PinCore = SNew(SImage)
					.Image(GetVerseTilePinBrush(Type, ConnectedSockets.Contains(Socket.Id)))
					.ColorAndOpacity_Lambda([this, SocketId = Socket.Id, PinColor]()
					{
						return GetSocketDragPinColor(SocketId, PinColor);
					})
					.RenderTransformPivot(FVector2D(0.5f, 0.5f))
					.RenderTransform_Lambda([this, SocketId = Socket.Id]()
					{
						return GetSocketDragPinTransform(SocketId);
					})
					.Visibility(EVisibility::HitTestInvisible)
					.DesiredSizeOverride(FVector2D(11.0f, 11.0f));
			}
			TSharedRef<SOverlay> PinWidget = SNew(SOverlay);
			PinWidget->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				SNew(SImage)
				.Image(GetVerseTilePinBrush(Type, true))
				.ColorAndOpacity_Lambda([this, SocketId = Socket.Id]()
				{
					return GetSocketDragHaloColor(SocketId);
				})
				.Visibility_Lambda([this, SocketId = Socket.Id]()
				{
					return GetSocketDragHaloVisibility(SocketId);
				})
				.RenderTransformPivot(FVector2D(0.5f, 0.5f))
				.RenderTransform(FSlateRenderTransform(FScale2D(1.45f, 1.45f)))
			];
			PinWidget->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				PinCore.ToSharedRef()
			];
			SocketAnchors.Add(Socket.Id, PinWidget);
			Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(bOutput ? 0.0f : -5.0f, 0.0f, bOutput ? -5.0f : 5.0f, 0.0f)
			[
				SNew(SBorder)
					.BorderImage(nullptr)
					.Padding(0.0f)
					.OnMouseButtonDown(this, &SVerseTile::HandleSocketMouseButtonDown,
						TSharedPtr<SWidget>(PinWidget), Socket, bOutput, SocketIndex)
				[
					PinWidget
				]
			];
		};
		auto AddName = [&]()
		{
			Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(5.0f, 0.0f)
			[
				SNew(STextBlock)
				.Visibility(Name.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
				.Text(Name)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				.ColorAndOpacity(VerseVisualEditorStyle::GetPrimaryTextColor())
			];
		};
		auto AddInlineLiteral = [&]()
		{
			if (bOutput || !Socket.InlineLiteralRange.IsSet())
			{
				return;
			}
			Row->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(5.0f, 1.0f, 5.0f, 1.0f)
			[
				SNew(SBox)
				.MinDesiredWidth(18.0f)
				.MaxDesiredWidth(GetVerseGraphMajorGridWidth())
				[
					SNew(SVerseLiteralEditor)
					.LiteralKind(Socket.InlineLiteralKind)
					.LiteralRange(Socket.InlineLiteralRange)
					.SourceText(Decode(Socket.InlineLiteralRange).ToString())
					.OnSourceCommitted(FOnVerseLiteralSourceCommitted::CreateLambda(
						[this](FVerseTextRange Range, FText Source)
						{
							OnInlineLiteralCommitted.ExecuteIfBound(Range, Source);
						}))
				]
			];
		};
		if (bOutput) { AddName(); AddPin(); }
		else { AddPin(); AddInlineLiteral(); AddName(); }
		TSharedRef<SBorder> RowSurface = SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
			.BorderBackgroundColor_Lambda([this, SocketId = Socket.Id, PinColor =
				Socket.Outcome == EVerseExpressionOutcome::FailureOnly
					? GetVerseFailureDecorationColor() : GetVerseTilePinColor(Type)]()
			{
				return GetSocketDragRowColor(SocketId, PinColor);
			})
			.Padding(0.0f)
			.ColorAndOpacity_Lambda([this, SocketId = Socket.Id]()
			{
				const float Opacity = GetSocketDragOpacity(SocketId);
				return FLinearColor(1.0f, 1.0f, 1.0f, Opacity);
			})
			[
				Row
			];
		Rows.Add(RowSurface);
		Column->AddSlot().AutoHeight().HAlign(bOutput ? HAlign_Right : HAlign_Left)
			.Padding(0.0f, 1.0f)[RowSurface];
	}
	if (bOutput)
	{
		ValueOutputColumn = Column;
	}
	else
	{
		ValueInputColumn = Column;
	}
	return Column;
}

FVerseSocketDragStart BuildVerseSocketDragDescriptor(
	const FVerseVisualTile& Tile,
	const FVerseVisualSocket& Socket,
	bool bOutput,
	int32 SocketIndex)
{
	FVerseSocketDragStart DragStart;
	DragStart.Endpoint = {Tile.Id, Socket.Id};
	DragStart.Socket = Socket;
	DragStart.bAdoptsProvisionalTile = Tile.bIsProvisional;
	DragStart.TileRange = Tile.Range;
	DragStart.bOutput = bOutput;
	DragStart.Outcome = Socket.Outcome;
	if (!bOutput)
	{
		DragStart.ParentExpressionKind = Tile.ExpressionKind;
		DragStart.ParentOperatorSpelling = Tile.OperatorSpelling;
		DragStart.ParentOperandIndex = SocketIndex;
		if (Socket.InlineLiteralRange.IsSet())
		{
			DragStart.TileRange = Socket.InlineLiteralRange;
		}
		else if (Tile.Children.IsValidIndex(SocketIndex))
		{
			DragStart.TileRange = Tile.Children[SocketIndex].Range;
		}
		else if (Socket.bUsesDeclaredDefault && Socket.bNamedParameter)
		{
			DragStart.MaterializedInputName = Socket.SemanticName;
		}
	}
	else
	{
		if ((Tile.Kind == EVerseVisualTileKind::Definition
				|| Tile.Kind == EVerseVisualTileKind::FunctionEntry
				|| Socket.Id.Role == EVerseVisualSocketRole::BoundaryBinding)
			&& Socket.NameRange.IsSet())
		{
			DragStart.BoundSourceRange = Socket.NameRange;
			DragStart.BoundExpressionKind = EVerseExpressionKind::Identifier;
		}
		else
		{
			DragStart.BoundSourceRange = Tile.Range;
			DragStart.BoundExpressionKind = Tile.ExpressionKind;
			DragStart.BoundOperatorSpelling = Tile.OperatorSpelling;
			DragStart.bBoundExpressionExplicitlyGrouped = !Tile.GroupingLayers.IsEmpty();
		}
		if (const FVerseVisualSocketInsertionTarget* Target =
			Tile.FindSocketInsertionTarget(Socket.Id))
		{
			DragStart.Clause = Target->Clause;
			DragStart.InsertionKind = Target->Kind;
			DragStart.InsertionOwnerRange = Target->OwnerExpressionRange;
			DragStart.ClauseInsertionIndex = Target->InsertIndex;
		}
	}
	return DragStart;
}

FReply SVerseTile::HandleSocketMouseButtonDown(
	const FGeometry& Geometry,
	const FPointerEvent& MouseEvent,
	TSharedPtr<SWidget> Anchor,
	FVerseVisualSocket Socket,
	bool bOutput,
	int32 SocketIndex)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton
		|| !OnSocketDragStarted.IsBound())
	{
		return FReply::Unhandled();
	}
	FVerseSocketDragStart DragStart = BuildVerseSocketDragDescriptor(
		Tile, Socket, bOutput, SocketIndex);
	DragStart.Anchor = MoveTemp(Anchor);
	DragStart.RenderScope = OwningRenderScope;
	DragStart.bScopedToNestedRenderScope = OwningRenderScope.IsValid();
	DragStart.DesktopPosition = FVerseDesktopPoint(MouseEvent.GetScreenSpacePosition());
	DragStart.WireColor = GetVerseTilePinColor(!Socket.SemanticTypeName.IsEmpty()
		? Socket.SemanticTypeName
		: Socket.TypeRange.IsSet()
		? Decode(Socket.TypeRange).ToString()
		: Socket.IntrinsicTypeName.ToString());
	if (Socket.Outcome == EVerseExpressionOutcome::FailureOnly)
	{
		DragStart.WireColor = GetVerseFailureDecorationColor();
	}
	if (DragStart.bAdoptsProvisionalTile)
	{
		// Adoption is transient UI state. Make the existing widget opaque
		// immediately; the editor removes the corresponding session marker.
		Tile.bIsProvisional = false;
		SetRenderOpacity(1.0f);
	}
	return OnSocketDragStarted.Execute(DragStart);
}

FReply SVerseTile::HandleClauseInsertionMouseButtonDown(
	const FGeometry& Geometry,
	const FPointerEvent& MouseEvent,
	TSharedPtr<SWidget> Anchor,
	FVector2D AnchorCoordinate,
	FVerseVisualSocketId SocketId,
	TOptional<FVerseVisualClauseDescriptor> Clause,
	EVerseVisualSocketInsertionKind InsertionKind,
	FVerseTextRange InsertionOwnerRange,
	int32 InsertIndex)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton
		|| !Clause.IsSet()
		|| InsertIndex == INDEX_NONE
		|| !OnSocketDragStarted.IsBound())
	{
		return FReply::Unhandled();
	}
	FVerseSocketDragStart DragStart;
	DragStart.Purpose = FVerseSocketDragStart::EPurpose::ClauseInsertion;
	DragStart.Anchor = MoveTemp(Anchor);
	const TWeakPtr<SVerseGraphRenderScope> SocketRenderScope =
		Tile.Kind == EVerseVisualTileKind::FailableBlock
			? BodyRenderScope
			: OwningRenderScope;
	DragStart.RenderScope = SocketRenderScope;
	DragStart.bScopedToNestedRenderScope = SocketRenderScope.IsValid();
	DragStart.Endpoint = {Tile.Id, SocketId};
	DragStart.AnchorCoordinate = AnchorCoordinate;
	DragStart.bAdoptsProvisionalTile = Tile.bIsProvisional;
	DragStart.TileRange = Tile.Range;
	DragStart.Clause = MoveTemp(Clause);
	DragStart.InsertionKind = InsertionKind;
	DragStart.InsertionOwnerRange = InsertionOwnerRange;
	DragStart.ClauseInsertionIndex = InsertIndex;
	DragStart.DesktopPosition = FVerseDesktopPoint(MouseEvent.GetScreenSpacePosition());
	DragStart.WireColor = FLinearColor::White;
	DragStart.PreviewAxis = GetVerseExecutionPreviewAxis(FunctionGraphPresentation);
	DragStart.bOutput = true;
	if (DragStart.bAdoptsProvisionalTile)
	{
		Tile.bIsProvisional = false;
		SetRenderOpacity(1.0f);
	}
	return OnSocketDragStarted.Execute(DragStart);
}

FVector2D SVerseTile::GetSocketAnchorCoordinate(
	const FVerseVisualSocketId& SocketId) const
{
	if (SocketId.Role == EVerseVisualSocketRole::ClauseInsertion)
	{
		return GetVerseExecutionPinAnchorCoordinate(
			false, true, FunctionGraphPresentation);
	}
	if (SocketId.Role == EVerseVisualSocketRole::Execution)
	{
		return GetVerseExecutionPinAnchorCoordinate(
			SocketId.Direction == EVerseVisualSocketDirection::Input,
			bCompactExecutionSpacing,
			FunctionGraphPresentation);
	}
	return FVector2D(0.5f, 0.5f);
}

FText SVerseTile::Decode(FVerseByteRange Range) const
{
	return Document.IsValid() && Range.IsSet()
		? FText::FromString(Document->DecodeOriginalRange(Range))
		: FText::GetEmpty();
}

FText SVerseTile::GetKindText() const
{
	switch (Tile.Kind)
	{
	case EVerseVisualTileKind::Definition: return FText::FromName(Tile.DefinitionKind);
	case EVerseVisualTileKind::Comment: return LOCTEXT("CommentKind", "Comment");
	case EVerseVisualTileKind::FailableBlock:
		return LOCTEXT("FailableBlockConditionKind", "Condition");
	case EVerseVisualTileKind::SyncArm:
		return FText::GetEmpty();
	case EVerseVisualTileKind::Expression:
		if (Tile.ExpressionKind == EVerseExpressionKind::Identifier)
		{
			return FText::GetEmpty();
		}
		if (Tile.ExpressionKind == EVerseExpressionKind::Call)
		{
			return LOCTEXT("CallKind", "Function");
		}
		if (Tile.ExpressionKind == EVerseExpressionKind::Literal)
		{
			switch (Tile.LiteralKind)
			{
			case EVerseLiteralKind::Integer: return LOCTEXT("IntegerLiteralKind", "int");
			case EVerseLiteralKind::Float: return LOCTEXT("FloatLiteralKind", "float");
			case EVerseLiteralKind::String: return LOCTEXT("StringLiteralKind", "string");
			case EVerseLiteralKind::Character: return LOCTEXT("CharacterLiteralKind", "char");
			case EVerseLiteralKind::Logic: return LOCTEXT("LogicLiteralKind", "logic");
			default: return FText::GetEmpty();
			}
		}
		if (Tile.ExpressionKind == EVerseExpressionKind::Control)
		{
			switch (Tile.ControlKind)
			{
			case EVerseControlKind::If: return LOCTEXT("IfKind", "If");
			case EVerseControlKind::For: return LOCTEXT("ForKind", "For");
			case EVerseControlKind::Loop: return LOCTEXT("LoopKind", "Loop");
			case EVerseControlKind::Sync: return LOCTEXT("SyncKind", "Sync");
			case EVerseControlKind::Block: return LOCTEXT("BlockKind", "Block");
			default: return LOCTEXT("ControlKind", "Control");
			}
		}
		return IsVerseOperatorExpression(Tile.ExpressionKind)
			? LOCTEXT("OperatorKind", "Operator")
			: LOCTEXT("ExpressionKind", "Expression");
	case EVerseVisualTileKind::FunctionEntry: return LOCTEXT("FunctionEntryKind", "Function");
	case EVerseVisualTileKind::FunctionReturn: return LOCTEXT("FunctionReturnKind", "Implicit Return");
	default: return LOCTEXT("UnknownKind", "unknown");
	}
}

FText SVerseTile::GetNameText() const
{
	if (Tile.Kind == EVerseVisualTileKind::FunctionReturn)
	{
		return FText::GetEmpty();
	}
	FText Name = Decode(Tile.NameRange);
	const FText Specifiers = GetSpecifierText();
	return Specifiers.IsEmpty() ? Name : FText::Format(LOCTEXT("NameWithSpecifiers", "{0}{1}"), Name, Specifiers);
}

FText SVerseTile::GetTypeText() const
{
	return !Tile.SemanticTypeName.IsEmpty()
		? FText::FromString(Tile.SemanticTypeName)
		: Tile.TypeRange.IsSet()
		? Decode(Tile.TypeRange)
		: FText::FromName(Tile.IntrinsicTypeName);
}

FText SVerseTile::GetSpecifierText() const
{
	FString Result;
	for (const FVerseTextRange& Range : Tile.SpecifierRanges)
	{
		const bool bFunctionEffect = Tile.FunctionEffectSpecifierRanges.ContainsByPredicate(
			[&Range](const FVerseTextRange& EffectRange)
			{
				return EffectRange.Revision == Range.Revision
					&& EffectRange.BeginByte == Range.BeginByte
					&& EffectRange.EndByte() == Range.EndByte();
			});
		if (bFunctionEffect)
		{
			continue;
		}
		Result += TEXT("<");
		Result += Decode(Range).ToString();
		Result += TEXT(">");
	}
	return FText::FromString(Result);
}

FText SVerseTile::GetLineText() const
{
	if (Tile.FirstSourceLine == INDEX_NONE || Tile.LastSourceLine == INDEX_NONE)
	{
		return FText::GetEmpty();
	}
	return FText::FromString(Tile.FirstSourceLine == Tile.LastSourceLine
		? FString::Printf(TEXT("L%d"), Tile.FirstSourceLine)
		: FString::Printf(TEXT("L%d-%d"), Tile.FirstSourceLine, Tile.LastSourceLine));
}

const FSlateBrush* SVerseTile::GetIcon() const
{
	if (Tile.Kind == EVerseVisualTileKind::Definition)
	{
		return FAppStyle::GetBrush(GetVerseDefinitionIconName(Tile.DefinitionKind));
	}
	if (Tile.Kind == EVerseVisualTileKind::FunctionEntry)
	{
		return FAppStyle::GetBrush("GraphEditor.Function_16x");
	}
	if (Tile.Kind == EVerseVisualTileKind::Expression
		&& Tile.ExpressionKind == EVerseExpressionKind::Call)
	{
		return FAppStyle::GetBrush("GraphEditor.Function_16x");
	}
	if (Tile.Kind == EVerseVisualTileKind::Expression
		&& Tile.ExpressionKind == EVerseExpressionKind::Control)
	{
		return FAppStyle::GetBrush(Tile.ControlKind == EVerseControlKind::If
			? "GraphEditor.Branch_16x"
			: "GraphEditor.StateMachine_16x");
	}
	return nullptr;
}

FReply SVerseTile::OnMouseButtonDoubleClick(
	const FGeometry& MyGeometry,
	const FPointerEvent& MouseEvent)
{
	return MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && OnOpened.IsBound()
		? OnOpened.Execute()
		: FReply::Unhandled();
}

FReply SVerseTile::OnDragDetected(
	const FGeometry& MyGeometry,
	const FPointerEvent& MouseEvent)
{
	const TSharedPtr<SVerseGraphMotionWidget> Target = MotionTarget.Pin();
	const bool bCanReorder = Tile.EditableClause.IsSet()
		&& Tile.ClauseItemIndex != INDEX_NONE
		&& OnClauseReordered.IsBound();
	if (!Target.IsValid() && !bCanReorder)
	{
		return FReply::Unhandled();
	}
	return FReply::Handled().BeginDragDrop(
		FVerseClauseTileDragDropOp::New(
			Tile.EditableClause,
			Tile.ClauseItemIndex,
			Target,
			MouseEvent.GetScreenSpacePosition()));
}

FReply SVerseTile::OnDrop(
	const FGeometry& MyGeometry,
	const FDragDropEvent& DragDropEvent)
{
	const TSharedPtr<FVerseClauseTileDragDropOp> Operation =
		DragDropEvent.GetOperationAs<FVerseClauseTileDragDropOp>();
	if (!Operation.IsValid()
		|| !Tile.EditableClause.IsSet()
		|| Tile.ClauseItemIndex == INDEX_NONE
		|| !OnClauseReordered.IsBound())
	{
		return FReply::Unhandled();
	}
	const FVerseVisualClauseDescriptor& TargetClause =
		Tile.EditableClause.GetValue();
	const bool bSameClause = Operation->Clause.InteriorRange
		== TargetClause.InteriorRange;
	return bSameClause
		? OnClauseReordered.Execute(
			TargetClause, Operation->ItemIndex, Tile.ClauseItemIndex)
		: FReply::Unhandled();
}

FReply SVerseTile::HandleTileMouseButtonDown(
	const FGeometry& MyGeometry,
	const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	FReply Reply = OnSelected.IsBound()
		? OnSelected.Execute()
		: FReply::Handled();
	return Reply.SetUserFocus(SharedThis(this), EFocusCause::Mouse);
}

FReply SVerseTile::HandleIdentityMouseButtonDown(
	const FGeometry& MyGeometry,
	const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton
		|| !OnSelected.IsBound())
	{
		return FReply::Unhandled();
	}
	FReply Reply = OnSelected.Execute();
	if (MotionTarget.IsValid()
		|| (Tile.EditableClause.IsSet()
			&& Tile.ClauseItemIndex != INDEX_NONE
			&& OnClauseReordered.IsBound()))
	{
		Reply.DetectDrag(SharedThis(this), EKeys::LeftMouseButton);
	}
	return Reply.SetUserFocus(SharedThis(this), EFocusCause::Mouse);
}

FSlateColor SVerseTile::GetOutlineColor() const
{
	if (IsSelected.Get(false))
	{
		return VerseVisualEditorStyle::Get().GetColor(TEXT("Color.Selection"));
	}
	return Tile.StatementFailure != EVerseStatementFailureDisposition::None
		? GetVerseFailureDecorationColor()
		: UnselectedOutlineColor;
}

FSlateColor SVerseTile::GetShadowColor() const
{
	const ISlateStyle& Style = VerseVisualEditorStyle::Get();
	return IsSelected.Get(false)
		? Style.GetColor(TEXT("Color.SelectedShadow"))
		: Style.GetColor(TEXT("Color.Shadow"));
}

EVerseSocketDragVisualState SVerseTile::GetSocketDragState(
	FVerseVisualSocketId SocketId) const
{
	return EndpointRegistry.IsValid()
		? EndpointRegistry->GetDragState({Tile.Id, SocketId})
		: EVerseSocketDragVisualState::Neutral;
}

float SVerseTile::GetSocketDragOpacity(FVerseVisualSocketId SocketId) const
{
	return GetSocketDragState(SocketId) == EVerseSocketDragVisualState::Incompatible
		? 0.25f : 1.0f;
}

FSlateColor SVerseTile::GetSocketDragRowColor(
	FVerseVisualSocketId SocketId,
	FLinearColor TypeColor) const
{
	const EVerseSocketDragVisualState State = GetSocketDragState(SocketId);
	return State == EVerseSocketDragVisualState::Compatible
		|| State == EVerseSocketDragVisualState::HoveredCompatible
		? TypeColor.CopyWithNewOpacity(
			State == EVerseSocketDragVisualState::HoveredCompatible ? 0.16f : 0.08f)
		: FLinearColor::Transparent;
}

FSlateColor SVerseTile::GetSocketDragPinColor(
	FVerseVisualSocketId SocketId,
	FLinearColor TypeColor) const
{
	const EVerseSocketDragVisualState State = GetSocketDragState(SocketId);
	if (State == EVerseSocketDragVisualState::Compatible
		|| State == EVerseSocketDragVisualState::HoveredCompatible
		|| State == EVerseSocketDragVisualState::Source)
	{
		return FLinearColor(
			FMath::Min(TypeColor.R * 1.35f + 0.08f, 1.0f),
			FMath::Min(TypeColor.G * 1.35f + 0.08f, 1.0f),
			FMath::Min(TypeColor.B * 1.35f + 0.08f, 1.0f),
			TypeColor.A);
	}
	return TypeColor;
}

FSlateRenderTransform SVerseTile::GetSocketDragPinTransform(
	FVerseVisualSocketId SocketId) const
{
	const EVerseSocketDragVisualState State = GetSocketDragState(SocketId);
	const float Scale = State == EVerseSocketDragVisualState::Compatible
		|| State == EVerseSocketDragVisualState::HoveredCompatible
		? 1.12f : 1.0f;
	return FSlateRenderTransform(FScale2D(Scale, Scale));
}

EVisibility SVerseTile::GetSocketDragHaloVisibility(
	FVerseVisualSocketId SocketId) const
{
	const EVerseSocketDragVisualState State = GetSocketDragState(SocketId);
	return State == EVerseSocketDragVisualState::Compatible
		|| State == EVerseSocketDragVisualState::HoveredCompatible
		|| State == EVerseSocketDragVisualState::Source
		? EVisibility::HitTestInvisible : EVisibility::Collapsed;
}

FSlateColor SVerseTile::GetSocketDragHaloColor(FVerseVisualSocketId SocketId) const
{
	const EVerseSocketDragVisualState State = GetSocketDragState(SocketId);
	const double Phase = FPlatformTime::Seconds() * 2.0 * PI / 0.9;
	const float Pulse = State == EVerseSocketDragVisualState::HoveredCompatible
		? 1.0f : 0.72f + 0.20f * static_cast<float>((FMath::Sin(Phase) + 1.0) * 0.5);
	return FLinearColor(0.96f, 0.98f, 1.0f, Pulse);
}

#undef LOCTEXT_NAMESPACE
