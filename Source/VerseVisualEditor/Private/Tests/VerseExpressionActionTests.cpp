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
		Functions[0].Parameters, Identifier, *Session.GetParseSnapshot().GetDocument());
	TestEqual(TEXT("Matching identifier and Add are offered"), Actions.Num(), 2);
	TestTrue(TEXT("The float parameter is excluded"), !Actions.ContainsByPredicate(
		[](const TSharedPtr<FVerseExpressionAction>& Action)
		{
			return Action->DisplayName.ToString() == TEXT("Other");
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
	TestTrue(TEXT("Add applies after prospective structural validation"),
		TryApplyVerseExpressionAction(Session, Identifier.Range, **Add, Error));
	const FString Edited = FString(UTF8_TO_TCHAR(*Session.GetCurrentUtf8()));
	TestTrue(TEXT("The existing terminal fills both valid operands"),
		Edited.Contains(TEXT("Input + Input")));
	return true;
}

#endif
