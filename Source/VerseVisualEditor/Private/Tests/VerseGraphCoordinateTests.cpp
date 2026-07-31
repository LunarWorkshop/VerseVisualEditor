#if WITH_DEV_AUTOMATION_TESTS

#include "SVerseGraphSurface.h"
#include "VerseGraphCoordinates.h"

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseGraphWindowOriginTest,
	"VerseVisualEditor.Graph.Coordinates.WindowOrigin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseGraphWindowOriginTest::RunTest(const FString& Parameters)
{
	const FVector2D LocalPoint(37.0f, 83.0f);
	const FGeometry FirstDesktopGeometry = FGeometry::MakeRoot(
		FVector2D(800.0f, 600.0f), FSlateLayoutTransform(1.0f, FVector2D(100.0f, 250.0f)));
	const FGeometry SecondDesktopGeometry = FGeometry::MakeRoot(
		FVector2D(800.0f, 600.0f), FSlateLayoutTransform(1.0f, FVector2D(475.0f, 25.0f)));
	const FGeometry PaintGeometry = FGeometry::MakeRoot(
		FVector2D(800.0f, 600.0f), FSlateLayoutTransform(1.0f, FVector2D::ZeroVector));

	const FVerseCanvasPoint FirstLocal = VerseDesktopToCanvas(
		FirstDesktopGeometry, FVerseDesktopPoint(FirstDesktopGeometry.LocalToAbsolute(LocalPoint)));
	const FVerseCanvasPoint SecondLocal = VerseDesktopToCanvas(
		SecondDesktopGeometry, FVerseDesktopPoint(SecondDesktopGeometry.LocalToAbsolute(LocalPoint)));
	const FVersePaintPoint FirstPaint = VerseCanvasToPaint(PaintGeometry, FirstLocal);
	const FVersePaintPoint SecondPaint = VerseCanvasToPaint(PaintGeometry, SecondLocal);

	TestEqual(TEXT("First desktop origin is removed once"), FirstPaint.Value, LocalPoint);
	TestEqual(TEXT("Moving the window does not move the paint endpoint"), SecondPaint.Value, LocalPoint);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseGraphPanZoomTest,
	"VerseVisualEditor.Graph.Coordinates.PanZoom",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseGraphPanZoomTest::RunTest(const FString& Parameters)
{
	const FVerseGraphPoint GraphPoint(FVector2D(120.0f, 80.0f));
	for (const FVector2D Pan : {FVector2D::ZeroVector, FVector2D(200.0f, 150.0f), FVector2D(900.0f, 700.0f)})
	{
		for (const float Zoom : {0.5f, 1.0f, 2.0f})
		{
			const FVerseCanvasPoint Origin(-Pan);
			const FVerseCanvasPoint Canvas = VerseGraphToCanvas(GraphPoint, Origin, Zoom);
			const FVerseGraphPoint RoundTrip = VerseCanvasToGraph(Canvas, Origin, Zoom);
			TestEqual(TEXT("Pan and zoom round-trip"), RoundTrip.Value, GraphPoint.Value);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseGraphScaleRoundTripTest,
	"VerseVisualEditor.Graph.Coordinates.ScaleRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseGraphScaleRoundTripTest::RunTest(const FString& Parameters)
{
	const FVector2D LocalPoint(72.0f, 54.0f);
	for (const float Scale : {0.5f, 1.0f, 2.0f})
	{
		const FGeometry DesktopGeometry = FGeometry::MakeRoot(
			FVector2D(800.0f, 600.0f), FSlateLayoutTransform(Scale, FVector2D(211.0f, 97.0f)));
		const FGeometry PaintGeometry = FGeometry::MakeRoot(
			FVector2D(800.0f, 600.0f), FSlateLayoutTransform(Scale, FVector2D(13.0f, 17.0f)));
		const FVerseCanvasPoint Local = VerseDesktopToCanvas(
			DesktopGeometry, FVerseDesktopPoint(DesktopGeometry.LocalToAbsolute(LocalPoint)));
		const FVersePaintPoint Paint = VerseCanvasToPaint(PaintGeometry, Local);
		TestEqual(
			FString::Printf(TEXT("Scale %.1f preserves local position"), Scale),
			Paint.Value,
			FVector2D(PaintGeometry.LocalToAbsolute(LocalPoint)));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseGraphCursorAnchoredZoomTest,
	"VerseVisualEditor.Graph.Coordinates.CursorAnchoredZoom",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseGraphCursorAnchoredZoomTest::RunTest(const FString& Parameters)
{
	const FVerseCanvasPoint Cursor(FVector2D(420.0f, 275.0f));
	const FVector2D Padding(760.0f, 560.0f);
	const FVector2D InitialScroll(915.0f, 680.0f);
	const float InitialZoom = 1.0f;
	const FVerseGraphPoint AnchoredGraphPoint = VerseCanvasToGraph(
		Cursor,
		FVerseCanvasPoint(Padding - InitialScroll),
		InitialZoom);
	for (const float NewZoom : {0.5f, 2.0f})
	{
		const FVector2D NewScroll = VerseScrollOffsetForZoomAnchor(
			Cursor, InitialScroll, Padding, InitialZoom, NewZoom);
		const FVerseCanvasPoint Reprojected = VerseGraphToCanvas(
			AnchoredGraphPoint,
			FVerseCanvasPoint(Padding - NewScroll),
			NewZoom);
		TestEqual(TEXT("Zoom keeps the anchored graph point beneath the cursor"),
			Reprojected.Value, Cursor.Value);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseGraphFailureMarkerCoordinatesTest,
	"VerseVisualEditor.Graph.Coordinates.FailureMarkers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseGraphFailureMarkerCoordinatesTest::RunTest(const FString& Parameters)
{
	const FVector2D Start(80.0f, 140.0f);
	const FVector2D End(320.0f, 260.0f);
	const FVector2D Tangent(180.0f, 0.0f);
	const TArray<FVector2D> Base = BuildVerseSplineMarkerCenters(
		Start, Tangent, End, Tangent);
	TestTrue(TEXT("A visible failable wire receives repeated markers"), Base.Num() > 1);

	const FVector2D WindowTranslation(413.0f, 227.0f);
	const TArray<FVector2D> Moved = BuildVerseSplineMarkerCenters(
		Start + WindowTranslation,
		Tangent,
		End + WindowTranslation,
		Tangent);
	TestEqual(TEXT("Moving the window does not change marker count"), Moved.Num(), Base.Num());
	for (int32 Index = 0; Index < Base.Num() && Index < Moved.Num(); ++Index)
	{
		TestTrue(
			TEXT("Every marker follows the same paint-space translation as its wire"),
			(Moved[Index] - WindowTranslation).Equals(Base[Index], 0.01));
	}

	for (const float Zoom : {0.5f, 1.0f, 2.0f})
	{
		const TArray<FVector2D> Zoomed = BuildVerseSplineMarkerCenters(
			Start * Zoom,
			Tangent * Zoom,
			End * Zoom,
			Tangent * Zoom);
		TestTrue(
			FString::Printf(TEXT("Zoom %.1f keeps markers on the wire"), Zoom),
			!Zoomed.IsEmpty());
	}
	return true;
}

#endif
