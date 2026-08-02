#include "SVerseTile.h"
#include "SVerseGraphSurface.h"

#include "SVerseLiteralEditor.h"

#include "Brushes/SlateColorBrush.h"
#include "Input/DragAndDrop.h"
#include "Rendering/DrawElements.h"
#include "Settings/EditorStyleSettings.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "VerseDefinitionIcon.h"
#include "VerseDocument.h"
#include "VerseParseSnapshotBuilder.h"
#include "VerseVisualEditorStyle.h"
#include "VerseGraphMotion.h"
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
	return FVector2D(0.5f, 8.0f / (bCompact ? 20.0f : 48.0f));
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
		: FVector2D(24.0f, bCompact ? 20.0f : 48.0f);
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
	class SVerseTileHeaderGradient final : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SVerseTileHeaderGradient)
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
	class SVerseTileBodyGradient final : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SVerseTileBodyGradient) {}
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
	OwningRenderScope = InArgs._OwningRenderScope;
	BodyRenderScope = InArgs._BodyRenderScope;
	UnselectedOutlineColor = InArgs._UnselectedOutlineColor;
	bShowBody = InArgs._ShowBody;
	bCollapsible = bShowBody && !(
		Tile.Kind == EVerseVisualTileKind::Expression
		&& IsVerseOperatorExpression(Tile.ExpressionKind));
	const bool bHasLabeledExecutionOutputs = !InArgs._ExecutionOutputLabels.IsEmpty();
	bCompactExecutionSpacing = InArgs._CompactExecutionSpacing;
	FunctionGraphPresentation = InArgs._FunctionGraphPresentation;
	const bool bHorizontalExecution = FunctionGraphPresentation
		!= EVerseFunctionGraphPresentation::VerticalExecution;

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
	const FText OperatorLines = bOperatorTile ? GetLineText() : FText::GetEmpty();
	TSharedRef<SWidget> BodyContent = InArgs._BodyContent.Widget;
	if (Tile.ExpressionKind == EVerseExpressionKind::Literal
		&& Tile.LiteralKind != EVerseLiteralKind::None)
	{
		BodyContent = SNew(SBorder)
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
	if (Tile.Kind == EVerseVisualTileKind::FailableBlock)
	{
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
		if (bHorizontalExecution)
		{
			FailureChain =
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					EntryPinButton.IsValid()
						? EntryPinButton.ToSharedRef()
						: SNullWidget::NullWidget
				]
				+ SHorizontalBox::Slot().AutoWidth()
				.Padding(FMargin(0.0f, 20.0f, 20.0f, 28.0f))
				[
					BodyContent
				];
		}
		else
		{
			BodyContent->SlatePrepass();
			const float BodyWidth = BodyContent->GetDesiredSize().X;
			const float FailureChainWidth = BodyWidth + 40.0f;
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
					BodyContent
				];
		}
		TSharedPtr<SVerseGraphRenderScope> LocalBodyRenderScope = InArgs._BodyRenderScope;
		if (!LocalBodyRenderScope.IsValid())
		{
			LocalBodyRenderScope = SNew(SVerseGraphRenderScope)
				.Background(EVerseGraphRenderScopeBackground::Failable)
				.ClipToBounds(true);
		}
		LocalBodyRenderScope->SetContent(FailureChain);
		BodyContent = LocalBodyRenderScope.ToSharedRef();
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
	if (Tile.Kind == EVerseVisualTileKind::FailableBlock)
	{
		const FVerseVisualSocketId FailureOutputId{
			EVerseVisualSocketDirection::Output,
			EVerseVisualSocketRole::FailureContext,
			0};
		const TSharedRef<SVerseFailableValuePin> Pin =
			SNew(SVerseFailableValuePin)
				.Color(GetVerseFailureDecorationColor())
				.Connected(ConnectedSockets.Contains(FailureOutputId))
				.Visibility(EVisibility::HitTestInvisible)
				.RenderTransform(FSlateRenderTransform(FVector2D(5.5f, 0.0f)));
		SocketAnchors.Add(FailureOutputId, Pin);
		FailureContextOutputWidget = Pin;
	}
	TSharedRef<SWidget> ValueOutputWidget = BuildSocketColumn(Tile.GetValueOutputs(), true);
	TSharedRef<SWidget> HeaderOutputGroup = ValueOutputWidget;
	if (Tile.Kind == EVerseVisualTileKind::FailableBlock)
	{
		HeaderOutputGroup =
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
	if (bHorizontalExecution)
	{
		int32 ExecutionOutputCount = 0;
		while (Tile.FindSocket({EVerseVisualSocketDirection::Output,
			EVerseVisualSocketRole::Execution, ExecutionOutputCount}) != nullptr)
		{
			++ExecutionOutputCount;
		}
		if (ExecutionOutputCount > 0)
		{
			constexpr float ExecutionRowHeight = 24.0f;
			constexpr float ExecutionRowGap = 3.0f;
			const float ExecutionColumnHeight =
				ExecutionOutputCount * ExecutionRowHeight
				+ (ExecutionOutputCount - 1) * ExecutionRowGap;
			HeaderOutputGroup =
				SNew(SBox)
				.Padding(0.0f, ExecutionColumnHeight, 0.0f, 0.0f)
				[
					HeaderOutputGroup
				];
		}
	}
	HeaderOutputGroupWidget = HeaderOutputGroup;
	const ISlateStyle& VisualStyle = VerseVisualEditorStyle::Get();

	TSharedRef<SVerticalBox> HeaderContents =
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SAssignNew(OperatorLineWidget, STextBlock)
			.Visibility(bOperatorTile && !OperatorLines.IsEmpty()
				? EVisibility::Visible
				: EVisibility::Collapsed)
			.Text(OperatorLines)
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			.ColorAndOpacity(VerseVisualEditorStyle::GetMetadataTextColor())
			.Margin(FMargin(5.0f, 2.0f, 0.0f, 0.0f))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SAssignNew(HeaderSocketRow, SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				FailureContextInputWidget
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				BuildSocketColumn(Tile.GetValueInputs(), false)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Top)
			.Padding(InArgs._ArrowPadding)
			[
				SNew(SButton)
				.Visibility(bCollapsible ? EVisibility::Visible : EVisibility::Collapsed)
				.ButtonStyle(FCoreStyle::Get(), "NoBorder")
				.ContentPadding(0.0f)
				.OnClicked(this, &SVerseTile::ToggleExpanded)
				[
					SNew(SImage).Image(this, &SVerseTile::GetExpansionImage)
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SBorder)
				.OnMouseButtonDown(this, &SVerseTile::HandleHeaderMouseButtonDown)
				.BorderImage(FCoreStyle::Get().GetBrush("NoBorder"))
				.Padding(InArgs._HeaderPadding)
				[
					BuildHeader(InArgs._Compact, InArgs._DiagnosticText)
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				HeaderOutputGroup
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				Tile.Kind == EVerseVisualTileKind::FailableBlock
					? SNullWidget::NullWidget
					: FailureContextOutputWidget
			]
		];

	TSharedRef<SOverlay> HeaderSurface =
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SBorder)
			.BorderImage(this, &SVerseTile::GetHeaderBrush)
			.BorderBackgroundColor(InArgs._TileColor)
		]
		+ SOverlay::Slot()
		[
			SNew(SVerseTileHeaderGradient)
			.RoundBottom_Lambda([this]()
			{
				return GetBodyVisibility() != EVisibility::Visible;
			})
		]
		+ SOverlay::Slot()
		[
			SNew(SBorder)
			.Visibility(EVisibility::HitTestInvisible)
			.BorderImage(this, &SVerseTile::GetHeaderHighlightBrush)
		]
		+ SOverlay::Slot()
		[
			HeaderContents
		];

	TSharedRef<SOverlay> BodySurface =
		SNew(SOverlay)
		.Visibility(this, &SVerseTile::GetBodyVisibility)
		+ SOverlay::Slot()
		[
			SNew(SBorder)
			.BorderImage(VisualStyle.GetBrush(TEXT("Tile.Body")))
			.Padding(0.0f)
		]
		+ SOverlay::Slot()
		[
			SNew(SVerseTileBodyGradient)
		]
		+ SOverlay::Slot()
		[
			BodyContent
		]
		+ SOverlay::Slot()
		.VAlign(VAlign_Top)
		[
			SNew(SBox)
			.HeightOverride(1.0f)
			[
				SNew(SImage)
				.Image(VisualStyle.GetBrush(TEXT("Tile.Separator")))
			]
		];

	TSharedRef<SBorder> TileSurface =
		SNew(SBorder)
		.OnMouseButtonDown(this, &SVerseTile::HandleTileMouseButtonDown)
		.BorderImage(VisualStyle.GetBrush(TEXT("Tile.Outline")))
		.BorderBackgroundColor(this, &SVerseTile::GetOutlineColor)
		.Padding(Tile.Kind == EVerseVisualTileKind::FailableBlock ? 2.0f : 1.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				HeaderSurface
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				BodySurface
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBox)
				.HeightOverride(bHasLabeledExecutionOutputs ? 32.0f : 0.0f)
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

	TSharedRef<SWidget> DecoratedTileSurface = ChromeSurface;
	if (Tile.Kind == EVerseVisualTileKind::FailableBlock)
	{
		TSharedRef<SOverlay> Decorated = SNew(SOverlay);
		Decorated->AddSlot()[ChromeSurface];
		const FLinearColor FailureColor = GetVerseFailureDecorationColor();
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
		DecoratedTileSurface = Decorated;
	}

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
			.HeightOverride(Tile.FindSocket({EVerseVisualSocketDirection::Output,
				EVerseVisualSocketRole::Execution, 0}) != nullptr
				&& !bHorizontalExecution
				? (bCompactExecutionSpacing ? 12.0f : 41.0f)
				: 0.0f)
		]
	];

	if (!bHorizontalExecution && Tile.FindSocket({EVerseVisualSocketDirection::Output,
		EVerseVisualSocketRole::Execution, 0}) != nullptr)
	{
		const TArray<FText>& OutputLabels = InArgs._ExecutionOutputLabels;
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
		TileAndOutput->AddSlot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Bottom)
		[
			OutputRow
		];
	}
	else if (bHorizontalExecution && Tile.FindSocket({
		EVerseVisualSocketDirection::Output,
		EVerseVisualSocketRole::Execution, 0}) != nullptr)
	{
		const TArray<FText>& OutputLabels = InArgs._ExecutionOutputLabels;
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
		TileAndOutput->AddSlot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Top)
		[
			OutputColumn
		];
	}

	TileWithExecution->AddSlot()
	.AutoHeight()
	.Padding(0.0f, bHasExecutionInput && !bHorizontalExecution ? -8.0f : 0.0f,
		0.0f, 0.0f)
	[
		TileAndOutput
	];
	if (ExecutionInputPin.IsValid())
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
}

