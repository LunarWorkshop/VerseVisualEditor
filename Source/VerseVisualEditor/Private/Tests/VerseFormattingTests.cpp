#if WITH_DEV_AUTOMATION_TESTS

#include "Document/VerseDocumentSession.h"
#include "Editing/VerseFormattingEdit.h"
#include "Editing/VerseFormattingStyle.h"
#include "VisualModel/VerseTileProperties.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

namespace VerseFormattingTests
{
	TSharedPtr<FVerseDocument> MakeDocument(
		FAutomationTestBase& Test,
		FUtf8StringView Source)
	{
		FText Error;
		const TConstArrayView<uint8> Bytes(
			reinterpret_cast<const uint8*>(Source.GetData()), Source.Len());
		TSharedPtr<FVerseDocument> Document = FVerseDocument::CreateFromBytes(Bytes, Error);
		Test.TestTrue(*FString::Printf(TEXT("Formatting source parses as UTF-8: %s"),
			*Error.ToString()), Document.IsValid());
		return Document;
	}

	const FVerseVisualTile* FindTile(
		TConstArrayView<FVerseVisualTile> Tiles,
		TFunctionRef<bool(const FVerseVisualTile&)> Predicate)
	{
		for (const FVerseVisualTile& Tile : Tiles)
		{
			if (Predicate(Tile))
			{
				return &Tile;
			}
			if (const FVerseVisualTile* Child = FindTile(Tile.Children, Predicate))
			{
				return Child;
			}
		}
		return nullptr;
	}

