#if WITH_DEV_AUTOMATION_TESTS

#include "Slate/VerseOrderedTilePacking.h"

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseOrderedTilePackingTest,
	"VerseVisualEditor.Graph.Layout.OrderedSquarePacking",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseOrderedTilePackingTest::RunTest(const FString& Parameters)
{
	const TArray<FVector2D> Sizes{
		{100.0, 50.0}, {100.0, 50.0}, {100.0, 50.0}, {100.0, 50.0},
		{100.0, 50.0}, {100.0, 50.0}, {100.0, 50.0}, {100.0, 50.0}};
	const FVerseOrderedTilePackingResult Layout =
		PackVerseTilesApproximatelySquare(Sizes, 16.0f, 8.0f);
	TestEqual(TEXT("Every source tile receives one position"),
		Layout.Positions.Num(), Sizes.Num());
	TestTrue(TEXT("The resulting bounds are approximately square"),
		Layout.Size.X > 0.0 && Layout.Size.Y > 0.0
		&& FMath::Max(Layout.Size.X, Layout.Size.Y)
			/ FMath::Min(Layout.Size.X, Layout.Size.Y) < 1.5);

	for (int32 Index = 1; Index < Layout.Positions.Num(); ++Index)
	{
		const FVector2D Previous = Layout.Positions[Index - 1];
		const FVector2D Current = Layout.Positions[Index];
		TestTrue(TEXT("Packing preserves top-to-bottom then left-to-right source order"),
			(Current.X == Previous.X && Current.Y > Previous.Y)
			|| (Current.X > Previous.X && Current.Y == 0.0));
	}

	const FVerseOrderedTilePackingResult Repeated =
		PackVerseTilesApproximatelySquare(Sizes, 16.0f, 8.0f);
	TestTrue(TEXT("Packing is deterministic"),
		Layout.Positions == Repeated.Positions && Layout.Size == Repeated.Size);
	return true;
}

#endif
