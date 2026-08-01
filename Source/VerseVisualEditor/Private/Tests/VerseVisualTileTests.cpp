#if WITH_DEV_AUTOMATION_TESTS

#include "VerseParseSnapshotBuilder.h"
#include "VerseTileProperties.h"
#include "VerseTileSelection.h"
#include "VerseVisualTile.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

namespace VerseVisualTileTests
{
	TSharedPtr<FVerseDocument> LoadPluginFile(FAutomationTestBase& Test, const FString& RelativePath)
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("VerseVisualEditor"));
		if (!Test.TestTrue(TEXT("VerseVisualEditor plugin is discoverable"), Plugin.IsValid()))
		{
			return nullptr;
		}

		FText Error;
		TSharedPtr<FVerseDocument> Document = FVerseDocument::LoadFromFile(
			FPaths::Combine(Plugin->GetBaseDir(), RelativePath),
			Error);
		Test.TestTrue(
			*FString::Printf(TEXT("Plugin file %s loads: %s"), *RelativePath, *Error.ToString()),
			Document.IsValid());
		return Document;
	}

	TSharedPtr<FVerseDocument> LoadFixture(FAutomationTestBase& Test, const TCHAR* FileName)
	{
		return LoadPluginFile(Test, FPaths::Combine(TEXT("Tests/Fixtures"), FileName));
	}

	const FVerseVisualTile* FindDefinition(
		const FVerseParseSnapshot& Snapshot,
		TConstArrayView<FVerseVisualTile> Tiles,
		FUtf8StringView Name)
	{
		for (const FVerseVisualTile& Tile : Tiles)
		{
			if (Tile.Kind == EVerseVisualTileKind::Definition
				&& Snapshot.GetSourceView(Tile.NameRange) == Name)
			{
				return &Tile;
			}
			if (const FVerseVisualTile* Nested = FindDefinition(Snapshot, Tile.Children, Name))
			{
				return Nested;
			}
		}
		return nullptr;
	}

	bool HasSocket(
		const FVerseVisualTile& Tile,
		EVerseVisualSocketDirection Direction,
		EVerseVisualSocketRole Role,
		int32 Index = 0)
	{
		return Tile.FindSocket({Direction, Role, Index}) != nullptr;
	}

	bool IsConnected(
		TConstArrayView<FVerseVisualConnection> Connections,
		const FVerseVisualTile& Tile,
		const FVerseVisualSocket& Socket)
	{
		return FVerseVisualTileBuilder::IsSocketConnected(
			Connections, {Tile.Id, Socket.Id});
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseGlobalScopeTilePresentationTest,
	"VerseVisualEditor.Foundation.VisualTiles.GlobalScopePresentation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseGlobalScopeTilePresentationTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FVerseDocument> Document = VerseVisualTileTests::LoadFixture(
		*this,
		TEXT("top_level_supported.verse"));
	if (!Document.IsValid())
	{
		return false;
	}

	const FVerseParseSnapshot Snapshot = FVerseParseSnapshotBuilder::Build(Document.ToSharedRef());
	const TArray<FVerseVisualTile> Tiles = FVerseVisualTileBuilder::Build(Snapshot);
	if (!TestEqual(TEXT("Nine definitions and two meaningful raw regions are presented"), Tiles.Num(), 11))
	{
		return false;
	}

	int32 PreviousEnd = 0;
	int32 DefinitionCount = 0;
	int32 CommentCount = 0;
	int32 UnknownCount = 0;
	bool bFoundUnsupportedUsing = false;
	bool bFunctionBodyExcludesDefinition = false;
	bool bEmptyClassHasEmptyBody = false;
	for (int32 Index = 0; Index < Tiles.Num(); ++Index)
	{
		const FVerseVisualTile& Tile = Tiles[Index];
		TestTrue(*FString::Printf(TEXT("Tile %d has a source range"), Index), Tile.Range.IsSet());
		TestEqual(
			*FString::Printf(TEXT("Tile %d starts on its original one-based source line"), Index),
			Tile.FirstSourceLine,
			Document->GetOriginalLineNumber(Tile.Range.BeginByte));
		TestEqual(
			*FString::Printf(TEXT("Tile %d ends on its last occupied original source line"), Index),
			Tile.LastSourceLine,
			Document->GetOriginalLineNumber(Tile.Range.EndByte() - 1));
		TestTrue(*FString::Printf(TEXT("Tile %d follows its predecessor"), Index), Tile.Range.BeginByte >= PreviousEnd);
		PreviousEnd = Tile.Range.EndByte();

		if (Tile.Kind == EVerseVisualTileKind::Definition)
		{
			++DefinitionCount;
			TestTrue(*FString::Printf(TEXT("Tile %d has a definition kind"), Index), !Tile.DefinitionKind.IsNone());
			TestTrue(*FString::Printf(TEXT("Tile %d has a name"), Index), Tile.NameRange.IsSet());
			if (Tile.DefinitionKind == VerseSyntaxKind::Function)
			{
				const FUtf8StringView Body = Snapshot.GetSourceView(Tile.BodyRange);
				bFunctionBodyExcludesDefinition = Body.Find(UTF8TEXTVIEW("Input")) != INDEX_NONE
					&& Body.Find(UTF8TEXTVIEW("ExampleFunction")) == INDEX_NONE;
			}
			else if (Tile.DefinitionKind == VerseSyntaxKind::Class)
			{
				bEmptyClassHasEmptyBody = Tile.BodyRange.IsSet()
					&& Snapshot.GetSourceView(Tile.BodyRange).IsEmpty();
			}
		}
		else if (Tile.Kind == EVerseVisualTileKind::Comment)
		{
			++CommentCount;
			TestEqual(*FString::Printf(TEXT("Comment tile %d has no definition kind"), Index), Tile.DefinitionKind, NAME_None);
			TestFalse(*FString::Printf(TEXT("Comment tile %d has no name"), Index), Tile.NameRange.IsSet());
			TestTrue(
				*FString::Printf(TEXT("Comment tile %d retains comment text"), Index),
				Snapshot.GetSourceView(Tile.Range).Find(UTF8TEXTVIEW("#")) != INDEX_NONE);
		}
		else
		{
			++UnknownCount;
			TestEqual(*FString::Printf(TEXT("Unknown tile %d has no definition kind"), Index), Tile.DefinitionKind, NAME_None);
			TestFalse(*FString::Printf(TEXT("Unknown tile %d has no name"), Index), Tile.NameRange.IsSet());
			TestFalse(*FString::Printf(TEXT("Unknown tile %d has no type"), Index), Tile.TypeRange.IsSet());
			bFoundUnsupportedUsing |= Snapshot.GetSourceView(Tile.Range).Find(UTF8TEXTVIEW("using")) != INDEX_NONE;
		}
	}

	TestEqual(TEXT("Every supported definition is presented"), DefinitionCount, 9);
	TestEqual(TEXT("Known source comment has a dedicated tile"), CommentCount, 1);
	TestEqual(TEXT("Only unsupported syntax remains unknown"), UnknownCount, 1);
	TestTrue(TEXT("Unsupported using expression retains its source range"), bFoundUnsupportedUsing);
	TestTrue(TEXT("Function tile body excludes its surrounding definition"), bFunctionBodyExcludesDefinition);
	TestTrue(TEXT("Empty class tile has an empty body"), bEmptyClassHasEmptyBody);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseRawTilePresentationTest,
	"VerseVisualEditor.Foundation.VisualTiles.RawFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseRawTilePresentationTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FVerseDocument> Document = VerseVisualTileTests::LoadFixture(
		*this,
		TEXT("top_level_error_tolerance.verse"));
	if (!Document.IsValid())
	{
		return false;
	}

	const FVerseParseSnapshot Snapshot = FVerseParseSnapshotBuilder::Build(Document.ToSharedRef());
	const TArray<FVerseVisualTile> Tiles = FVerseVisualTileBuilder::Build(Snapshot);
	if (TestEqual(TEXT("Failed parsing still produces one visual tile"), Tiles.Num(), 1))
	{
		TestTrue(TEXT("Failed parsing is presented as unknown"), Tiles[0].Kind == EVerseVisualTileKind::Unknown);
		TestEqual(
			TEXT("Unknown tile retains the complete source range"),
			Tiles[0].Range,
			FVerseTextRange({}, Document->GetWholeOriginalRange()));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseNestedModulePresentationTest,
	"VerseVisualEditor.Foundation.VisualTiles.NestedModules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseNestedModulePresentationTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FVerseDocument> Document = VerseVisualTileTests::LoadFixture(
		*this,
		TEXT("nested_modules.verse"));
	if (!Document.IsValid())
	{
		return false;
	}

	const FVerseParseSnapshot Snapshot = FVerseParseSnapshotBuilder::Build(Document.ToSharedRef());
	const FVerseDocumentRevision Revision{17};
	const TArray<FVerseVisualTile> Tiles = FVerseVisualTileBuilder::Build(Snapshot, Revision);
	const FVerseVisualTile* Root = VerseVisualTileTests::FindDefinition(Snapshot, Tiles, UTF8TEXTVIEW("RootModule"));
	const FVerseVisualTile* Nested = VerseVisualTileTests::FindDefinition(Snapshot, Tiles, UTF8TEXTVIEW("NestedModule"));
	const FVerseVisualTile* Deep = VerseVisualTileTests::FindDefinition(Snapshot, Tiles, UTF8TEXTVIEW("DeepModule"));

	if (TestNotNull(TEXT("Root module tile exists"), Root))
	{
		TestEqual(TEXT("Root module is a module"), Root->DefinitionKind, VerseSyntaxKind::Module);
		TestEqual(TEXT("Root module retains its revision"), Root->Range.Revision, Revision);
		TestEqual(TEXT("Root module has one VST specifier"), Root->SpecifierRanges.Num(), 1);
		if (Root->SpecifierRanges.Num() == 1)
		{
			TestTrue(TEXT("Root module displays the public specifier source"), Snapshot.GetSourceView(Root->SpecifierRanges[0]) == UTF8TEXTVIEW("public"));
		}
		TestTrue(TEXT("Root module has nested visual children"), !Root->Children.IsEmpty());
		TestTrue(TEXT("Root module header contains its declaration"), Snapshot.GetSourceView(Root->HeaderRange).Find(UTF8TEXTVIEW("RootModule<public> := module")) != INDEX_NONE);
		TestTrue(TEXT("Root module header excludes its body"), Snapshot.GetSourceView(Root->HeaderRange).Find(UTF8TEXTVIEW("NestedModule")) == INDEX_NONE);
		const TArray<FVerseTileProperty> Properties = FVerseTileProperties::Build(*Root, Snapshot);
		const FVerseTileProperty* SpecifierProperty = Properties.FindByPredicate([](const FVerseTileProperty& Property)
		{
			return Property.Name == TEXT("Effects / Specifiers");
		});
		TestTrue(
			TEXT("Module effects/specifiers are exposed in Details"),
			SpecifierProperty != nullptr && SpecifierProperty->Value == TEXT("<public>"));
	}
	if (TestNotNull(TEXT("Brace-style nested module tile exists"), Nested))
	{
		TestEqual(TEXT("Nested module retains brace punctuation"), Nested->BodyClause.PunctuationStyle, EVerseClausePunctuationStyle::Braces);
		TestEqual(TEXT("Nested module has one VST specifier"), Nested->SpecifierRanges.Num(), 1);
		if (Nested->SpecifierRanges.Num() == 1)
		{
			TestTrue(TEXT("Nested module displays the internal specifier source"), Snapshot.GetSourceView(Nested->SpecifierRanges[0]) == UTF8TEXTVIEW("internal"));
		}

		const TArray<FName> ExpectedKinds = {
			VerseSyntaxKind::Class,
			VerseSyntaxKind::Struct,
			VerseSyntaxKind::Interface,
			VerseSyntaxKind::Enum,
			VerseSyntaxKind::Function,
			VerseSyntaxKind::Variable,
			VerseSyntaxKind::Constant,
			VerseSyntaxKind::TypeAlias};
		for (const FName ExpectedKind : ExpectedKinds)
		{
			TestTrue(
				*FString::Printf(TEXT("Nested module contains %s"), *ExpectedKind.ToString()),
				Nested->Children.ContainsByPredicate([ExpectedKind](const FVerseVisualTile& Tile)
				{
					return Tile.Kind == EVerseVisualTileKind::Definition
						&& Tile.DefinitionKind == ExpectedKind;
				}));
		}
	}
	if (TestNotNull(TEXT("Arbitrarily deep nested module tile exists"), Deep))
	{
		TestEqual(TEXT("Deep module retains the current revision"), Deep->Range.Revision, Revision);
		TestTrue(TEXT("Deep module range is independently selectable"), Deep->Range.IsSet());
		FVerseTileSelection Selection;
		Selection.Select(Deep->Range);
		TestTrue(TEXT("Single-tile selection accepts a nested module"), Selection.IsSelected(Deep->Range));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseGlobalScopeStableFixtureTest,
	"VerseVisualEditor.Foundation.VisualTiles.StableGlobalScopeFixture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseGlobalScopeStableFixtureTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FVerseDocument> Document = VerseVisualTileTests::LoadFixture(
		*this,
		TEXT("global_scope_visual_tiles.verse"));
	if (!Document.IsValid())
	{
		return false;
	}

	const FVerseParseSnapshot Snapshot = FVerseParseSnapshotBuilder::Build(Document.ToSharedRef());
	const TArray<FVerseVisualTile> Tiles = FVerseVisualTileBuilder::Build(Snapshot);
	TMap<FName, int32> DefinitionCounts;
	int32 CommentCount = 0;
	int32 UnknownCount = 0;
	int32 ModuleOneFirstLine = INDEX_NONE;
	int32 ModuleOneLastLine = INDEX_NONE;
	int32 ModuleTwoFirstLine = INDEX_NONE;
	int32 ModuleTwoLastLine = INDEX_NONE;
	const FVerseVisualTile* ModuleOneTile = nullptr;
	TArray<const FVerseVisualTile*> Comments;
	for (const FVerseVisualTile& Tile : Tiles)
	{
		if (Tile.Kind == EVerseVisualTileKind::Definition)
		{
			++DefinitionCounts.FindOrAdd(Tile.DefinitionKind);
			const FString Name = Document->DecodeOriginalRange(Tile.NameRange);
			if (Name == TEXT("FixtureModuleOne"))
			{
				ModuleOneTile = &Tile;
				ModuleOneFirstLine = Tile.FirstSourceLine;
				ModuleOneLastLine = Tile.LastSourceLine;
			}
			else if (Name == TEXT("FixtureModuleTwo"))
			{
				ModuleTwoFirstLine = Tile.FirstSourceLine;
				ModuleTwoLastLine = Tile.LastSourceLine;
			}
		}
		else if (Tile.Kind == EVerseVisualTileKind::Comment)
		{
			++CommentCount;
			Comments.Add(&Tile);
		}
		else
		{
			++UnknownCount;
		}
	}

	const TArray<FName> ExpectedKinds = {
		VerseSyntaxKind::Module,
		VerseSyntaxKind::Class,
		VerseSyntaxKind::Struct,
		VerseSyntaxKind::Interface,
		VerseSyntaxKind::Enum,
		VerseSyntaxKind::Function,
		VerseSyntaxKind::Constant,
		VerseSyntaxKind::TypeAlias};
	for (const FName Kind : ExpectedKinds)
	{
		TestEqual(*FString::Printf(TEXT("Corpus contains two %s definitions"), *Kind.ToString()), DefinitionCounts.FindRef(Kind), 2);
	}
	TestEqual(TEXT("Corpus contains two dedicated comments"), CommentCount, 2);
	TestEqual(TEXT("Valid corpus contains no unknown tiles"), UnknownCount, 0);
	TestEqual(TEXT("Single-line module starts on line 5"), ModuleOneFirstLine, 5);
	TestEqual(TEXT("Single-line module ends on line 5"), ModuleOneLastLine, 5);
	TestEqual(TEXT("Multi-line module starts on line 6"), ModuleTwoFirstLine, 6);
	TestEqual(TEXT("Multi-line module ends on line 16"), ModuleTwoLastLine, 16);
	if (TestNotNull(TEXT("Single-line module tile is available for selection and properties"), ModuleOneTile))
	{
		FVerseTileSelection Selection;
		Selection.Select(ModuleOneTile->Range);
		TestTrue(TEXT("Selected module is the only selected tile"), Selection.IsSelected(ModuleOneTile->Range));
		Selection.Select(Tiles.Last().Range);
		TestFalse(TEXT("Selecting another tile replaces the previous selection"), Selection.IsSelected(ModuleOneTile->Range));
		TestTrue(TEXT("Replacement tile is selected"), Selection.IsSelected(Tiles.Last().Range));
		Selection.Clear();
		TestFalse(TEXT("Clearing selection leaves no tile selected"), Selection.GetSelectedRange().IsSet());

		const TArray<FVerseTileProperty> Properties = FVerseTileProperties::Build(*ModuleOneTile, Snapshot);
		auto FindProperty = [&Properties](const TCHAR* Name) -> const FVerseTileProperty*
		{
			return Properties.FindByPredicate([Name](const FVerseTileProperty& Property)
			{
				return Property.Name == Name;
			});
		};
		const FVerseTileProperty* NameProperty = FindProperty(TEXT("Name"));
		const FVerseTileProperty* LinesProperty = FindProperty(TEXT("Lines"));
		TestTrue(
			TEXT("Definition properties expose the original name"),
			NameProperty && NameProperty->Value == TEXT("FixtureModuleOne"));
		TestTrue(
			TEXT("Definition properties expose its source lines"),
			LinesProperty && LinesProperty->Value == TEXT("L5"));
		TestTrue(
			TEXT("Property filter matches values case-insensitively"),
			NameProperty && FVerseTileProperties::MatchesFilter(*NameProperty, TEXT("moduleone")));
		TestFalse(
			TEXT("Property filter rejects unrelated text"),
			NameProperty && FVerseTileProperties::MatchesFilter(*NameProperty, TEXT("not present")));
	}
	if (TestEqual(TEXT("Comment stack has a line group and a block comment"), Comments.Num(), 2))
	{
		const FUtf8StringView LineGroup = Snapshot.GetSourceView(Comments[0]->BodyRange);
		const FUtf8StringView BlockComment = Snapshot.GetSourceView(Comments[1]->BodyRange);
		TestEqual(TEXT("Merged line-comment tile starts on line 1"), Comments[0]->FirstSourceLine, 1);
		TestEqual(TEXT("Merged line-comment tile ends on line 2"), Comments[0]->LastSourceLine, 2);
		TestEqual(TEXT("Block-comment tile starts on line 3"), Comments[1]->FirstSourceLine, 3);
		TestEqual(TEXT("Block-comment tile ends on line 3"), Comments[1]->LastSourceLine, 3);
		TestTrue(TEXT("Adjacent hashtag comments merge into one visual tile"),
			LineGroup.Find(UTF8TEXTVIEW("first comment")) != INDEX_NONE
			&& LineGroup.Find(UTF8TEXTVIEW("continuation")) != INDEX_NONE);
		TestTrue(TEXT("Container comment remains its own visual tile"),
			Comments[1]->CommentKind == EVerseCommentKind::Block
			&& BlockComment.Find(UTF8TEXTVIEW("<#")) != INDEX_NONE);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseFunctionTilePresentationTest,
	"VerseVisualEditor.Prototype.Functions.VisualTile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseFunctionTilePresentationTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FVerseDocument> Document = VerseVisualTileTests::LoadFixture(*this, TEXT("functions.verse"));
	if (!Document.IsValid())
	{
		return false;
	}

	const FVerseParseSnapshot Snapshot = FVerseParseSnapshotBuilder::Build(Document.ToSharedRef());
	const FVerseDocumentRevision Revision{23};
	const TArray<FVerseVisualTile> Tiles = FVerseVisualTileBuilder::Build(Snapshot, Revision);
	const FVerseVisualTile* Function = VerseVisualTileTests::FindDefinition(
		Snapshot,
		Tiles,
		UTF8TEXTVIEW("Transform"));
	if (!TestNotNull(TEXT("Function visual tile exists"), Function))
	{
		return false;
	}

	if (TestEqual(TEXT("Function has two visual parameters"), Function->FunctionParameters.Num(), 2))
	{
		TestEqual(TEXT("Function parameters retain the tile revision"),
			Function->FunctionParameters[0].NameRange.Revision,
			Revision);
		TestTrue(TEXT("Used parameter exposes hover reference locations"),
			Function->FunctionParameters[0].IsUsed()
			&& Function->FunctionParameters[0].ReferenceRanges.Num() == 2);
		TestTrue(TEXT("Unused parameter remains visibly distinguishable"),
			!Function->FunctionParameters[1].IsUsed());
	}
	TestTrue(TEXT("Raw body is a child unknown tile"),
		Function->Children.Num() == 1
		&& Function->Children[0].Kind == EVerseVisualTileKind::Unknown
		&& Function->Children[0].Range == Function->BodyRange);

	const TArray<FVerseVisualTile> GraphTiles =
		FVerseVisualTileBuilder::BuildFunctionGraph(*Function, Snapshot);
	const TArray<FVerseVisualConnection> GraphConnections =
		FVerseVisualTileBuilder::BuildConnections(GraphTiles);
	if (TestEqual(TEXT("Function graph uses entry, expression, and return visual tiles"), GraphTiles.Num(), 3))
	{
		TestTrue(TEXT("Function entry uses the shared visual tile model"),
			GraphTiles[0].Kind == EVerseVisualTileKind::FunctionEntry
			&& VerseVisualTileTests::HasSocket(GraphTiles[0],
				EVerseVisualSocketDirection::Output, EVerseVisualSocketRole::Execution)
			&& FVerseVisualTileBuilder::IsSocketConnected(GraphConnections,
				{GraphTiles[0].Id, {EVerseVisualSocketDirection::Output,
					EVerseVisualSocketRole::Execution, 0}})
			&& GraphTiles[0].GetValueOutputs().Num() == 2);
		TestTrue(TEXT("Function body expression uses the shared visual tile model"),
			GraphTiles[1].Kind == EVerseVisualTileKind::Expression
			&& GraphTiles[1].ExpressionKind == EVerseExpressionKind::BinaryOperator
			&& GraphTiles[1].OperatorRange.IsSet()
			&& Snapshot.GetDocument()->DecodeOriginalRange(GraphTiles[1].OperatorRange) == TEXT("+")
			&& VerseVisualTileTests::HasSocket(GraphTiles[1],
				EVerseVisualSocketDirection::Input, EVerseVisualSocketRole::Execution)
			&& VerseVisualTileTests::HasSocket(GraphTiles[1],
				EVerseVisualSocketDirection::Output, EVerseVisualSocketRole::Execution)
			&& FVerseVisualTileBuilder::IsSocketConnected(GraphConnections,
				{GraphTiles[1].Id, {EVerseVisualSocketDirection::Input,
					EVerseVisualSocketRole::Execution, 0}})
			&& GraphTiles[1].bImplicitReturnValue
			&& GraphTiles[1].GetValueInputs().Num() == 2
			&& VerseVisualTileTests::IsConnected(GraphConnections,
				GraphTiles[1], GraphTiles[1].GetValueInputs()[0])
			&& VerseVisualTileTests::IsConnected(GraphConnections,
				GraphTiles[1], GraphTiles[1].GetValueInputs()[1])
			&& GraphTiles[1].GetValueOutputs().Num() == 1
			&& VerseVisualTileTests::IsConnected(GraphConnections,
				GraphTiles[1], GraphTiles[1].GetValueOutputs()[0])
			&& GraphTiles[1].Children.Num() == 2
			&& GraphTiles[1].Range.Revision == Revision);
		if (GraphTiles[1].Children.Num() == 2)
		{
			TestTrue(TEXT("Upper-left operand is an identifier value source"),
				GraphTiles[1].Children[0].ExpressionKind == EVerseExpressionKind::Identifier
				&& !VerseVisualTileTests::HasSocket(GraphTiles[1].Children[0],
					EVerseVisualSocketDirection::Input, EVerseVisualSocketRole::Execution)
				&& !VerseVisualTileTests::HasSocket(GraphTiles[1].Children[0],
					EVerseVisualSocketDirection::Output, EVerseVisualSocketRole::Execution)
				&& GraphTiles[1].Children[0].GetValueOutputs().Num() == 1
				&& VerseVisualTileTests::IsConnected(GraphConnections,
					GraphTiles[1].Children[0], GraphTiles[1].Children[0].GetValueOutputs()[0]));
			TestTrue(TEXT("Second operand is another identifier value source"),
				GraphTiles[1].Children[1].ExpressionKind == EVerseExpressionKind::Identifier
				&& GraphTiles[1].Children[1].GetValueOutputs().Num() == 1);
		}
		TestTrue(TEXT("Function return uses the shared visual tile model"),
			GraphTiles[2].Kind == EVerseVisualTileKind::FunctionReturn
			&& !VerseVisualTileTests::HasSocket(GraphTiles[2],
				EVerseVisualSocketDirection::Input, EVerseVisualSocketRole::Execution)
			&& GraphTiles[2].GetValueInputs().Num() == 1
			&& VerseVisualTileTests::IsConnected(GraphConnections,
				GraphTiles[2], GraphTiles[2].GetValueInputs()[0]));
	}

	const FVerseVisualTile* IntLiteralFunction = VerseVisualTileTests::FindDefinition(
		Snapshot,
		Tiles,
		UTF8TEXTVIEW("AddIntLiteral"));
	if (TestNotNull(TEXT("Integer-literal Add visual tile exists"), IntLiteralFunction))
	{
		const TArray<FVerseVisualTile> IntGraph =
			FVerseVisualTileBuilder::BuildFunctionGraph(*IntLiteralFunction, Snapshot);
		if (TestEqual(TEXT("Integer-literal Add graph has three root tiles"), IntGraph.Num(), 3)
			&& TestEqual(TEXT("Integer-literal Add has two child operands"), IntGraph[1].Children.Num(), 2))
		{
			TestTrue(TEXT("Literal is represented by an unconnected inline editor on its parent input"),
				IntGraph[1].Children[1].ExpressionKind == EVerseExpressionKind::Literal
				&& IntGraph[1].Children[1].LiteralKind == EVerseLiteralKind::Integer
				&& IntGraph[1].Children[1].IntrinsicTypeName == TEXT("int")
				&& IntGraph[1].Children[1].GetValueOutputs().IsEmpty()
				&& IntGraph[1].GetValueInputs().Num() == 2
				&& IntGraph[1].GetValueInputs()[1].InlineLiteralKind == EVerseLiteralKind::Integer
				&& IntGraph[1].GetValueInputs()[1].InlineLiteralRange == IntGraph[1].Children[1].Range);
		}
	}

	const FVerseVisualTile* NegativeIntLiteralFunction = VerseVisualTileTests::FindDefinition(
		Snapshot,
		Tiles,
		UTF8TEXTVIEW("AddNegativeIntLiteral"));
	if (TestNotNull(TEXT("Negative integer-literal Add visual tile exists"), NegativeIntLiteralFunction))
	{
		const TArray<FVerseVisualTile> NegativeIntGraph =
			FVerseVisualTileBuilder::BuildFunctionGraph(*NegativeIntLiteralFunction, Snapshot);
		if (TestEqual(TEXT("Negative integer-literal Add graph has three root tiles"), NegativeIntGraph.Num(), 3))
		{
			TestTrue(TEXT("Negative literal is represented by the parent's inline editor"),
				NegativeIntGraph[1].GetValueInputs().Num() == 2
				&& NegativeIntGraph[1].GetValueInputs()[1].InlineLiteralKind == EVerseLiteralKind::Integer
				&& Snapshot.GetDocument()->DecodeOriginalRange(
					NegativeIntGraph[1].GetValueInputs()[1].InlineLiteralRange) == TEXT("-12"));
		}
	}

	const FVerseVisualTile* CallFunction = VerseVisualTileTests::FindDefinition(
		Snapshot,
		Tiles,
		UTF8TEXTVIEW("CallAbsolute"));
	if (TestNotNull(TEXT("Call visual tile exists"), CallFunction))
	{
		const TArray<FVerseVisualTile> CallGraph =
			FVerseVisualTileBuilder::BuildFunctionGraph(*CallFunction, Snapshot);
		if (TestEqual(TEXT("Call graph has entry, call, and return tiles"), CallGraph.Num(), 3))
		{
			TestTrue(TEXT("Call uses one generic compiler-bindable visual shape"),
				CallGraph[1].ExpressionKind == EVerseExpressionKind::Call
				&& Snapshot.GetDocument()->DecodeOriginalRange(CallGraph[1].NameRange) == TEXT("Abs")
				&& CallGraph[1].GetValueInputs().Num() == 1
				&& CallGraph[1].GetValueOutputs().Num() == 1
				&& CallGraph[1].Children.Num() == 1
				&& CallGraph[1].Children[0].ExpressionKind == EVerseExpressionKind::Identifier);
		}
	}

	const FVerseVisualTile* IfFunction = VerseVisualTileTests::FindDefinition(
		Snapshot, Tiles, UTF8TEXTVIEW("ControlIf"));
	if (TestNotNull(TEXT("If control visual tile exists"), IfFunction))
	{
		const TArray<FVerseVisualTile> IfGraph =
			FVerseVisualTileBuilder::BuildFunctionGraph(*IfFunction, Snapshot);
		TestTrue(TEXT("If uses one control tile with separate nested regions"),
			IfGraph.Num() == 3
			&& IfGraph[1].ExpressionKind == EVerseExpressionKind::Control
			&& IfGraph[1].ControlKind == EVerseControlKind::If
			&& IfGraph[1].ControlRegions.Num() == 3
			&& IfGraph[1].Children.Num() >= 3
			&& IfGraph[1].Children.ContainsByPredicate([](const FVerseVisualTile& Child)
			{
				return VerseVisualTileTests::HasSocket(Child,
						EVerseVisualSocketDirection::Input, EVerseVisualSocketRole::Execution)
					&& VerseVisualTileTests::HasSocket(Child,
						EVerseVisualSocketDirection::Output, EVerseVisualSocketRole::Execution);
			}));
		if (IfGraph.Num() == 3 && IfGraph[1].ControlRegions.Num() == 3)
		{
			const FVerseVisualTile& IfTile = IfGraph[1];
			const int32 ConditionIndex = IfTile.ControlRegions[0].FirstOperandIndex;
			TestTrue(TEXT("If no longer synthesizes a Boolean condition input"),
				IfTile.GetValueInputs().IsEmpty());
			if (TestTrue(TEXT("If owns one reusable failable predicate block"),
				IfTile.Children.IsValidIndex(ConditionIndex)))
			{
				const FVerseVisualTile& Predicate = IfTile.Children[ConditionIndex];
				TestTrue(TEXT("If predicate is contained and discards its final value"),
					Predicate.Kind == EVerseVisualTileKind::FailableBlock
					&& VerseVisualTileTests::HasSocket(Predicate,
						EVerseVisualSocketDirection::Output,
						EVerseVisualSocketRole::ClauseInsertion)
					&& Predicate.GetValueOutputs().IsEmpty()
					&& Predicate.VstNodeType == IfTile.VstNodeType
					&& Predicate.VstTag == IfTile.VstTag
					&& Predicate.Children.Num() == 1);
				if (!Predicate.Children.IsEmpty())
				{
					TestTrue(TEXT("Predicate expressions form an internal execution chain"),
						VerseVisualTileTests::HasSocket(Predicate.Children[0],
							EVerseVisualSocketDirection::Input, EVerseVisualSocketRole::Execution)
						&& VerseVisualTileTests::HasSocket(Predicate.Children[0],
							EVerseVisualSocketDirection::Output, EVerseVisualSocketRole::Execution));
				}
				TestTrue(TEXT("Predicate descriptor remains revision-specific and source exact"),
					Predicate.Range.Revision == IfTile.Range.Revision
					&& Predicate.ControlRegions.Num() == 1
					&& Predicate.ControlRegions[0].Range == Predicate.Range
					&& Predicate.ControlRegions[0].Items.Num() == 1);
			}
		}
	}
	const FVerseVisualTile* MultipleIfFunction = VerseVisualTileTests::FindDefinition(
		Snapshot, Tiles, UTF8TEXTVIEW("ControlIfMultiple"));
	if (TestNotNull(TEXT("Multiple-predicate if fixture exists"), MultipleIfFunction))
	{
		const TArray<FVerseVisualTile> Graph =
			FVerseVisualTileBuilder::BuildFunctionGraph(*MultipleIfFunction, Snapshot);
		if (TestTrue(TEXT("Multiple predicates use one contained failable block"),
			Graph.Num() == 3 && Graph[1].Children.Num() >= 1))
		{
			const FVerseVisualTile& Predicate = Graph[1].Children[0];
			TestTrue(TEXT("Every ordered predicate expression remains in the block"),
				Predicate.Kind == EVerseVisualTileKind::FailableBlock
					&& Predicate.Children.Num() == 2
					&& Predicate.ControlRegions.Num() == 1
					&& Predicate.ControlRegions[0].Items.Num() == 2
					&& Predicate.ControlRegions[0].Items[0].Separator
						== EVerseClauseItemSeparator::Semicolon);
		}
	}

	const FVerseVisualTile* BraceIfFunction = VerseVisualTileTests::FindDefinition(
		Snapshot, Tiles, UTF8TEXTVIEW("ControlIfBraces"));
	if (TestNotNull(TEXT("Brace-form if fixture exists"), BraceIfFunction))
	{
		const TArray<FVerseVisualTile> Graph =
			FVerseVisualTileBuilder::BuildFunctionGraph(*BraceIfFunction, Snapshot);
		const FVerseVisualExpressionDescriptor::FControlRegion* Body =
			Graph.Num() == 3
				? Graph[1].ControlRegions.FindByPredicate(
					[](const FVerseVisualExpressionDescriptor::FControlRegion& Region)
					{
						return Region.Kind == EVerseControlRegionKind::Body;
					})
				: nullptr;
		TestTrue(TEXT("Brace-form if survives into the revision-specific tile tree"),
			Body != nullptr
				&& Body->PunctuationStyle == EVerseClausePunctuationStyle::Braces
				&& Body->OpeningPunctuationRange.Revision == Graph[1].Range.Revision);
	}

	const FVerseVisualTile* NestedIfFunction = VerseVisualTileTests::FindDefinition(
		Snapshot, Tiles, UTF8TEXTVIEW("ControlIfNested"));
	if (TestNotNull(TEXT("Nested-if fixture exists"), NestedIfFunction))
	{
		const TArray<FVerseVisualTile> Graph =
			FVerseVisualTileBuilder::BuildFunctionGraph(*NestedIfFunction, Snapshot);
		const FVerseVisualTile* NestedIf = Graph.Num() == 3
			? Graph[1].Children.FindByPredicate([](const FVerseVisualTile& Child)
				{
					return Child.Kind == EVerseVisualTileKind::Expression
						&& Child.ControlKind == EVerseControlKind::If;
				})
			: nullptr;
		TestTrue(TEXT("Nested if owns its own nested failable predicate block"),
			NestedIf != nullptr
				&& NestedIf->Children.ContainsByPredicate([](const FVerseVisualTile& Child)
				{
					return Child.Kind == EVerseVisualTileKind::FailableBlock;
				}));
	}
	if (IfFunction != nullptr && !IfFunction->BodyClause.Items.IsEmpty())
	{
		FVerseVisualTile EmptyPredicateFunction = *IfFunction;
		FVerseVisualExpressionDescriptor& IfDescriptor =
			EmptyPredicateFunction.BodyClause.Items[0].Expression;
		FVerseVisualExpressionDescriptor::FControlRegion* PredicateRegion =
			IfDescriptor.ControlRegions.FindByPredicate(
				[](const FVerseVisualExpressionDescriptor::FControlRegion& Region)
				{
					return Region.Kind == EVerseControlRegionKind::Condition;
				});
		if (PredicateRegion != nullptr && PredicateRegion->OperandCount == 1)
		{
			IfDescriptor.Operands.RemoveAt(PredicateRegion->FirstOperandIndex);
			PredicateRegion->OperandCount = 0;
			PredicateRegion->Items.Reset();
			for (FVerseVisualExpressionDescriptor::FControlRegion& Region :
				IfDescriptor.ControlRegions)
			{
				if (Region.Kind != EVerseControlRegionKind::Condition)
				{
					--Region.FirstOperandIndex;
				}
			}
			const TArray<FVerseVisualTile> EmptyGraph =
				FVerseVisualTileBuilder::BuildFunctionGraph(
					EmptyPredicateFunction,
					Snapshot);
			TestTrue(TEXT("An empty predicate still produces an editable failable block"),
				EmptyGraph.Num() == 3
					&& !EmptyGraph[1].Children.IsEmpty()
					&& EmptyGraph[1].Children[0].Kind
						== EVerseVisualTileKind::FailableBlock
					&& EmptyGraph[1].Children[0].Children.IsEmpty()
					&& VerseVisualTileTests::HasSocket(
						EmptyGraph[1].Children[0],
						EVerseVisualSocketDirection::Output,
						EVerseVisualSocketRole::ClauseInsertion));
		}
	}

	const FVerseVisualTile* LocalFunction = VerseVisualTileTests::FindDefinition(
		Snapshot, Tiles, UTF8TEXTVIEW("LocalDefinitions"));
	if (TestNotNull(TEXT("Local-definition visual fixture exists"), LocalFunction))
	{
		const TArray<FVerseVisualTile> LocalGraph =
			FVerseVisualTileBuilder::BuildFunctionGraph(*LocalFunction, Snapshot);
		if (TestTrue(TEXT("Local definitions become statement-level graph tiles"),
			LocalGraph.Num() == 5
			&& LocalGraph[1].Kind == EVerseVisualTileKind::Definition
			&& LocalGraph[1].DefinitionKind == VerseSyntaxKind::Variable
			&& LocalGraph[2].Kind == EVerseVisualTileKind::Definition
			&& LocalGraph[2].DefinitionKind == VerseSyntaxKind::Constant))
		{
			TestTrue(TEXT("Variable initializer connects to its typed left input"),
				VerseVisualTileTests::HasSocket(LocalGraph[1],
					EVerseVisualSocketDirection::Input, EVerseVisualSocketRole::Execution)
				&& LocalGraph[1].GetValueInputs().Num() == 1
				&& Snapshot.GetDocument()->DecodeOriginalRange(
					LocalGraph[1].GetValueInputs()[0].TypeRange) == TEXT("int")
				&& LocalGraph[1].Children.Num() == 1
				&& LocalGraph[1].Children[0].GetValueOutputs().Num() == 1);
			TestTrue(TEXT("Literal local initializer uses the shared inline editor"),
				LocalGraph[2].GetValueInputs().Num() == 1
				&& LocalGraph[2].GetValueInputs()[0].InlineLiteralKind
					== EVerseLiteralKind::Integer);
			const TArray<FVerseTileProperty> VariableProperties =
				FVerseTileProperties::Build(LocalGraph[1], Snapshot);
			TestTrue(TEXT("Local variable type is editable through the type selector"),
				VariableProperties.ContainsByPredicate(
					[&LocalGraph](const FVerseTileProperty& Property)
					{
						return Property.EditKind == EVerseTilePropertyEditKind::Type
							&& Property.EditRange == LocalGraph[1].TypeRange;
					}));
			const TArray<FVerseTileProperty> ConstantProperties =
				FVerseTileProperties::Build(LocalGraph[2], Snapshot);
			TestTrue(TEXT("Local constant type is editable through the type selector"),
				ConstantProperties.ContainsByPredicate(
					[&LocalGraph](const FVerseTileProperty& Property)
					{
						return Property.EditKind == EVerseTilePropertyEditKind::Type
							&& Property.EditRange == LocalGraph[2].TypeRange;
					}));
		}
	}

	const FVerseVisualTile* OperatorInitializer = VerseVisualTileTests::FindDefinition(
		Snapshot, Tiles, UTF8TEXTVIEW("OperatorInitializer"));
	if (TestNotNull(TEXT("Operator-initializer visual fixture exists"), OperatorInitializer))
	{
		const TArray<FVerseVisualTile> OperatorInitializerGraph =
			FVerseVisualTileBuilder::BuildFunctionGraph(*OperatorInitializer, Snapshot);
		const TArray<FVerseVisualConnection> OperatorInitializerConnections =
			FVerseVisualTileBuilder::BuildConnections(OperatorInitializerGraph);
		if (TestTrue(TEXT("Operator initializer becomes the definition's value child"),
			OperatorInitializerGraph.Num() == 4
				&& OperatorInitializerGraph[1].DefinitionKind == VerseSyntaxKind::Constant
				&& OperatorInitializerGraph[1].Children.Num() == 1))
		{
			const FVerseVisualTile& Subtract = OperatorInitializerGraph[1].Children[0];
			int32 SubtractOutputConnectionCount = 0;
			if (!Subtract.GetValueOutputs().IsEmpty())
			{
				for (const FVerseVisualConnection& Connection : OperatorInitializerConnections)
				{
					SubtractOutputConnectionCount +=
						Connection.Source.Tile == Subtract.Id
						&& Connection.Source.Socket == Subtract.GetValueOutputs()[0].Id
						? 1 : 0;
				}
			}
			TestTrue(TEXT("Definition reuses the operator's sole connected output socket"),
				Subtract.ExpressionKind == EVerseExpressionKind::BinaryOperator
					&& Subtract.OperatorSpelling == TEXT("-")
					&& Subtract.GetValueOutputs().Num() == 1
					&& !Subtract.GetValueOutputs().IsEmpty()
					&& SubtractOutputConnectionCount == 1);
		}
	}

	struct FExpectedBinaryTile
	{
		FUtf8StringView Function;
	};
	const FExpectedBinaryTile BinaryTiles[] = {
		{UTF8TEXTVIEW("Subtract")},
		{UTF8TEXTVIEW("Multiply")},
		{UTF8TEXTVIEW("Divide")},
		{UTF8TEXTVIEW("Equal")},
		{UTF8TEXTVIEW("NotEqual")},
		{UTF8TEXTVIEW("LessThan")},
		{UTF8TEXTVIEW("LessThanOrEqual")},
		{UTF8TEXTVIEW("GreaterThan")},
		{UTF8TEXTVIEW("GreaterThanOrEqual")},
	};
	for (const FExpectedBinaryTile& Expected : BinaryTiles)
	{
		const FVerseVisualTile* Definition = VerseVisualTileTests::FindDefinition(
			Snapshot, Tiles, Expected.Function);
		if (TestNotNull(TEXT("Binary operator visual definition exists"), Definition))
		{
			const TArray<FVerseVisualTile> Graph =
				FVerseVisualTileBuilder::BuildFunctionGraph(*Definition, Snapshot);
			TestTrue(TEXT("Binary operator uses the common two-input operator tile"),
				Graph.Num() >= 2
				&& Graph[1].ExpressionKind == EVerseExpressionKind::BinaryOperator
				&& Graph[1].OperatorRange.IsSet()
				&& Graph[1].GetValueInputs().Num() == 2);
		}
	}

	const FVerseVisualTile* ComplexFunction = VerseVisualTileTests::FindDefinition(
		Snapshot, Tiles, UTF8TEXTVIEW("ComplexBinary"));
	if (TestNotNull(TEXT("Complex binary-expression fixture exists"), ComplexFunction))
	{
		const TArray<FVerseVisualTile> Graph =
			FVerseVisualTileBuilder::BuildFunctionGraph(*ComplexFunction, Snapshot);
		const TArray<FVerseVisualConnection> Connections =
			FVerseVisualTileBuilder::BuildConnections(Graph);
		const FVerseVisualTile* Add = Graph.Num() == 3 ? &Graph[1] : nullptr;
		const FVerseVisualTile* Multiply = Add != nullptr && Add->Children.Num() == 2
			? &Add->Children[1] : nullptr;
		const FVerseVisualTile* Divide = Multiply != nullptr
			&& Multiply->Children.Num() == 2 ? &Multiply->Children[1] : nullptr;
		const FVerseVisualTile* NestedSubtract = Divide != nullptr
			&& Divide->Children.Num() == 2 ? &Divide->Children[0] : nullptr;
		TestTrue(TEXT("Complex binary expressions remain recursive visual tiles"),
			Add != nullptr
				&& Multiply != nullptr
				&& Divide != nullptr
				&& NestedSubtract != nullptr);
		TestTrue(TEXT("Every nested operator exposes one connected result socket"),
			Multiply != nullptr
				&& Multiply->GetValueOutputs().Num() == 1
				&& VerseVisualTileTests::IsConnected(
					Connections, *Multiply, Multiply->GetValueOutputs()[0])
				&& Divide != nullptr
				&& Divide->GetValueOutputs().Num() == 1
				&& VerseVisualTileTests::IsConnected(
					Connections, *Divide, Divide->GetValueOutputs()[0])
				&& NestedSubtract != nullptr
				&& NestedSubtract->GetValueOutputs().Num() == 1
				&& VerseVisualTileTests::IsConnected(
					Connections, *NestedSubtract, NestedSubtract->GetValueOutputs()[0]));
		TestTrue(TEXT("Every nested operator token is source exact"),
			Multiply != nullptr
				&& Snapshot.GetDocument()->DecodeOriginalRange(Multiply->OperatorRange) == TEXT("*")
				&& Divide != nullptr
				&& Snapshot.GetDocument()->DecodeOriginalRange(Divide->OperatorRange) == TEXT("/")
				&& NestedSubtract != nullptr
				&& Snapshot.GetDocument()->DecodeOriginalRange(
					NestedSubtract->OperatorRange) == TEXT("-"));
	}

	const FVerseVisualTile* EmptyFunction = VerseVisualTileTests::FindDefinition(
		Snapshot,
		Tiles,
		UTF8TEXTVIEW("EmptyFunction"));
	if (TestNotNull(TEXT("Empty function visual definition exists"), EmptyFunction))
	{
		const TArray<FVerseVisualTile> EmptyGraph =
			FVerseVisualTileBuilder::BuildFunctionGraph(*EmptyFunction, Snapshot);
		if (TestEqual(TEXT("Empty function graph contains only its entry tile"), EmptyGraph.Num(), 1))
		{
			TestEqual(
				TEXT("Empty function graph omits the implicit return tile"),
				EmptyGraph[0].Kind,
				EVerseVisualTileKind::FunctionEntry);
			TestFalse(
				TEXT("Empty function entry has an unconnected execution output"),
				FVerseVisualTileBuilder::IsSocketConnected(
					FVerseVisualTileBuilder::BuildConnections(EmptyGraph),
					{EmptyGraph[0].Id, {EVerseVisualSocketDirection::Output,
						EVerseVisualSocketRole::Execution, 0}}));
		}
	}

	const FVerseVisualTile* VoidFunction = VerseVisualTileTests::FindDefinition(
		Snapshot,
		Tiles,
		UTF8TEXTVIEW("ControlLoop"));
	if (TestNotNull(TEXT("Non-empty void function visual definition exists"), VoidFunction))
	{
		const TArray<FVerseVisualTile> VoidGraph =
			FVerseVisualTileBuilder::BuildFunctionGraph(*VoidFunction, Snapshot);
		TestFalse(
			TEXT("Non-empty void function graph omits the implicit return tile"),
			VoidGraph.ContainsByPredicate([](const FVerseVisualTile& Tile)
			{
				return Tile.Kind == EVerseVisualTileKind::FunctionReturn;
			}));
	}

	const TArray<FVerseTileProperty> Properties = FVerseTileProperties::Build(*Function, Snapshot);
	const FVerseTileProperty* Access = Properties.FindByPredicate([](const FVerseTileProperty& Property)
	{
		return Property.EditKind == EVerseTilePropertyEditKind::AccessSpecifiers;
	});
	const FVerseTileProperty* Effects = Properties.FindByPredicate([](const FVerseTileProperty& Property)
	{
		return Property.EditKind == EVerseTilePropertyEditKind::EffectSpecifiers;
	});
	TestTrue(TEXT("Access specifiers are editable in Details"),
		Access != nullptr && Access->bEditable && Access->Value == TEXT("<public>"));
	TestTrue(TEXT("Effects are editable in Details"),
		Effects != nullptr && Effects->bEditable && Effects->Value == TEXT("<computes>"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseLiteralTilePresentationTest,
	"VerseVisualEditor.Expressions.Literals.TilePresentation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseLiteralTilePresentationTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FVerseDocument> Document = VerseVisualTileTests::LoadFixture(
		*this, TEXT("literal_expressions.verse"));
	if (!Document.IsValid())
	{
		return false;
	}

	const FVerseParseSnapshot Snapshot = FVerseParseSnapshotBuilder::Build(Document.ToSharedRef());
	const TArray<FVerseVisualTile> Tiles = FVerseVisualTileBuilder::Build(Snapshot);
	struct FExpectedLiteral
	{
		FUtf8StringView FunctionName;
		EVerseLiteralKind Kind;
		FName TypeName;
		FUtf8StringView Source;
	};
	const FExpectedLiteral Expected[] = {
		{UTF8TEXTVIEW("LiteralLogicTrue"), EVerseLiteralKind::Logic, TEXT("logic"), UTF8TEXTVIEW("true")},
		{UTF8TEXTVIEW("LiteralLogicFalse"), EVerseLiteralKind::Logic, TEXT("logic"), UTF8TEXTVIEW("false")},
		{UTF8TEXTVIEW("LiteralInteger"), EVerseLiteralKind::Integer, TEXT("int"), UTF8TEXTVIEW("42")},
		{UTF8TEXTVIEW("LiteralNegativeInteger"), EVerseLiteralKind::Integer, TEXT("int"), UTF8TEXTVIEW("-12")},
		{UTF8TEXTVIEW("LiteralFloat"), EVerseLiteralKind::Float, TEXT("float"), UTF8TEXTVIEW("3.5")},
		{UTF8TEXTVIEW("LiteralNegativeFloat"), EVerseLiteralKind::Float, TEXT("float"), UTF8TEXTVIEW("-2.25")},
		{UTF8TEXTVIEW("LiteralString"), EVerseLiteralKind::String, TEXT("string"), UTF8TEXTVIEW("\"hello\"")},
		{UTF8TEXTVIEW("LiteralCharacter"), EVerseLiteralKind::Character, TEXT("char"), UTF8TEXTVIEW("'A'")},
	};
	for (const FExpectedLiteral& Literal : Expected)
	{
		const FVerseVisualTile* Function = VerseVisualTileTests::FindDefinition(
			Snapshot, Tiles, Literal.FunctionName);
		if (!TestNotNull(TEXT("Literal fixture function exists"), Function))
		{
			continue;
		}
		const TArray<FVerseVisualTile> Graph =
			FVerseVisualTileBuilder::BuildFunctionGraph(*Function, Snapshot);
		if (TestTrue(TEXT("Literal function has an editable literal expression tile"),
			Graph.Num() == 3
				&& Graph[1].ExpressionKind == EVerseExpressionKind::Literal
				&& Graph[1].LiteralKind == Literal.Kind
				&& Graph[1].GetValueOutputs().Num() == 1))
		{
			TestEqual(TEXT("Literal tile carries its primitive type"),
				Graph[1].IntrinsicTypeName, Literal.TypeName);
			TestEqual(TEXT("Literal output socket carries the literal type"),
				Graph[1].GetValueOutputs()[0].IntrinsicTypeName, Literal.TypeName);
			TestTrue(TEXT("Implicit return input exists"),
				Graph[2].Kind == EVerseVisualTileKind::FunctionReturn
					&& Graph[2].GetValueInputs().Num() == 1);
			const FUTF8ToTCHAR ExpectedSource(
				reinterpret_cast<const ANSICHAR*>(Literal.Source.GetData()),
				Literal.Source.Len());
			TestEqual(TEXT("Literal tile retains its exact source range"),
				Snapshot.GetDocument()->DecodeOriginalRange(Graph[1].Range),
				FString(ExpectedSource.Length(), ExpectedSource.Get()));
			const TArray<FVerseTileProperty> Properties =
				FVerseTileProperties::Build(Graph[1], Snapshot);
			TestTrue(TEXT("Literal value is editable in Details"),
				Properties.ContainsByPredicate([&Graph](const FVerseTileProperty& Property)
				{
					return Property.EditKind == EVerseTilePropertyEditKind::Literal
						&& Property.LiteralKind == Graph[1].LiteralKind
						&& Property.EditRange == Graph[1].Range;
				}));
		}
	}

	const FVerseVisualTile* QueryFunction = VerseVisualTileTests::FindDefinition(
		Snapshot, Tiles, UTF8TEXTVIEW("LiteralLogicQuery"));
	if (TestNotNull(TEXT("Logic query fixture function exists"), QueryFunction))
	{
		// This is the behavioral guard for the official VST representation. If Epic
		// stops emitting PrePostCall(Expression, Option), these assertions identify
		// the user-visible contract that the replacement recognizer must preserve.
		const TArray<FVerseVisualTile> Graph =
			FVerseVisualTileBuilder::BuildFunctionGraph(*QueryFunction, Snapshot);
		if (TestEqual(TEXT("Postfix query function has one statement and return"),
			Graph.Num(), 3))
		{
			const FVerseVisualTile& Query = Graph[1];
			TestEqual(TEXT("Postfix query uses a unary expression tile"),
				Query.ExpressionKind, EVerseExpressionKind::UnaryOperator);
			TestEqual(TEXT("Postfix query uses query spelling"),
				Query.OperatorSpelling, FString(TEXT("?")));
			TestEqual(TEXT("Postfix query owns its operand"), Query.Children.Num(), 1);
			TestEqual(TEXT("Postfix query has one input socket"), Query.GetValueInputs().Num(), 1);
			TestEqual(TEXT("Postfix query has one output socket"), Query.GetValueOutputs().Num(), 1);
			if (Query.GetValueInputs().Num() == 1 && Query.GetValueOutputs().Num() == 1)
			{
				TestEqual(TEXT("Postfix query input carries logic"),
					Query.GetValueInputs()[0].IntrinsicTypeName, FName(TEXT("logic")));
				TestEqual(TEXT("Postfix query output carries logic"),
					Query.GetValueOutputs()[0].IntrinsicTypeName, FName(TEXT("logic")));
				TestEqual(TEXT("Postfix query output is failable"),
					Query.GetValueOutputs()[0].Outcome,
					EVerseExpressionOutcome::FailableValue);
			}
			TestTrue(TEXT("Postfix query range is source exact"),
				Snapshot.GetSourceView(Query.OperatorRange) == UTF8TEXTVIEW("?"));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseImmutableSocketTopologyTest,
	"VerseVisualEditor.Foundation.VisualTiles.ImmutableSocketTopology",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseImmutableSocketTopologyTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FVerseDocument> Document = VerseVisualTileTests::LoadFixture(
		*this, TEXT("functions.verse"));
	if (!Document.IsValid())
	{
		return false;
	}
	const FVerseParseSnapshot Snapshot =
		FVerseParseSnapshotBuilder::Build(Document.ToSharedRef());
	const TArray<FVerseVisualTile> FileTiles = FVerseVisualTileBuilder::Build(Snapshot);
	const FVerseVisualTile* Function = VerseVisualTileTests::FindDefinition(
		Snapshot, FileTiles, UTF8TEXTVIEW("ComplexBinary"));
	if (!TestNotNull(TEXT("Topology fixture function exists"), Function))
	{
		return false;
	}
	const TArray<FVerseVisualTile> Graph =
		FVerseVisualTileBuilder::BuildFunctionGraph(*Function, Snapshot);
	const TArray<FVerseVisualConnection> Connections =
		FVerseVisualTileBuilder::BuildConnections(Graph);
	FString Diagnostic;
	TestTrue(TEXT("Builder produces a valid endpoint graph"),
		FVerseVisualTileBuilder::ValidateConnections(Graph, Connections, &Diagnostic));
	for (const FVerseVisualConnection& Connection : Connections)
	{
		const FVerseVisualTile* Source = nullptr;
		const FVerseVisualTile* Target = nullptr;
		TFunction<void(TConstArrayView<FVerseVisualTile>)> FindEndpoints =
			[&](TConstArrayView<FVerseVisualTile> Tiles)
			{
				for (const FVerseVisualTile& Tile : Tiles)
				{
					Source = Tile.Id == Connection.Source.Tile ? &Tile : Source;
					Target = Tile.Id == Connection.Target.Tile ? &Tile : Target;
					FindEndpoints(Tile.Children);
				}
			};
		FindEndpoints(Graph);
		TestTrue(TEXT("Every connection resolves two declared endpoints"),
			Source != nullptr && Target != nullptr
			&& Source->FindSocket(Connection.Source.Socket) != nullptr
			&& Target->FindSocket(Connection.Target.Socket) != nullptr);
	}
	if (!Connections.IsEmpty())
	{
		TArray<FVerseVisualConnection> MissingEndpoint = Connections;
		MissingEndpoint[0].Target.Socket.Index = 9999;
		TestFalse(TEXT("A missing endpoint is rejected"),
			FVerseVisualTileBuilder::ValidateConnections(
				Graph, MissingEndpoint, &Diagnostic));

		TArray<FVerseVisualConnection> DuplicateInput = Connections;
		DuplicateInput.Add(Connections[0]);
		TestFalse(TEXT("Input cardinality violations are rejected"),
			FVerseVisualTileBuilder::ValidateConnections(
				Graph, DuplicateInput, &Diagnostic));

		TArray<FVerseVisualConnection> InvalidDirections = Connections;
		Swap(InvalidDirections[0].Source, InvalidDirections[0].Target);
		TestFalse(TEXT("Incompatible endpoint directions are rejected"),
			FVerseVisualTileBuilder::ValidateConnections(
				Graph, InvalidDirections, &Diagnostic));
		const FVerseVisualConnection* ExecutionConnection = Connections.FindByPredicate(
			[](const FVerseVisualConnection& Candidate)
			{
				return Candidate.Source.Socket.Role
					== EVerseVisualSocketRole::Execution;
			});
		if (ExecutionConnection != nullptr && Graph.Num() >= 2)
		{
			TArray<FVerseVisualTile> CardinalityGraph = Graph;
			FVerseVisualTile ExtraTarget = Graph[1];
			ExtraTarget.Id.Value = 100000;
			ExtraTarget.Children.Reset();
			CardinalityGraph.Add(MoveTemp(ExtraTarget));
			TArray<FVerseVisualConnection> DuplicateExecutionSource;
			DuplicateExecutionSource.Add(*ExecutionConnection);
			FVerseVisualConnection Second = *ExecutionConnection;
			Second.Target.Tile = CardinalityGraph.Last().Id;
			DuplicateExecutionSource.Add(Second);
			TestFalse(TEXT("Single-cardinality outputs reject multiple connections"),
				FVerseVisualTileBuilder::ValidateConnections(
					CardinalityGraph, DuplicateExecutionSource, &Diagnostic));
		}
	}
	if (Graph.Num() >= 2)
	{
		TArray<FVerseVisualTile> DuplicateTileIds = Graph;
		DuplicateTileIds[1].Id = DuplicateTileIds[0].Id;
		TestFalse(TEXT("Duplicate revision-local tile ids are rejected"),
			FVerseVisualTileBuilder::ValidateConnections(
				DuplicateTileIds, {}, &Diagnostic));

		TArray<FVerseVisualTile> DuplicateSocketIds = Graph;
		if (!DuplicateSocketIds[1].GetValueInputs().IsEmpty())
		{
			const FVerseVisualSocket Socket =
				DuplicateSocketIds[1].GetValueInputs()[0];
			DuplicateSocketIds[1].SocketTopology =
				FVerseVisualSocketTopology::MakeInvalidForTesting({Socket, Socket});
			TestFalse(TEXT("Duplicate tile-local socket ids are rejected"),
				FVerseVisualTileBuilder::ValidateConnections(
					DuplicateSocketIds, {}, &Diagnostic));
		}
	}
	return true;
}

#endif
