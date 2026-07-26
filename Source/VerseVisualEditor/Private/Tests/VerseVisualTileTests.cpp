#if WITH_DEV_AUTOMATION_TESTS

#include "VerseParseSnapshotBuilder.h"
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
		TestEqual(TEXT("Unknown tile retains the complete source range"), Tiles[0].Range, Document->GetWholeOriginalRange());
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
	TArray<const FVerseVisualTile*> Comments;
	for (const FVerseVisualTile& Tile : Tiles)
	{
		if (Tile.Kind == EVerseVisualTileKind::Definition)
		{
			++DefinitionCounts.FindOrAdd(Tile.DefinitionKind);
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
	if (TestEqual(TEXT("Comment stack has a line group and a block comment"), Comments.Num(), 2))
	{
		const FUtf8StringView LineGroup = Snapshot.GetSourceView(Comments[0]->BodyRange);
		const FUtf8StringView BlockComment = Snapshot.GetSourceView(Comments[1]->BodyRange);
		TestTrue(TEXT("Adjacent hashtag comments merge into one visual tile"),
			LineGroup.Find(UTF8TEXTVIEW("first comment")) != INDEX_NONE
			&& LineGroup.Find(UTF8TEXTVIEW("continuation")) != INDEX_NONE);
		TestTrue(TEXT("Container comment remains its own visual tile"),
			Comments[1]->CommentKind == EVerseCommentKind::Block
			&& BlockComment.Find(UTF8TEXTVIEW("<#")) != INDEX_NONE);
	}
	return true;
}

#endif
