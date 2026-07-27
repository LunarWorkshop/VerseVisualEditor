#if WITH_DEV_AUTOMATION_TESTS

#include "VerseSpecifier.h"

#include "Misc/AutomationTest.h"
#include "VerseDocument.h"
#include "VerseDocumentSession.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseSpecifierValidationTest,
	"VerseVisualEditor.Prototype.Functions.SpecifierValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseSpecifierValidationTest::RunTest(const FString& Parameters)
{
	FString Normalized;
	TestTrue(TEXT("Canonical groups are accepted"),
		NormalizeVerseSpecifiers(TEXT(" <public>  <computes> "), Normalized).IsEmpty());
	TestEqual(TEXT("Whitespace between groups is normalized"), Normalized, TEXT("<public><computes>"));
	TestFalse(TEXT("Missing angle brackets are rejected"),
		NormalizeVerseSpecifiers(TEXT("public"), Normalized).IsEmpty());
	TestFalse(TEXT("Invalid identifiers are rejected"),
		NormalizeVerseSpecifiers(TEXT("<123bad>"), Normalized).IsEmpty());

	FText Error;
	const FUtf8StringView Source = UTF8TEXTVIEW("Function()<computes> : void = false");
	TArray<uint8> SourceBytes;
	SourceBytes.Append(
		reinterpret_cast<const uint8*>(Source.GetData()),
		Source.Len());
	TSharedPtr<FVerseDocument> Document = FVerseDocument::CreateFromBytes(SourceBytes, Error);
	if (!TestTrue(TEXT("Test document is valid"), Document.IsValid()))
	{
		return false;
	}
	FVerseDocumentSession Session(Document.ToSharedRef());
	const FUtf8String Before = Session.GetCurrentUtf8();
	const bool bReplaced = TryReplaceWithValidatedVerseSpecifiers(
		Session,
		FVerseTextRange(Session.GetRevision(), FVerseByteRange{10, 10}),
		TEXT("not-valid"),
		Error);
	TestFalse(TEXT("Invalid UI text is never applied"), bReplaced);
	TestTrue(TEXT("Rejected specifiers leave source byte-exact"), Session.GetCurrentUtf8() == Before);
	return true;
}

#endif
