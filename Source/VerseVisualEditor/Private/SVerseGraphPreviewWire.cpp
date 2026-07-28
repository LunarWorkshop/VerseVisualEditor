#include "SVerseGraphPreviewWire.h"

#include "GraphEditorSettings.h"
#include "Rendering/DrawElements.h"

void SVerseGraphPreviewWire::Construct(const FArguments& InArgs)
{
	FixedAnchor = InArgs._FixedAnchor;
	FreeEnd = InArgs._FreeEnd;
	bFreeEndIsSource = InArgs._FreeEndIsSource;
	WireColor = InArgs._WireColor;
	WireThickness = InArgs._WireThickness;
	SetVisibility(EVisibility::HitTestInvisible);
	SetCanTick(false);
}

int32 SVerseGraphPreviewWire::OnPaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	bool bParentEnabled) const
{
	const TSharedPtr<SWidget> Anchor = FixedAnchor.Pin();
	if (!Anchor.IsValid() || Anchor->GetPaintSpaceGeometry().GetLocalSize().IsNearlyZero())
	{
		return LayerId;
	}
	const FVector2D Fixed = Anchor->GetPaintSpaceGeometry().GetAbsolutePositionAtCoordinates(FVector2D(0.5f));
	const FVector2D Free = FreeEnd.Get();
	const FVector2D Start = bFreeEndIsSource ? Free : Fixed;
	const FVector2D End = bFreeEndIsSource ? Fixed : Free;
	const FVector2D Tangent = GetDefault<UGraphEditorSettings>()->ComputeSplineTangent(Start, End);
	FSlateDrawElement::MakeDrawSpaceSpline(
		OutDrawElements, LayerId, Start, Tangent, End, Tangent,
		WireThickness, ESlateDrawEffect::None, WireColor);
	return LayerId;
}
