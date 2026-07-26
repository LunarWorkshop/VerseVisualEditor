#if WITH_DEV_AUTOMATION_TESTS

#include "VerseDocument.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace VerseDocumentTests
{
	TArray<uint8> Bytes(FUtf8StringView Text, bool bWithBom = false)
	{
		TArray<uint8> Result;
		if (bWithBom)
		{
			Result.Append({0xEF, 0xBB, 0xBF});
		}
		Result.Append(
			reinterpret_cast<const uint8*>(Text.GetData()),
			Text.Len());
		return Result;
	}

	bool ArraysEqual(TConstArrayView<uint8> Left, TConstArrayView<uint8> Right)
	{
		return Left.Num() == Right.Num()
			&& (Left.Num() == 0 || FMemory::Memcmp(Left.GetData(), Right.GetData(), Left.Num()) == 0);
	}

	TArray<uint8> ReconstructOriginalFile(const FVerseDocument& Document)
	{
		TArray<uint8> Result;
		if (Document.HasUtf8Bom())
		{
			Result.Append({0xEF, 0xBB, 0xBF});
		}

		const FUtf8StringView Text = Document.GetOriginalUtf8View();
		Result.Append(reinterpret_cast<const uint8*>(Text.GetData()), Text.Len());
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseDocumentRoundTripTest,
	"VerseVisualEditor.Foundation.Document.RoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseDocumentRoundTripTest::RunTest(const FString& Parameters)
{
	struct FFixture
	{
		FString Name;
		TArray<uint8> Bytes;
		EVerseLineEnding ExpectedLineEnding;
		bool bExpectedBom;
	};

	TArray<FFixture> Fixtures;
	Fixtures.Add({
		TEXT("LF and indentation"),
		VerseDocumentTests::Bytes(UTF8TEXTVIEW("module_a := module:\n\n\tValue : int = 1\n")),
		EVerseLineEnding::Lf,
		false});
	Fixtures.Add({
		TEXT("CRLF and braces"),
		VerseDocumentTests::Bytes(UTF8TEXTVIEW("Function() : void =\r\n{\r\n\treturn\r\n}\r\n")),
		EVerseLineEnding::CrLf,
		false});
	Fixtures.Add({
		TEXT("UTF-8 BOM"),
		VerseDocumentTests::Bytes(UTF8TEXTVIEW("Value : int = 7\n"), true),
		EVerseLineEnding::Lf,
		true});
	Fixtures.Add({
		TEXT("Mixed line endings"),
		VerseDocumentTests::Bytes(UTF8TEXTVIEW("a\nb\r\nc\r")),
		EVerseLineEnding::Mixed,
		false});
	Fixtures.Add({
		TEXT("No trailing newline"),
		VerseDocumentTests::Bytes(UTF8TEXTVIEW("Name := class{}")),
		EVerseLineEnding::None,
		false});
	Fixtures.Add({
		TEXT("Embedded NUL"),
		TArray<uint8>({0x61, 0x00, 0x62}),
		EVerseLineEnding::None,
		false});
	Fixtures.Add({
		TEXT("Empty file"),
		TArray<uint8>(),
		EVerseLineEnding::None,
		false});

	for (const FFixture& Fixture : Fixtures)
	{
		FText Error;
		const TSharedPtr<FVerseDocument> Document = FVerseDocument::CreateFromBytes(Fixture.Bytes, Error);
		TestTrue(*FString::Printf(TEXT("%s loads: %s"), *Fixture.Name, *Error.ToString()), Document.IsValid());
		if (!Document.IsValid())
		{
			continue;
		}

		TestTrue(
			*FString::Printf(TEXT("%s round trips byte-for-byte"), *Fixture.Name),
			VerseDocumentTests::ArraysEqual(Fixture.Bytes, VerseDocumentTests::ReconstructOriginalFile(*Document)));
		TestEqual(
			*FString::Printf(TEXT("%s UTF-8 backing length"), *Fixture.Name),
			Document->GetOriginalUtf8().Len(),
			Fixture.Bytes.Num() - (Fixture.bExpectedBom ? 3 : 0));
		TestEqual(*FString::Printf(TEXT("%s line ending"), *Fixture.Name), Document->GetLineEnding(), Fixture.ExpectedLineEnding);
		TestEqual(*FString::Printf(TEXT("%s BOM"), *Fixture.Name), Document->HasUtf8Bom(), Fixture.bExpectedBom);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseDocumentFileFixtureTest,
	"VerseVisualEditor.Foundation.Document.FileFixture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseDocumentFileFixtureTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("VerseVisualEditor"));
	if (!TestTrue(TEXT("VerseVisualEditor plugin is discoverable"), Plugin.IsValid()))
	{
		return false;
	}

	const FString FixturePath = FPaths::Combine(
		Plugin->GetBaseDir(),
		TEXT("Tests/Fixtures/global_scope_lf.verse"));
	TArray<uint8> FixtureBytes;
	if (!TestTrue(TEXT("Fixture can be read"), FFileHelper::LoadFileToArray(FixtureBytes, *FixturePath)))
	{
		return false;
	}

	FText Error;
	TSharedPtr<FVerseDocument> Document = FVerseDocument::LoadFromFile(FixturePath, Error);
	if (!TestTrue(*FString::Printf(TEXT("Fixture loads: %s"), *Error.ToString()), Document.IsValid()))
	{
		return false;
	}

	TestTrue(
		TEXT("Loaded fixture retains the exact input bytes"),
		VerseDocumentTests::ArraysEqual(FixtureBytes, VerseDocumentTests::ReconstructOriginalFile(*Document)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseDocumentSourceRangeTest,
	"VerseVisualEditor.Foundation.Document.SourceRanges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseDocumentSourceRangeTest::RunTest(const FString& Parameters)
{
	const TArray<uint8> Bytes = VerseDocumentTests::Bytes(UTF8TEXTVIEW("alpha\nbeta\n"));
	FText Error;
	TSharedPtr<FVerseDocument> Document = FVerseDocument::CreateFromBytes(Bytes, Error);
	if (!TestTrue(TEXT("Document loads"), Document.IsValid()))
	{
		return false;
	}

	TestTrue(
		TEXT("Block range is a view into original UTF-8"),
		Document->GetOriginalUtf8View({6, 4}) == UTF8TEXTVIEW("beta"));
	TestEqual(
		TEXT("Range constructed from bounds"),
		FVerseByteRange::FromBounds(6, 10),
		FVerseByteRange({6, 4}));
	TestEqual(TEXT("Decoded block range"), Document->DecodeOriginalRange({6, 4}), FString(TEXT("beta")));
	TestEqual(TEXT("First line number"), Document->GetOriginalLineNumber(0), 1);
	TestEqual(TEXT("Second line number"), Document->GetOriginalLineNumber(6), 2);
	TestEqual(TEXT("Initial source is one raw region"), Document->GetSourceRegions().Num(), 1);
	TestEqual(TEXT("Raw region covers all content"), Document->GetSourceRegions()[0].Range, Document->GetWholeOriginalRange());

	TArray<FVerseSourceRegion> Regions;
	Regions.Add({{0, 5}, EVerseSourceRegionKind::Syntax, TEXT("Identifier")});
	Regions.Add({{5, 1}, EVerseSourceRegionKind::Raw, NAME_None});
	Regions.Add({{6, 4}, EVerseSourceRegionKind::Syntax, TEXT("Identifier")});
	Regions.Add({{10, 1}, EVerseSourceRegionKind::Raw, NAME_None});
	TestTrue(TEXT("Non-overlapping syntax and raw regions are accepted"), Document->SetSourceRegions(MoveTemp(Regions), Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseDocumentUtf8ValidationTest,
	"VerseVisualEditor.Foundation.Document.Utf8Validation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseDocumentUtf8ValidationTest::RunTest(const FString& Parameters)
{
	FText Error;
	TestFalse(
		TEXT("Truncated UTF-8 is rejected"),
		FVerseDocument::CreateFromBytes(TArray<uint8>({0x61, 0xC3}), Error).IsValid());
	TestFalse(
		TEXT("Overlong UTF-8 is rejected"),
		FVerseDocument::CreateFromBytes(TArray<uint8>({0xC0, 0xAF}), Error).IsValid());
	TestFalse(
		TEXT("UTF-16 LE is rejected"),
		FVerseDocument::CreateFromBytes(TArray<uint8>({0xFF, 0xFE, 0x61, 0x00}), Error).IsValid());

	const TArray<uint8> UnicodeBytes({0x61, 0xC3, 0xA9, 0x62});
	TSharedPtr<FVerseDocument> UnicodeDocument = FVerseDocument::CreateFromBytes(UnicodeBytes, Error);
	if (!TestTrue(TEXT("Valid non-ASCII UTF-8 loads"), UnicodeDocument.IsValid()))
	{
		return false;
	}

	TestTrue(
		TEXT("Valid non-ASCII UTF-8 retains its exact bytes"),
		VerseDocumentTests::ArraysEqual(UnicodeBytes, VerseDocumentTests::ReconstructOriginalFile(*UnicodeDocument)));
	return true;
}

#endif
