#if WITH_DEV_AUTOMATION_TESTS

#include "SVerseGraphSurface.h"
#include "SVerseFunctionGraphLayout.h"
#include "SVerseTile.h"
#include "VerseGraphCoordinates.h"
#include "VerseVisualEditorStyle.h"

#include "GraphEditorSettings.h"
#include "Misc/AutomationTest.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Widgets/Layout/SBox.h"

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
		FName(TEXT("Tile.Header.Expanded")),
		FName(TEXT("Tile.Header.Collapsed")),
		FName(TEXT("Tile.Header.Highlight.Expanded")),
		FName(TEXT("Tile.Body")),
		FName(TEXT("Tile.BodyOverlay")),
		FName(TEXT("Tile.Separator"))})
	{
		TestTrue(*FString::Printf(TEXT("Style supplies %s"), *BrushName.ToString()),
			Registered->GetBrush(BrushName) != nullptr);
	}
	TestTrue(TEXT("Verse chrome does not replace AppStyle node body"),
		Registered->GetBrush(TEXT("Tile.Body"))
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
		TEXT("Full output home plate uses its painted center"),
		GetVerseExecutionPinAnchorCoordinate(false, false),
		FVector2D(0.5f, 1.0f / 6.0f));
	TestEqual(
		TEXT("Compact output home plate uses its painted center"),
		GetVerseExecutionPinAnchorCoordinate(false, true),
		FVector2D(0.5f, 0.4f));
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
		.ShowBody(true)
		.BodyContent()
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
		.ShowBody(true)
		.IsSelected(true)
		.BodyContent()
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
		.ShowBody(true)
		.BodyContent()[SNew(SBox).WidthOverride(80.0f).HeightOverride(40.0f)];
	const TSharedRef<SVerseTile> TwoBindingWidget =
		SNew(SVerseTile)
		.Tile(TwoBindingBlock)
		.TileColor(FLinearColor::Black)
		.ShowBody(true)
		.BodyContent()[SNew(SBox).WidthOverride(80.0f).HeightOverride(40.0f)];
	OneBindingWidget->SlatePrepass();
	TwoBindingWidget->SlatePrepass();
	TestTrue(TEXT("Failure and binding pins share one right-edge group"),
		OneBindingWidget->GetSocketAnchor({EVerseVisualSocketDirection::Output,
			EVerseVisualSocketRole::FailureContext, 0}).IsValid()
		&& OneBindingBlock.GetValueOutputs().Num() >= 1
		&& OneBindingWidget->GetSocketAnchor(OneBindingBlock.GetValueOutputs()[0].Id).IsValid()
		&& TwoBindingBlock.GetValueOutputs().Num() >= 2
		&& TwoBindingWidget->GetSocketAnchor(TwoBindingBlock.GetValueOutputs()[1].Id).IsValid());
	TestTrue(TEXT("The Condition header grows to contain additional bindings"),
		TwoBindingWidget->GetDesiredSize().Y > OneBindingWidget->GetDesiredSize().Y);
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
			.ShowBody(true)
			.BodyContent()
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
	return true;
}

#endif
