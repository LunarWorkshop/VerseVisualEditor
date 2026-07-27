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
	FVerseGlobalScopeDevelopmentCorpusTest,
	"VerseVisualEditor.Foundation.VisualTiles.DevelopmentCorpus",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseGlobalScopeDevelopmentCorpusTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FVerseDocument> Document = VerseVisualTileTests::LoadPluginFile(
		*this,
		TEXT("Content/TestCorpus/GlobalScopeCorpus.verse"));
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
			if (Name == TEXT("CorpusModuleOne"))
			{
				ModuleOneTile = &Tile;
				ModuleOneFirstLine = Tile.FirstSourceLine;
				ModuleOneLastLine = Tile.LastSourceLine;
			}
			else if (Name == TEXT("CorpusModuleTwo"))
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
	TestEqual(TEXT("Multi-line module ends on line 22"), ModuleTwoLastLine, 22);
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
			NameProperty && NameProperty->Value == TEXT("CorpusModuleOne"));
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

#endif
