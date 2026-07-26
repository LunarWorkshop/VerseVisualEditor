#include "SVerseTileCanvas.h"

#include "Layout/Clipping.h"
#include "Rendering/DrawElements.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "VerseParseSnapshotBuilder.h"
#include "VerseVisualTile.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SScrollBar.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Text/SMultiLineEditableText.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SVerseTileCanvas"

namespace
{
	constexpr float MinimumZoom = 0.5f;
	constexpr float MaximumZoom = 2.0f;
	constexpr float ZoomStep = 0.1f;

	bool BelongsInCompactStack(const FVerseVisualTile& Tile)
	{
		return Tile.Kind == EVerseVisualTileKind::Comment
			|| (Tile.Kind == EVerseVisualTileKind::Definition
				&& (Tile.DefinitionKind == VerseSyntaxKind::Constant
					|| Tile.DefinitionKind == VerseSyntaxKind::TypeAlias));
	}
}

void SVerseTileCanvas::Construct(
	const FArguments& InArgs,
	FVerseParseSnapshot InSnapshot,
	float InitialVerticalScrollOffset)
{
	Snapshot.Emplace(MoveTemp(InSnapshot));
	HorizontalScrollbar = SNew(SScrollBar).Orientation(Orient_Horizontal);
	VerticalScrollbar = SNew(SScrollBar).Orientation(Orient_Vertical);

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SAssignNew(VerticalScrollBox, SScrollBox)
				.Orientation(Orient_Vertical)
				.ExternalScrollbar(VerticalScrollbar)
				.ConsumeMouseWheel(EConsumeMouseWheel::Never)
				+ SScrollBox::Slot()
				[
					SAssignNew(HorizontalScrollBox, SScrollBox)
					.Orientation(Orient_Horizontal)
					.ExternalScrollbar(HorizontalScrollbar)
					.ConsumeMouseWheel(EConsumeMouseWheel::Never)
					+ SScrollBox::Slot()
					[
						SAssignNew(ScaleBox, SScaleBox)
						.Stretch(EStretch::UserSpecified)
						.StretchDirection(EStretchDirection::Both)
						.UserSpecifiedScale(Zoom)
						.HAlign(HAlign_Left)
						.VAlign(VAlign_Top)
						[
							BuildTileRow()
						]
					]
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				VerticalScrollbar.ToSharedRef()
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				HorizontalScrollbar.ToSharedRef()
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SSpacer)
				.Size(FVector2D(12.0f, 12.0f))
			]
		]
	];

	VerticalScrollBox->SetScrollOffset(FMath::Max(0.0f, InitialVerticalScrollOffset));
}

float SVerseTileCanvas::GetVerticalScrollOffset() const
{
	return VerticalScrollBox.IsValid() ? VerticalScrollBox->GetScrollOffset() : 0.0f;
}

int32 SVerseTileCanvas::OnPaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	bool bParentEnabled) const
{
	const int32 ContentLayer = SCompoundWidget::OnPaint(
		Args,
		AllottedGeometry,
		MyCullingRect,
		OutDrawElements,
		LayerId,
		InWidgetStyle,
		bParentEnabled);
	if (!bIsPanning)
	{
		return ContentLayer;
	}

	const FVector2D CanvasSize = VerticalScrollBox->GetCachedGeometry().GetLocalSize();
	const FPaintGeometry CanvasPaintGeometry = AllottedGeometry.ToPaintGeometry(
		CanvasSize,
		FSlateLayoutTransform(FVector2D::ZeroVector));
	const FSlateBrush* CursorBrush = FAppStyle::GetBrush(TEXT("SoftwareCursor_Grab"));
	OutDrawElements.PushClip(FSlateClippingZone(CanvasPaintGeometry));
	FSlateDrawElement::MakeBox(
		OutDrawElements,
		ContentLayer + 1,
		AllottedGeometry.ToPaintGeometry(
			CursorBrush->ImageSize,
			FSlateLayoutTransform(SoftwareCursorPosition - CursorBrush->ImageSize / 2.0f)),
		CursorBrush);
	OutDrawElements.PopClip();
	return ContentLayer + 1;
}

FReply SVerseTileCanvas::OnPreviewMouseButtonDown(
	const FGeometry& MyGeometry,
	const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::RightMouseButton
		|| !VerticalScrollBox.IsValid())
	{
		return FReply::Unhandled();
	}

	const FVector2D LocalCursorPosition = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	const FVector2D CanvasSize = VerticalScrollBox->GetCachedGeometry().GetLocalSize();
	if (LocalCursorPosition.X < 0.0f
		|| LocalCursorPosition.Y < 0.0f
		|| LocalCursorPosition.X > CanvasSize.X
		|| LocalCursorPosition.Y > CanvasSize.Y)
	{
		return FReply::Unhandled();
	}

	bIsPanning = true;
	SoftwareCursorPosition = LocalCursorPosition;
	Invalidate(EInvalidateWidgetReason::Paint);
	return FReply::Handled()
		.CaptureMouse(SharedThis(this))
		.UseHighPrecisionMouseMovement(SharedThis(this));
}

