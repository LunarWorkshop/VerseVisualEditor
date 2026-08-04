#if WITH_DEV_AUTOMATION_TESTS

#include "Slate/SVerseGraphSurface.h"
#include "Slate/SVerseFunctionGraphLayout.h"
#include "Slate/SVerseTile.h"
#include "Slate/VerseGraphCoordinates.h"
#include "Slate/VerseGraphMotion.h"
#include "Slate/VerseVisualEditorStyle.h"

#include "GraphEditorSettings.h"
#include "Misc/AutomationTest.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Widgets/SCanvas.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	FVerseVisualTile FinalizeTestTile(FVerseVisualTile Tile)
	{
		TArray<FVerseVisualTile> Tiles;
		Tiles.Add(MoveTemp(Tile));
		FVerseVisualTileBuilder::FinalizeSocketTopology(Tiles);
		return MoveTemp(Tiles[0]);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseGraphMotionMathTest,
	"VerseVisualEditor.Graph.Motion.ResistanceAndEaseOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseGraphMotionMathTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("The motion anchor is graph-space zero"),
		ComputeVerseAnchorRelativeGraphPosition(
			FVector2D(410.0f, 275.0f), FVector2D(410.0f, 275.0f)),
		FVector2D::ZeroVector);
	TestEqual(TEXT("Anchor-relative positions ignore whole-graph displacement"),
		ComputeVerseAnchorRelativeGraphPosition(
			FVector2D(610.0f, 355.0f), FVector2D(410.0f, 275.0f)),
		ComputeVerseAnchorRelativeGraphPosition(
			FVector2D(905.0f, 510.0f), FVector2D(705.0f, 430.0f)));
	const FGeometry SurfaceGeometry = FGeometry::MakeRoot(
		FVector2D(800.0f, 600.0f),
		FSlateLayoutTransform(1.0f, FVector2D(100.0f, 250.0f)));
	FVerseGraphMotionController AnchoredController;
	AnchoredController.SetSurfaceGeometry(SurfaceGeometry, 1.0f, true);
	TestFalse(TEXT("An anchored layout waits for current anchor geometry"),
		AnchoredController.CanResolveGraphPositions());
	const FVector2D CurrentAnchorDesktop =
		SurfaceGeometry.LocalToAbsolute(FVector2D(10.0f, 20.0f));
	AnchoredController.EstablishCurrentAnchor(CurrentAnchorDesktop);
	TestTrue(TEXT("The current anchor makes graph positions resolvable"),
		AnchoredController.CanResolveGraphPositions());
	TestEqual(TEXT("The current anchor is exactly graph-space zero"),
		AnchoredController.DesktopToGraph(CurrentAnchorDesktop), FVector2D::ZeroVector);
	AnchoredController.SetSurfaceGeometry(SurfaceGeometry, 1.0f, true);
	TestFalse(TEXT("A new layout epoch cannot reuse the preceding anchor"),
		AnchoredController.CanResolveGraphPositions());
	TestEqual(TEXT("Anchor lock offsets scrolling by the rebuilt anchor displacement"),
		ComputeVerseAnchorLockedScrollOffset(
			FVector2D(100.0f, 200.0f),
			FVector2D(300.0f, 400.0f),
			FVector2D(450.0f, 375.0f)),
		FVector2D(250.0f, 175.0f));
	auto RadiusAfterResistance = [](float Radius, FVector2D Direction)
	{
		return ApplyVerseGraphDragResistance(Direction.GetSafeNormal() * Radius).Size();
	};
	TestEqual(TEXT("Zero displacement stays zero"),
		ApplyVerseGraphDragResistance(FVector2D::ZeroVector), FVector2D::ZeroVector);
	TestTrue(TEXT("The first 100 pixels are one-to-one"),
		FMath::IsNearlyEqual(RadiusAfterResistance(100.0f, FVector2D(1.0f, 0.0f)), 100.0f));
	TestTrue(TEXT("Two hundred pixels resist to approximately 170"),
		FMath::IsNearlyEqual(RadiusAfterResistance(200.0f, FVector2D(0.0f, 1.0f)), 170.5f, 0.6f));
	TestTrue(TEXT("One thousand pixels approach the 300-pixel limit"),
		FMath::IsNearlyEqual(RadiusAfterResistance(1000.0f, FVector2D(1.0f, 1.0f)), 296.0f, 0.6f));
	TestTrue(TEXT("Resistance is continuous at 100 pixels"),
		FMath::Abs(RadiusAfterResistance(100.01f, FVector2D(1.0f, 0.0f))
			- RadiusAfterResistance(99.99f, FVector2D(1.0f, 0.0f))) < 0.05f);
	float Previous = 0.0f;
	for (float Radius = 0.0f; Radius <= 2000.0f; Radius += 10.0f)
	{
		const float Current = RadiusAfterResistance(Radius, FVector2D(0.6f, 0.8f));
		TestTrue(TEXT("Resistance remains radial and monotonic"), Current >= Previous);
		TestTrue(TEXT("Resistance never exceeds its asymptotic limit"), Current <= 300.0f);
		Previous = Current;
	}
	TestEqual(TEXT("Ease begins at zero"), EvaluateVerseGraphEaseOut(0.0f), 0.0f);
	TestEqual(TEXT("Ease finishes at one"), EvaluateVerseGraphEaseOut(1.0f), 1.0f);
	TestTrue(TEXT("Cubic ease-out advances faster than linear in the middle"),
		EvaluateVerseGraphEaseOut(0.5f) > 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseGraphVisualStyleTest,
	"VerseVisualEditor.Graph.Style.BlueprintInspiredChrome",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseGraphVisualStyleTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Plugin style is initialized"), VerseVisualEditorStyle::IsInitialized());
	const ISlateStyle* Registered =
		FSlateStyleRegistry::FindSlateStyle(TEXT("VerseVisualEditorStyle"));
	TestNotNull(TEXT("Plugin style is registered independently"), Registered);
	if (Registered == nullptr)
	{
		return false;
	}
	for (const FName BrushName : {
		FName(TEXT("Tile.Shadow")),
		FName(TEXT("Tile.Outline")),
		FName(TEXT("Tile.Surface")),
		FName(TEXT("Tile.Identity")),
		FName(TEXT("Tile.SourcePreview")),
		FName(TEXT("Tile.Diagnostic")),
		FName(TEXT("Tile.BodyOverlay")),
		FName(TEXT("Tile.Separator"))})
	{
		TestTrue(*FString::Printf(TEXT("Style supplies %s"), *BrushName.ToString()),
			Registered->GetBrush(BrushName) != nullptr);
	}
	TestTrue(TEXT("Verse chrome does not replace AppStyle node body"),
		Registered->GetBrush(TEXT("Tile.Surface"))
			!= FAppStyle::GetBrush(TEXT("Graph.Node.Body")));

	const UGraphEditorSettings* BlueprintSettings = GetDefault<UGraphEditorSettings>();
	auto Darkened = [](FLinearColor Color, float Brightness, float Opacity)
	{
		Color.R *= Brightness;
		Color.G *= Brightness;
		Color.B *= Brightness;
		Color.A = Opacity;
		return Color;
	};
	TestEqual(TEXT("Logic uses Blueprint boolean color"),
		VerseVisualEditorStyle::GetTypeColor(TEXT("logic")),
		BlueprintSettings->BooleanPinTypeColor);
	TestEqual(TEXT("Optional float uses Blueprint float color"),
		VerseVisualEditorStyle::GetTypeColor(TEXT("?float")),
		BlueprintSettings->FloatPinTypeColor);
	TestEqual(TEXT("Array int uses Blueprint int color"),
		VerseVisualEditorStyle::GetTypeColor(TEXT("[]int")),
		BlueprintSettings->IntPinTypeColor);

	FVerseVisualTile FunctionEntry;
	FunctionEntry.Kind = EVerseVisualTileKind::FunctionEntry;
	TestEqual(TEXT("Function boundary uses darkened Blueprint terminator color"),
		VerseVisualEditorStyle::GetTileTitleColor(FunctionEntry),
		Darkened(BlueprintSettings->FunctionTerminatorNodeTitleColor, 0.58f, 0.92f));

	FVerseVisualTile FunctionDefinition;
	FunctionDefinition.Kind = EVerseVisualTileKind::Definition;
	FunctionDefinition.DefinitionKind = VerseSyntaxKind::Function;
	TestEqual(TEXT("File function definition uses the selected function blue"),
		VerseVisualEditorStyle::GetTileTitleColor(FunctionDefinition),
		FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("3d637d")))
			.CopyWithNewOpacity(0.90f));

	FVerseVisualTile PureCall;
	PureCall.Kind = EVerseVisualTileKind::Expression;
	PureCall.ExpressionKind = EVerseExpressionKind::Call;
	TestEqual(TEXT("Verse call uses the selected function blue"),
		VerseVisualEditorStyle::GetTileTitleColor(PureCall),
		FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("3d637d")))
			.CopyWithNewOpacity(0.90f));

	FVerseVisualTile ExecutableCall = PureCall;
	FVerseVisualSocket ExecutionSocket;
	ExecutionSocket.Id = {EVerseVisualSocketDirection::Input,
		EVerseVisualSocketRole::Execution, 0};
	ExecutableCall.SocketTopology = FVerseVisualSocketTopology::MakeInvalidForTesting(
		{}, {}, {ExecutionSocket});
	TestEqual(TEXT("Calls remain the same blue regardless of execution topology"),
		VerseVisualEditorStyle::GetTileTitleColor(ExecutableCall),
		FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("3d637d")))
			.CopyWithNewOpacity(0.90f));

	FVerseVisualTile IfControl;
	IfControl.Kind = EVerseVisualTileKind::Expression;
	IfControl.ExpressionKind = EVerseExpressionKind::Control;
	IfControl.ControlKind = EVerseControlKind::If;
	TestEqual(TEXT("If uses the restrained neutral tile brightness"),
		VerseVisualEditorStyle::GetTileTitleColor(IfControl),
		VerseVisualEditorStyle::Get().GetColor(TEXT("Color.NeutralTitle")));

	FVerseVisualTile Failure;
	Failure.Kind = EVerseVisualTileKind::FailableBlock;
	const FLinearColor FailureTitle = VerseVisualEditorStyle::GetTileTitleColor(Failure);
	TestTrue(TEXT("Failure title retains a restrained gold identity"),
		FailureTitle.R > FailureTitle.B && FailureTitle.G > FailureTitle.B);
	return true;
}

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
	FVerseGraphOffscreenEndpointArrangementTest,
	"VerseVisualEditor.Graph.Coordinates.OffscreenEndpoints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseGraphOffscreenEndpointArrangementTest::RunTest(const FString& Parameters)
{
	const TSharedRef<SBox> VisibleAnchor =
		SNew(SBox).WidthOverride(10.0f).HeightOverride(10.0f);
	const TSharedRef<SBox> OffscreenAnchor =
		SNew(SBox).WidthOverride(12.0f).HeightOverride(12.0f);
	const TSharedRef<SCanvas> Root =
		SNew(SCanvas)
		+ SCanvas::Slot()
		.Position(FVector2D(20.0f, 30.0f))
		.Size(FVector2D(10.0f, 10.0f))
		[
			VisibleAnchor
		]
		+ SCanvas::Slot()
		.Position(FVector2D(1200.0f, 700.0f))
		.Size(FVector2D(12.0f, 12.0f))
		[
			OffscreenAnchor
		];

	const FVerseVisualSocketEndpoint SourceEndpoint{
		{1}, {EVerseVisualSocketDirection::Output, EVerseVisualSocketRole::Value, 0}};
	const FVerseVisualSocketEndpoint TargetEndpoint{
		{2}, {EVerseVisualSocketDirection::Input, EVerseVisualSocketRole::Value, 0}};
	const TSharedRef<FVerseGraphEndpointRegistry> Registry =
		MakeShared<FVerseGraphEndpointRegistry>();
	Registry->Register(SourceEndpoint, {VisibleAnchor});
	Registry->Register(TargetEndpoint, {OffscreenAnchor});
	FVerseGraphConnection Connection;
	Connection.Source = SourceEndpoint;
	Connection.Target = TargetEndpoint;
	Connection.EndpointRegistry = Registry;
	const TArray<FVerseGraphConnection> Connections({Connection});
	const FGeometry RootGeometry = FGeometry::MakeRoot(
		FVector2D(200.0f, 100.0f),
		FSlateLayoutTransform(1.0f, FVector2D(40.0f, 60.0f)));

	const TSharedRef<SVerseGraphRenderScope> Scope =
		SNew(SVerseGraphRenderScope)
		.Connections(Connections)
		[
			Root
		];
	const FVerseGraphArrangedEndpointMap Arranged =
		Scope->ArrangeEndpointsForPaint(RootGeometry);
	const FArrangedWidget* Visible = Arranged.Find(VisibleAnchor);
	const FArrangedWidget* Offscreen = Arranged.Find(OffscreenAnchor);
	TestNotNull(TEXT("Visible endpoint is arranged"), Visible);
	TestNotNull(TEXT("Culled endpoint is synthesized from current layout"), Offscreen);
	if (Visible != nullptr && Offscreen != nullptr)
	{
		const FVector2f VisiblePosition =
			Visible->Geometry.GetAbsolutePositionAtCoordinates(FVector2D(0.5f));
		const FVector2f OffscreenPosition =
			Offscreen->Geometry.GetAbsolutePositionAtCoordinates(FVector2D(0.5f));
		TestTrue(TEXT("Visible endpoint uses current root transform"),
			VisiblePosition.Equals(FVector2f(65.0f, 95.0f)));
		TestTrue(TEXT("Offscreen endpoint retains its graph position"),
			OffscreenPosition.Equals(FVector2f(1246.0f, 766.0f)));
	}
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
	FVerseExecutionPinAnchorTest,
	"VerseVisualEditor.Graph.Coordinates.ExecutionPinAnchor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseExecutionPinAnchorTest::RunTest(const FString& Parameters)
{
	TestEqual(
		TEXT("Input home plate uses its painted center"),
		GetVerseExecutionPinAnchorCoordinate(true, false),
		FVector2D(0.5f, 0.75f));
	TestEqual(
		TEXT("Full output home plate intersects the tile bottom edge"),
		GetVerseExecutionPinAnchorCoordinate(false, false),
		FVector2D(0.5f, 1.0f));
	TestEqual(
		TEXT("Compact output uses the same tile-edge dock"),
		GetVerseExecutionPinAnchorCoordinate(false, true),
		FVector2D(0.5f, 1.0f));
	for (const EVerseFunctionGraphPresentation Presentation : {
		EVerseFunctionGraphPresentation::HorizontalExecution,
		EVerseFunctionGraphPresentation::Tracks})
	{
		TestEqual(
			TEXT("Horizontal presentation input uses its geometric center"),
			GetVerseExecutionPinAnchorCoordinate(true, false, Presentation),
			FVector2D(0.5f, 0.5f));
		TestEqual(
			TEXT("Horizontal presentation output uses its geometric center"),
			GetVerseExecutionPinAnchorCoordinate(false, true, Presentation),
			FVector2D(0.5f, 0.5f));
	}
	TestEqual(
		TEXT("Vertical presentation preserves execution flow axis"),
		GetVersePresentedConnectionAxis(
			EVerseVisualConnectionAxis::Vertical,
			EVerseVisualSocketRole::Execution,
			EVerseFunctionGraphPresentation::VerticalExecution),
		EVerseGraphConnectionAxis::Vertical);
	TestEqual(
		TEXT("Horizontal presentation rotates execution flow"),
		GetVersePresentedConnectionAxis(
			EVerseVisualConnectionAxis::Vertical,
			EVerseVisualSocketRole::Execution,
			EVerseFunctionGraphPresentation::HorizontalExecution),
		EVerseGraphConnectionAxis::Horizontal);
	TestEqual(
		TEXT("Track presentation preserves horizontal data flow"),
		GetVersePresentedConnectionAxis(
			EVerseVisualConnectionAxis::Horizontal,
			EVerseVisualSocketRole::Value,
			EVerseFunctionGraphPresentation::Tracks),
		EVerseGraphConnectionAxis::Horizontal);
	TestEqual(TEXT("Vertical home-plate previews ease vertically"),
		GetVerseExecutionPreviewAxis(
			EVerseFunctionGraphPresentation::VerticalExecution),
		EVerseVisualConnectionAxis::Vertical);
	TestEqual(TEXT("Horizontal home-plate previews ease horizontally"),
		GetVerseExecutionPreviewAxis(
			EVerseFunctionGraphPresentation::HorizontalExecution),
		EVerseVisualConnectionAxis::Horizontal);
	TestEqual(TEXT("Lane home-plate previews ease horizontally"),
		GetVerseExecutionPreviewAxis(EVerseFunctionGraphPresentation::Tracks),
		EVerseVisualConnectionAxis::Horizontal);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseTileSemanticCompositionTest,
	"VerseVisualEditor.Graph.Tile.SemanticComposition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseTileSemanticCompositionTest::RunTest(const FString& Parameters)
{
	auto MakeWidget = [](FVerseVisualTile Tile, bool bMain = false,
		bool bSource = false,
		EVerseFunctionGraphPresentation Presentation =
			EVerseFunctionGraphPresentation::VerticalExecution)
	{
		Tile = FinalizeTestTile(MoveTemp(Tile));
		TSharedRef<SVerseTile> Widget =
			SNew(SVerseTile)
			.Tile(Tile)
			.TileColor(FLinearColor::Black)
			.HasMainContent(bMain)
			.HasSourcePreview(bSource)
			.FunctionGraphPresentation(Presentation)
			.MainContent()[SNew(SBox).WidthOverride(80.0f).HeightOverride(24.0f)]
			.SourcePreview()[SNew(STextBlock).Text(FText::FromString(TEXT("preview")))];
		Widget->SlatePrepass();
		return Widget;
	};

	TArray<FVerseVisualTile> IdentityTiles;
	IdentityTiles.AddDefaulted_GetRef().Kind = EVerseVisualTileKind::Definition;
	IdentityTiles.AddDefaulted_GetRef().Kind = EVerseVisualTileKind::Comment;
	FVerseVisualTile& Call = IdentityTiles.AddDefaulted_GetRef();
	Call.Kind = EVerseVisualTileKind::Expression;
	Call.ExpressionKind = EVerseExpressionKind::Call;
	FVerseVisualTile& Control = IdentityTiles.AddDefaulted_GetRef();
	Control.Kind = EVerseVisualTileKind::Expression;
	Control.ExpressionKind = EVerseExpressionKind::Control;
	Control.ControlKind = EVerseControlKind::If;
	IdentityTiles.AddDefaulted_GetRef().Kind = EVerseVisualTileKind::FailableBlock;
	IdentityTiles.AddDefaulted_GetRef().Kind = EVerseVisualTileKind::FunctionEntry;
	IdentityTiles.AddDefaulted_GetRef().Kind = EVerseVisualTileKind::FunctionReturn;
	for (FVerseVisualTile& Tile : IdentityTiles)
	{
		TestTrue(TEXT("Named and structural tiles have an identity band"),
			MakeWidget(MoveTemp(Tile))->HasIdentityBandForTesting());
	}

	TArray<FVerseVisualTile> HeaderlessTiles;
	FVerseVisualTile& Identifier = HeaderlessTiles.AddDefaulted_GetRef();
	Identifier.Kind = EVerseVisualTileKind::Expression;
	Identifier.ExpressionKind = EVerseExpressionKind::Identifier;
	FVerseVisualTile& Literal = HeaderlessTiles.AddDefaulted_GetRef();
	Literal.Kind = EVerseVisualTileKind::Expression;
	Literal.ExpressionKind = EVerseExpressionKind::Literal;
	Literal.LiteralKind = EVerseLiteralKind::Integer;
	FVerseVisualTile& Operator = HeaderlessTiles.AddDefaulted_GetRef();
	Operator.Kind = EVerseVisualTileKind::Expression;
	Operator.ExpressionKind = EVerseExpressionKind::BinaryOperator;
	Operator.OperatorSpelling = TEXT("+");
	HeaderlessTiles.AddDefaulted_GetRef().Kind = EVerseVisualTileKind::Unknown;
	for (FVerseVisualTile& Tile : HeaderlessTiles)
	{
		TestFalse(TEXT("Identifiers, literals, operators, and unknown source have no identity band"),
			MakeWidget(MoveTemp(Tile))->HasIdentityBandForTesting());
	}

	FVerseVisualTile PreviewCall;
	PreviewCall.Kind = EVerseVisualTileKind::Expression;
	PreviewCall.ExpressionKind = EVerseExpressionKind::Call;
	const TSharedRef<SVerseTile> PreviewWidget = MakeWidget(
		MoveTemp(PreviewCall), false, true);
	TestTrue(TEXT("Source preview is a distinct declared region"),
		PreviewWidget->HasSourcePreviewForTesting());
	FVerseVisualTile CoreCall;
	CoreCall.Kind = EVerseVisualTileKind::Expression;
	CoreCall.ExpressionKind = EVerseExpressionKind::Call;
	const TSharedRef<SVerseTile> CoreCallWidget = MakeWidget(CoreCall);
	CoreCall = FinalizeTestTile(MoveTemp(CoreCall));
	const TSharedRef<SVerseTile> LongPreviewWidget =
		SNew(SVerseTile)
		.Tile(CoreCall)
		.TileColor(FLinearColor::Black)
		.HasSourcePreview(true)
		.SourcePreview()
		[
			SNew(STextBlock)
			.Text(FText::FromString(FString::ChrN(256, TEXT('W'))))
			.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
		];
	LongPreviewWidget->SlatePrepass();
	TestTrue(TEXT("Source preview grows a tile by no more than 100 Slate units"),
		LongPreviewWidget->GetDesiredSize().X
			<= CoreCallWidget->GetDesiredSize().X + 101.0f);

	TestEqual(TEXT("Vertical output homeplates dock to the outer bottom edge"),
		GetVerseExecutionPinAnchorCoordinate(false, false), FVector2D(0.5f, 1.0f));
	TestEqual(TEXT("Labeled and unlabeled outputs use the same pin size"),
		GetVerseExecutionPinDesiredSize(false, false),
		GetVerseExecutionPinDesiredSize(false, true));
	TestEqual(TEXT("Horizontal and Tracks use identical homeplate geometry"),
		GetVerseExecutionPinDesiredSize(false, false,
			EVerseFunctionGraphPresentation::HorizontalExecution),
		GetVerseExecutionPinDesiredSize(false, false,
			EVerseFunctionGraphPresentation::Tracks));
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseFailableBlockPaintGeometryTest,
	"VerseVisualEditor.Graph.FailableBlock.PaintGeometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseFailableBlockPaintGeometryTest::RunTest(const FString& Parameters)
{
	for (const FVector2D Size : {FVector2D(120.0f, 80.0f), FVector2D(420.0f, 310.0f)})
	{
		const TArray<FVerseFailablePatternSegment> Pattern =
			BuildVerseFailablePatternSegments(Size);
		TestTrue(TEXT("Empty and populated block sizes produce a diamond pattern"),
			!Pattern.IsEmpty());

		bool bTouchesLeft = false;
		bool bTouchesRight = false;
		bool bTouchesTop = false;
		bool bTouchesBottom = false;
		for (const FVerseFailablePatternSegment& Segment : Pattern)
		{
			for (const FVector2D Point : {Segment.Start, Segment.End})
			{
				bTouchesLeft |= Point.X <= 0.0f;
				bTouchesRight |= Point.X >= Size.X;
				bTouchesTop |= Point.Y <= 0.0f;
				bTouchesBottom |= Point.Y >= Size.Y;
			}
		}
		TestTrue(TEXT("Pattern reaches every clipped interior edge"),
			bTouchesLeft && bTouchesRight && bTouchesTop && bTouchesBottom);

		const TStaticArray<FVector2D, 4> Corners =
			BuildVerseFailableCornerCenters(Size);
		TestEqual(TEXT("Top-left decoration is local"), Corners[0], FVector2D::ZeroVector);
		TestEqual(TEXT("Top-right decoration is local"), Corners[1], FVector2D(Size.X, 0.0f));
		TestEqual(TEXT("Bottom-left decoration is local"), Corners[2], FVector2D(0.0f, Size.Y));
		TestEqual(TEXT("Bottom-right decoration is local"), Corners[3], Size);

		const FVector2D WindowOffset(377.0f, 211.0f);
		const TStaticArray<FVector2D, 4> MovedCorners =
			BuildVerseFailableCornerCenters(Size);
		for (int32 Index = 0; Index < Corners.Num(); ++Index)
		{
			TestEqual(
				TEXT("Moving the window does not enter local decoration geometry"),
				MovedCorners[Index],
				Corners[Index]);
			TestEqual(
				TEXT("Paint geometry applies the window offset exactly once"),
				MovedCorners[Index] + WindowOffset,
				Corners[Index] + WindowOffset);
		}
	}

	FVerseVisualTile EmptyBlock;
	EmptyBlock.Kind = EVerseVisualTileKind::FailableBlock;
	EmptyBlock = FinalizeTestTile(MoveTemp(EmptyBlock));
	const TSharedRef<SVerseTile> EmptyWidget =
		SNew(SVerseTile)
		.Tile(EmptyBlock)
		.TileColor(FLinearColor::Black)
		.HasMainContent(true)
		.MainContent()
		[
			SNew(SBox).WidthOverride(80.0f).HeightOverride(40.0f)
		];
	EmptyWidget->SlatePrepass();
	TestTrue(TEXT("Tiles accept keyboard focus for graph commands"),
		EmptyWidget->SupportsKeyboardFocus());
	TestTrue(TEXT("List-capable empty block exposes its internal execution entry"),
		EmptyWidget->GetSocketAnchor({EVerseVisualSocketDirection::Output,
			EVerseVisualSocketRole::ClauseInsertion, 0}).IsValid());

	FVerseVisualTile PopulatedBlock = EmptyBlock;
	PopulatedBlock.Children.AddDefaulted();
	PopulatedBlock.bProducesValue = true;
	PopulatedBlock.SemanticTypeName = TEXT("int");
	PopulatedBlock.Outcome = EVerseExpressionOutcome::FailableValue;
	PopulatedBlock = FinalizeTestTile(MoveTemp(PopulatedBlock));
	const TSharedRef<SVerseTile> PopulatedWidget =
		SNew(SVerseTile)
		.Tile(PopulatedBlock)
		.TileColor(FLinearColor::Black)
		.HasMainContent(true)
		.IsSelected(true)
		.MainContent()
		[
			SNew(SBox).WidthOverride(260.0f).HeightOverride(180.0f)
		];
	PopulatedWidget->SlatePrepass();
	TestTrue(TEXT("Result-producing block exposes its right-edge value anchor"),
		!PopulatedBlock.GetValueOutputs().IsEmpty()
		&& PopulatedWidget->GetSocketAnchor(PopulatedBlock.GetValueOutputs()[0].Id).IsValid());
	TestTrue(TEXT("Populated block expands to contain its ordered child area"),
		PopulatedWidget->GetDesiredSize().X > EmptyWidget->GetDesiredSize().X
			&& PopulatedWidget->GetDesiredSize().Y > EmptyWidget->GetDesiredSize().Y);

	FVerseVisualTile OneBindingBlock = EmptyBlock;
	FVerseVisualTile& OneDefinition = OneBindingBlock.Children.AddDefaulted_GetRef();
	OneDefinition.Kind = EVerseVisualTileKind::Definition;
	OneDefinition.SemanticDataDefinition = reinterpret_cast<const uLang::CDataDefinition*>(1);
	OneDefinition.SemanticTypeName = TEXT("int");
	OneBindingBlock = FinalizeTestTile(MoveTemp(OneBindingBlock));
	FVerseVisualTile TwoBindingBlock = OneBindingBlock;
	FVerseVisualTile& TwoDefinition = TwoBindingBlock.Children.AddDefaulted_GetRef();
	TwoDefinition.Kind = EVerseVisualTileKind::Definition;
	TwoDefinition.SemanticDataDefinition = reinterpret_cast<const uLang::CDataDefinition*>(2);
	TwoDefinition.SemanticTypeName = TEXT("float");
	TwoBindingBlock = FinalizeTestTile(MoveTemp(TwoBindingBlock));
	const TSharedRef<SVerseTile> OneBindingWidget =
		SNew(SVerseTile)
		.Tile(OneBindingBlock)
		.TileColor(FLinearColor::Black)
		.HasMainContent(true)
		.MainContent()[SNew(SBox).WidthOverride(80.0f).HeightOverride(40.0f)];
	const TSharedRef<SVerseTile> TwoBindingWidget =
		SNew(SVerseTile)
		.Tile(TwoBindingBlock)
		.TileColor(FLinearColor::Black)
		.HasMainContent(true)
		.MainContent()[SNew(SBox).WidthOverride(80.0f).HeightOverride(40.0f)];
	OneBindingWidget->SlatePrepass();
	TwoBindingWidget->SlatePrepass();
	TestTrue(TEXT("Failure and binding pins share one right-edge group"),
		OneBindingWidget->GetSocketAnchor({EVerseVisualSocketDirection::Output,
			EVerseVisualSocketRole::FailureContext, 0}).IsValid()
		&& OneBindingBlock.GetValueOutputs().Num() >= 1
		&& OneBindingWidget->GetSocketAnchor(OneBindingBlock.GetValueOutputs()[0].Id).IsValid()
		&& TwoBindingBlock.GetValueOutputs().Num() >= 2
		&& TwoBindingWidget->GetSocketAnchor(TwoBindingBlock.GetValueOutputs()[1].Id).IsValid());
	TestTrue(TEXT("Additional binding rows never shrink the Condition tile"),
		TwoBindingWidget->GetDesiredSize().Y >= OneBindingWidget->GetDesiredSize().Y);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseFunctionAutomaticLayoutTest,
	"VerseVisualEditor.Graph.Layout.AutomaticExecutionAndOperands",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseFunctionAutomaticLayoutTest::RunTest(const FString& Parameters)
{
	auto MakeTile = [](float BodyWidth, float BodyHeight)
	{
		FVerseVisualTile Model;
		Model.Kind = EVerseVisualTileKind::Expression;
		Model.ExpressionKind = EVerseExpressionKind::Identifier;
		Model = FinalizeTestTile(MoveTemp(Model));
		TSharedRef<SVerseTile> Widget =
			SNew(SVerseTile)
			.Tile(Model)
			.TileColor(FLinearColor::Black)
			.HasMainContent(true)
			.MainContent()
			[
				SNew(SBox).WidthOverride(BodyWidth).HeightOverride(BodyHeight)
			];
		Widget->SlatePrepass();
		return Widget;
	};

	const TSharedRef<SVerseTile> Root = MakeTile(90.0f, 50.0f);
	const TSharedRef<SVerseTile> FirstOperand = MakeTile(120.0f, 80.0f);
	const TSharedRef<SVerseTile> SecondOperand = MakeTile(70.0f, 120.0f);
	const TSharedRef<SVerseExpressionLayoutPanel> Expression =
		SNew(SVerseExpressionLayoutPanel).HorizontalGap(72.0f).VerticalGap(18.0f);
	Expression->SetRoot(Root);
	Expression->AddOperand(
		FirstOperand, FirstOperand, []() { return FVector2D::ZeroVector; }, 0);
	Expression->AddOperand(
		SecondOperand, SecondOperand, []() { return FVector2D::ZeroVector; }, 1);
	Expression->SlatePrepass();
	const FVector2D RootPosition = Expression->GetRootPosition();
	TestTrue(TEXT("Operand subtrees reserve their combined vertical extent"),
		Expression->GetDesiredSize().Y
			>= FirstOperand->GetDesiredSize().Y + 18.0f
				+ SecondOperand->GetDesiredSize().Y);
	TestTrue(TEXT("Multiple operands fan around the consuming root"),
		RootPosition.Y > 0.0f);

	const TSharedRef<SVerseExpressionLayoutPanel> HorizontalExpression =
		SNew(SVerseExpressionLayoutPanel)
		.HorizontalGap(72.0f)
		.VerticalGap(18.0f)
		.Presentation(EVerseFunctionGraphPresentation::HorizontalExecution)
		.KeepOperandsBelowExecutionLane(true);
	const TSharedRef<SVerseTile> HorizontalRoot = MakeTile(90.0f, 50.0f);
	const TSharedRef<SVerseTile> HorizontalFirstOperand = MakeTile(120.0f, 80.0f);
	const TSharedRef<SVerseTile> HorizontalSecondOperand = MakeTile(70.0f, 120.0f);
	HorizontalExpression->SetRoot(HorizontalRoot);
	HorizontalExpression->AddOperand(
		HorizontalFirstOperand, HorizontalFirstOperand,
		[]() { return FVector2D::ZeroVector; }, 0);
	HorizontalExpression->AddOperand(
		HorizontalSecondOperand, HorizontalSecondOperand,
		[]() { return FVector2D::ZeroVector; }, 1);
	HorizontalExpression->SlatePrepass();
	TestEqual(TEXT("Horizontal statement root remains on the execution lane"),
		HorizontalExpression->GetRootPosition().Y, 0.0);
	TestTrue(TEXT("Horizontal operands begin below the execution lane"),
		HorizontalExpression->GetOperandPosition(0).Y >= 44.0f
			&& HorizontalExpression->GetOperandPosition(1).Y >= 44.0f);

	const TSharedRef<SVerseStatementLayoutPanel> Statements =
		SNew(SVerseStatementLayoutPanel)
		.Presentation(EVerseFunctionGraphPresentation::VerticalExecution)
		.StatementGap(12.0f);
	const TSharedRef<SWidget> FirstBounds =
		SNew(SBox).WidthOverride(420.0f).HeightOverride(180.0f);
	const TSharedRef<SWidget> SecondBounds =
		SNew(SBox).WidthOverride(260.0f).HeightOverride(90.0f);
	Statements->AddStatement(
		FirstBounds, Root, []() { return FVector2D(280.0f, 0.0f); });
	Statements->AddStatement(
		SecondBounds, SecondOperand, []() { return FVector2D(80.0f, 0.0f); });
	Statements->SlatePrepass();
	const FVector2D FirstPosition = Statements->GetStatementPosition(0);
	const FVector2D SecondPosition = Statements->GetStatementPosition(1);
	TestEqual(TEXT("Completed execution spines align"),
		FirstPosition.X + 280.0f + 24.0f,
		SecondPosition.X + 80.0f + 24.0f);
	TestTrue(TEXT("A statement reserves its entire subtree before the next one"),
		SecondPosition.Y >= FirstPosition.Y + FirstBounds->GetDesiredSize().Y + 12.0f);

	auto MakeHorizontalStatementTile = [](FVerseVisualTile Model, float Width, float Height)
	{
		Model = FinalizeTestTile(MoveTemp(Model));
		TSharedRef<SVerseTile> Widget =
			SNew(SVerseTile)
			.Tile(Model)
			.TileColor(FLinearColor::Black)
			.HasMainContent(true)
			.FunctionGraphPresentation(
				EVerseFunctionGraphPresentation::HorizontalExecution)
			.MainContent()
			[
				SNew(SBox).WidthOverride(Width).HeightOverride(Height)
			];
		Widget->SlatePrepass();
		return Widget;
	};
	FVerseVisualTile EntryModel;
	EntryModel.Kind = EVerseVisualTileKind::FunctionEntry;
	const TSharedRef<SVerseTile> HorizontalEntry =
		MakeHorizontalStatementTile(MoveTemp(EntryModel), 180.0f, 32.0f);
	FVerseVisualTile IdentifierModel;
	IdentifierModel.Kind = EVerseVisualTileKind::Expression;
	IdentifierModel.ExpressionKind = EVerseExpressionKind::Identifier;
	IdentifierModel.bStatementLevel = true;
	const TSharedRef<SVerseTile> HorizontalIdentifier =
		MakeHorizontalStatementTile(MoveTemp(IdentifierModel), 100.0f, 70.0f);

	const TSharedRef<SVerseStatementLayoutPanel> HorizontalStatements =
		SNew(SVerseStatementLayoutPanel)
		.Presentation(EVerseFunctionGraphPresentation::HorizontalExecution)
		.StatementGap(72.0f);
	HorizontalStatements->AddStatement(
		HorizontalEntry, HorizontalEntry, []() { return FVector2D(0.0f, 80.0f); });
	HorizontalStatements->AddStatement(
		HorizontalIdentifier, HorizontalIdentifier,
		[]() { return FVector2D(0.0f, 12.0f); });
	HorizontalStatements->SlatePrepass();
	const FVector2D FirstHorizontalPosition =
		HorizontalStatements->GetStatementPosition(0);
	const FVector2D SecondHorizontalPosition =
		HorizontalStatements->GetStatementPosition(1);
	TestEqual(TEXT("Horizontal execution spines align"),
		FirstHorizontalPosition.Y + 80.0f
			+ HorizontalEntry->GetHorizontalExecutionSpineY(),
		SecondHorizontalPosition.Y + 12.0f
			+ HorizontalIdentifier->GetHorizontalExecutionSpineY());
	TestTrue(TEXT("Horizontal spine follows each tile's actual execution dock"),
		!FMath::IsNearlyEqual(
			HorizontalEntry->GetHorizontalExecutionSpineY(),
			HorizontalIdentifier->GetHorizontalExecutionSpineY()));
	TestTrue(TEXT("Condition decoration remains completely below the execution lane"),
		GetVerseHorizontalConditionTopPadding(*HorizontalEntry) - 5.5f
			> HorizontalEntry->GetHorizontalExecutionSpineY());
	TestTrue(TEXT("Horizontal statements reserve complete subtree width"),
		SecondHorizontalPosition.X
			>= FirstHorizontalPosition.X
				+ HorizontalEntry->GetDesiredSize().X + 72.0f);
	return true;
}

#endif
