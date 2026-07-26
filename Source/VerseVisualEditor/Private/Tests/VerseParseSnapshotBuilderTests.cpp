#if WITH_DEV_AUTOMATION_TESTS

#include "VerseParseSnapshotBuilder.h"

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
			}
			else
			{
				Test.TestTrue(TEXT("Typed region has a syntax kind"), !Region.SyntaxKind.IsNone());
				Test.TestTrue(TEXT("Typed region has a name range"), Region.NameRange.IsSet());
				Test.TestTrue(TEXT("Name begins inside its typed region"), Region.NameRange.BeginByte >= Region.Range.BeginByte);
				Test.TestTrue(TEXT("Name ends inside its typed region"), Region.NameRange.EndByte() <= Region.Range.EndByte());
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

#endif
