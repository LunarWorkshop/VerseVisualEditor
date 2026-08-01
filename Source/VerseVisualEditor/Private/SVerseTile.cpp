#include "SVerseTile.h"
#include "SVerseGraphSurface.h"

#include "SVerseLiteralEditor.h"

#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Input/DragAndDrop.h"
#include "GraphEditorSettings.h"
#include "Rendering/DrawElements.h"
#include "Settings/EditorStyleSettings.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "VerseDefinitionIcon.h"
#include "VerseDocument.h"
#include "VerseParseSnapshotBuilder.h"
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

		static TSharedRef<FVerseClauseTileDragDropOp> New(
			const FVerseVisualClauseDescriptor& InClause,
			int32 InItemIndex)
		{
			TSharedRef<FVerseClauseTileDragDropOp> Operation =
				MakeShared<FVerseClauseTileDragDropOp>();
			Operation->Clause = InClause;
			Operation->ItemIndex = InItemIndex;
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

	constexpr float HalfWidth = 12.0f;
	constexpr float HalfHeight = 18.0f;
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

FVector2D GetVerseExecutionPinAnchorCoordinate(bool bInput, bool bCompact)
{
	if (bInput)
	{
		return FVector2D(0.5f, 24.0f / 32.0f);
	}
	return FVector2D(0.5f, 8.0f / (bCompact ? 20.0f : 48.0f));
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
		const UGraphEditorSettings* Settings = GetDefault<UGraphEditorSettings>();
		FString Type = VerseType.TrimStartAndEnd().ToLower();
		while (Type.RemoveFromStart(TEXT("?")) || Type.RemoveFromStart(TEXT("[]")))
		{
		}
		if (Type == TEXT("logic")) return Settings->BooleanPinTypeColor;
		if (Type == TEXT("int")) return Settings->IntPinTypeColor;
		if (Type == TEXT("float")) return Settings->FloatPinTypeColor;
		if (Type == TEXT("string")) return Settings->StringPinTypeColor;
		if (Type == TEXT("message")) return Settings->TextPinTypeColor;
		if (Type == TEXT("char")) return Settings->BytePinTypeColor;
		if (Type == TEXT("type")) return Settings->ClassPinTypeColor;
		return Settings->ObjectPinTypeColor;
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
		{}
			SLATE_ARGUMENT(bool, Input)
			SLATE_ARGUMENT(bool, Connected)
			SLATE_ARGUMENT(bool, Compact)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			bInput = InArgs._Input;
			bConnected = InArgs._Connected;
			bCompact = InArgs._Compact;
			SetCanTick(false);
		}

		virtual FVector2D ComputeDesiredSize(float) const override
		{
			return bInput
				? FVector2D(48.0f, 32.0f)
				: FVector2D(24.0f, bCompact ? 20.0f : 48.0f);
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
				* GetVerseExecutionPinAnchorCoordinate(bInput, bCompact);
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
				HALF_PI,
				PinSize * 0.5f,
				FSlateDrawElement::RelativeToElement,
				InWidgetStyle.GetColorAndOpacityTint());
			return LayerId;
		}

	private:
		bool bInput = false;
		bool bConnected = false;
		bool bCompact = false;
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

}