FReply SVerseTileCanvas::OnMouseButtonUp(
	const FGeometry& MyGeometry,
	const FPointerEvent& MouseEvent)
{
	if (!bIsPanning || MouseEvent.GetEffectingButton() != EKeys::RightMouseButton)
	{
		return FReply::Unhandled();
	}

	const FVector2D CanvasSize = VerticalScrollBox->GetCachedGeometry().GetLocalSize();
	const FVector2D CanvasScreenSpaceTopLeft = MyGeometry.LocalToAbsolute(FVector2D::ZeroVector);
	const FVector2D CanvasScreenSpaceBottomRight = MyGeometry.LocalToAbsolute(CanvasSize);
	const FVector2D UnclampedScreenSpaceCursorPosition = MyGeometry.LocalToAbsolute(SoftwareCursorPosition);
	const FVector2D ScreenSpaceCursorPosition(
		FMath::Clamp(
			UnclampedScreenSpaceCursorPosition.X,
			CanvasScreenSpaceTopLeft.X,
			CanvasScreenSpaceBottomRight.X),
		FMath::Clamp(
			UnclampedScreenSpaceCursorPosition.Y,
			CanvasScreenSpaceTopLeft.Y,
			CanvasScreenSpaceBottomRight.Y));
	bIsPanning = false;
	Invalidate(EInvalidateWidgetReason::Paint);
	return FReply::Handled()
		.ReleaseMouseCapture()
		.SetMousePos(FIntPoint(
			FMath::RoundToInt(ScreenSpaceCursorPosition.X),
			FMath::RoundToInt(ScreenSpaceCursorPosition.Y)));
}

FReply SVerseTileCanvas::OnMouseMove(
	const FGeometry& MyGeometry,
	const FPointerEvent& MouseEvent)
{
	if (!bIsPanning || !HasMouseCapture())
	{
		return FReply::Unhandled();
	}

	const FVector2D CursorDelta = MouseEvent.GetCursorDelta();
	const float PreviousHorizontalOffset = FMath::Clamp(
		HorizontalScrollBox->GetScrollOffset(),
		0.0f,
		HorizontalScrollBox->GetScrollOffsetOfEnd());
	const float PreviousVerticalOffset = FMath::Clamp(
		VerticalScrollBox->GetScrollOffset(),
		0.0f,
		VerticalScrollBox->GetScrollOffsetOfEnd());
	const float NewHorizontalOffset = FMath::Clamp(
		PreviousHorizontalOffset - CursorDelta.X,
		0.0f,
		HorizontalScrollBox->GetScrollOffsetOfEnd());
	const float NewVerticalOffset = FMath::Clamp(
		PreviousVerticalOffset - CursorDelta.Y,
		0.0f,
		VerticalScrollBox->GetScrollOffsetOfEnd());

	HorizontalScrollBox->SetScrollOffset(NewHorizontalOffset);
	VerticalScrollBox->SetScrollOffset(NewVerticalOffset);
	SoftwareCursorPosition.X -= NewHorizontalOffset - PreviousHorizontalOffset;
	SoftwareCursorPosition.Y -= NewVerticalOffset - PreviousVerticalOffset;
	Invalidate(EInvalidateWidgetReason::Paint);
	return FReply::Handled();
}

FReply SVerseTileCanvas::OnMouseWheel(
	const FGeometry& MyGeometry,
	const FPointerEvent& MouseEvent)
{
	const float NewZoom = FMath::Clamp(
		Zoom + FMath::Sign(MouseEvent.GetWheelDelta()) * ZoomStep,
		MinimumZoom,
		MaximumZoom);
	if (!FMath::IsNearlyEqual(NewZoom, Zoom))
	{
		Zoom = NewZoom;
		ScaleBox->SetUserSpecifiedScale(Zoom);
	}
	return FReply::Handled();
}

FCursorReply SVerseTileCanvas::OnCursorQuery(
	const FGeometry& MyGeometry,
	const FPointerEvent& CursorEvent) const
{
	return FCursorReply::Cursor(bIsPanning ? EMouseCursor::None : EMouseCursor::Default);
}

void SVerseTileCanvas::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	bIsPanning = false;
	Invalidate(EInvalidateWidgetReason::Paint);
	SCompoundWidget::OnMouseCaptureLost(CaptureLostEvent);
}

TSharedRef<SWidget> SVerseTileCanvas::BuildTileRow()
{
	TSharedRef<SHorizontalBox> TileRow = SNew(SHorizontalBox);
	const TArray<FVerseVisualTile> Tiles = FVerseVisualTileBuilder::Build(Snapshot.GetValue());
	for (int32 TileIndex = 0; TileIndex < Tiles.Num();)
	{
		TSharedRef<SWidget> Presentation = BuildTile(Tiles[TileIndex]);
		if (BelongsInCompactStack(Tiles[TileIndex]))
		{
			TSharedRef<SVerticalBox> CompactStack = SNew(SVerticalBox);
			do
			{
				CompactStack->AddSlot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 8.0f)
				[
					BuildTile(Tiles[TileIndex])
				];
				++TileIndex;
			}
			while (TileIndex < Tiles.Num() && BelongsInCompactStack(Tiles[TileIndex]));
			Presentation = CompactStack;
		}
		else
		{
			++TileIndex;
		}

		TileRow->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Top)
		.Padding(8.0f, 5.0f)
		[
			Presentation
		];
	}

	if (Tiles.IsEmpty())
	{
		TileRow->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Top)
		.Padding(12.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("EmptyDocument", "This Verse file is empty."))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		];
	}
	return TileRow;
}

