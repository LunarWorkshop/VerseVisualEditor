#if WITH_DEV_AUTOMATION_TESTS

#include "VerseDocumentSession.h"

#include "Misc/AutomationTest.h"

namespace VerseDocumentSessionTests
{
	TSharedPtr<FVerseDocument> MakeDocument(FAutomationTestBase& Test, FUtf8StringView Source)
	{
		FText Error;
		const TConstArrayView<uint8> Bytes(
			reinterpret_cast<const uint8*>(Source.GetData()),
			Source.Len());
		TSharedPtr<FVerseDocument> Document = FVerseDocument::CreateFromBytes(Bytes, Error);
		Test.TestTrue(TEXT("Source document is valid UTF-8"), Document.IsValid());
		return Document;
	}

	FUtf8StringView View(const FUtf8String& Text)
	{
		return FUtf8StringView(*Text, Text.Len());
	}

	bool HasCompleteCoverage(const FVerseDocumentSession& Session)
	{
		const TArray<FVerseSourceRegion>& Regions = Session.GetParseSnapshot().GetSourceRegions();
		int32 Cursor = 0;
		for (const FVerseSourceRegion& Region : Regions)
		{
			if (Region.Range.BeginByte != Cursor || Region.Range.NumBytes <= 0)
			{
				return false;
			}
			Cursor = Region.Range.EndByte();
		}
		return Cursor == Session.GetCurrentUtf8().Len();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseEditBufferLocalizedReplacementTest,
	"VerseVisualEditor.Foundation.EditBuffer.LocalizedReplacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseEditBufferLocalizedReplacementTest::RunTest(const FString& Parameters)
{
	using namespace VerseDocumentSessionTests;
	const TSharedPtr<FVerseDocument> Document = MakeDocument(*this, UTF8TEXTVIEW("alpha β gamma"));
	if (!Document.IsValid())
	{
		return false;
	}

	FVerseEditBuffer Buffer(Document.ToSharedRef());
	FText Error;
	TestTrue(TEXT("A multibyte character can be replaced at its exact boundaries"),
		Buffer.Replace({6, 2}, UTF8TEXTVIEW("B"), Error));
	TestEqual(TEXT("Localized replacement preserves all surrounding bytes"),
		View(Buffer.Materialize()), UTF8TEXTVIEW("alpha B gamma"));
	TestEqual(TEXT("Replacement splits the original into two spans around one added span"), Buffer.GetSpans().Num(), 3);
	TestEqual(TEXT("Added storage contains only inserted bytes"), View(Buffer.GetAddedText()), UTF8TEXTVIEW("B"));

	TestTrue(TEXT("Insertion adjacent to added text succeeds"),
		Buffer.Replace({7, 0}, UTF8TEXTVIEW("!"), Error));
	TestEqual(TEXT("Adjacent added spans coalesce"), Buffer.GetSpans().Num(), 3);
	TestEqual(TEXT("Append-only storage retains both insertions in order"),
		View(Buffer.GetAddedText()), UTF8TEXTVIEW("B!"));
	TestEqual(TEXT("Coalesced source is exact"),
		View(Buffer.Materialize()), UTF8TEXTVIEW("alpha B! gamma"));

	TestTrue(TEXT("Deletion is represented without appending bytes"),
		Buffer.Replace({9, 5}, FUtf8StringView(), Error));
	TestEqual(TEXT("Deletion preserves unaffected prefix and separator"),
		View(Buffer.Materialize()), UTF8TEXTVIEW("alpha B! "));
	TestEqual(TEXT("Deletion does not shrink or rewrite append-only storage"),
		View(Buffer.GetAddedText()), UTF8TEXTVIEW("B!"));
	TestEqual(TEXT("Immutable original source is unchanged"),
		Document->GetOriginalUtf8View(), UTF8TEXTVIEW("alpha β gamma"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseDocumentSessionRangeValidationTest,
	"VerseVisualEditor.Foundation.DocumentSession.RangeValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseDocumentSessionRangeValidationTest::RunTest(const FString& Parameters)
{
	using namespace VerseDocumentSessionTests;
	const TSharedPtr<FVerseDocument> Document = MakeDocument(*this, UTF8TEXTVIEW("AβZ"));
	if (!Document.IsValid())
	{
		return false;
	}

	FVerseDocumentSession Session(Document.ToSharedRef());
	FText Error;
	TestFalse(TEXT("An edit cannot begin inside a UTF-8 code point"),
		Session.Replace(FVerseTextRange(Session.GetRevision(), {2, 0}), UTF8TEXTVIEW("x"), Error));
	TestFalse(TEXT("An edit cannot extend past current source"),
		Session.Replace(FVerseTextRange(Session.GetRevision(), {0, 99}), UTF8TEXTVIEW("x"), Error));

	const UTF8CHAR InvalidBytes[] = {
		static_cast<UTF8CHAR>(0xC0),
	};
	TestFalse(TEXT("Invalid replacement UTF-8 is rejected"),
		Session.Replace(
			FVerseTextRange(Session.GetRevision(), {0, 1}),
			FUtf8StringView(InvalidBytes, UE_ARRAY_COUNT(InvalidBytes)),
			Error));
	TestEqual(TEXT("Rejected edits do not advance the revision"), Session.GetRevision().Value, uint64(0));

	const FVerseTextRange RevisionZeroRange(Session.GetRevision(), {0, 1});
	TestTrue(TEXT("A valid localized replacement advances the session"),
		Session.Replace(RevisionZeroRange, UTF8TEXTVIEW("Q"), Error));
	TestEqual(TEXT("Successful replacement increments revision exactly once"), Session.GetRevision().Value, uint64(1));
	TestFalse(TEXT("A range from the prior revision is stale"),
		Session.Replace(RevisionZeroRange, UTF8TEXTVIEW("R"), Error));
	TestEqual(TEXT("Stale edit rejection preserves current text"),
		View(Session.GetCurrentUtf8()), UTF8TEXTVIEW("QβZ"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseDocumentSessionReparseTest,
	"VerseVisualEditor.Foundation.DocumentSession.Reparse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseDocumentSessionReparseTest::RunTest(const FString& Parameters)
{
	using namespace VerseDocumentSessionTests;
	const TSharedPtr<FVerseDocument> Document = MakeDocument(*this, UTF8TEXTVIEW("Thing := class {}\n"));
	if (!Document.IsValid())
	{
		return false;
	}

	FVerseDocumentSession Session(Document.ToSharedRef());
	TestEqual(TEXT("Construction does not materialize unchanged original source"), Session.GetMaterializationCount(), uint32(0));
	const FUtf8String& FirstMaterialization = Session.GetCurrentUtf8();
	const FUtf8String& ReusedMaterialization = Session.GetCurrentUtf8();
	TestEqual(TEXT("Repeated consumers reuse one revision cache"), Session.GetMaterializationCount(), uint32(1));
	TestTrue(TEXT("Repeated consumers receive the cached object"), &FirstMaterialization == &ReusedMaterialization);

	FText Error;
	TestTrue(TEXT("Renaming source through a localized replacement succeeds"),
		Session.Replace(FVerseTextRange(Session.GetRevision(), {0, 5}), UTF8TEXTVIEW("Renamed"), Error));
	TestEqual(TEXT("Reparsing materializes the new revision once"), Session.GetMaterializationCount(), uint32(2));
	TestEqual(TEXT("Current source contains the localized replacement"),
		View(Session.GetCurrentUtf8()), UTF8TEXTVIEW("Renamed := class {}\n"));
	TestEqual(TEXT("Reading a parsed revision reuses its materialization"), Session.GetMaterializationCount(), uint32(2));

	const FVerseVisualTile* ClassTile = Session.GetTiles().FindByPredicate([](const FVerseVisualTile& Tile)
	{
		return Tile.Kind == EVerseVisualTileKind::Definition && Tile.DefinitionKind == VerseSyntaxKind::Class;
	});
	if (TestNotNull(TEXT("Edited source reparses into a class tile"), ClassTile))
	{
		TestEqual(TEXT("Rebuilt tile carries the current revision"), ClassTile->Range.Revision.Value, uint64(1));
		TestEqual(TEXT("Rebuilt tile resolves its edited name"),
			Session.GetParseSnapshot().GetDocument()->DecodeOriginalRange(ClassTile->NameRange),
			FString(TEXT("Renamed")));
	}

	TestTrue(TEXT("A line insertion is another localized edit"),
		Session.Replace(FVerseTextRange(Session.GetRevision(), {0, 0}), UTF8TEXTVIEW("# note\n"), Error));
	const FVerseVisualTile* ShiftedClassTile = Session.GetTiles().FindByPredicate([](const FVerseVisualTile& Tile)
	{
		return Tile.Kind == EVerseVisualTileKind::Definition && Tile.DefinitionKind == VerseSyntaxKind::Class;
	});
	if (TestNotNull(TEXT("Class remains recognized after inserting a comment"), ShiftedClassTile))
	{
		TestEqual(TEXT("Line numbers are rebuilt from current revision source"), ShiftedClassTile->FirstSourceLine, 2);
		TestEqual(TEXT("Shifted tile carries the second revision"), ShiftedClassTile->Range.Revision.Value, uint64(2));
	}

	const FUtf8String InvalidSource(UTF8TEXT("Renamed := class {"));
	TestTrue(TEXT("Syntactically invalid UTF-8 remains an accepted source edit"),
		Session.Replace(Session.GetWholeTextRange(), View(InvalidSource), Error));
	TestEqual(TEXT("Invalid edited text remains authoritative"),
		View(Session.GetCurrentUtf8()), View(InvalidSource));
	TestTrue(TEXT("Error-tolerant reparse preserves complete invalid source coverage"), HasCompleteCoverage(Session));
	TestTrue(TEXT("Invalid edited source produces at least one raw region"),
		Session.GetParseSnapshot().GetSourceRegions().ContainsByPredicate([](const FVerseSourceRegion& Region)
		{
			return Region.Kind == EVerseSourceRegionKind::Raw;
		}));
	return true;
}

#endif