void SVerseTile::Construct(const FArguments& InArgs)
{
	constexpr float BlueprintCornerRadius = 6.0f;
	constexpr float InnerCornerRadius = BlueprintCornerRadius - 1.0f;
	OuterBrush = MakeUnique<FSlateRoundedBoxBrush>(FLinearColor::White, BlueprintCornerRadius);
	ExpandedHeaderBrush = MakeUnique<FSlateRoundedBoxBrush>(
		FLinearColor::White,
		FVector4(InnerCornerRadius, InnerCornerRadius, 0.0f, 0.0f));
	CollapsedHeaderBrush = MakeUnique<FSlateRoundedBoxBrush>(FLinearColor::White, InnerCornerRadius);
	BodyBrush = MakeUnique<FSlateRoundedBoxBrush>(
		FLinearColor::White,
		FVector4(0.0f, 0.0f, InnerCornerRadius, InnerCornerRadius));
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
			.Connected(ConnectedSockets.Contains(ExecutionInputId));
		SocketAnchors.Add(ExecutionInputId, ExecutionInputPin);
		TileWithExecution->AddSlot()
		.AutoHeight()
		.HAlign(HAlign_Left)
		[
			SNew(SBox)
			.WidthOverride(48.0f)
			.HeightOverride(32.0f)
		];
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
		TSharedRef<SVerticalBox> FailureChain = SNew(SVerticalBox);
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
				.RenderTransform(FSlateRenderTransform(FVector2D(0.0f, -2.0f)));
			SocketAnchors.Add(ClauseInsertionId, EntryPin);
			FailureChain->AddSlot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(0.0f, 0.0f, 0.0f, 4.0f)
			[
				SNew(SBorder)
				.BorderImage(nullptr)
				.Padding(0.0f)
				.OnMouseButtonDown(
					this,
					&SVerseTile::HandleClauseInsertionMouseButtonDown,
					TSharedPtr<SWidget>(EntryPin),
					GetVerseExecutionPinAnchorCoordinate(false, true),
					ClauseInsertionId,
					InsertionClause,
					InsertionKind,
					InsertionOwnerRange,
					InsertionIndex)
				[
					EntryPin
				]
			];
		}
		FailureChain->AddSlot()
		.AutoHeight()
		.Padding(FMargin(20.0f, 20.0f, 20.0f, 28.0f))
		[
			BodyContent
		];
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

	TSharedRef<SBorder> TileSurface =
		SNew(SBorder)
		.OnMouseButtonDown(this, &SVerseTile::HandleTileMouseButtonDown)
		.BorderImage(OuterBrush.Get())
		.BorderBackgroundColor(this, &SVerseTile::GetOutlineColor)
		.Padding(Tile.Kind == EVerseVisualTileKind::FailableBlock ? 2.0f : 1.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBorder)
				.BorderImage(this, &SVerseTile::GetHeaderBrush)
				.BorderBackgroundColor(InArgs._TileColor)
				.Padding(0.0f)
				[
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
						.ColorAndOpacity(FLinearColor(0.68f, 0.72f, 0.78f, 1.0f))
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
					]
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBorder)
				.Visibility(this, &SVerseTile::GetBodyVisibility)
				.BorderImage(BodyBrush.Get())
				.BorderBackgroundColor(FLinearColor(0.04f, 0.04f, 0.05f, 1.0f))
				.Padding(0.0f)
				[
					BodyContent
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBox)
				.HeightOverride(bHasLabeledExecutionOutputs ? 32.0f : 0.0f)
			]
		]
	;

	TSharedRef<SWidget> DecoratedTileSurface = TileSurface;
	if (Tile.Kind == EVerseVisualTileKind::FailableBlock)
	{
		TSharedRef<SOverlay> Decorated = SNew(SOverlay);
		Decorated->AddSlot()[TileSurface];
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
				? (bCompactExecutionSpacing ? 12.0f : 41.0f)
				: 0.0f)
		]
	];

	if (Tile.FindSocket({EVerseVisualSocketDirection::Output,
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
				.Compact(bCompactExecutionSpacing);
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
						.ColorAndOpacity(FLinearColor(0.92f, 0.92f, 0.94f, 1.0f))
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
								bCompactExecutionSpacing),
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

	TileWithExecution->AddSlot()
	.AutoHeight()
	.Padding(0.0f, bHasExecutionInput ? -8.0f : 0.0f, 0.0f, 0.0f)
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
		EVerseVisualSocketRole::Execution, 0}) != nullptr ? 24.0f : 0.0f;
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
	const float ColumnTop = (HeaderRowHeight - Column->GetDesiredSize().Y) * 0.5f;
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
			.ColorAndOpacity(FLinearColor(0.84f, 0.91f, 1.0f, 1.0f))
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(bCompact ? 10.0f : 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Visibility(bCompact && !HeaderName.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed)
			.Text(HeaderName)
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
			.ColorAndOpacity(FLinearColor(0.95f, 0.95f, 0.97f, 1.0f))
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(bCompact ? 8.0f : 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Visibility(bCompact && !bInlineDefinitionType && !Type.IsEmpty()
				? EVisibility::Visible
				: EVisibility::Collapsed)
			.Text(Type.IsEmpty() ? FText::GetEmpty() : FText::Format(LOCTEXT("CompactType", ": {0}"), Type))
			.ColorAndOpacity(FLinearColor(0.82f, 0.84f, 0.88f, 1.0f))
		]
	];
	if (!bCompact && !HeaderName.IsEmpty())
	{
		Header->AddSlot().AutoHeight().Padding(0.0f, 2.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(HeaderName)
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
			.ColorAndOpacity(FLinearColor(0.95f, 0.95f, 0.97f, 1.0f))
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
			.ColorAndOpacity(FLinearColor(0.68f, 0.72f, 0.78f, 1.0f))
		];
	}
	if (!bCompact && !bInlineDefinitionType && !Type.IsEmpty()
		&& Tile.Kind == EVerseVisualTileKind::Definition)
	{
		Header->AddSlot().AutoHeight()
		[
			SNew(STextBlock)
			.Text(FText::Format(LOCTEXT("DefinitionType", "Type: {0}"), Type))
			.ColorAndOpacity(FLinearColor(0.82f, 0.84f, 0.88f, 1.0f))
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
				.ColorAndOpacity(FLinearColor(0.92f, 0.92f, 0.94f, 1.0f))
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
		return GetVerseExecutionPinAnchorCoordinate(false, true);
	}
	if (SocketId.Role == EVerseVisualSocketRole::Execution)
	{
		return GetVerseExecutionPinAnchorCoordinate(
			SocketId.Direction == EVerseVisualSocketDirection::Input,
			bCompactExecutionSpacing);
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
	if (!Tile.EditableClause.IsSet()
		|| Tile.ClauseItemIndex == INDEX_NONE
		|| !OnClauseReordered.IsBound())
	{
		return FReply::Unhandled();
	}
	return FReply::Handled().BeginDragDrop(
		FVerseClauseTileDragDropOp::New(
			Tile.EditableClause.GetValue(), Tile.ClauseItemIndex));
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
	if (Tile.EditableClause.IsSet()
		&& Tile.ClauseItemIndex != INDEX_NONE
		&& OnClauseReordered.IsBound())
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
	return bShowBody && (!bCollapsible || bExpanded)
		? ExpandedHeaderBrush.Get()
		: CollapsedHeaderBrush.Get();
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

#undef LOCTEXT_NAMESPACE
