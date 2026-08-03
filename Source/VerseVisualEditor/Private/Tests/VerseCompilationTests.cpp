#if WITH_DEV_AUTOMATION_TESTS

#include "Semantics/VerseCompilation.h"
#include "Document/VerseDocumentSession.h"
#include "VisualModel/VerseVisualTile.h"

#include "Misc/AutomationTest.h"
#include "SolBuildResults.h"

namespace VerseCompilationTests
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
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseProjectDiagnosticConversionTest,
	"VerseVisualEditor.Compilation.ProjectDiagnosticConversion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseProjectDiagnosticConversionTest::RunTest(const FString& Parameters)
{
	const FUtf8String Source(UTF8TEXT("Alpha := class {}\nBeta := class {}\n"));
	FSolDiagnostic ProjectDiagnostic;
	ProjectDiagnostic.Info.ReferenceCode = 3500;
	ProjectDiagnostic.Info.Severity = ELogVerbosity::Error;
	ProjectDiagnostic.Info.Message = TEXT("Example semantic error");
	ProjectDiagnostic.Location.FilePath = TEXT("Example.verse");
	ProjectDiagnostic.Location.RowSpan = FIntPoint(2, 2);
	ProjectDiagnostic.Location.ColSpan = FIntPoint(1, 5);

	const FVerseDocumentRevision Revision{11};
	const FVerseCompilationResult Result = VerseCompilation::FromProjectBuildDiagnostics(
		VerseCompilationTests::View(Source),
		Revision,
		MakeArrayView(&ProjectDiagnostic, 1));
	if (!TestEqual(TEXT("The project diagnostic is retained"), Result.Diagnostics.Num(), 1))
	{
		return false;
	}
	TestFalse(TEXT("An engine compiler error marks the result failed"), Result.bSucceeded);
	TestTrue(TEXT("The project diagnostic keeps the requested revision"),
		Result.Diagnostics[0].Range.Revision == Revision);
	TestEqual(TEXT("One-based row and column become the correct UTF-8 byte offset"),
		Result.Diagnostics[0].Range.BeginByte, 18);
	TestEqual(TEXT("The exclusive compiler span is preserved"),
		Result.Diagnostics[0].Range.NumBytes, 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseCompilationDiagnosticMappingTest,
	"VerseVisualEditor.Compilation.DiagnosticMapping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseCompilationDiagnosticMappingTest::RunTest(const FString& Parameters)
{
	const FVerseDocumentRevision Revision{7};
	TArray<FVerseVisualTile> Tiles;
	Tiles.AddDefaulted_GetRef().Range = FVerseTextRange(Revision, {0, 10});
	Tiles.AddDefaulted_GetRef().Range = FVerseTextRange(Revision, {10, 10});
	Tiles.AddDefaulted_GetRef().Range = FVerseTextRange(Revision, {20, 10});

	FVerseCompilationResult Incoming;
	Incoming.Revision = Revision;
	Incoming.Diagnostics.AddDefaulted_GetRef().Range = FVerseTextRange(Revision, {8, 5});

	FVerseCompilationResult Accepted;
	if (!TestTrue(
		TEXT("A result for the current revision is accepted"),
		VerseCompilation::TryAcceptResult(MoveTemp(Incoming), Revision, Tiles, Accepted)))
	{
		return false;
	}

	if (!TestEqual(TEXT("The diagnostic remains present"), Accepted.Diagnostics.Num(), 1))
	{
		return false;
	}
	if (!TestEqual(TEXT("A range crossing a tile boundary maps to both tiles"),
		Accepted.Diagnostics[0].AffectedTileIndices.Num(), 2))
	{
		return false;
	}
	TestEqual(TEXT("The first overlapping tile is mapped"),
		Accepted.Diagnostics[0].AffectedTileIndices[0], 0);
	TestEqual(TEXT("The second overlapping tile is mapped"),
		Accepted.Diagnostics[0].AffectedTileIndices[1], 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseCompilationStaleResultTest,
	"VerseVisualEditor.Compilation.StaleResultRejection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseCompilationStaleResultTest::RunTest(const FString& Parameters)
{
	FVerseCompilationResult Stale;
	Stale.Revision = {4};
	FVerseCompilationResult Existing;
	Existing.Revision = {5};
	Existing.bSucceeded = true;

	TestFalse(
		TEXT("A result from a superseded revision is rejected"),
		VerseCompilation::TryAcceptResult(MoveTemp(Stale), {5}, {}, Existing));
	TestTrue(TEXT("Rejecting a stale result does not overwrite the accepted result"),
		Existing.Revision == FVerseDocumentRevision{5} && Existing.bSucceeded);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseCompilationFailureIsNonMutatingTest,
	"VerseVisualEditor.Compilation.FailureDoesNotMutateSource",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseCompilationFailureIsNonMutatingTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FVerseDocument> Document = VerseCompilationTests::MakeDocument(
		*this,
		UTF8TEXTVIEW("Broken := class {\n"));
	if (!Document.IsValid())
	{
		return false;
	}

	FVerseDocumentSession Session(Document.ToSharedRef());
	const FVerseDocumentRevision RevisionBefore = Session.GetRevision();
	const FVerseContentStateId ContentBefore = Session.GetContentStateId();
	const FUtf8String SourceBefore = Session.GetCurrentUtf8();
	const FVerseCompilationResult Result = VerseCompilation::Compile(
		Session.GetCurrentUtf8(),
		RevisionBefore,
		TEXT("Broken.verse"));

	TestFalse(TEXT("Invalid Verse source reports compilation failure"), Result.bSucceeded);
	TestTrue(TEXT("Invalid Verse source produces a structured diagnostic"), !Result.Diagnostics.IsEmpty());
	TestTrue(TEXT("Compilation does not advance the document revision"), Session.GetRevision() == RevisionBefore);
	TestTrue(TEXT("Compilation does not change the content state"), Session.GetContentStateId() == ContentBefore);
	TestTrue(TEXT("Compilation does not change source bytes"),
		VerseCompilationTests::View(Session.GetCurrentUtf8()) == VerseCompilationTests::View(SourceBefore));
	return true;
}

#endif
