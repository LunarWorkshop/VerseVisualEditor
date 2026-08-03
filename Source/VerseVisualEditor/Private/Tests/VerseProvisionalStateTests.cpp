#if WITH_DEV_AUTOMATION_TESTS

#include "Editing/VerseProvisionalState.h"

#include "Misc/AutomationTest.h"

namespace
{
	FUtf8StringView View(const FUtf8String& Source)
	{
		return FUtf8StringView(*Source, Source.Len());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseProvisionalStateRebaseTest,
	"VerseVisualEditor.Provisional.RebaseUnrelatedEdits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseProvisionalStateRebaseTest::RunTest(const FString& Parameters)
{
	const FVerseDocumentRevision FirstRevision{1};
	FUtf8String Source(UTF8TEXT("A\ntrue?\nB\n"));
	FVerseProvisionalState State;
	State.Add(FVerseTextRange(FirstRevision, {2, 5}), View(Source));

	Source = FUtf8String(UTF8TEXT("X\nA\ntrue?\nB\n"));
	State.Rebase(View(Source), FVerseDocumentRevision{2});
	TestTrue(
		TEXT("Insertion before provisional content shifts rather than adopts it"),
		State.Contains(FVerseTextRange(FVerseDocumentRevision{2}, {4, 5})));

	Source = FUtf8String(UTF8TEXT("X\nA\ntrue?\n"));
	State.Rebase(View(Source), FVerseDocumentRevision{3});
	TestTrue(
		TEXT("Deletion after provisional content preserves it"),
		State.Contains(FVerseTextRange(FVerseDocumentRevision{3}, {4, 5})));

	Source = FUtf8String(UTF8TEXT("X\nA\nfalse?\n"));
	State.Rebase(View(Source), FVerseDocumentRevision{4});
	TestTrue(TEXT("Editing provisional source adopts it"), State.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseProvisionalStateDirectInteractionTest,
	"VerseVisualEditor.Provisional.DirectInteraction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseProvisionalStateDirectInteractionTest::RunTest(const FString& Parameters)
{
	const FVerseDocumentRevision Revision{7};
	const FUtf8String Source(UTF8TEXT("A\ntrue?\nB\n"));
	FVerseProvisionalState State;
	State.Add(FVerseTextRange(Revision, {2, 5}), View(Source));
	State.AdoptContaining(FVerseTextRange(Revision, {3, 1}));
	TestTrue(
		TEXT("An interaction with a range inside the provisional tile adopts it"),
		State.IsEmpty());
	return true;
}

#endif
