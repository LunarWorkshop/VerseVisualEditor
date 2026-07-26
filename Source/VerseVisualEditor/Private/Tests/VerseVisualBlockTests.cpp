#if WITH_DEV_AUTOMATION_TESTS

#include "VerseParseSnapshotBuilder.h"
#include "VerseVisualBlock.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

namespace VerseVisualBlockTests
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
	FVerseGlobalScopeBlockPresentationTest,
	"VerseVisualEditor.Foundation.VisualBlocks.GlobalScopePresentation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseGlobalScopeBlockPresentationTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FVerseDocument> Document = VerseVisualBlockTests::LoadFixture(
		*this,
		TEXT("top_level_supported.verse"));
	if (!Document.IsValid())
	{
		return false;
	}

	const FVerseParseSnapshot Snapshot = FVerseParseSnapshotBuilder::Build(Document.ToSharedRef());
	const TArray<FVerseVisualBlock> Blocks = FVerseVisualBlockBuilder::Build(Snapshot);
	if (!TestEqual(TEXT("Nine definitions and two meaningful raw regions are presented"), Blocks.Num(), 11))
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
	for (int32 Index = 0; Index < Blocks.Num(); ++Index)
	{
		const FVerseVisualBlock& Block = Blocks[Index];
		TestTrue(*FString::Printf(TEXT("Block %d has a source range"), Index), Block.Range.IsSet());
		TestTrue(*FString::Printf(TEXT("Block %d follows its predecessor"), Index), Block.Range.BeginByte >= PreviousEnd);
		PreviousEnd = Block.Range.EndByte();

		if (Block.Kind == EVerseVisualBlockKind::Definition)
		{
			++DefinitionCount;
			TestTrue(*FString::Printf(TEXT("Block %d has a definition kind"), Index), !Block.DefinitionKind.IsNone());
			TestTrue(*FString::Printf(TEXT("Block %d has a name"), Index), Block.NameRange.IsSet());
			if (Block.DefinitionKind == VerseSyntaxKind::Function)
			{
				const FUtf8StringView Body = Snapshot.GetSourceView(Block.BodyRange);
				bFunctionBodyExcludesDefinition = Body.Find(UTF8TEXTVIEW("Input")) != INDEX_NONE
					&& Body.Find(UTF8TEXTVIEW("ExampleFunction")) == INDEX_NONE;
			}
			else if (Block.DefinitionKind == VerseSyntaxKind::Class)
			{
				bEmptyClassHasEmptyBody = Block.BodyRange.IsSet()
					&& Snapshot.GetSourceView(Block.BodyRange).IsEmpty();
			}
		}
		else if (Block.Kind == EVerseVisualBlockKind::Comment)
		{
			++CommentCount;
			TestEqual(*FString::Printf(TEXT("Comment block %d has no definition kind"), Index), Block.DefinitionKind, NAME_None);
			TestFalse(*FString::Printf(TEXT("Comment block %d has no name"), Index), Block.NameRange.IsSet());
			TestTrue(
				*FString::Printf(TEXT("Comment block %d retains comment text"), Index),
				Snapshot.GetSourceView(Block.Range).Find(UTF8TEXTVIEW("#")) != INDEX_NONE);
		}
		else
		{
			++UnknownCount;
			TestEqual(*FString::Printf(TEXT("Unknown block %d has no definition kind"), Index), Block.DefinitionKind, NAME_None);
			TestFalse(*FString::Printf(TEXT("Unknown block %d has no name"), Index), Block.NameRange.IsSet());
			TestFalse(*FString::Printf(TEXT("Unknown block %d has no type"), Index), Block.TypeRange.IsSet());
			bFoundUnsupportedUsing |= Snapshot.GetSourceView(Block.Range).Find(UTF8TEXTVIEW("using")) != INDEX_NONE;
		}
	}

	TestEqual(TEXT("Every supported definition is presented"), DefinitionCount, 9);
	TestEqual(TEXT("Known source comment has a dedicated block"), CommentCount, 1);
	TestEqual(TEXT("Only unsupported syntax remains unknown"), UnknownCount, 1);
	TestTrue(TEXT("Unsupported using expression retains its source range"), bFoundUnsupportedUsing);
	TestTrue(TEXT("Function tile body excludes its surrounding definition"), bFunctionBodyExcludesDefinition);
	TestTrue(TEXT("Empty class tile has an empty body"), bEmptyClassHasEmptyBody);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseRawBlockPresentationTest,
	"VerseVisualEditor.Foundation.VisualBlocks.RawFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseRawBlockPresentationTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FVerseDocument> Document = VerseVisualBlockTests::LoadFixture(
		*this,
		TEXT("top_level_error_tolerance.verse"));
	if (!Document.IsValid())
	{
		return false;
	}

	const FVerseParseSnapshot Snapshot = FVerseParseSnapshotBuilder::Build(Document.ToSharedRef());
	const TArray<FVerseVisualBlock> Blocks = FVerseVisualBlockBuilder::Build(Snapshot);
	if (TestEqual(TEXT("Failed parsing still produces one visual block"), Blocks.Num(), 1))
	{
		TestTrue(TEXT("Failed parsing is presented as unknown"), Blocks[0].Kind == EVerseVisualBlockKind::Unknown);
		TestEqual(TEXT("Unknown block retains the complete source range"), Blocks[0].Range, Document->GetWholeOriginalRange());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseGlobalScopeDevelopmentCorpusTest,
	"VerseVisualEditor.Foundation.VisualBlocks.DevelopmentCorpus",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseGlobalScopeDevelopmentCorpusTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FVerseDocument> Document = VerseVisualBlockTests::LoadPluginFile(
		*this,
		TEXT("Content/TestCorpus/GlobalScopeCorpus.verse"));
	if (!Document.IsValid())
	{
		return false;
	}

	const FVerseParseSnapshot Snapshot = FVerseParseSnapshotBuilder::Build(Document.ToSharedRef());
	const TArray<FVerseVisualBlock> Blocks = FVerseVisualBlockBuilder::Build(Snapshot);
	TMap<FName, int32> DefinitionCounts;
	int32 CommentCount = 0;
	int32 UnknownCount = 0;
	TArray<const FVerseVisualBlock*> Comments;
	for (const FVerseVisualBlock& Block : Blocks)
	{
		if (Block.Kind == EVerseVisualBlockKind::Definition)
		{
			++DefinitionCounts.FindOrAdd(Block.DefinitionKind);
		}
		else if (Block.Kind == EVerseVisualBlockKind::Comment)
		{
			++CommentCount;
			Comments.Add(&Block);
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
	TestEqual(TEXT("Valid corpus contains no unknown blocks"), UnknownCount, 0);
	if (TestEqual(TEXT("Comment stack has a line group and a block comment"), Comments.Num(), 2))
	{
		const FUtf8StringView LineGroup = Snapshot.GetSourceView(Comments[0]->BodyRange);
		const FUtf8StringView BlockComment = Snapshot.GetSourceView(Comments[1]->BodyRange);
		TestTrue(TEXT("Adjacent hashtag comments merge into one visual block"),
			LineGroup.Find(UTF8TEXTVIEW("first comment")) != INDEX_NONE
			&& LineGroup.Find(UTF8TEXTVIEW("continuation")) != INDEX_NONE);
		TestTrue(TEXT("Container comment remains its own visual block"),
			Comments[1]->CommentKind == EVerseCommentKind::Block
			&& BlockComment.Find(UTF8TEXTVIEW("<#")) != INDEX_NONE);
	}
	return true;
}

#endif
