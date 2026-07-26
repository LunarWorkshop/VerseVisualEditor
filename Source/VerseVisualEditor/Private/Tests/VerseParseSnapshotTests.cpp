#if WITH_DEV_AUTOMATION_TESTS

#include "VerseParseSnapshot.h"

#include "Misc/AutomationTest.h"

namespace VerseParseSnapshotTests
{
	TArray<uint8> Bytes(FUtf8StringView Text, bool bWithBom = false)
	{
		TArray<uint8> Result;
		if (bWithBom)
		{
			Result.Append({0xEF, 0xBB, 0xBF});
		}
		Result.Append(reinterpret_cast<const uint8*>(Text.GetData()), Text.Len());
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseRawParseSnapshotTest,
	"VerseVisualEditor.Foundation.ParseSnapshot.RawFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseRawParseSnapshotTest::RunTest(const FString& Parameters)
{
	struct FFixture
	{
		FString Name;
		TArray<uint8> Bytes;
		FUtf8StringView ExpectedSource;
	};

	const TArray<FFixture> Fixtures = {
		{TEXT("Ordinary Verse"), VerseParseSnapshotTests::Bytes(UTF8TEXTVIEW("Value : int = 1\n")), UTF8TEXTVIEW("Value : int = 1\n")},
		{TEXT("Empty source"), {}, FUtf8StringView()},
		{TEXT("BOM-prefixed source"), VerseParseSnapshotTests::Bytes(UTF8TEXTVIEW("Value := 2\n"), true), UTF8TEXTVIEW("Value := 2\n")},
		{TEXT("Incomplete Verse"), VerseParseSnapshotTests::Bytes(UTF8TEXTVIEW("broken := class:\n    Value :")), UTF8TEXTVIEW("broken := class:\n    Value :")},
		{TEXT("Malformed Verse"), VerseParseSnapshotTests::Bytes(UTF8TEXTVIEW("??? { ] :=\n")), UTF8TEXTVIEW("??? { ] :=\n")},
		{TEXT("Unsupported construct"), VerseParseSnapshotTests::Bytes(UTF8TEXTVIEW("future_syntax<unknown> := 42\n")), UTF8TEXTVIEW("future_syntax<unknown> := 42\n")},
	};

	for (const FFixture& Fixture : Fixtures)
	{
		FText Error;
		TSharedPtr<FVerseDocument> Document = FVerseDocument::CreateFromBytes(Fixture.Bytes, Error);
		if (!TestTrue(*FString::Printf(TEXT("%s document loads: %s"), *Fixture.Name, *Error.ToString()), Document.IsValid()))
		{
			continue;
		}

		FVerseParseSnapshot Snapshot = FVerseParseSnapshot::CreateRaw(Document.ToSharedRef());
		TestTrue(
			*FString::Printf(TEXT("%s snapshot retains its exact document"), *Fixture.Name),
			&Snapshot.GetDocument().Get() == Document.Get());
		TestEqual(*FString::Printf(TEXT("%s has one fallback region"), *Fixture.Name), Snapshot.GetSourceRegions().Num(), 1);

		const FVerseSourceRegion& Region = Snapshot.GetSourceRegions()[0];
		TestEqual(*FString::Printf(TEXT("%s fallback is raw"), *Fixture.Name), Region.Kind, EVerseSourceRegionKind::Raw);
		TestEqual(*FString::Printf(TEXT("%s raw region has no syntax kind"), *Fixture.Name), Region.SyntaxKind, NAME_None);
		TestEqual(*FString::Printf(TEXT("%s region covers the complete BOM-free source"), *Fixture.Name), Region.Range, Document->GetWholeOriginalRange());
		TestTrue(*FString::Printf(TEXT("%s region resolves to exact source bytes"), *Fixture.Name), Snapshot.GetSourceView(Region) == Fixture.ExpectedSource);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseParseSnapshotLifetimeTest,
	"VerseVisualEditor.Foundation.ParseSnapshot.RetainsSourceRevision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseParseSnapshotLifetimeTest::RunTest(const FString& Parameters)
{
	FText Error;
	TSharedPtr<FVerseDocument> Document = FVerseDocument::CreateFromBytes(
		VerseParseSnapshotTests::Bytes(UTF8TEXTVIEW("Retained : int = 7\n")),
		Error);
	if (!TestTrue(TEXT("Document loads"), Document.IsValid()))
	{
		return false;
	}

	FVerseParseSnapshot Snapshot = FVerseParseSnapshot::CreateRaw(Document.ToSharedRef());
	Document.Reset();

	TestTrue(TEXT("Snapshot keeps its source revision alive"), Snapshot.GetSourceView(Snapshot.GetSourceRegions()[0]) == UTF8TEXTVIEW("Retained : int = 7\n"));
	return true;
}

#endif
