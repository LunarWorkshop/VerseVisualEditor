#include "SVerseGraphWire.h"

#include "GraphEditorSettings.h"
#include "Rendering/DrawElements.h"

void SVerseGraphWire::Construct(const FArguments& InArgs)
{
	SourceAnchor = InArgs._SourceAnchor;
	TargetAnchor = InArgs._TargetAnchor;
	WireColor = InArgs._WireColor;
	WireThickness = InArgs._WireThickness;
	// Overlay wires paint above graph content, but must never replace the
	// sockets and tiles beneath them as the hit-test target.
	SetVisibility(EVisibility::HitTestInvisible);
	SetCanTick(false);
}

FVector2D SVerseGraphWire::ComputeDesiredSize(float) const
{
	return FVector2D::ZeroVector;
}

int32 SVerseGraphWire::OnPaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	bool bParentEnabled) const
{
	const TSharedPtr<SWidget> Source = SourceAnchor.Pin();
	const TSharedPtr<SWidget> Target = TargetAnchor.Pin();
	if (!Source.IsValid() || !Target.IsValid())
	{
		return LayerId;
	}

	// The wire is painted after its anchors, so these geometries belong to the
	// current paint pass and already include the graph's pan and zoom transforms.
	const FGeometry& SourceGeometry = Source->GetPaintSpaceGeometry();
	const FGeometry& TargetGeometry = Target->GetPaintSpaceGeometry();
	if (SourceGeometry.GetLocalSize().IsNearlyZero()
		|| TargetGeometry.GetLocalSize().IsNearlyZero())
	{
		return LayerId;
	}

	const FVector2D Start = SourceGeometry.GetAbsolutePositionAtCoordinates(FVector2D(0.5f, 0.5f));
	const FVector2D End = TargetGeometry.GetAbsolutePositionAtCoordinates(FVector2D(0.5f, 0.5f));
	const FVector2D SplineTangent =
		GetDefault<UGraphEditorSettings>()->ComputeSplineTangent(Start, End);
	FSlateDrawElement::MakeDrawSpaceSpline(
		OutDrawElements,
		LayerId,
		Start,
		SplineTangent,
		End,
		SplineTangent,
		WireThickness,
		ESlateDrawEffect::None,
		WireColor);
	return LayerId;
}