TSharedRef<SWidget> SVerseTileCanvas::BuildTile(const FVerseVisualTile& Tile)
{
	const bool bCompactDefinition = Tile.Kind == EVerseVisualTileKind::Definition
		&& (Tile.DefinitionKind == VerseSyntaxKind::Constant
			|| Tile.DefinitionKind == VerseSyntaxKind::TypeAlias);
	return bCompactDefinition ? BuildCompactTile(Tile) : BuildStructuralTile(Tile);
}

TSharedRef<SWidget> SVerseTileCanvas::BuildStructuralTile(const FVerseVisualTile& Tile)
{
	const bool bDefinition = Tile.Kind == EVerseVisualTileKind::Definition;
	const bool bComment = Tile.Kind == EVerseVisualTileKind::Comment;
	const FText KindText = bDefinition
		? FText::FromName(Tile.DefinitionKind)
		: bComment
			? LOCTEXT("CommentTileKind", "Comment")
			: LOCTEXT("UnknownTileKind", "unknown");
	const FText NameText = bDefinition
		? Decode(Tile.NameRange)
		: bComment
			? FText::GetEmpty()
			: LOCTEXT("UnknownTileName", "raw source");
	const FText TypeText = bDefinition && Tile.TypeRange.IsSet() ? Decode(Tile.TypeRange) : FText::GetEmpty();
	const FLinearColor TileColor = bDefinition
		? FLinearColor(0.12f, 0.25f, 0.45f, 1.0f)
		: bComment
			? FLinearColor(0.10f, 0.30f, 0.16f, 1.0f)
			: FLinearColor(0.35f, 0.20f, 0.08f, 1.0f);
	const FVerseByteRange ContentRange = Tile.Kind == EVerseVisualTileKind::Unknown
		? Tile.Range
		: Tile.BodyRange;

	return SNew(SBox)
		.MaxDesiredWidth(720.0f)
		[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.BorderBackgroundColor(TileColor)
		.Padding(2.0f)
		[
			SNew(SExpandableArea)
			.InitiallyCollapsed(false)
			.AllowAnimatedTransition(false)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.BorderBackgroundColor(TileColor)
			.BodyBorderBackgroundColor(FLinearColor(0.025f, 0.025f, 0.035f, 1.0f))
			.HeaderPadding(FMargin(8.0f, 6.0f))
			.HeaderContent()
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(KindText)
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
					.ColorAndOpacity(FLinearColor(0.65f, 0.80f, 1.0f, 1.0f))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 2.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(NameText)
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(TypeText.IsEmpty()
						? FText::GetEmpty()
						: FText::Format(LOCTEXT("TileDefinitionType", "Type: {0}"), TypeText))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
			]
			.BodyContent()
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.Padding(10.0f)
				[
					SNew(SMultiLineEditableText)
					.Text(Decode(ContentRange))
					.IsReadOnly(true)
					.AutoWrapText(true)
				]
			]
		]
		];
}

TSharedRef<SWidget> SVerseTileCanvas::BuildCompactTile(const FVerseVisualTile& Tile)
{
	const FText KindText = FText::FromName(Tile.DefinitionKind);
	const FText NameText = Decode(Tile.NameRange);
	const FText TypeText = Tile.TypeRange.IsSet() ? Decode(Tile.TypeRange) : FText::GetEmpty();

	return SNew(SExpandableArea)
		.InitiallyCollapsed(false)
		.AllowAnimatedTransition(false)
		.MinWidth(420.0f)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.BorderBackgroundColor(FLinearColor(0.12f, 0.25f, 0.45f, 1.0f))
		.BodyBorderBackgroundColor(FLinearColor(0.025f, 0.025f, 0.035f, 1.0f))
		.HeaderContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2.0f, 0.0f, 10.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(KindText)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
				.ColorAndOpacity(FLinearColor(0.65f, 0.80f, 1.0f, 1.0f))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(NameText)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(8.0f, 0.0f, 2.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(TypeText.IsEmpty()
					? FText::GetEmpty()
					: FText::Format(LOCTEXT("CompactDefinitionType", ": {0}"), TypeText))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]
		]
		.BodyContent()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(8.0f)
			[
				SNew(SMultiLineEditableText)
				.Text(Decode(Tile.Range))
				.IsReadOnly(true)
			]
		];
}

FText SVerseTileCanvas::Decode(FVerseByteRange Range) const
{
	return Range.IsSet()
		? FText::FromString(Snapshot->GetDocument()->DecodeOriginalRange(Range))
		: FText::GetEmpty();
}

#undef LOCTEXT_NAMESPACE
