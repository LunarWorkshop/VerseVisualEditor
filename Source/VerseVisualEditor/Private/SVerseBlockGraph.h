#pragma once

#include "Input/Reply.h"
#include "VerseParseSnapshot.h"
#include "Widgets/SCompoundWidget.h"

class SScaleBox;
class SScrollBox;

/** Zoomable, pannable, read-only presentation of a Verse parse snapshot. */
class SVerseBlockGraph final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SVerseBlockGraph) {}
	SLATE_END_ARGS()

	void Construct(
		const FArguments& InArgs,
		FVerseParseSnapshot InSnapshot,
		float InitialVerticalScrollOffset);

	float GetVerticalScrollOffset() const;

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

private:
	TSharedRef<SWidget> BuildBlockList();
	TSharedRef<SWidget> BuildBlock(const struct FVerseVisualBlock& Block);
	FText Decode(FVerseByteRange Range) const;

	TOptional<FVerseParseSnapshot> Snapshot;
	TSharedPtr<SScrollBox> HorizontalScrollBox;
	TSharedPtr<SScrollBox> VerticalScrollBox;
	TSharedPtr<SScaleBox> ScaleBox;
	float Zoom = 1.0f;
	bool bIsPanning = false;
	FVector2D PreviousPointerPosition = FVector2D::ZeroVector;
};