float SVerseTile::GetValueSocketCenterY(int32 SocketIndex, bool bOutput) const
{
	// Execution input consumes 32 Slate units, while the tile surface overlaps
	// it by 8. The outer one-unit outline precedes the header contents. Both
	// value-pin columns are vertically centered in HeaderSocketRow.
	const float ExecutionOffset = Tile.FindSocket({EVerseVisualSocketDirection::Input,
		EVerseVisualSocketRole::Execution, 0}) != nullptr
		&& FunctionGraphPresentation == EVerseFunctionGraphPresentation::VerticalExecution
		? 24.0f : 0.0f;
	const float OperatorLineHeight = OperatorLineWidget.IsValid()
		? OperatorLineWidget->GetDesiredSize().Y
		: 0.0f;
	const float HeaderRowHeight = HeaderSocketRow.IsValid()
		? HeaderSocketRow->GetDesiredSize().Y
		: 0.0f;
	const TSharedPtr<SWidget>& Column = bOutput ? ValueOutputColumn : ValueInputColumn;
	const TArray<TSharedPtr<SWidget>>& Rows = bOutput ? ValueOutputRows : ValueInputRows;
	if (!Column.IsValid() || !Rows.IsValidIndex(SocketIndex))
	{
		return ExecutionOffset + 1.0f + OperatorLineHeight + HeaderRowHeight * 0.5f;
	}

	float RowCenter = 0.0f;
	for (int32 Index = 0; Index < SocketIndex; ++Index)
	{
		RowCenter += Rows[Index]->GetDesiredSize().Y + 2.0f;
	}
	RowCenter += 1.0f + Rows[SocketIndex]->GetDesiredSize().Y * 0.5f;
	float ColumnTop = (HeaderRowHeight - Column->GetDesiredSize().Y) * 0.5f;
	if (bOutput && HeaderOutputGroupWidget.IsValid())
	{
		const float GroupHeight = HeaderOutputGroupWidget->GetDesiredSize().Y;
		ColumnTop = (HeaderRowHeight - GroupHeight) * 0.5f
			+ GroupHeight - Column->GetDesiredSize().Y;
	}
	return ExecutionOffset + 1.0f + OperatorLineHeight + ColumnTop + RowCenter;
}

