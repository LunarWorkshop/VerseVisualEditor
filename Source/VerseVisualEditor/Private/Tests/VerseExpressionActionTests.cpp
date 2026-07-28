#if WITH_DEV_AUTOMATION_TESTS

#include "VerseDocument.h"
#include "VerseDocumentSession.h"
#include "VerseExpressionActions.h"
#include "VerseFunctionNavigation.h"

#include "Misc/AutomationTest.h"
#include "Containers/StringConv.h"

namespace
{
	TSharedPtr<FVerseDocument> MakeDocument(const FString& Source, FText& Error)
	{
		FTCHARToUTF8 Utf8(*Source);
		TArray<uint8> Bytes;
		Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
		return FVerseDocument::CreateFromBytes(Bytes, Error);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseTypedExpressionSearchActionsTest,
	"VerseVisualEditor.Expressions.Search.TypedScopedActions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseTypedExpressionSearchActionsTest::RunTest(const FString& Parameters)
{
	FText Error;
	TSharedPtr<FVerseDocument> Document = MakeDocument(
		TEXT("Increment(Input : int, Other : float) : int =\n    Input\n"), Error);
	if (!TestTrue(TEXT("Fixture parses"), Document.IsValid()))
	{
		return false;
	}
	FVerseDocumentSession Session(Document.ToSharedRef());
	const TArray<FVerseFunctionNavigationItem> Functions = FVerseFunctionNavigationBuilder::Build(
		Session.GetTiles(), Session.GetParseSnapshot());
	if (!TestEqual(TEXT("One function is found"), Functions.Num(), 1)
		|| !TestTrue(TEXT("Function graph has a statement"), Functions[0].GraphTiles.Num() >= 3))
	{
		return false;
	}
	const FVerseVisualTile& Identifier = Functions[0].GraphTiles[1];
	const TArray<TSharedPtr<FVerseExpressionAction>> Actions = FVerseExpressionActionQuery::Build(
		Functions[0].Parameters,
		Identifier.ValueOutputs[0],
		true,
		*Session.GetParseSnapshot().GetDocument());
	TestEqual(TEXT("Only consumers are offered for an output drag"), Actions.Num(), 1);
	TestTrue(TEXT("Identifiers are excluded from output-drag consumers"), !Actions.ContainsByPredicate(
		[](const TSharedPtr<FVerseExpressionAction>& Action)
		{
			return Action->Kind == EVerseExpressionActionKind::Identifier;
		}));
	const TSharedPtr<FVerseExpressionAction>* Add = Actions.FindByPredicate(
		[](const TSharedPtr<FVerseExpressionAction>& Action)
		{
			return Action->Kind == EVerseExpressionActionKind::Addition;
		});
	if (!TestNotNull(TEXT("Add action exists"), Add))
	{
		return false;
	}
	FVerseVisualSocket FloatSocket;
	FloatSocket.IntrinsicTypeName = TEXT("float");
	const TArray<TSharedPtr<FVerseExpressionAction>> FloatConsumers =
		FVerseExpressionActionQuery::Build(
			Functions[0].Parameters, FloatSocket, true,
			*Session.GetParseSnapshot().GetDocument());
	TestTrue(TEXT("Polymorphic Add accepts float"), FloatConsumers.ContainsByPredicate(
		[](const TSharedPtr<FVerseExpressionAction>& Action)
		{
			return Action->Kind == EVerseExpressionActionKind::Addition;
		}));
	FVerseVisualSocket StringSocket;
	StringSocket.IntrinsicTypeName = TEXT("string");
	const TArray<TSharedPtr<FVerseExpressionAction>> StringConsumers =
		FVerseExpressionActionQuery::Build(
			Functions[0].Parameters, StringSocket, true,
			*Session.GetParseSnapshot().GetDocument());
	TestTrue(TEXT("Polymorphic Add accepts string concatenation"),
		StringConsumers.ContainsByPredicate(
		[](const TSharedPtr<FVerseExpressionAction>& Action)
		{
			return Action->Kind == EVerseExpressionActionKind::Addition;
		}));
	const TArray<TSharedPtr<FVerseExpressionAction>> IntProducers =
		FVerseExpressionActionQuery::Build(
			Functions[0].Parameters, Identifier.ValueOutputs[0], false,
			*Session.GetParseSnapshot().GetDocument());
	TestTrue(TEXT("A matching identifier naturally appears as a producer"),
		IntProducers.ContainsByPredicate([](const TSharedPtr<FVerseExpressionAction>& Action)
		{
			return Action->Kind == EVerseExpressionActionKind::Identifier
				&& Action->DisplayName.ToString() == TEXT("Input");
		}));
	TestTrue(TEXT("Add applies after prospective structural validation"),
		TryApplyVerseExpressionAction(Session, Identifier.Range, **Add, Error));
	const FString Edited = FString(UTF8_TO_TCHAR(*Session.GetCurrentUtf8()));
	TestTrue(TEXT("The original expression becomes the first operand"),
		Edited.Contains(TEXT("Input + 0")));
	TestFalse(TEXT("The original expression is not duplicated"),
		Edited.Contains(TEXT("Input + Input")));
	const TArray<FVerseFunctionNavigationItem> EditedFunctions =
		FVerseFunctionNavigationBuilder::Build(
			Session.GetTiles(), Session.GetParseSnapshot());
	if (!TestEqual(TEXT("Edited function remains available"), EditedFunctions.Num(), 1)
		|| !TestTrue(TEXT("Edited graph contains Add"), EditedFunctions[0].GraphTiles.Num() >= 3))
	{
		return false;
	}
	const FVerseVisualTile& EditedAdd = EditedFunctions[0].GraphTiles[1];
	TestEqual(TEXT("Add has two operand tiles"), EditedAdd.Children.Num(), 2);
	if (EditedAdd.Children.Num() == 2)
	{
		const FVerseDocument& EditedDocument = *Session.GetParseSnapshot().GetDocument();
		TestEqual(TEXT("First operand retains the original identifier"),
			EditedDocument.DecodeOriginalRange(EditedAdd.Children[0].Range), FString(TEXT("Input")));
		TestEqual(TEXT("Second operand receives the default literal"),
			EditedDocument.DecodeOriginalRange(EditedAdd.Children[1].Range), FString(TEXT("0")));
		TestEqual(TEXT("Unsupported literal remains an expression tile"),
			EditedAdd.Children[1].ExpressionKind, EVerseExpressionKind::Unsupported);
	}
	return true;
}

#endif
