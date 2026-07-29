#if WITH_DEV_AUTOMATION_TESTS

#include "VerseDocument.h"
#include "VerseDocumentSession.h"
#include "VerseExpressionActions.h"
#include "VerseFunctionNavigation.h"
#include "VerseIntrinsicPresentation.h"

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
	FVerseIntrinsicPresentationRegistryTest,
	"VerseVisualEditor.Expressions.Presentation.IntrinsicRegistry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseIntrinsicPresentationRegistryTest::RunTest(const FString& Parameters)
{
	const TConstArrayView<FVerseIntrinsicPresentationDescriptor> Table =
		GetVerseIntrinsicPresentationTable();
	TestTrue(TEXT("Intrinsic presentation table is populated"), !Table.IsEmpty());
	for (const FVerseIntrinsicPresentationDescriptor& Descriptor : Table)
	{
		TestFalse(TEXT("Every descriptor has a callable spelling"),
			Descriptor.Key.Spelling.IsEmpty());
		if (Descriptor.BlueprintLibrary != EVerseIntrinsicBlueprintLibrary::None)
		{
			TestNotNull(
				*FString::Printf(
					TEXT("Blueprint target resolves for %s"),
					*Descriptor.Key.Spelling),
				ResolveVerseIntrinsicBlueprintFunction(Descriptor));
		}
	}

	FVerseIntrinsicPresentationKey AddKey;
	AddKey.Form = EVerseIntrinsicCallableForm::InfixOperator;
	AddKey.Spelling = TEXT("+");
	AddKey.ParameterTypes = {TEXT("int"), TEXT("int")};
	AddKey.ResultType = TEXT("int");
	const FVerseIntrinsicPresentationDescriptor* Add =
		FindVerseIntrinsicPresentation(AddKey);
	if (!TestNotNull(TEXT("Typed Add descriptor resolves"), Add))
	{
		return false;
	}
	FVerseIntrinsicPresentationKey IntegerLessKey;
	IntegerLessKey.Form = EVerseIntrinsicCallableForm::InfixOperator;
	IntegerLessKey.Spelling = TEXT("<");
	IntegerLessKey.ParameterTypes = {TEXT("int"), TEXT("int")};
	IntegerLessKey.ResultType = TEXT("int");
	const FVerseIntrinsicPresentationDescriptor* IntegerLess =
		FindVerseIntrinsicPresentation(IntegerLessKey);
	if (TestNotNull(TEXT("Typed integer relation descriptor resolves"), IntegerLess))
	{
		TestEqual(TEXT("Integer relation mirrors Blueprint's action-menu name"),
			IntegerLess->FallbackDisplayName.ToString(),
			FString(TEXT("Less (<)")));
		TestEqual(TEXT("Integer relation remains in the operators category"),
			IntegerLess->FallbackCategory.ToString(),
			FString(TEXT("Utilities|Operators")));
	}
	FVerseIntrinsicPresentationKey IntegerDivideKey;
	IntegerDivideKey.Form = EVerseIntrinsicCallableForm::InfixOperator;
	IntegerDivideKey.Spelling = TEXT("/");
	IntegerDivideKey.ParameterTypes = {TEXT("int"), TEXT("int")};
	IntegerDivideKey.ResultType = TEXT("rational");
	const FVerseIntrinsicPresentationDescriptor* IntegerDivide =
		FindVerseIntrinsicPresentation(IntegerDivideKey);
	if (TestNotNull(TEXT("Integer division descriptor resolves"), IntegerDivide))
	{
		TestEqual(TEXT("Integer division is named Divide"),
			IntegerDivide->FallbackDisplayName.ToString(), FString(TEXT("Divide")));
		TestEqual(TEXT("Integer division uses the operators category"),
			IntegerDivide->FallbackCategory.ToString(),
			FString(TEXT("Utilities|Operators")));
	}

	const FVerseResolvedExpressionPresentation VerseWins =
		ResolveVerseExpressionPresentation(
			FText::FromString(TEXT("Verse Name")),
			FText::FromString(TEXT("Verse Category")),
			FText::FromString(TEXT("UFunction Name")),
			FText::FromString(TEXT("UFunction Category")),
			Add,
			TEXT("+"));
	TestEqual(TEXT("Verse display name has highest precedence"),
		VerseWins.DisplayName.ToString(), FString(TEXT("Verse Name")));
	TestEqual(TEXT("Verse category has highest precedence"),
		VerseWins.Category.ToString(), FString(TEXT("Verse Category")));

	const FVerseResolvedExpressionPresentation UFunctionWins =
		ResolveVerseExpressionPresentation(
			FText::GetEmpty(),
			FText::GetEmpty(),
			FText::FromString(TEXT("UFunction Name")),
			FText::FromString(TEXT("UFunction Category")),
			Add,
			TEXT("+"));
	TestEqual(TEXT("Explicit UFunction display name precedes the table"),
		UFunctionWins.DisplayName.ToString(), FString(TEXT("UFunction Name")));
	TestEqual(TEXT("UFunction category precedes the table"),
		UFunctionWins.Category.ToString(), FString(TEXT("UFunction Category")));

	const FVerseResolvedExpressionPresentation TableFallback =
		ResolveVerseExpressionPresentation(
			FText::GetEmpty(), FText::GetEmpty(),
			FText::GetEmpty(), FText::GetEmpty(), Add, TEXT("+"));
	TestEqual(TEXT("Intrinsic display name comes from the table"),
		TableFallback.DisplayName.ToString(), FString(TEXT("Add")));
	TestEqual(TEXT("Intrinsic category comes from the table"),
		TableFallback.Category.ToString(), FString(TEXT("Utilities|Operators")));

	const FVerseResolvedExpressionPresentation FinalFallback =
		ResolveVerseExpressionPresentation(
			FText::GetEmpty(), FText::GetEmpty(),
			FText::GetEmpty(), FText::GetEmpty(), nullptr, TEXT("Unmapped"));
	TestEqual(TEXT("Unmapped callable retains its actual name"),
		FinalFallback.DisplayName.ToString(), FString(TEXT("Unmapped")));
	TestEqual(TEXT("Unmapped callable is uncategorized"),
		FinalFallback.Category.ToString(), FString(TEXT("Uncategorized")));

	const auto FindDescriptor = [](const TCHAR* Spelling,
		std::initializer_list<const TCHAR*> ParameterTypes, const TCHAR* ResultType)
	{
		FVerseIntrinsicPresentationKey Key;
		Key.Form = EVerseIntrinsicCallableForm::Ordinary;
		Key.Spelling = Spelling;
		for (const TCHAR* ParameterType : ParameterTypes)
		{
			Key.ParameterTypes.Add(ParameterType);
		}
		Key.ResultType = ResultType;
		return FindVerseIntrinsicPresentation(Key);
	};
	const FVerseIntrinsicPresentationDescriptor* ArrayFind =
		FindDescriptor(TEXT("Find"), {TEXT("[]int"), TEXT("int")}, TEXT("int"));
	TestNotNull(TEXT("Array Find matches the array descriptor"), ArrayFind);
	if (ArrayFind != nullptr)
	{
		TestEqual(TEXT("Array Find uses Blueprint's array category"),
			ArrayFind->FallbackCategory.ToString(), FString(TEXT("Utilities|Array")));
	}
	TestNull(TEXT("A non-array Find does not match the array descriptor"),
		FindDescriptor(TEXT("Find"), {TEXT("string"), TEXT("char")}, TEXT("int")));
	const auto TestArrayOperation = [this, &FindDescriptor](
		const TCHAR* Spelling, std::initializer_list<const TCHAR*> ParameterTypes)
	{
		const FVerseIntrinsicPresentationDescriptor* Descriptor =
			FindDescriptor(Spelling, ParameterTypes, TEXT("[]false"));
		if (TestNotNull(*FString::Printf(TEXT("%s matches an array descriptor"), Spelling),
			Descriptor))
		{
			TestEqual(*FString::Printf(TEXT("%s uses the array category"), Spelling),
				Descriptor->FallbackCategory.ToString(),
				FString(TEXT("Utilities|Array")));
		}
	};
	TestArrayOperation(TEXT("Slice"), {TEXT("[]any"), TEXT("int")});
	TestArrayOperation(TEXT("RemoveElement"), {TEXT("[]any"), TEXT("int")});
	TestArrayOperation(TEXT("RemoveLastElement"), {TEXT("[]any"), TEXT("tuple()")});
	const FVerseIntrinsicPresentationDescriptor* MakeError =
		FindDescriptor(TEXT("MakeError"), {TEXT("MyError")}, TEXT("result(false,MyError)"));
	TestNotNull(TEXT("Generic MakeError matches the result descriptor"), MakeError);
	if (MakeError != nullptr)
	{
		TestEqual(TEXT("MakeError uses the result category"),
			MakeError->FallbackCategory.ToString(), FString(TEXT("Utilities|Result")));
	}
	const FVerseIntrinsicPresentationDescriptor* VectorToString =
		FindDescriptor(TEXT("ToString"), {TEXT("/Verse.org/SpatialMath/vector3")},
			TEXT("[]char"));
	TestNotNull(TEXT("Non-scalar engine ToString matches the string fallback"),
		VectorToString);
	if (VectorToString != nullptr)
	{
		TestEqual(TEXT("Non-scalar ToString uses the string category"),
			VectorToString->FallbackCategory.ToString(),
			FString(TEXT("Utilities|String")));
	}
	return true;
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
			return Action->SourceForm
				== EVerseExpressionSourceForm::IdentifierReference;
		}));
	const TSharedPtr<FVerseExpressionAction>* Add = Actions.FindByPredicate(
		[](const TSharedPtr<FVerseExpressionAction>& Action)
		{
			return Action->SourceForm == EVerseExpressionSourceForm::InfixOperator
				&& Action->SourceSpelling == TEXT("+");
		});
	if (!TestNotNull(TEXT("Add action exists"), Add))
	{
		return false;
	}
	TestEqual(
		TEXT("The editor-supported Add action uses structural validation"),
		(*Add)->Validation,
		EVerseExpressionActionValidation::Structural);
	TestEqual(
		TEXT("Operators use Blueprint's promotable operator category"),
		(*Add)->Category.ToString(),
		FString(TEXT("Utilities|Operators")));
	TestEqual(
		TEXT("Operators use Blueprint's action-menu display name"),
		(*Add)->DisplayName.ToString(),
		FString(TEXT("Add")));
	TestEqual(
		TEXT("Operators also have a module grouping"),
		(*Add)->ModuleCategory.ToString(),
		FString(TEXT("Verse")));
	TestEqual(
		TEXT("The Add icon carries its resolved result type"),
		(*Add)->ResultTypeName,
		FString(TEXT("int")));
	FVerseVisualSocket FloatSocket;
	FloatSocket.IntrinsicTypeName = TEXT("float");
	const TArray<TSharedPtr<FVerseExpressionAction>> FloatConsumers =
		FVerseExpressionActionQuery::Build(
			Functions[0].Parameters, FloatSocket, true,
			*Session.GetParseSnapshot().GetDocument());
	TestTrue(TEXT("Polymorphic Add accepts float"), FloatConsumers.ContainsByPredicate(
		[](const TSharedPtr<FVerseExpressionAction>& Action)
		{
			return Action->SourceForm == EVerseExpressionSourceForm::InfixOperator
				&& Action->SourceSpelling == TEXT("+");
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
			return Action->SourceForm == EVerseExpressionSourceForm::InfixOperator
				&& Action->SourceSpelling == TEXT("+");
		}));
	const TArray<TSharedPtr<FVerseExpressionAction>> IntProducers =
		FVerseExpressionActionQuery::Build(
			Functions[0].Parameters, Identifier.ValueOutputs[0], false,
			*Session.GetParseSnapshot().GetDocument());
	TestTrue(TEXT("A matching identifier naturally appears as a producer"),
		IntProducers.ContainsByPredicate([](const TSharedPtr<FVerseExpressionAction>& Action)
		{
			return Action->SourceForm
					== EVerseExpressionSourceForm::IdentifierReference
				&& Action->Validation == EVerseExpressionActionValidation::Structural
				&& Action->DisplayName.ToString() == TEXT("Input")
				&& Action->Category.ToString() == TEXT("Variables");
		}));
	const TSharedPtr<FVerseExpressionAction>* InputAction =
		IntProducers.FindByPredicate([](
			const TSharedPtr<FVerseExpressionAction>& Action)
		{
			return Action.IsValid()
				&& Action->SourceForm
					== EVerseExpressionSourceForm::IdentifierReference
				&& Action->DisplayName.ToString() == TEXT("Input");
		});
	if (TestNotNull(TEXT("Input action is available for icon typing"), InputAction))
	{
		TestEqual(
			TEXT("The identifier icon carries the identifier's value type"),
			(*InputAction)->ResultTypeName,
			FString(TEXT("int")));
	}
	const FUtf8String BeforeRejectedAction = Session.GetCurrentUtf8();
	const FVerseDocumentRevision BeforeRejectedRevision = Session.GetRevision();
	TestFalse(TEXT("Semantic rejection prevents the localized replacement"),
		TryApplyVerseExpressionAction(
			Session,
			Identifier.Range,
			**Add,
			[](const FUtf8String&, FText& OutError)
			{
				OutError = FText::FromString(TEXT("Expected semantic rejection"));
				return false;
			},
			Error));
	TestTrue(TEXT("Semantic rejection leaves source unchanged"),
		Session.GetCurrentUtf8() == BeforeRejectedAction);
	TestEqual(TEXT("Semantic rejection leaves revision unchanged"),
		Session.GetRevision(), BeforeRejectedRevision);
	TestTrue(TEXT("Add applies after prospective structural and semantic validation"),
		TryApplyVerseExpressionAction(
			Session,
			Identifier.Range,
			**Add,
			[](const FUtf8String&, FText&) { return true; },
			Error));
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
