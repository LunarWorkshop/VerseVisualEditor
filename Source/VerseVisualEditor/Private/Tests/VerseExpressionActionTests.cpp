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
	IntegerLessKey.ResultType = TEXT("void");
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
	FVerseIntrinsicPresentationKey IntegerNotEqualKey;
	IntegerNotEqualKey.Form = EVerseIntrinsicCallableForm::InfixOperator;
	IntegerNotEqualKey.Spelling = TEXT("<>");
	IntegerNotEqualKey.ParameterTypes = {TEXT("int"), TEXT("comparable")};
	IntegerNotEqualKey.ResultType = TEXT("int");
	const FVerseIntrinsicPresentationDescriptor* IntegerNotEqual =
		FindVerseIntrinsicPresentation(IntegerNotEqualKey);
	if (TestNotNull(TEXT("Comparable Not Equal descriptor resolves"), IntegerNotEqual))
	{
		TestEqual(TEXT("Not Equal mirrors Blueprint's action-menu name"),
			IntegerNotEqual->FallbackDisplayName.ToString(),
			FString(TEXT("Not Equal (!=)")));
		TestEqual(TEXT("Not Equal remains in the operators category"),
			IntegerNotEqual->FallbackCategory.ToString(),
			FString(TEXT("Utilities|Operators")));
		TestTrue(TEXT("Not Equal derives its abstract RHS default from its LHS"),
			IntegerNotEqual->DefaultSourceTypeParameterIndices.Num() == 2
			&& IntegerNotEqual->DefaultSourceTypeParameterIndices[1] == 0);
		TestTrue(TEXT("Not Equal declares symmetric operands"),
			IntegerNotEqual->bSymmetricOperands);
		TestEqual(TEXT("Not Equal has a source-safe untyped placeholder"),
			IntegerNotEqual->UntypedDefaultSource,
			FString(TEXT("0")));
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
	const FVerseIntrinsicPresentationDescriptor* FitsInPlayerMap =
		FindDescriptor(TEXT("FitsInPlayerMap"), {TEXT("persistable")},
			TEXT("persistable"));
	TestNotNull(TEXT("FitsInPlayerMap matches the persistence descriptor"),
		FitsInPlayerMap);
	if (FitsInPlayerMap != nullptr)
	{
		TestEqual(TEXT("FitsInPlayerMap uses the persistence category"),
			FitsInPlayerMap->FallbackCategory.ToString(),
			FString(TEXT("Utilities|Persistence")));
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
		Identifier.GetValueOutputs()[0],
		true,
		*Session.GetParseSnapshot().GetDocument());
	TestTrue(TEXT("Syntax-only fallback never invents callable or operator signatures"),
		Actions.IsEmpty());
	const TArray<TSharedPtr<FVerseExpressionAction>> IntProducers =
		FVerseExpressionActionQuery::Build(
			Functions[0].Parameters, Identifier.GetValueOutputs()[0], false,
			*Session.GetParseSnapshot().GetDocument());
	TestTrue(TEXT("A matching identifier naturally appears as a producer"),
		IntProducers.ContainsByPredicate([](const TSharedPtr<FVerseExpressionAction>& Action)
		{
			return Action->SourceForm
					== EVerseExpressionSourceForm::IdentifierReference
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
	TestTrue(TEXT("An integer socket offers an integer literal"),
		IntProducers.ContainsByPredicate([](const TSharedPtr<FVerseExpressionAction>& Action)
		{
			return Action.IsValid()
				&& Action->SourceForm == EVerseExpressionSourceForm::Literal
				&& Action->SourceSpelling == TEXT("0")
				&& Action->ResultTypeName == TEXT("int");
		}));

	FVerseVisualSocket FloatSocket;
	FloatSocket.IntrinsicTypeName = TEXT("float");
	const TArray<TSharedPtr<FVerseExpressionAction>> FloatActions =
		FVerseExpressionActionQuery::Build({}, FloatSocket, false, *Document);
	TestTrue(TEXT("A float socket offers an ordinary float literal"),
		FloatActions.ContainsByPredicate([](const TSharedPtr<FVerseExpressionAction>& Action)
		{
			return Action.IsValid()
				&& Action->SourceForm == EVerseExpressionSourceForm::Literal
				&& Action->SourceSpelling == TEXT("0.0");
		}));
	TestFalse(TEXT("Special floats are edited through Details, not expression search"),
		FloatActions.ContainsByPredicate([](const TSharedPtr<FVerseExpressionAction>& Action)
		{
			return Action.IsValid()
				&& (Action->SourceSpelling == TEXT("NaN")
					|| Action->SourceSpelling == TEXT("Inf")
					|| Action->SourceSpelling == TEXT("-Inf"));
		}));

	FVerseVisualSocket LogicSocket;
	LogicSocket.IntrinsicTypeName = TEXT("logic");
	const TArray<TSharedPtr<FVerseExpressionAction>> LogicActions =
		FVerseExpressionActionQuery::Build({}, LogicSocket, false, *Document);
	TestTrue(TEXT("A logic socket explicitly offers True"),
		LogicActions.ContainsByPredicate([](const TSharedPtr<FVerseExpressionAction>& Action)
		{
			return Action.IsValid()
				&& Action->SourceForm == EVerseExpressionSourceForm::Literal
				&& Action->SourceSpelling == TEXT("true");
		}));
	TestTrue(TEXT("A logic socket explicitly offers False"),
		LogicActions.ContainsByPredicate([](const TSharedPtr<FVerseExpressionAction>& Action)
		{
			return Action.IsValid()
				&& Action->SourceForm == EVerseExpressionSourceForm::Literal
				&& Action->SourceSpelling == TEXT("false");
		}));
	TestFalse(TEXT("Literals do not appear when searching for actions that take a value"),
		FVerseExpressionActionQuery::Build({}, LogicSocket, true, *Document)
			.ContainsByPredicate([](const TSharedPtr<FVerseExpressionAction>& Action)
			{
				return Action.IsValid()
					&& Action->SourceForm == EVerseExpressionSourceForm::Literal;
			}));

	const TArray<TSharedPtr<FVerseExpressionAction>> UntypedActions =
		FVerseExpressionActionQuery::BuildAll(
			Functions[0].Parameters,
			*Document,
			Identifier.Range,
			FString(),
			{});
	const TSharedPtr<FVerseExpressionAction>* IfAction =
		UntypedActions.FindByPredicate([](const TSharedPtr<FVerseExpressionAction>& Action)
		{
			return Action.IsValid()
				&& Action->SourceForm == EVerseExpressionSourceForm::StructuralExpression
				&& Action->SourceSpelling == TEXT("if (true?) {}");
		});
	if (TestNotNull(TEXT("Untyped clause search offers If"), IfAction))
	{
		TestEqual(
			TEXT("If identifies its generated condition as provisional content"),
			(*IfAction)->ProvisionalContentTarget,
			EVerseProvisionalContentTarget::FirstConditionExpression);
	}
	const TSharedPtr<FVerseExpressionAction>* VariableDefinition =
		UntypedActions.FindByPredicate([](const TSharedPtr<FVerseExpressionAction>& Action)
		{
			return Action.IsValid()
				&& Action->SourceForm == EVerseExpressionSourceForm::Definition
				&& Action->DisplayName.ToString() == TEXT("Variable Definition");
		});
	if (TestNotNull(TEXT("Untyped clause search offers a variable definition"), VariableDefinition))
	{
		FString Source;
		FText SourceError;
		TestTrue(TEXT("Variable definition has a source-safe creation template"),
			BuildVerseExpressionActionSource(
				**VariableDefinition, FStringView(), Source, SourceError));
		TestEqual(TEXT("Variable definition template is valid Verse source"),
			Source, FString(TEXT("var NewVariable : int = 0")));
	}
	const TSharedPtr<FVerseExpressionAction>* ConstantDefinition =
		UntypedActions.FindByPredicate([](const TSharedPtr<FVerseExpressionAction>& Action)
		{
			return Action.IsValid()
				&& Action->SourceForm == EVerseExpressionSourceForm::Definition
				&& Action->DisplayName.ToString() == TEXT("Constant Definition");
		});
	if (TestNotNull(TEXT("Untyped clause search offers a constant definition"), ConstantDefinition))
	{
		FString Source;
		FText SourceError;
		TestTrue(TEXT("Constant definition has a source-safe creation template"),
			BuildVerseExpressionActionSource(
				**ConstantDefinition, FStringView(), Source, SourceError));
		TestEqual(TEXT("Constant definition template is valid Verse source"),
			Source, FString(TEXT("NewConstant : int = 0")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseNamedInputMaterializationTest,
	"VerseVisualEditor.Expressions.Search.NamedDefaultMaterialization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseNamedInputMaterializationTest::RunTest(const FString& Parameters)
{
	FText Error;
	TSharedPtr<FVerseDocument> Document = MakeDocument(
		TEXT("WithDefault(Required : int, ?Optional : float = 1.0)<computes> : float = Optional\n")
		TEXT("UseDefault()<computes> : float = WithDefault(1)\n"),
		Error);
	if (!TestTrue(TEXT("Named-default fixture parses"), Document.IsValid()))
	{
		return false;
	}
	FVerseDocumentSession Session(Document.ToSharedRef());
	const TArray<FVerseFunctionNavigationItem> Functions =
		FVerseFunctionNavigationBuilder::Build(
			Session.GetTiles(), Session.GetParseSnapshot());
	const FVerseFunctionNavigationItem* Caller = Functions.FindByPredicate(
		[](const FVerseFunctionNavigationItem& Function)
		{
			return Function.Name == TEXT("UseDefault");
		});
	const FVerseVisualTile* Call = Caller != nullptr
		? Caller->GraphTiles.FindByPredicate(
			[](const FVerseVisualTile& Tile)
			{
				return Tile.ExpressionKind == EVerseExpressionKind::Call;
			})
		: nullptr;
	if (!TestNotNull(TEXT("Call with omitted default is found"), Call))
	{
		return false;
	}
	FVerseExpressionAction Provider;
	Provider.SourceForm = EVerseExpressionSourceForm::Literal;
	Provider.SourceSpelling = TEXT("2.0");
	TestTrue(*FString::Printf(TEXT("Named input materializes safely: %s"), *Error.ToString()),
		TryMaterializeVerseNamedInput(
			Session, Call->Range, TEXT("Optional"), Provider, Error));
	TestTrue(TEXT("Materialized input uses Verse named-argument syntax"),
		FString(UTF8_TO_TCHAR(*Session.GetCurrentUtf8())).Contains(
			TEXT("WithDefault(1, ?Optional := 2.0)")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVersePrimitiveDefinitionDefaultTest,
	"VerseVisualEditor.Expressions.Literals.PrimitiveDefinitionDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVersePrimitiveDefinitionDefaultTest::RunTest(const FString& Parameters)
{
	struct FExpectedDefault
	{
		const TCHAR* Type;
		const TCHAR* Source;
	};
	static const FExpectedDefault Expected[] = {
		{TEXT("logic"), TEXT("false")},
		{TEXT("int"), TEXT("0")},
		{TEXT("float"), TEXT("0.0")},
		{TEXT("string"), TEXT("\"\"")},
		{TEXT("char"), TEXT("'a'")},
	};

	for (const FExpectedDefault& Item : Expected)
	{
		const TOptional<FString> Source =
			GetDefaultVerseLiteralSourceForType(Item.Type);
		if (TestTrue(
			*FString::Printf(TEXT("%s has a canonical definition initializer"), Item.Type),
			Source.IsSet()))
		{
			TestEqual(
				*FString::Printf(TEXT("%s uses its matching literal syntax"), Item.Type),
				Source.GetValue(), FString(Item.Source));
		}
	}
	TestFalse(TEXT("A non-primitive type has no invented initializer"),
		GetDefaultVerseLiteralSourceForType(TEXT("SomeClass")).IsSet());
	return true;
}

#endif