TSharedRef<SWidget> SVerseTile::BuildHeader(bool bCompact, const FText& DiagnosticText) const
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
	const FText HeaderName = bInlineDefinitionType
		? FText::Format(LOCTEXT("DefinitionNameAndType", "{0} : {1}"), Name, Type)
		: Name;
	TSharedRef<SVerticalBox> Header = SNew(SVerticalBox);
	if (Tile.Kind == EVerseVisualTileKind::Expression
		&& IsVerseOperatorExpression(Tile.ExpressionKind))
	{
		Header->AddSlot()
		.AutoHeight()
		[
			SNew(SBox)
			.MinDesiredWidth(72.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Tile.OperatorSpelling))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 22))
				.ColorAndOpacity(VerseVisualEditorStyle::GetPrimaryTextColor())
				.Justification(ETextJustify::Center)
				.Margin(FMargin(0.0f, 2.0f))
			]
		];
		if (!DiagnosticText.IsEmpty())
		{
			Header->AddSlot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(DiagnosticText)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				.ColorAndOpacity(FLinearColor(1.0f, 0.20f, 0.12f, 1.0f))
				.AutoWrapText(true)
			];
		}
		return Header;
	}
	Header->AddSlot()
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
			.Visibility(bCompact && !HeaderName.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed)
			.Text(HeaderName)
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
	if (!bCompact && !HeaderName.IsEmpty())
	{
		Header->AddSlot().AutoHeight().Padding(0.0f, 2.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(HeaderName)
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
			.ColorAndOpacity(VerseVisualEditorStyle::GetPrimaryTextColor())
		];
	}
	if (!Lines.IsEmpty())
	{
		const float LineLeftPadding = bShowBody ? -19.0f : 0.0f;
		const float LineTopPadding =
			Tile.Kind == EVerseVisualTileKind::FailableBlock ? 10.0f : 6.0f;
		Header->AddSlot().AutoHeight().Padding(
			LineLeftPadding, LineTopPadding, 0.0f, 0.0f)
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
		Header->AddSlot().AutoHeight()
		[
			SNew(STextBlock)
			.Text(FText::Format(LOCTEXT("DefinitionType", "Type: {0}"), Type))
			.ColorAndOpacity(VerseVisualEditorStyle::GetSecondaryTextColor())
		];
	}
	if (!DiagnosticText.IsEmpty())
	{
		Header->AddSlot().AutoHeight().Padding(-19.0f, 4.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(DiagnosticText)
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			.ColorAndOpacity(FLinearColor(1.0f, 0.20f, 0.12f, 1.0f))
			.AutoWrapText(true)
		];
	}
	return Header;
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
		const FString Type = !Socket.SemanticTypeName.IsEmpty()
			? Socket.SemanticTypeName
			: Socket.TypeRange.IsSet()
			? Decode(Socket.TypeRange).ToString()
			: Socket.IntrinsicTypeName.ToString();
		const bool bHeaderAlreadyShowsOutputName = bOutput
			&& Tile.Kind == EVerseVisualTileKind::Definition;
		const FText Name = bHeaderAlreadyShowsOutputName
			|| (Tile.Kind == EVerseVisualTileKind::Expression
				&& Tile.OperatorRange.IsSet())
			? FText::GetEmpty()
			: !Socket.SemanticName.IsEmpty()
			? FText::FromString(Socket.SemanticName)
			: Decode(Socket.NameRange);
		TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
		Rows.Add(Row);
		auto AddPin = [&]()
		{
			const bool bFailable =
				Socket.Outcome == EVerseExpressionOutcome::FailableValue
				|| Socket.Outcome == EVerseExpressionOutcome::FailureOnly;
			const FLinearColor PinColor =
				Socket.Outcome == EVerseExpressionOutcome::FailureOnly
					? GetVerseFailureDecorationColor()
					: GetVerseTilePinColor(Type);
			TSharedPtr<SWidget> PinWidget;
			if (bFailable)
			{
				PinWidget = SNew(SVerseFailableValuePin)
					.Color(PinColor)
					.Connected(ConnectedSockets.Contains(Socket.Id))
					.Visibility(EVisibility::HitTestInvisible);
			}
			else
			{
				PinWidget = SNew(SImage)
					.Image(GetVerseTilePinBrush(Type, ConnectedSockets.Contains(Socket.Id)))
					.ColorAndOpacity(PinColor)
					.Visibility(EVisibility::HitTestInvisible)
					.DesiredSizeOverride(FVector2D(11.0f, 11.0f));
			}
			if (bOutput)
			{
				SocketAnchors.Add(Socket.Id, PinWidget);
			}
			else
			{
				SocketAnchors.Add(Socket.Id, PinWidget);
			}
			Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(bOutput ? 0.0f : -5.0f, 0.0f, bOutput ? -5.0f : 5.0f, 0.0f)
			[
				SNew(SBorder)
					.BorderImage(nullptr)
					.Padding(0.0f)
					.OnMouseButtonDown(this, &SVerseTile::HandleSocketMouseButtonDown,
						PinWidget, Socket, bOutput, SocketIndex)
				[
					PinWidget.ToSharedRef()
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
		Column->AddSlot().AutoHeight().HAlign(bOutput ? HAlign_Right : HAlign_Left).Padding(0.0f, 1.0f)[Row];
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
	FVerseSocketDragStart DragStart;
	DragStart.Anchor = MoveTemp(Anchor);
	DragStart.RenderScope = OwningRenderScope;
	DragStart.bScopedToNestedRenderScope = OwningRenderScope.IsValid();
	DragStart.Endpoint = {Tile.Id, Socket.Id};
	DragStart.bAdoptsProvisionalTile = Tile.bIsProvisional;
	DragStart.TileRange = Tile.Range;
	if (!bOutput)
	{
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
		}
		else
		{
			DragStart.BoundSourceRange = Tile.Range;
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
	DragStart.DesktopPosition = FVerseDesktopPoint(MouseEvent.GetScreenSpacePosition());
	DragStart.WireColor = GetVerseTilePinColor(!Socket.SemanticTypeName.IsEmpty()
		? Socket.SemanticTypeName
		: Socket.TypeRange.IsSet()
		? Decode(Socket.TypeRange).ToString()
		: Socket.IntrinsicTypeName.ToString());
	DragStart.Outcome = Socket.Outcome;
	if (Socket.Outcome == EVerseExpressionOutcome::FailureOnly)
	{
		DragStart.WireColor = GetVerseFailureDecorationColor();
	}
	DragStart.bOutput = bOutput;
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

FReply SVerseTile::HandleHeaderMouseButtonDown(
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

FReply SVerseTile::ToggleExpanded()
{
	if (!bCollapsible)
	{
		return FReply::Handled();
	}
	bExpanded = !bExpanded;
	Invalidate(EInvalidateWidgetReason::Layout | EInvalidateWidgetReason::Paint);
	return FReply::Handled();
}

const FSlateBrush* SVerseTile::GetExpansionImage() const
{
	return FCoreStyle::Get().GetBrush(bExpanded ? "TreeArrow_Expanded" : "TreeArrow_Collapsed");
}

const FSlateBrush* SVerseTile::GetHeaderBrush() const
{
	const ISlateStyle& Style = VerseVisualEditorStyle::Get();
	return bShowBody && (!bCollapsible || bExpanded)
		? Style.GetBrush(TEXT("Tile.Header.Expanded"))
		: Style.GetBrush(TEXT("Tile.Header.Collapsed"));
}

const FSlateBrush* SVerseTile::GetHeaderHighlightBrush() const
{
	const ISlateStyle& Style = VerseVisualEditorStyle::Get();
	return bShowBody && (!bCollapsible || bExpanded)
		? Style.GetBrush(TEXT("Tile.Header.Highlight.Expanded"))
		: Style.GetBrush(TEXT("Tile.Header.Highlight.Collapsed"));
}

EVisibility SVerseTile::GetBodyVisibility() const
{
	return bShowBody && (!bCollapsible || bExpanded)
		? EVisibility::Visible
		: EVisibility::Collapsed;
}

FSlateColor SVerseTile::GetOutlineColor() const
{
	return IsSelected.Get(false)
		? FLinearColor(1.0f, 0.82f, 0.05f, 1.0f)
		: UnselectedOutlineColor;
}

FSlateColor SVerseTile::GetShadowColor() const
{
	const ISlateStyle& Style = VerseVisualEditorStyle::Get();
	return IsSelected.Get(false)
		? Style.GetColor(TEXT("Color.SelectedShadow"))
		: Style.GetColor(TEXT("Color.Shadow"));
}

#undef LOCTEXT_NAMESPACE
