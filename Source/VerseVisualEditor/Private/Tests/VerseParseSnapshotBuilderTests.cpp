#if WITH_DEV_AUTOMATION_TESTS

#include "VerseParseSnapshotBuilder.h"
#include "VerseOperatorTyping.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

namespace VerseParseSnapshotBuilderTests
{
	TSharedPtr<FVerseDocument> LoadFixture(FAutomationTestBase& Test, const TCHAR* FileName)
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("VerseVisualEditor"));
		if (!Test.TestTrue(TEXT("VerseVisualEditor plugin is discoverable"), Plugin.IsValid()))
		{
			return nullptr;
		}

		FText Error;
		const FString FixturePath = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Tests/Fixtures"), FileName);
		TSharedPtr<FVerseDocument> Document = FVerseDocument::LoadFromFile(FixturePath, Error);
		Test.TestTrue(
			*FString::Printf(TEXT("Fixture %s loads: %s"), FileName, *Error.ToString()),
			Document.IsValid());
		return Document;
	}

	bool TestCompleteCoverage(FAutomationTestBase& Test, const FVerseParseSnapshot& Snapshot)
	{
		const TArray<FVerseSourceRegion>& Regions = Snapshot.GetSourceRegions();
		if (!Test.TestTrue(TEXT("Snapshot has at least one source region"), !Regions.IsEmpty()))
		{
			return false;
		}

		int32 Cursor = 0;
		for (int32 RegionIndex = 0; RegionIndex < Regions.Num(); ++RegionIndex)
		{
			const FVerseSourceRegion& Region = Regions[RegionIndex];
			Test.TestEqual(
				*FString::Printf(TEXT("Region %d begins at the previous region end"), RegionIndex),
				Region.Range.BeginByte,
				Cursor);
			Test.TestTrue(
				*FString::Printf(TEXT("Region %d is non-empty"), RegionIndex),
				Region.Range.NumBytes > 0);
			Test.TestEqual(
				*FString::Printf(TEXT("Region %d resolves without truncation"), RegionIndex),
				Snapshot.GetSourceView(Region).Len(),
				Region.Range.NumBytes);

			if (Region.Kind == EVerseSourceRegionKind::Raw)
			{
				Test.TestEqual(TEXT("Raw region has no syntax kind"), Region.SyntaxKind, NAME_None);
				Test.TestFalse(TEXT("Raw region has no name range"), Region.NameRange.IsSet());
				Test.TestFalse(TEXT("Raw region has no type range"), Region.TypeRange.IsSet());
				Test.TestTrue(TEXT("Raw region has no specifier ranges"), Region.SpecifierRanges.IsEmpty());
				Test.TestFalse(TEXT("Raw region has no header range"), Region.HeaderRange.IsSet());
				Test.TestFalse(TEXT("Raw region has no body range"), Region.BodyRange.IsSet());
				Test.TestTrue(TEXT("Raw region has no comment kind"), Region.CommentKind == EVerseCommentKind::None);
			}
			else if (Region.Kind == EVerseSourceRegionKind::Comment)
			{
				Test.TestEqual(TEXT("Comment region has no syntax kind"), Region.SyntaxKind, NAME_None);
				Test.TestFalse(TEXT("Comment region has no name range"), Region.NameRange.IsSet());
				Test.TestFalse(TEXT("Comment region has no type range"), Region.TypeRange.IsSet());
				Test.TestTrue(TEXT("Comment region has no specifier ranges"), Region.SpecifierRanges.IsEmpty());
				Test.TestFalse(TEXT("Comment region has no header range"), Region.HeaderRange.IsSet());
				Test.TestEqual(TEXT("Comment body is its complete source"), Region.BodyRange, Region.Range);
				Test.TestTrue(TEXT("Comment region retains its parser comment kind"), Region.CommentKind != EVerseCommentKind::None);
			}
			else
			{
				Test.TestTrue(TEXT("Typed region has a syntax kind"), !Region.SyntaxKind.IsNone());
				Test.TestTrue(TEXT("Typed region has a name range"), Region.NameRange.IsSet());
				Test.TestTrue(TEXT("Typed region has a body range"), Region.BodyRange.IsSet());
				Test.TestTrue(TEXT("Typed region has a header range"), Region.HeaderRange.IsSet());
				Test.TestTrue(TEXT("Name begins inside its typed region"), Region.NameRange.BeginByte >= Region.Range.BeginByte);
				Test.TestTrue(TEXT("Name ends inside its typed region"), Region.NameRange.EndByte() <= Region.Range.EndByte());
				for (const FVerseByteRange SpecifierRange : Region.SpecifierRanges)
				{
					Test.TestTrue(TEXT("Specifier begins inside its typed region"), SpecifierRange.BeginByte >= Region.Range.BeginByte);
					Test.TestTrue(TEXT("Specifier ends inside its typed region"), SpecifierRange.EndByte() <= Region.Range.EndByte());
				}
			}
			Cursor = Region.Range.EndByte();
		}

		Test.TestEqual(
			TEXT("Regions cover the complete source"),
			Cursor,
			Snapshot.GetDocument()->GetWholeOriginalRange().NumBytes);
		return Cursor == Snapshot.GetDocument()->GetWholeOriginalRange().NumBytes;
	}

	TArray<const FVerseSourceRegion*> TypedRegions(const FVerseParseSnapshot& Snapshot)
	{
		TArray<const FVerseSourceRegion*> Result;
		for (const FVerseSourceRegion& Region : Snapshot.GetSourceRegions())
		{
			if (Region.Kind == EVerseSourceRegionKind::Syntax)
			{
				Result.Add(&Region);
			}
		}
		return Result;
	}

	const FVerseSourceRegion* FindTypedRegion(
		const FVerseParseSnapshot& Snapshot,
		TConstArrayView<FVerseSourceRegion> Regions,
		FUtf8StringView Name)
	{
		for (const FVerseSourceRegion& Region : Regions)
		{
			if (Region.Kind == EVerseSourceRegionKind::Syntax
				&& Snapshot.GetSourceView(Region.NameRange) == Name)
			{
				return &Region;
			}
			if (const FVerseSourceRegion* Nested = FindTypedRegion(Snapshot, Region.Children, Name))
			{
				return Nested;
			}
		}
		return nullptr;
	}

	bool TestBodyCoverage(
		FAutomationTestBase& Test,
		const FVerseSourceRegion& Region,
		const TCHAR* Label)
	{
		int32 Cursor = Region.BodyRange.BeginByte;
		for (int32 Index = 0; Index < Region.Children.Num(); ++Index)
		{
			const FVerseSourceRegion& Child = Region.Children[Index];
			Test.TestEqual(
				*FString::Printf(TEXT("%s child %d begins at the coverage cursor"), Label, Index),
				Child.Range.BeginByte,
				Cursor);
			Test.TestTrue(
				*FString::Printf(TEXT("%s child %d is non-empty"), Label, Index),
				Child.Range.NumBytes > 0);
			Cursor = Child.Range.EndByte();
		}
		return Test.TestEqual(
			*FString::Printf(TEXT("%s children cover the exact body interior"), Label),
			Cursor,
			Region.BodyRange.EndByte());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseSupportedTopLevelRecognitionTest,
	"VerseVisualEditor.Foundation.TopLevelRecognition.SupportedDefinitions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseSupportedTopLevelRecognitionTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FVerseDocument> Document = VerseParseSnapshotBuilderTests::LoadFixture(
		*this,
		TEXT("top_level_supported.verse"));
	if (!Document.IsValid())
	{
		return false;
	}

	FVerseParseSnapshot Snapshot = FVerseParseSnapshotBuilder::Build(Document.ToSharedRef());
	VerseParseSnapshotBuilderTests::TestCompleteCoverage(*this, Snapshot);

	const TArray<const FVerseSourceRegion*> Typed = VerseParseSnapshotBuilderTests::TypedRegions(Snapshot);
	const TArray<FName> ExpectedKinds = {
		VerseSyntaxKind::Module,
		VerseSyntaxKind::Class,
		VerseSyntaxKind::Struct,
		VerseSyntaxKind::Interface,
		VerseSyntaxKind::Enum,
		VerseSyntaxKind::Function,
		VerseSyntaxKind::Variable,
		VerseSyntaxKind::Constant,
		VerseSyntaxKind::TypeAlias};
	const TArray<FUtf8StringView> ExpectedNames = {
		UTF8TEXTVIEW("ExampleModule"),
		UTF8TEXTVIEW("ExampleClass"),
		UTF8TEXTVIEW("ExampleStruct"),
		UTF8TEXTVIEW("ExampleInterface"),
		UTF8TEXTVIEW("ExampleEnum"),
		UTF8TEXTVIEW("ExampleFunction"),
		UTF8TEXTVIEW("MutableValue"),
		UTF8TEXTVIEW("ConstantValue"),
		UTF8TEXTVIEW("IntegerAlias")};

	if (TestEqual(TEXT("Every supported definition kind is recognized"), Typed.Num(), ExpectedKinds.Num()))
	{
		for (int32 Index = 0; Index < Typed.Num(); ++Index)
		{
			TestEqual(*FString::Printf(TEXT("Definition %d kind"), Index), Typed[Index]->SyntaxKind, ExpectedKinds[Index]);
			TestTrue(
				*FString::Printf(TEXT("Definition %d name"), Index),
				Snapshot.GetSourceView(Typed[Index]->NameRange) == ExpectedNames[Index]);
		}

		TestTrue(TEXT("Function return type is captured"), Snapshot.GetSourceView(Typed[5]->TypeRange) == UTF8TEXTVIEW("int"));
		TestTrue(TEXT("Variable type is captured"), Snapshot.GetSourceView(Typed[6]->TypeRange) == UTF8TEXTVIEW("int"));
		TestTrue(TEXT("Constant type is captured"), Snapshot.GetSourceView(Typed[7]->TypeRange) == UTF8TEXTVIEW("string"));
	}

	bool bFoundUnsupportedRaw = false;
	for (const FVerseSourceRegion& Region : Snapshot.GetSourceRegions())
	{
		if (Region.Kind == EVerseSourceRegionKind::Raw
			&& Snapshot.GetSourceView(Region).Find(UTF8TEXTVIEW("using")) != INDEX_NONE)
		{
			bFoundUnsupportedRaw = true;
		}
	}
	TestTrue(TEXT("Unsupported using expression remains raw"), bFoundUnsupportedRaw);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseErrorTolerantTopLevelRecognitionTest,
	"VerseVisualEditor.Foundation.TopLevelRecognition.ErrorTolerance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseErrorTolerantTopLevelRecognitionTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FVerseDocument> Document = VerseParseSnapshotBuilderTests::LoadFixture(
		*this,
		TEXT("top_level_error_tolerance.verse"));
	if (!Document.IsValid())
	{
		return false;
	}

	FVerseParseSnapshot Snapshot = FVerseParseSnapshotBuilder::Build(Document.ToSharedRef());
	VerseParseSnapshotBuilderTests::TestCompleteCoverage(*this, Snapshot);

	const TArray<const FVerseSourceRegion*> Typed = VerseParseSnapshotBuilderTests::TypedRegions(Snapshot);
	TestEqual(TEXT("A whole-snippet compiler parse failure exposes no guessed definitions"), Typed.Num(), 0);
	TestEqual(TEXT("Failed parse produces one usable fallback region"), Snapshot.GetSourceRegions().Num(), 1);
	TestEqual(TEXT("Failed parse fallback is raw"), Snapshot.GetSourceRegions()[0].Kind, EVerseSourceRegionKind::Raw);
	TestEqual(TEXT("Failed parse fallback covers the complete source"), Snapshot.GetSourceRegions()[0].Range, Document->GetWholeOriginalRange());
	TestTrue(
		TEXT("Invalid source remains exact raw text"),
		Snapshot.GetSourceView(Snapshot.GetSourceRegions()[0]) == Document->GetOriginalUtf8View());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseNestedBodyRangeTest,
	"VerseVisualEditor.Foundation.NestedBodies.ClauseDescriptorsAndCoverage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseNestedBodyRangeTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FVerseDocument> Document = VerseParseSnapshotBuilderTests::LoadFixture(
		*this,
		TEXT("nested_body_ranges.verse"));
	if (!Document.IsValid())
	{
		return false;
	}

	const FVerseParseSnapshot Snapshot = FVerseParseSnapshotBuilder::Build(Document.ToSharedRef());
	VerseParseSnapshotBuilderTests::TestCompleteCoverage(*this, Snapshot);
	const FVerseSourceRegion* Brace = VerseParseSnapshotBuilderTests::FindTypedRegion(
		Snapshot,
		Snapshot.GetSourceRegions(),
		UTF8TEXTVIEW("BraceContainer"));
	const FVerseSourceRegion* Colon = VerseParseSnapshotBuilderTests::FindTypedRegion(
		Snapshot,
		Snapshot.GetSourceRegions(),
		UTF8TEXTVIEW("ColonContainer"));
	const FVerseSourceRegion* NestedEmpty = VerseParseSnapshotBuilderTests::FindTypedRegion(
		Snapshot,
		Snapshot.GetSourceRegions(),
		UTF8TEXTVIEW("NestedEmpty"));
	const FVerseSourceRegion* NestedStruct = VerseParseSnapshotBuilderTests::FindTypedRegion(
		Snapshot,
		Snapshot.GetSourceRegions(),
		UTF8TEXTVIEW("NestedStruct"));

	if (TestNotNull(TEXT("Brace container is represented"), Brace))
	{
		TestEqual(TEXT("Brace punctuation style comes from VST"), Brace->BodyClause.PunctuationStyle, EVerseClausePunctuationStyle::Braces);
		TestTrue(TEXT("Brace opening punctuation is exact"), Snapshot.GetSourceView(Brace->BodyClause.OpeningPunctuationRange) == UTF8TEXTVIEW("{"));
		TestTrue(TEXT("Brace closing punctuation is exact"), Snapshot.GetSourceView(Brace->BodyClause.ClosingPunctuationRange) == UTF8TEXTVIEW("}"));
		TestEqual(TEXT("Durable body range is the descriptor interior"), Brace->BodyRange, Brace->BodyClause.InteriorRange);
		TestTrue(TEXT("Brace interior retains its leading comment"), Snapshot.GetSourceView(Brace->BodyRange).Find(UTF8TEXTVIEW("# Leading body comment.")) != INDEX_NONE);
		VerseParseSnapshotBuilderTests::TestBodyCoverage(*this, *Brace, TEXT("Brace body"));
	}
	if (TestNotNull(TEXT("Colon container is represented"), Colon))
	{
		TestEqual(TEXT("Colon punctuation style comes from VST"), Colon->BodyClause.PunctuationStyle, EVerseClausePunctuationStyle::ColonOrIndentation);
		TestTrue(TEXT("Colon opening punctuation is exact"), Snapshot.GetSourceView(Colon->BodyClause.OpeningPunctuationRange) == UTF8TEXTVIEW(":"));
		TestFalse(TEXT("Colon body has no closing punctuation"), Colon->BodyClause.ClosingPunctuationRange.IsSet());
		TestTrue(TEXT("Colon interior retains trivia immediately after the colon"), Snapshot.GetSourceView(Colon->BodyRange).StartsWith(UTF8TEXTVIEW("\n")));
		VerseParseSnapshotBuilderTests::TestBodyCoverage(*this, *Colon, TEXT("Colon body"));
	}
	if (TestNotNull(TEXT("Empty nested class is represented recursively"), NestedEmpty))
	{
		TestTrue(TEXT("Empty brace body has an exact empty interior"), NestedEmpty->BodyRange.IsSet() && NestedEmpty->BodyRange.NumBytes == 0);
		TestEqual(TEXT("Empty body insertion anchor is inside its opening brace"), NestedEmpty->BodyClause.EmptyBodyInsertionByte, NestedEmpty->BodyRange.BeginByte);
		TestTrue(TEXT("Empty body has no child coverage regions"), NestedEmpty->Children.IsEmpty());
	}
	if (TestNotNull(TEXT("Colon-nested struct is represented recursively"), NestedStruct))
	{
		TestEqual(TEXT("Nested struct retains its own clause style"), NestedStruct->BodyClause.PunctuationStyle, EVerseClausePunctuationStyle::ColonOrIndentation);
		VerseParseSnapshotBuilderTests::TestBodyCoverage(*this, *NestedStruct, TEXT("Nested struct body"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseInvalidNestedBodyRetentionTest,
	"VerseVisualEditor.Foundation.NestedBodies.InvalidSourceRetention",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseInvalidNestedBodyRetentionTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FVerseDocument> Document = VerseParseSnapshotBuilderTests::LoadFixture(
		*this,
		TEXT("nested_body_invalid.verse"));
	if (!Document.IsValid())
	{
		return false;
	}

	const FVerseParseSnapshot Snapshot = FVerseParseSnapshotBuilder::Build(Document.ToSharedRef());
	VerseParseSnapshotBuilderTests::TestCompleteCoverage(*this, Snapshot);
	TestTrue(
		TEXT("Invalid nested source remains byte-for-byte recoverable"),
		Snapshot.GetDocument()->GetOriginalUtf8View() == Document->GetOriginalUtf8View());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseFunctionRecognitionTest,
	"VerseVisualEditor.Prototype.Functions.VstMetadata",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseFunctionRecognitionTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Add has a data-driven binary signature"),
		FVerseOperatorTyping::SupportsOperandCount(TEXT("+"), 2));
	TSharedPtr<FVerseDocument> Document = VerseParseSnapshotBuilderTests::LoadFixture(
		*this,
		TEXT("functions.verse"));
	if (!Document.IsValid())
	{
		return false;
	}

	const FVerseParseSnapshot Snapshot = FVerseParseSnapshotBuilder::Build(Document.ToSharedRef());
	const FVerseSourceRegion* Function = VerseParseSnapshotBuilderTests::FindTypedRegion(
		Snapshot,
		Snapshot.GetSourceRegions(),
		UTF8TEXTVIEW("Transform"));
	if (!TestNotNull(TEXT("Function is recognized from the VST"), Function))
	{
		return false;
	}

	TestEqual(TEXT("Function kind is retained"), Function->SyntaxKind, VerseSyntaxKind::Function);
	TestTrue(TEXT("Function signature is outside its body"),
		Snapshot.GetSourceView(Function->HeaderRange).Find(UTF8TEXTVIEW("Transform")) != INDEX_NONE
		&& Snapshot.GetSourceView(Function->BodyRange).Find(UTF8TEXTVIEW("Transform")) == INDEX_NONE);
	TestEqual(TEXT("Function has two parameters"), Function->FunctionParameters.Num(), 2);
	if (Function->FunctionParameters.Num() == 2)
	{
		const FVerseFunctionParameter& Used = Function->FunctionParameters[0];
		const FVerseFunctionParameter& Unused = Function->FunctionParameters[1];
		TestTrue(TEXT("First parameter name is exact"), Snapshot.GetSourceView(Used.NameRange) == UTF8TEXTVIEW("Used"));
		TestTrue(TEXT("First parameter type is exact"), Snapshot.GetSourceView(Used.TypeRange) == UTF8TEXTVIEW("int"));
		TestEqual(TEXT("Every body reference is retained"), Used.ReferenceRanges.Num(), 2);
		TestFalse(TEXT("Second parameter is classified as unused"), Unused.IsUsed());
	}
	TestTrue(TEXT("Return type is exact"), Snapshot.GetSourceView(Function->TypeRange) == UTF8TEXTVIEW("int"));
	TestEqual(TEXT("Access and effects are classified around the parameter clause"),
		Function->FunctionAccessSpecifierRanges.Num(), 1);
	TestEqual(TEXT("One effect is retained"), Function->FunctionEffectSpecifierRanges.Num(), 1);
	TestEqual(TEXT("Function body remains one raw region"), Function->Children.Num(), 1);
	if (Function->Children.Num() == 1)
	{
		TestEqual(TEXT("Raw body covers the descriptor interior"), Function->Children[0].Range, Function->BodyRange);
		TestEqual(TEXT("Raw body is not prematurely parsed into expressions"),
			Function->Children[0].Kind,
			EVerseSourceRegionKind::Raw);
	}
	TestEqual(TEXT("Add root is represented as one clause item"),
		Function->BodyClause.Items.Num(), 1);
	if (Function->BodyClause.Items.Num() == 1)
	{
		const FVerseExpressionDescriptor& Add = Function->BodyClause.Items[0].Expression;
		TestEqual(TEXT("Binary plus uses the generic operator shape"),
			Add.Kind,
			EVerseExpressionKind::BinaryOperator);
		TestEqual(TEXT("Add retains two ordered operands"), Add.Operands.Num(), 2);
		TestTrue(TEXT("Add operator range is exact"),
			Snapshot.GetSourceView(Add.OperatorRange) == UTF8TEXTVIEW("+"));
		TestTrue(TEXT("Add type resolves to the parameter's int spelling"),
			Snapshot.GetSourceView(Add.Type.SourceRange) == UTF8TEXTVIEW("int"));
		if (Add.Operands.Num() == 2)
		{
			TestTrue(TEXT("First Add operand is exact"),
				Snapshot.GetSourceView(Add.Operands[0].Range) == UTF8TEXTVIEW("Used"));
			TestTrue(TEXT("Second Add operand is exact"),
				Snapshot.GetSourceView(Add.Operands[1].Range) == UTF8TEXTVIEW("Used"));
		}
	}

	const FVerseSourceRegion* IdentifierFunction = VerseParseSnapshotBuilderTests::FindTypedRegion(
		Snapshot,
		Snapshot.GetSourceRegions(),
		UTF8TEXTVIEW("Describe"));
	if (TestNotNull(TEXT("Identifier-body function is recognized"), IdentifierFunction)
		&& TestEqual(TEXT("Single identifier becomes one clause item"),
			IdentifierFunction->BodyClause.Items.Num(), 1))
	{
		const FVerseClauseItemDescriptor& Item = IdentifierFunction->BodyClause.Items[0];
		TestEqual(TEXT("Bare identifier is the first supported expression kind"),
			Item.Expression.Kind,
			EVerseExpressionKind::Identifier);
		TestTrue(TEXT("Identifier range is source exact"),
			Snapshot.GetSourceView(Item.Expression.Range) == UTF8TEXTVIEW("Label"));
		TestTrue(TEXT("Identifier type resolves from the function parameter"),
			Snapshot.GetSourceView(Item.Expression.Type.SourceRange) == UTF8TEXTVIEW("string"));
		TestTrue(TEXT("Single identifier is the final value position"),
			Item.bIsFinalValuePosition);
		TestEqual(TEXT("Single identifier ends its clause"),
			Item.Separator,
			EVerseClauseItemSeparator::EndOfClause);
	}

	const FVerseSourceRegion* SpacedFunction = VerseParseSnapshotBuilderTests::FindTypedRegion(
		Snapshot,
		Snapshot.GetSourceRegions(),
		UTF8TEXTVIEW("ChooseLast"));
	if (TestNotNull(TEXT("Multi-expression function is recognized"), SpacedFunction)
		&& TestEqual(TEXT("Each root identifier becomes a clause item"),
			SpacedFunction->BodyClause.Items.Num(), 2))
	{
		const FVerseClauseItemDescriptor& First = SpacedFunction->BodyClause.Items[0];
		const FVerseClauseItemDescriptor& Last = SpacedFunction->BodyClause.Items[1];
		TestTrue(TEXT("First root identifier is exact"),
			Snapshot.GetSourceView(First.Expression.Range) == UTF8TEXTVIEW("First"));
		TestEqual(TEXT("Blank source line is retained as visual spacing metadata"),
			First.ExtraBlankLineCount, 1);
		TestEqual(TEXT("Newline separator is classified from the source gap"),
			First.Separator,
			EVerseClauseItemSeparator::Newline);
		TestFalse(TEXT("First expression is not the function result position"),
			First.bIsFinalValuePosition);
		TestTrue(TEXT("Last expression is the function result position"),
			Last.bIsFinalValuePosition);
		TestTrue(TEXT("Trailing trivia preserves source through the next expression"),
			Snapshot.GetSourceView(First.TrailingTriviaRange).Find(UTF8TEXTVIEW("\n\n")) != INDEX_NONE);
	}

	auto FindOnlyExpression = [this, &Snapshot](FUtf8StringView Name) -> const FVerseExpressionDescriptor*
	{
		const FVerseSourceRegion* Region = VerseParseSnapshotBuilderTests::FindTypedRegion(
			Snapshot, Snapshot.GetSourceRegions(), Name);
		if (!TestNotNull(TEXT("Typed Add fixture exists"), Region)
			|| !TestEqual(TEXT("Fixture has one root expression"), Region->BodyClause.Items.Num(), 1))
		{
			return nullptr;
		}
		return &Region->BodyClause.Items[0].Expression;
	};
	if (const FVerseExpressionDescriptor* AddInt = FindOnlyExpression(UTF8TEXTVIEW("AddIntLiteral")))
	{
		TestEqual(TEXT("Identifier plus literal is a binary operator"),
			AddInt->Kind, EVerseExpressionKind::BinaryOperator);
		TestTrue(TEXT("Integer literal retains literal identity while its expression kind remains generic"),
			AddInt->Operands.Num() == 2
			&& AddInt->Operands[1].Kind == EVerseExpressionKind::Unsupported
			&& AddInt->Operands[1].LiteralKind == EVerseLiteralKind::Integer
			&& AddInt->Operands[1].Type.IntrinsicName == TEXT("int"));
	}
	if (const FVerseExpressionDescriptor* AddNegativeInt = FindOnlyExpression(UTF8TEXTVIEW("AddNegativeIntLiteral")))
	{
		TestTrue(TEXT("Signed integer operand is retained as one source-exact literal"),
			AddNegativeInt->Operands.Num() == 2
			&& AddNegativeInt->Operands[1].LiteralKind == EVerseLiteralKind::Integer
			&& AddNegativeInt->Operands[1].Type.IntrinsicName == TEXT("int")
			&& Snapshot.GetSourceView(AddNegativeInt->Operands[1].Range) == UTF8TEXTVIEW("-12"));
	}
	if (const FVerseExpressionDescriptor* AddFloat = FindOnlyExpression(UTF8TEXTVIEW("AddFloat")))
	{
		TestTrue(TEXT("Float Add resolves from source-backed evidence"),
			Snapshot.GetSourceView(AddFloat->Type.SourceRange) == UTF8TEXTVIEW("float")
			&& AddFloat->Operands.Num() == 2);
	}
	if (const FVerseExpressionDescriptor* AddArray = FindOnlyExpression(UTF8TEXTVIEW("AddArray")))
	{
		TestTrue(TEXT("Generic array Add retains the concrete source type"),
			Snapshot.GetSourceView(AddArray->Type.SourceRange) == UTF8TEXTVIEW("[]int"));
	}
	if (const FVerseExpressionDescriptor* Conflict = FindOnlyExpression(UTF8TEXTVIEW("AddConflict")))
	{
		TestFalse(TEXT("Conflicting local evidence remains unresolved"), Conflict->Type.IsResolved());
	}
	if (const FVerseExpressionDescriptor* Chain = FindOnlyExpression(UTF8TEXTVIEW("AddChain")))
	{
		TestEqual(TEXT("Add chains remain unsupported in this slice"),
			Chain->Kind, EVerseExpressionKind::Unsupported);
	}
	if (const FVerseExpressionDescriptor* Subtract = FindOnlyExpression(UTF8TEXTVIEW("Subtract")))
	{
		TestEqual(TEXT("Subtraction uses the generic operator shape"),
			Subtract->Kind, EVerseExpressionKind::BinaryOperator);
	}
	struct FExpectedOperator
	{
		FUtf8StringView Function;
		FUtf8StringView Spelling;
	};
	const FExpectedOperator ExpectedOperators[] = {
		{UTF8TEXTVIEW("Multiply"), UTF8TEXTVIEW("*")},
		{UTF8TEXTVIEW("Divide"), UTF8TEXTVIEW("/")},
		{UTF8TEXTVIEW("Equal"), UTF8TEXTVIEW("=")},
		{UTF8TEXTVIEW("NotEqual"), UTF8TEXTVIEW("<>")},
		{UTF8TEXTVIEW("LessThan"), UTF8TEXTVIEW("<")},
		{UTF8TEXTVIEW("LessThanOrEqual"), UTF8TEXTVIEW("<=")},
		{UTF8TEXTVIEW("GreaterThan"), UTF8TEXTVIEW(">")},
		{UTF8TEXTVIEW("GreaterThanOrEqual"), UTF8TEXTVIEW(">=")},
	};
	for (const FExpectedOperator& Expected : ExpectedOperators)
	{
		if (const FVerseExpressionDescriptor* Expression = FindOnlyExpression(Expected.Function))
		{
			TestEqual(TEXT("Binary operator uses the generic expression shape"),
				Expression->Kind, EVerseExpressionKind::BinaryOperator);
			TestTrue(TEXT("Binary operator range is source exact"),
				Snapshot.GetSourceView(Expression->OperatorRange) == Expected.Spelling);
			TestEqual(TEXT("Binary operator retains two operands"), Expression->Operands.Num(), 2);
		}
	}
	return true;
}

#endif
