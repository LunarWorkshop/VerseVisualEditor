#include "SVerseBlockGraph.h"

#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "VerseParseSnapshotBuilder.h"
#include "VerseVisualBlock.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SScrollBar.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Text/SMultiLineEditableText.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SVerseBlockGraph"

namespace
{
	constexpr float MinimumZoom = 0.5f;
	constexpr float MaximumZoom = 2.0f;
	constexpr float ZoomStep = 0.1f;

	bool BelongsInCompactStack(const FVerseVisualBlock& Block)
	{
		return Block.Kind == EVerseVisualBlockKind::Comment
			|| (Block.Kind == EVerseVisualBlockKind::Definition
				&& (Block.DefinitionKind == VerseSyntaxKind::Constant
					|| Block.DefinitionKind == VerseSyntaxKind::TypeAlias));
	}
}

void SVerseBlockGraph::Construct(
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
							BuildBlockList()
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

float SVerseBlockGraph::GetVerticalScrollOffset() const
{
	return VerticalScrollBox.IsValid() ? VerticalScrollBox->GetScrollOffset() : 0.0f;
}

FReply SVerseBlockGraph::OnMouseButtonDown(
	const FGeometry& MyGeometry,
	const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::MiddleMouseButton)
	{
		return FReply::Unhandled();
	}

	bIsPanning = true;
	PreviousPointerPosition = MouseEvent.GetScreenSpacePosition();
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SVerseBlockGraph::OnMouseButtonUp(
	const FGeometry& MyGeometry,
	const FPointerEvent& MouseEvent)
{
	if (!bIsPanning || MouseEvent.GetEffectingButton() != EKeys::MiddleMouseButton)
	{
		return FReply::Unhandled();
	}

	bIsPanning = false;
	return FReply::Handled().ReleaseMouseCapture();
}

FReply SVerseBlockGraph::OnMouseMove(
	const FGeometry& MyGeometry,
	const FPointerEvent& MouseEvent)
{
	if (!bIsPanning || !HasMouseCapture())
	{
		return FReply::Unhandled();
	}

	const FVector2D PointerPosition = MouseEvent.GetScreenSpacePosition();
	const FVector2D Delta = (PointerPosition - PreviousPointerPosition) / Zoom;
	PreviousPointerPosition = PointerPosition;
	HorizontalScrollBox->SetScrollOffset(FMath::Max(0.0f, HorizontalScrollBox->GetScrollOffset() - Delta.X));
	VerticalScrollBox->SetScrollOffset(FMath::Max(0.0f, VerticalScrollBox->GetScrollOffset() - Delta.Y));
	return FReply::Handled();
}

FReply SVerseBlockGraph::OnMouseWheel(
	const FGeometry& MyGeometry,
	const FPointerEvent& MouseEvent)
{
	if (!MouseEvent.IsControlDown())
	{
		constexpr float ScrollStep = 32.0f;
		SScrollBox& ScrollBox = MouseEvent.IsShiftDown()
			? *HorizontalScrollBox
			: *VerticalScrollBox;
		ScrollBox.SetScrollOffset(FMath::Max(
			0.0f,
			ScrollBox.GetScrollOffset() - MouseEvent.GetWheelDelta() * ScrollStep));
		return FReply::Handled();
	}

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

TSharedRef<SWidget> SVerseBlockGraph::BuildBlockList()
{
	TSharedRef<SHorizontalBox> BlockList = SNew(SHorizontalBox);
	const TArray<FVerseVisualBlock> Blocks = FVerseVisualBlockBuilder::Build(Snapshot.GetValue());
	for (int32 BlockIndex = 0; BlockIndex < Blocks.Num();)
	{
		TSharedRef<SWidget> Presentation = BuildBlock(Blocks[BlockIndex]);
		if (BelongsInCompactStack(Blocks[BlockIndex]))
		{
			TSharedRef<SVerticalBox> CompactStack = SNew(SVerticalBox);
			do
			{
				CompactStack->AddSlot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 8.0f)
				[
					BuildBlock(Blocks[BlockIndex])
				];
				++BlockIndex;
			}
			while (BlockIndex < Blocks.Num() && BelongsInCompactStack(Blocks[BlockIndex]));
			Presentation = CompactStack;
		}
		else
		{
			++BlockIndex;
		}

		BlockList->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Top)
		.Padding(8.0f, 5.0f)
		[
			Presentation
		];
	}

	if (Blocks.IsEmpty())
	{
		BlockList->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Top)
		.Padding(12.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("EmptyDocument", "This Verse file is empty."))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		];
	}
	return BlockList;
}

TSharedRef<SWidget> SVerseBlockGraph::BuildBlock(const FVerseVisualBlock& Block)
{
	const bool bCompactDefinition = Block.Kind == EVerseVisualBlockKind::Definition
		&& (Block.DefinitionKind == VerseSyntaxKind::Constant
			|| Block.DefinitionKind == VerseSyntaxKind::TypeAlias);
	return bCompactDefinition ? BuildCompactBlock(Block) : BuildTileBlock(Block);
}

TSharedRef<SWidget> SVerseBlockGraph::BuildTileBlock(const FVerseVisualBlock& Block)
{
	const bool bDefinition = Block.Kind == EVerseVisualBlockKind::Definition;
	const bool bComment = Block.Kind == EVerseVisualBlockKind::Comment;
	const FText KindText = bDefinition
		? FText::FromName(Block.DefinitionKind)
		: bComment
			? LOCTEXT("CommentBlockKind", "Comment")
			: LOCTEXT("UnknownBlockKind", "unknown");
	const FText NameText = bDefinition
		? Decode(Block.NameRange)
		: bComment
			? FText::GetEmpty()
			: LOCTEXT("UnknownBlockName", "raw source");
	const FText TypeText = bDefinition && Block.TypeRange.IsSet() ? Decode(Block.TypeRange) : FText::GetEmpty();
	const FLinearColor BlockColor = bDefinition
		? FLinearColor(0.12f, 0.25f, 0.45f, 1.0f)
		: bComment
			? FLinearColor(0.10f, 0.30f, 0.16f, 1.0f)
			: FLinearColor(0.35f, 0.20f, 0.08f, 1.0f);
	const FVerseByteRange ContentRange = Block.Kind == EVerseVisualBlockKind::Unknown
		? Block.Range
		: Block.BodyRange;

	return SNew(SBox)
		.MaxDesiredWidth(720.0f)
		[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.BorderBackgroundColor(BlockColor)
		.Padding(2.0f)
		[
			SNew(SExpandableArea)
			.InitiallyCollapsed(false)
			.AllowAnimatedTransition(false)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.BorderBackgroundColor(BlockColor)
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

TSharedRef<SWidget> SVerseBlockGraph::BuildCompactBlock(const FVerseVisualBlock& Block)
{
	const FText KindText = FText::FromName(Block.DefinitionKind);
	const FText NameText = Decode(Block.NameRange);
	const FText TypeText = Block.TypeRange.IsSet() ? Decode(Block.TypeRange) : FText::GetEmpty();

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
				.Text(Decode(Block.Range))
				.IsReadOnly(true)
			]
		];
}

FText SVerseBlockGraph::Decode(FVerseByteRange Range) const
{
	return Range.IsSet()
		? FText::FromString(Snapshot->GetDocument()->DecodeOriginalRange(Range))
		: FText::GetEmpty();
}

#undef LOCTEXT_NAMESPACE