	TArray<FVerseVisualTile> BuildFunctionGraph(
		const FVerseDocumentSession& Session,
		FStringView Name)
	{
		const FVerseVisualTile* Function = FindTile(
			Session.GetTiles(),
			[&](const FVerseVisualTile& Tile)
			{
				return Tile.DefinitionKind == VerseSyntaxKind::Function
					&& Tile.NameRange.IsSet()
					&& Session.GetParseSnapshot().GetDocument()
						->DecodeOriginalRange(Tile.NameRange) == Name;
			});
		return Function != nullptr
			? FVerseVisualTileBuilder::BuildFunctionGraph(
				*Function, Session.GetParseSnapshot())
			: TArray<FVerseVisualTile>();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseFormattingSourceExactModelTest,
	"VerseVisualEditor.Formatting.SourceExactModel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseFormattingSourceExactModelTest::RunTest(const FString& Parameters)
{
	using namespace VerseFormattingTests;
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("VerseVisualEditor"));
	if (!TestTrue(TEXT("Plugin is available"), Plugin.IsValid()))
	{
		return false;
	}
	FText Error;
	const TSharedPtr<FVerseDocument> Document = FVerseDocument::LoadFromFile(
		FPaths::Combine(Plugin->GetBaseDir(), TEXT("Tests/Fixtures/formatting_syntax.verse")), Error);
	if (!TestTrue(*FString::Printf(TEXT("Formatting fixture loads: %s"), *Error.ToString()),
		Document.IsValid()))
	{
		return false;
	}
	FVerseDocumentSession Session(Document.ToSharedRef());
	const TArray<FVerseVisualTile> Graph = BuildFunctionGraph(Session, TEXTVIEW("FormattingFunction"));
	const FVerseVisualTile* Add = FindTile(Graph, [](const FVerseVisualTile& Tile)
	{
		return Tile.ExpressionKind == EVerseExpressionKind::BinaryOperator
			&& Tile.OperatorSpelling == TEXT("+");
	});
	if (TestNotNull(TEXT("Grouped addition is recognized"), Add))
	{
		TestEqual(TEXT("Every explicit grouping layer is retained"),
			Add->GroupingLayers.Num(), 2);
		const TArray<FVerseTileProperty> Properties =
			FVerseTileProperties::Build(*Add, Session.GetParseSnapshot());
		const FVerseTileProperty* Grouping = Properties.FindByPredicate(
			[](const FVerseTileProperty& Property)
			{
				return Property.SyntaxControl == EVerseSyntaxControlKind::GroupingLayers;
			});
		TestNull(TEXT("Statement-level expressions do not expose grouping controls"), Grouping);
		TestEqual(TEXT("Lines follow tile identity metadata"),
			Properties.IsValidIndex(1) ? Properties[1].Name : FString(), TEXT("Lines"));
		TestFalse(TEXT("Line endings are settings-only"),
			Properties.ContainsByPredicate([](const FVerseTileProperty& Property)
			{
				return Property.SyntaxControl == EVerseSyntaxControlKind::LineEnding;
			}));
	}
	const TArray<FVerseVisualTile> ConditionGraph =
		BuildFunctionGraph(Session, TEXTVIEW("FormattingCondition"));
	const FVerseVisualTile* NestedSubtract = FindTile(
		ConditionGraph, [](const FVerseVisualTile& Tile)
		{
			return Tile.OperatorSpelling == TEXT("-") && !Tile.bStatementLevel;
		});
	if (TestNotNull(TEXT("Nested subtraction exists"), NestedSubtract))
	{
		const TArray<FVerseTileProperty> NestedProperties =
			FVerseTileProperties::Build(*NestedSubtract, Session.GetParseSnapshot());
		const FVerseTileProperty* Grouping = NestedProperties.FindByPredicate(
			[](const FVerseTileProperty& Property)
			{
				return Property.SyntaxControl == EVerseSyntaxControlKind::GroupingLayers;
			});
		TestTrue(TEXT("Nested value expressions retain the grouping checkbox"),
			Grouping != nullptr
				&& Grouping->Value == TEXT("0")
				&& Grouping->Options == TArray<FString>{TEXT("0"), TEXT("1")});
	}
	TestFalse(TEXT("Reading syntax leaves the document clean"), Session.IsDirty());
	TestEqual(TEXT("Reading syntax materializes byte-identical source"),
		Session.GetCurrentUtf8(), FUtf8String(Document->GetOriginalUtf8View()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseFormattingLocalizedEditsTest,
	"VerseVisualEditor.Formatting.LocalizedEdits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseFormattingLocalizedEditsTest::RunTest(const FString& Parameters)
{
	using namespace VerseFormattingTests;
	const TSharedPtr<FVerseDocument> Document = MakeDocument(*this, UTF8TEXTVIEW(
		"Format(Input : int)<computes> : int =\n"
		"    Local : int = Abs( Input )\n"
		"    ((Local + 1))\n"));
	if (!Document.IsValid()) return false;
	FVerseDocumentSession Session(Document.ToSharedRef());

	auto FindGraphTile = [&](TFunctionRef<bool(const FVerseVisualTile&)> Predicate)
		-> const FVerseVisualTile*
	{
		static TArray<FVerseVisualTile> Graph;
		Graph = BuildFunctionGraph(Session, TEXTVIEW("Format"));
		return FindTile(Graph, Predicate);
	};

	const FVerseVisualTile* Add = FindGraphTile([](const FVerseVisualTile& Tile)
	{
		return Tile.OperatorSpelling == TEXT("+");
	});
	if (!TestNotNull(TEXT("Addition exists"), Add)) return false;
	FText Error;
	const FVerseDocumentRevision BeforeGrouping = Session.GetRevision();
	TestTrue(*FString::Printf(TEXT("Grouping edit succeeds: %s"), *Error.ToString()),
		FVerseFormattingEditService::Apply(Session, *Add,
			EVerseSyntaxControlKind::GroupingLayers, TEXTVIEW("1"), Error));
	TestEqual(TEXT("Grouping is one atomic revision"),
		Session.GetRevision().Value, BeforeGrouping.Value + 1);
	TestTrue(TEXT("Only one explicit wrapper remains"),
		FString(UTF8_TO_TCHAR(*Session.GetCurrentUtf8())).Contains(TEXT("(Local + 1)")));

	Add = FindGraphTile([](const FVerseVisualTile& Tile)
	{
		return Tile.OperatorSpelling == TEXT("+");
	});
	Error = FText::GetEmpty();
	TestTrue(*FString::Printf(TEXT("Grouping removal succeeds: %s"), *Error.ToString()),
		Add != nullptr && FVerseFormattingEditService::Apply(Session, *Add,
			EVerseSyntaxControlKind::GroupingLayers, TEXTVIEW("0"), Error));
	Add = FindGraphTile([](const FVerseVisualTile& Tile)
	{
		return Tile.OperatorSpelling == TEXT("+");
	});
	Error = FText::GetEmpty();
	TestTrue(*FString::Printf(TEXT("Grouping addition succeeds: %s"), *Error.ToString()),
		Add != nullptr && FVerseFormattingEditService::Apply(Session, *Add,
			EVerseSyntaxControlKind::GroupingLayers, TEXTVIEW("1"), Error));

	Add = FindGraphTile([](const FVerseVisualTile& Tile)
	{
		return Tile.OperatorSpelling == TEXT("+");
	});
	Error = FText::GetEmpty();
	TestTrue(*FString::Printf(TEXT("Operator spacing edit succeeds: %s"), *Error.ToString()),
		Add != nullptr && FVerseFormattingEditService::Apply(Session, *Add,
			EVerseSyntaxControlKind::OperatorSpacing, TEXTVIEW("None"), Error));
	TestTrue(TEXT("Operator edit changes only its gaps"),
		FString(UTF8_TO_TCHAR(*Session.GetCurrentUtf8())).Contains(TEXT("(Local+1)")));

	const FVerseVisualTile* Call = FindGraphTile([](const FVerseVisualTile& Tile)
	{
		return Tile.ExpressionKind == EVerseExpressionKind::Call;
	});
	Error = FText::GetEmpty();
	TestTrue(*FString::Printf(TEXT("Call layout edit succeeds: %s"), *Error.ToString()),
		Call != nullptr && FVerseFormattingEditService::Apply(Session, *Call,
			EVerseSyntaxControlKind::CallSpacing, TEXTVIEW("Standard"), Error));
	TestTrue(TEXT("Call layout removes preserved inner padding"),
		FString(UTF8_TO_TCHAR(*Session.GetCurrentUtf8())).Contains(TEXT("Abs(Input)")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseFormattingStyleEmitterTest,
	"VerseVisualEditor.Formatting.StyleEmitter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseFormattingStyleEmitterTest::RunTest(const FString& Parameters)
{
	FVerseFormattingStyleProfile Style;
	Style.LineEnding = EVerseLineEnding::CrLf;
	Style.bSpaceAroundOperators = false;
	Style.bSpaceAfterComma = true;
	TestEqual(TEXT("Emitter owns infix spacing"),
		FVerseSyntaxEmitter::Infix(TEXTVIEW("A"), TEXTVIEW("+"), TEXTVIEW("B"), Style),
		TEXT("A+B"));
	TestEqual(TEXT("Emitter owns call argument spacing"),
		FVerseSyntaxEmitter::Arguments({TEXT("A"), TEXT("B")}, false, Style),
		TEXT("(A, B)"));
	TestEqual(TEXT("Emitter owns selected line endings"),
		FVerseSyntaxEmitter::Separator(EVerseSeparatorToken::None,
			EVerseSeparatorLayout::Newline, 1, Style, TEXTVIEW("    ")),
		TEXT("\r\n\r\n    "));
	Style.BodyDelimiter = EVerseClauseDelimiter::Braces;
	Style.IndentationUnit = TEXT("    ");
	TestEqual(TEXT("Brace if templates are multiline"),
		FVerseSyntaxEmitter::IfTemplate(Style),
		TEXT("if (true?) {\r\n}"));
	Style.BodyDelimiter = EVerseClauseDelimiter::Colon;
	TestEqual(TEXT("Colon if templates contain a provisional no-op expression"),
		FVerseSyntaxEmitter::IfTemplate(Style),
		TEXT("if (true?):\r\n    block {}"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseFormattingControlEditsTest,
	"VerseVisualEditor.Formatting.ControlEdits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseFormattingControlEditsTest::RunTest(const FString& Parameters)
{
	using namespace VerseFormattingTests;
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("VerseVisualEditor"));
	FText Error;
	const TSharedPtr<FVerseDocument> Document = Plugin.IsValid()
		? FVerseDocument::LoadFromFile(
			FPaths::Combine(Plugin->GetBaseDir(), TEXT("Tests/Fixtures/formatting_syntax.verse")), Error)
		: nullptr;
	if (!TestTrue(*FString::Printf(TEXT("Control fixture loads: %s"), *Error.ToString()),
		Document.IsValid())) return false;
	FVerseDocumentSession Session(Document.ToSharedRef());
	TArray<FVerseVisualTile> Graph = BuildFunctionGraph(Session, TEXTVIEW("FormattingCondition"));
	const FVerseVisualTile* Comparison = FindTile(Graph, [](const FVerseVisualTile& Tile)
	{
		return Tile.OperatorSpelling == TEXT("<>");
	});
	TestTrue(*FString::Printf(TEXT("Condition grouping succeeds: %s"), *Error.ToString()),
		Comparison != nullptr && FVerseFormattingEditService::Apply(
			Session, *Comparison, EVerseSyntaxControlKind::GroupingLayers, TEXTVIEW("1"), Error));
	Graph = BuildFunctionGraph(Session, TEXTVIEW("FormattingCondition"));
	const FVerseVisualTile* IfTile = FindTile(Graph, [](const FVerseVisualTile& Tile)
	{
		return Tile.ControlKind == EVerseControlKind::If;
	});
	const TArray<FVerseTileProperty> Properties = IfTile != nullptr
		? FVerseTileProperties::Build(*IfTile, Session.GetParseSnapshot())
		: TArray<FVerseTileProperty>();
	const FVerseTileProperty* ConditionSyntax = Properties.FindByPredicate(
		[](const FVerseTileProperty& Property)
		{
			return Property.Name == TEXT("Condition Syntax");
		});
	TestEqual(TEXT("Condition syntax shows the selected source form"),
		ConditionSyntax != nullptr ? ConditionSyntax->Example : FString(),
		TEXT("if:\n    Condition\nthen:"));
	TestTrue(TEXT("Condition syntax offers parentheses and colon"),
		ConditionSyntax != nullptr
			&& ConditionSyntax->Value == TEXT("Colon")
			&& ConditionSyntax->Options
				== TArray<FString>{TEXT("Parentheses"), TEXT("Colon")});
	auto ExampleFor = [&Properties](const TCHAR* Name)
	{
		const FVerseTileProperty* Property = Properties.FindByPredicate(
			[Name](const FVerseTileProperty& Candidate)
			{
				return Candidate.Name == Name;
			});
		return Property != nullptr ? Property->Example : FString();
	};
	TestTrue(TEXT("If condition layout is controlled by its syntax"),
		!Properties.ContainsByPredicate([](const FVerseTileProperty& Property)
		{
			return Property.Name == TEXT("Condition Layout");
		}));
	TestFalse(TEXT("Indentation remains a file/project setting"),
		Properties.ContainsByPredicate([](const FVerseTileProperty& Property)
		{
			return Property.SyntaxControl == EVerseSyntaxControlKind::Indentation;
		}));
	TestEqual(TEXT("True-body syntax uses the then keyword"),
		ExampleFor(TEXT("True Body Syntax")), TEXT("then:"));
	TestEqual(TEXT("True-body layout previews multiple body expressions"),
		ExampleFor(TEXT("True Body Layout")),
		TEXT("then:\n    FirstExpression\n    SecondExpression"));
	TestEqual(TEXT("False-body syntax uses the else keyword"),
		ExampleFor(TEXT("False Body Syntax")), TEXT("else:"));
	TestEqual(TEXT("False-body layout previews multiple body expressions"),
		ExampleFor(TEXT("False Body Layout")),
		TEXT("else:\n    FirstExpression\n    SecondExpression"));
	Error = FText::GetEmpty();
	TestTrue(*FString::Printf(TEXT("Condition syntax succeeds: %s"), *Error.ToString()),
		IfTile != nullptr && ConditionSyntax != nullptr
			&& FVerseFormattingEditService::Apply(
				Session, *IfTile, EVerseSyntaxControlKind::ConditionSyntax,
				TEXTVIEW("Parentheses"), Error, ConditionSyntax->SyntaxRegionIndex));
	Graph = BuildFunctionGraph(Session, TEXTVIEW("FormattingCondition"));
	IfTile = FindTile(Graph, [](const FVerseVisualTile& Tile)
	{
		return Tile.ControlKind == EVerseControlKind::If;
	});
	const auto* InlineCondition = IfTile != nullptr
		? IfTile->ControlRegions.FindByPredicate([](const auto& Region)
		{
			return Region.Kind == EVerseControlRegionKind::Condition;
		})
		: nullptr;
	TestTrue(TEXT("Parenthesized condition is forced inline"),
		InlineCondition != nullptr
			&& InlineCondition->Syntax.Layout == EVerseSyntaxLayout::Inline);

	Graph = BuildFunctionGraph(Session, TEXTVIEW("FormattingParenthesizedCondition"));
	IfTile = FindTile(Graph, [](const FVerseVisualTile& Tile)
	{
		return Tile.ControlKind == EVerseControlKind::If;
	});
	const TArray<FVerseTileProperty> ParenthesizedProperties = IfTile != nullptr
		? FVerseTileProperties::Build(*IfTile, Session.GetParseSnapshot())
		: TArray<FVerseTileProperty>();
	ConditionSyntax = ParenthesizedProperties.FindByPredicate(
		[](const FVerseTileProperty& Property)
		{
			return Property.SyntaxControl == EVerseSyntaxControlKind::ConditionSyntax;
		});
	TestTrue(TEXT("Parenthesized if exposes the condition-syntax control"),
		ConditionSyntax != nullptr
			&& ConditionSyntax->Value == TEXT("Parentheses"));
	Error = FText::GetEmpty();
	TestTrue(*FString::Printf(TEXT("Parenthesized condition converts to colon syntax: %s"), *Error.ToString()),
		IfTile != nullptr && ConditionSyntax != nullptr
			&& FVerseFormattingEditService::Apply(
				Session, *IfTile, EVerseSyntaxControlKind::ConditionSyntax,
				TEXTVIEW("Colon"), Error, ConditionSyntax->SyntaxRegionIndex));
	Graph = BuildFunctionGraph(Session, TEXTVIEW("FormattingParenthesizedCondition"));
	IfTile = FindTile(Graph, [](const FVerseVisualTile& Tile)
	{
		return Tile.ControlKind == EVerseControlKind::If;
	});
	const auto* MultilineCondition = IfTile != nullptr
		? IfTile->ControlRegions.FindByPredicate([](const auto& Region)
		{
			return Region.Kind == EVerseControlRegionKind::Condition;
		})
		: nullptr;
	TestTrue(TEXT("Colon condition is forced multiline"),
		MultilineCondition != nullptr
			&& MultilineCondition->Syntax.Layout == EVerseSyntaxLayout::Multiline);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseFormattingGreedyBraceClosureTest,
	"VerseVisualEditor.Formatting.GreedyBraceClosure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseFormattingGreedyBraceClosureTest::RunTest(const FString& Parameters)
{
	using namespace VerseFormattingTests;
	const TSharedPtr<FVerseDocument> Document = MakeDocument(*this, UTF8TEXTVIEW(
		"Greedy(Input : logic) : void =\n"
		"    if:\n"
		"        Input?\n"
		"    then:\n"
		"        block {}\n"
		"        \n"
		"\n"
		"    else:\n"
		"        block {}\n"
		"\n"
		"Next() : void = {}\n"));
	if (!Document.IsValid()) return false;
	FVerseDocumentSession Session(Document.ToSharedRef());
	const TArray<FVerseVisualTile> Graph =
		BuildFunctionGraph(Session, TEXTVIEW("Greedy"));
	const FVerseVisualTile* IfTile = FindTile(Graph, [](const FVerseVisualTile& Tile)
	{
		return Tile.ControlKind == EVerseControlKind::If;
	});
	int32 BodyIndex = INDEX_NONE;
	if (IfTile != nullptr)
	{
		BodyIndex = IfTile->ControlRegions.IndexOfByPredicate([](const auto& Region)
		{
			return Region.Kind == EVerseControlRegionKind::Body;
		});
	}
	FText Error;
	TestTrue(*FString::Printf(TEXT("Colon body converts to braces: %s"), *Error.ToString()),
		IfTile != nullptr
			&& BodyIndex != INDEX_NONE
			&& FVerseFormattingEditService::Apply(
				Session, *IfTile, EVerseSyntaxControlKind::BodyDelimiter,
				TEXTVIEW("Braces"), Error, BodyIndex));
	const FString Source = FString(UTF8_TO_TCHAR(*Session.GetCurrentUtf8()));
	TestTrue(TEXT("Closing brace follows the last body expression immediately"),
		Source.Contains(TEXT("then {\n        block {}\n    }")));
	TestTrue(TEXT("Else follows the closing brace immediately"),
		Source.Contains(TEXT("block {}\n    }\n    else:")));
	TestFalse(TEXT("True-body blank lines do not remain in front of else"),
		Source.Contains(TEXT("block {}\n    }\n        \n\n    else:")));
	TestTrue(TEXT("The enclosing function's two-newline separator is preserved"),
		Source.Contains(TEXT("else:\n        block {}\n\nNext()")));
	return true;
}

#endif
