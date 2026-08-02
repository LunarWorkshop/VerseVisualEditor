#if WITH_DEV_AUTOMATION_TESTS

#include "VerseDocumentSession.h"
#include "VerseClauseEditing.h"
#include "VerseExpressionActions.h"
#include "VerseExternalChange.h"
#include "VerseIdentifier.h"

#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

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

	TArray<uint8> FileBytes(FUtf8StringView Source, bool bWithBom)
	{
		TArray<uint8> Bytes;
		if (bWithBom)
		{
			Bytes.Append({0xEF, 0xBB, 0xBF});
		}
		Bytes.Append(reinterpret_cast<const uint8*>(Source.GetData()), Source.Len());
		return Bytes;
	}

	bool BytesEqual(TConstArrayView<uint8> Left, TConstArrayView<uint8> Right)
	{
		return Left.Num() == Right.Num()
			&& (Left.IsEmpty() || FMemory::Memcmp(Left.GetData(), Right.GetData(), Left.Num()) == 0);
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
	const FVerseDocumentRevision TransactionRevision = Session.GetRevision();
	const TArray<FVerseDocumentEdit> Transaction = {
		{FVerseTextRange(TransactionRevision, {0, 1}), FUtf8String(UTF8TEXT("A"))},
		{FVerseTextRange(TransactionRevision, {3, 1}), FUtf8String(UTF8TEXT("B"))}};
	TestTrue(TEXT("Non-overlapping localized edits apply atomically"),
		Session.ReplaceMany(Transaction, Error));
	TestEqual(TEXT("An atomic transaction advances one revision"),
		Session.GetRevision().Value, uint64(2));
	TestEqual(TEXT("Both atomic replacements are visible"),
		View(Session.GetCurrentUtf8()), UTF8TEXTVIEW("AβB"));
	TestTrue(TEXT("The successful transaction records a source transition"),
		Session.GetLastSourceTransition().IsSet());
	if (Session.GetLastSourceTransition().IsSet())
	{
		const FVerseDocumentSourceTransition& Transition =
			Session.GetLastSourceTransition().GetValue();
		TestEqual(TEXT("Transition records the prior revision"),
			Transition.PreviousRevision.Value, uint64(1));
		TestEqual(TEXT("Transition records the current revision"),
			Transition.CurrentRevision.Value, uint64(2));
		TestEqual(TEXT("Transition records every atomic edit"),
			Transition.Edits.Num(), 2);
	}
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
	if (Session.GetLastSourceTransition().IsSet())
	{
		const FVerseDocumentTransitionEdit& Edit =
			Session.GetLastSourceTransition()->Edits[0];
		TestEqual(TEXT("Transition retains the old byte span"), Edit.PreviousRange.NumBytes, 5);
		TestEqual(TEXT("Transition computes the replacement byte span"), Edit.CurrentRange.NumBytes, 7);
	}
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseIfPredicateReparseTest,
	"VerseVisualEditor.Prototype.FailureContexts.IfPredicateReparse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseIfPredicateReparseTest::RunTest(const FString& Parameters)
{
	using namespace VerseDocumentSessionTests;
	const TSharedPtr<FVerseDocument> Document = MakeDocument(*this, UTF8TEXTVIEW(
		"ReparseIf(Input : int)<computes> : int =\n"
		"    if (Input > 0):\n"
		"        Input\n"
		"    else:\n"
		"        0\n"));
	if (!Document.IsValid())
	{
		return false;
	}

	FVerseDocumentSession Session(Document.ToSharedRef());
	auto FindFunction = [&Session]() -> const FVerseVisualTile*
	{
		return Session.GetTiles().FindByPredicate([](const FVerseVisualTile& Tile)
		{
			return Tile.Kind == EVerseVisualTileKind::Definition
				&& Tile.DefinitionKind == VerseSyntaxKind::Function;
		});
	};
	const FVerseVisualTile* OriginalFunction = FindFunction();
	if (!TestNotNull(TEXT("Initial if function parses"), OriginalFunction))
	{
		return false;
	}
	const TArray<FVerseVisualTile> OriginalGraph =
		FVerseVisualTileBuilder::BuildFunctionGraph(
			*OriginalFunction,
			Session.GetParseSnapshot());
	if (!TestTrue(TEXT("Initial if owns one failable predicate"),
		OriginalGraph.Num() == 3
			&& !OriginalGraph[1].Children.IsEmpty()
			&& OriginalGraph[1].Children[0].Kind
				== EVerseVisualTileKind::FailableBlock))
	{
		return false;
	}
	const FVerseDocumentRevision OriginalPredicateRevision =
		OriginalGraph[1].Children[0].Range.Revision;

	const FUtf8StringView Current = View(Session.GetCurrentUtf8());
	const FUtf8StringView PredicateTail = UTF8TEXTVIEW("Input > 0)");
	const int32 PredicateOffset = Current.Find(PredicateTail);
	if (!TestTrue(TEXT("Predicate insertion point is found"), PredicateOffset != INDEX_NONE))
	{
		return false;
	}
	FText Error;
	TestTrue(TEXT("A localized predicate edit is accepted"),
		Session.Replace(
			FVerseTextRange(
				Session.GetRevision(),
				{PredicateOffset + PredicateTail.Len() - 1, 0}),
			UTF8TEXTVIEW("; Input < 10"),
			Error));

	const FVerseVisualTile* UpdatedFunction = FindFunction();
	if (!TestNotNull(TEXT("Edited if function reparses"), UpdatedFunction))
	{
		return false;
	}
	const TArray<FVerseVisualTile> UpdatedGraph =
		FVerseVisualTileBuilder::BuildFunctionGraph(
			*UpdatedFunction,
			Session.GetParseSnapshot());
	TestTrue(TEXT("Edited source rebuilds a fresh two-expression predicate block"),
		UpdatedGraph.Num() == 3
			&& !UpdatedGraph[1].Children.IsEmpty()
			&& UpdatedGraph[1].Children[0].Kind
				== EVerseVisualTileKind::FailableBlock
			&& UpdatedGraph[1].Children[0].Children.Num() == 2
			&& UpdatedGraph[1].Children[0].Range.Revision == Session.GetRevision()
			&& UpdatedGraph[1].Children[0].Range.Revision
				!= OriginalPredicateRevision);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseOrderedClauseEditingTest,
	"VerseVisualEditor.Prototype.FailureContexts.OrderedClauseEditing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseOrderedClauseEditingTest::RunTest(const FString& Parameters)
{
	using namespace VerseDocumentSessionTests;
	const TSharedPtr<FVerseDocument> Document = MakeDocument(*this, UTF8TEXTVIEW(
		"EditPredicate(MaybeValue : ?int)<allocates><reads> : int =\r\n"
		"    if (Value := MaybeValue?; <# fixed comment #> var Mutable : int = Value):\n"
		"        Value + Mutable\r\n"
		"    else:\n"
		"        0\r\n"));
	if (!Document.IsValid())
	{
		return false;
	}
	FVerseDocumentSession Session(Document.ToSharedRef());
	auto FindPredicate = [&]() -> const FVerseVisualTile*
	{
		const FVerseVisualTile* Function = Session.GetTiles().FindByPredicate(
			[](const FVerseVisualTile& Tile)
			{
				return Tile.DefinitionKind == VerseSyntaxKind::Function;
			});
		if (Function == nullptr)
		{
			return nullptr;
		}
		const TArray<FVerseVisualTile> Graph =
			FVerseVisualTileBuilder::BuildFunctionGraph(*Function, Session.GetParseSnapshot());
		for (const FVerseVisualTile& Tile : Graph)
		{
			if (Tile.ControlKind == EVerseControlKind::If && !Tile.Children.IsEmpty()
				&& Tile.Children[0].Kind == EVerseVisualTileKind::FailableBlock)
			{
				// Return a stable copy because Graph is local.
				static FVerseVisualTile Predicate;
				Predicate = Tile.Children[0];
				return &Predicate;
			}
		}
		return nullptr;
	};

	const FVerseVisualTile* Predicate = FindPredicate();
	if (!TestNotNull(TEXT("The fixture exposes an editable predicate clause"), Predicate))
	{
		return false;
	}
	TestEqual(TEXT("The initial predicate contains two expressions"),
		Predicate->BodyClause.Items.Num(), 2);

	FVerseExpressionAction InsertAction;
	InsertAction.SourceForm = EVerseExpressionSourceForm::StructuralExpression;
	InsertAction.SourceSpelling = TEXT("MaybeValue?");
	FText Error;
	TestTrue(TEXT("Insertion after the first predicate expression succeeds"),
		FVerseClauseEditing::InsertExpression(
			Session, Predicate->BodyClause, 1, InsertAction, Error));
	TestEqual(TEXT("Insertion advances exactly one document revision"),
		Session.GetRevision().Value, uint64(1));
	TestTrue(TEXT("Mixed line endings remain byte-exact outside the insertion"),
		FString(UTF8_TO_TCHAR(*Session.GetCurrentUtf8())).Contains(TEXT("\r\n    if"))
		&& FString(UTF8_TO_TCHAR(*Session.GetCurrentUtf8())).Contains(TEXT("body")) == false);

	Predicate = FindPredicate();
	if (!TestNotNull(TEXT("The inserted predicate reparses"), Predicate))
	{
		return false;
	}
	TestEqual(TEXT("The predicate now contains three expressions"),
		Predicate->BodyClause.Items.Num(), 3);
	TestTrue(TEXT("Reordering stays within the same clause"),
		FVerseClauseEditing::ReorderExpression(
			Session, Predicate->BodyClause, 0, 2, Error));
	const FString Reordered = FString(UTF8_TO_TCHAR(*Session.GetCurrentUtf8()));
	TestTrue(TEXT("Ambiguous comment trivia remains at its original clause slot"),
		Reordered.Contains(TEXT("<# fixed comment #>")));

	Predicate = FindPredicate();
	if (!TestNotNull(TEXT("The reordered predicate reparses"), Predicate))
	{
		return false;
	}
	TestTrue(TEXT("Deleting the middle expression succeeds"),
		FVerseClauseEditing::DeleteExpression(
			Session, Predicate->BodyClause, 1, Error));
	Predicate = FindPredicate();
	TestTrue(TEXT("Deletion rebuilds a two-expression clause"),
		Predicate != nullptr && Predicate->BodyClause.Items.Num() == 2);
	if (Predicate != nullptr)
	{
		TestTrue(TEXT("Deleting down to one failable expression succeeds"),
			FVerseClauseEditing::DeleteExpression(
				Session, Predicate->BodyClause, 1, Error));
		Predicate = FindPredicate();
	}
	if (TestNotNull(TEXT("The one-item predicate reparses"), Predicate))
	{
		FVerseTextRange ProvisionalRange;
		TestTrue(TEXT("Deleting the final condition installs a provisional placeholder"),
			FVerseClauseEditing::DeleteExpression(
				Session, Predicate->BodyClause, 0, Error, &ProvisionalRange));
		Predicate = FindPredicate();
		TestTrue(TEXT("A failable condition never persists as an empty clause"),
			Predicate != nullptr
			&& Predicate->BodyClause.Items.Num() == 1
			&& ProvisionalRange.IsSet()
			&& Session.GetParseSnapshot().GetDocument()->DecodeOriginalRange(
				ProvisionalRange) == TEXT("true?"));
	}
	TestTrue(TEXT("Unowned comment bytes survive insertion, reorder, and delete"),
		FString(UTF8_TO_TCHAR(*Session.GetCurrentUtf8())).Contains(
			TEXT("<# fixed comment #>")));
	const FVerseVisualTile* UpdatedFunction = Session.GetTiles().FindByPredicate(
		[](const FVerseVisualTile& Tile)
		{
			return Tile.DefinitionKind == VerseSyntaxKind::Function;
		});
	if (TestNotNull(TEXT("The containing function remains recognized"), UpdatedFunction))
	{
		FVerseExpressionAction FunctionInsert;
		FunctionInsert.SourceForm = EVerseExpressionSourceForm::StructuralExpression;
		FunctionInsert.SourceSpelling = TEXT("0");
		TestTrue(TEXT("The same editor inserts into an ordinary function body"),
			FVerseClauseEditing::InsertExpression(
				Session, UpdatedFunction->BodyClause, 0, FunctionInsert, Error));
		const FVerseVisualTile* RebuiltFunction = Session.GetTiles().FindByPredicate(
			[](const FVerseVisualTile& Tile)
			{
				return Tile.DefinitionKind == VerseSyntaxKind::Function;
			});
		TestTrue(TEXT("Function-body insertion reparses as another ordered item"),
			RebuiltFunction != nullptr && RebuiltFunction->BodyClause.Items.Num() == 2);
	}

	const TSharedPtr<FVerseDocument> EmptyDocument = MakeDocument(
		*this,
		UTF8TEXTVIEW("EmptyFunction()<computes> : void = {}\n"));
	if (TestTrue(TEXT("Empty function fixture parses"), EmptyDocument.IsValid()))
	{
		FVerseDocumentSession EmptySession(EmptyDocument.ToSharedRef());
		const FVerseVisualTile* EmptyFunction = EmptySession.GetTiles().FindByPredicate(
			[](const FVerseVisualTile& Tile)
			{
				return Tile.DefinitionKind == VerseSyntaxKind::Function;
			});
		if (TestNotNull(TEXT("Empty function exposes its editable clause"), EmptyFunction))
		{
			FVerseExpressionAction IfAction;
			IfAction.SourceForm = EVerseExpressionSourceForm::StructuralExpression;
			IfAction.SourceSpelling = TEXT("if (true?) {}");
			FVerseTextRange InsertedIfRange;
			TestTrue(
				TEXT("An empty if with a valid default condition inserts into an empty function"),
				FVerseClauseEditing::InsertExpression(
					EmptySession,
					EmptyFunction->BodyClause,
					0,
					IfAction,
					Error,
					&InsertedIfRange));
			TestEqual(
				TEXT("Clause insertion reports the exact generated expression range"),
				EmptySession.GetParseSnapshot().GetDocument()->DecodeOriginalRange(InsertedIfRange),
				FString(TEXT("if (true?) {}")));
			const FVerseVisualTile* RebuiltEmptyFunction =
				EmptySession.GetTiles().FindByPredicate(
					[](const FVerseVisualTile& Tile)
					{
						return Tile.DefinitionKind == VerseSyntaxKind::Function;
					});
			if (TestNotNull(TEXT("Function remains recognized after if insertion"), RebuiltEmptyFunction))
			{
				const TArray<FVerseVisualTile> Graph =
					FVerseVisualTileBuilder::BuildFunctionGraph(
						*RebuiltEmptyFunction,
						EmptySession.GetParseSnapshot());
				const FVerseVisualTile* IfTile = Graph.FindByPredicate(
					[](const FVerseVisualTile& Tile)
					{
						return Tile.ControlKind == EVerseControlKind::If;
					});
				const bool bHasGeneratedCondition =
					IfTile != nullptr
						&& !IfTile->Children.IsEmpty()
						&& IfTile->Children[0].Kind
							== EVerseVisualTileKind::FailableBlock
						&& IfTile->Children[0].Children.Num() == 1;
				TestTrue(
					TEXT("Inserted if rebuilds with its automatic failable context"),
					bHasGeneratedCondition);
				if (bHasGeneratedCondition)
				{
					const FVerseVisualClauseDescriptor& PredicateClause =
						IfTile->Children[0].BodyClause;
					FVerseExpressionAction Replacement;
					Replacement.SourceForm = EVerseExpressionSourceForm::StructuralExpression;
					Replacement.SourceSpelling = TEXT("false?");
					FVerseTextRange ReplacementRange;
					const bool bReplaced = FVerseClauseEditing::ReplaceExpression(
						EmptySession,
						PredicateClause,
						0,
						Replacement,
						Error,
						&ReplacementRange);
					TestTrue(
						*FString::Printf(
							TEXT("A generated predicate can be replaced without inserting a second item: %s"),
							*Error.ToString()),
						bReplaced);
					TestEqual(
						TEXT("Replacement reports the exact replacement expression range"),
						EmptySession.GetParseSnapshot().GetDocument()->DecodeOriginalRange(
							ReplacementRange),
						FString(TEXT("false?")));
					const FString ReplacedSource =
						FString(UTF8_TO_TCHAR(*EmptySession.GetCurrentUtf8()));
					TestTrue(
						TEXT("Replacement changes the predicate in place"),
						ReplacedSource.Contains(TEXT("if (false?) {}")));
					TestFalse(
						TEXT("Replacement removes the generated placeholder"),
						ReplacedSource.Contains(TEXT("true?")));
				}
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseControlBranchDeletionTest,
	"VerseVisualEditor.Prototype.Functions.ControlBranchDeletion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseControlBranchDeletionTest::RunTest(const FString& Parameters)
{
	using namespace VerseDocumentSessionTests;
	const TSharedPtr<FVerseDocument> Document = MakeDocument(*this, UTF8TEXTVIEW(
		"DeleteBranch(Input : int)<computes> : int =\n"
		"    if (Input > 0):\n"
		"        Input\n"
		"        0\n"
		"    else:\n"
		"        -1\n"));
	if (!Document.IsValid())
	{
		return false;
	}
	FVerseDocumentSession Session(Document.ToSharedRef());
	const FVerseVisualTile* Function = Session.GetTiles().FindByPredicate(
		[](const FVerseVisualTile& Tile)
		{
			return Tile.DefinitionKind == VerseSyntaxKind::Function;
		});
	if (!TestNotNull(TEXT("Control-branch deletion fixture parses"), Function))
	{
		return false;
	}
	const TArray<FVerseVisualTile> Graph =
		FVerseVisualTileBuilder::BuildFunctionGraph(*Function, Session.GetParseSnapshot());
	const FVerseVisualTile* IfTile = Graph.FindByPredicate(
		[](const FVerseVisualTile& Tile)
		{
			return Tile.ControlKind == EVerseControlKind::If;
		});
	const FVerseVisualTile* BranchLiteral = IfTile != nullptr
		? IfTile->Children.FindByPredicate(
			[&Session](const FVerseVisualTile& Tile)
			{
				return Tile.bStatementLevel
					&& Tile.LiteralKind == EVerseLiteralKind::Integer
					&& Session.GetParseSnapshot().GetDocument()
						->DecodeOriginalRange(Tile.Range) == TEXT("0");
			})
		: nullptr;
	if (!TestNotNull(TEXT("True-branch integer literal is represented"), BranchLiteral))
	{
		return false;
	}
	if (!TestTrue(TEXT("Branch literal retains its owning ordered clause"),
		BranchLiteral->EditableClause.IsSet()
			&& BranchLiteral->ClauseItemIndex == 1))
	{
		return false;
	}
	FText Error;
	const bool bDeleted = FVerseClauseEditing::DeleteExpression(
		Session,
		BranchLiteral->EditableClause.GetValue(),
		BranchLiteral->ClauseItemIndex,
		Error);
	TestTrue(
		*FString::Printf(TEXT("Deleting the selected branch literal succeeds: %s"),
			*Error.ToString()),
		bDeleted);
	const FString Source(UTF8_TO_TCHAR(*Session.GetCurrentUtf8()));
	TestFalse(TEXT("Deleting the branch item removes its source and separator"),
		Source.Contains(TEXT("        Input\n        0")));
	const FVerseVisualTile* RebuiltFunction = Session.GetTiles().FindByPredicate(
		[](const FVerseVisualTile& Tile)
		{
			return Tile.DefinitionKind == VerseSyntaxKind::Function;
		});
	const TArray<FVerseVisualTile> RebuiltGraph = RebuiltFunction != nullptr
		? FVerseVisualTileBuilder::BuildFunctionGraph(
			*RebuiltFunction, Session.GetParseSnapshot())
		: TArray<FVerseVisualTile>();
	const FVerseVisualTile* RebuiltIf = RebuiltGraph.FindByPredicate(
		[](const FVerseVisualTile& Tile)
		{
			return Tile.ControlKind == EVerseControlKind::If;
		});
	const FVerseVisualExpressionDescriptor::FControlRegion* RebuiltBody =
		RebuiltIf != nullptr
			? RebuiltIf->ControlRegions.FindByPredicate(
				[](const FVerseVisualExpressionDescriptor::FControlRegion& Region)
				{
					return Region.Kind == EVerseControlRegionKind::Body;
				})
			: nullptr;
	TestTrue(TEXT("The true branch reparses with only its preceding expression"),
		RebuiltBody != nullptr && RebuiltBody->OperandCount == 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseSocketSourceEditingTest,
	"VerseVisualEditor.Prototype.Functions.SocketSourceEditing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseSocketSourceEditingTest::RunTest(const FString& Parameters)
{
	using namespace VerseDocumentSessionTests;
	auto FindFunctionGraph = [](FVerseDocumentSession& Session)
	{
		const FVerseVisualTile* Function = Session.GetTiles().FindByPredicate(
			[](const FVerseVisualTile& Tile)
			{
				return Tile.DefinitionKind == VerseSyntaxKind::Function;
			});
		return Function != nullptr
			? FVerseVisualTileBuilder::BuildFunctionGraph(
				*Function, Session.GetParseSnapshot())
			: TArray<FVerseVisualTile>();
	};

	const TSharedPtr<FVerseDocument> BranchDocument = MakeDocument(*this, UTF8TEXTVIEW(
		"Branch(Input : int)<computes> : void =\n"
		"    if (Input > 0):\n"
		"        Input\n"));
	if (!BranchDocument.IsValid())
	{
		return false;
	}
	FVerseDocumentSession BranchSession(BranchDocument.ToSharedRef());
	TArray<FVerseVisualTile> BranchGraph = FindFunctionGraph(BranchSession);
	const FVerseVisualTile* IfTile = BranchGraph.FindByPredicate(
		[](const FVerseVisualTile& Tile)
		{
			return Tile.ControlKind == EVerseControlKind::If;
		});
	const FVerseVisualSocketId TrueSocket{
		EVerseVisualSocketDirection::Output, EVerseVisualSocketRole::Execution, 1};
	const FVerseVisualSocketId FalseSocket{
		EVerseVisualSocketDirection::Output, EVerseVisualSocketRole::Execution, 2};
	if (!TestNotNull(TEXT("If without else is represented"), IfTile))
	{
		return false;
	}
	TestTrue(TEXT("True branch home plate owns its body insertion target"),
		IfTile->FindSocketInsertionTarget(TrueSocket) != nullptr
		&& IfTile->FindSocketInsertionTarget(TrueSocket)->Kind
			== EVerseVisualSocketInsertionKind::Clause);
	const FVerseVisualSocketInsertionTarget* MissingElse =
		IfTile->FindSocketInsertionTarget(FalseSocket);
	if (!TestTrue(TEXT("False branch home plate can create a missing else clause"),
		MissingElse != nullptr
		&& MissingElse->Kind == EVerseVisualSocketInsertionKind::MissingElseClause))
	{
		return false;
	}
	FVerseExpressionAction ElseAction;
	ElseAction.SourceForm = EVerseExpressionSourceForm::Literal;
	ElseAction.SourceSpelling = TEXT("0");
	FText Error;
	TestTrue(*FString::Printf(TEXT("False branch insertion succeeds: %s"), *Error.ToString()),
		FVerseClauseEditing::AddElseExpression(
			BranchSession,
			MissingElse->OwnerExpressionRange,
			MissingElse->Clause.PunctuationStyle,
			ElseAction,
			Error));
	TestTrue(TEXT("False branch insertion writes an else body"),
		FString(UTF8_TO_TCHAR(*BranchSession.GetCurrentUtf8())).Contains(
			TEXT("else:\n        0")));
	BranchGraph = FindFunctionGraph(BranchSession);
	IfTile = BranchGraph.FindByPredicate(
		[](const FVerseVisualTile& Tile)
		{
			return Tile.ControlKind == EVerseControlKind::If;
		});
	TestTrue(TEXT("Reparsed false home plate targets the real else clause"),
		IfTile != nullptr
		&& IfTile->FindSocketInsertionTarget(FalseSocket) != nullptr
		&& IfTile->FindSocketInsertionTarget(FalseSocket)->Kind
			== EVerseVisualSocketInsertionKind::Clause);

	const TSharedPtr<FVerseDocument> PredicateDocument = MakeDocument(*this, UTF8TEXTVIEW(
		"Predicate(Maybe : ?int)<decides><computes> : void =\n"
		"    if (Value := Maybe?):\n"
		"        false?\n"));
	if (!PredicateDocument.IsValid())
	{
		return false;
	}
	FVerseDocumentSession PredicateSession(PredicateDocument.ToSharedRef());
	TArray<FVerseVisualTile> PredicateGraph = FindFunctionGraph(PredicateSession);
	const FVerseVisualTile* PredicateIf = PredicateGraph.FindByPredicate(
		[](const FVerseVisualTile& Tile)
		{
			return Tile.ControlKind == EVerseControlKind::If;
		});
	const FVerseVisualTile* Failable = PredicateIf != nullptr
		? PredicateIf->Children.FindByPredicate(
			[](const FVerseVisualTile& Tile)
			{
				return Tile.Kind == EVerseVisualTileKind::FailableBlock;
			})
		: nullptr;
	if (!TestNotNull(TEXT("Predicate exposes its failable context"), Failable)
		|| !TestTrue(TEXT("Predicate starts with one definition"),
			Failable->Children.Num() == 1))
	{
		return false;
	}
	const FVerseVisualTile& Definition = Failable->Children[0];
	if (!TestTrue(TEXT("Definition binding output owns an after-definition insertion target"),
		!Definition.GetValueOutputs().IsEmpty()
		&& Definition.FindSocketInsertionTarget(Definition.GetValueOutputs()[0].Id) != nullptr))
	{
		return false;
	}
	const FVerseVisualSocketInsertionTarget* AfterDefinition =
		Definition.FindSocketInsertionTarget(Definition.GetValueOutputs()[0].Id);
	FVerseExpressionAction Consumer;
	Consumer.SourceForm = EVerseExpressionSourceForm::InfixOperator;
	Consumer.SourceSpelling = TEXT("=");
	Consumer.InputDefaultSources = {TEXT("0"), TEXT("0")};
	Consumer.BoundInputIndex = 0;
	TestTrue(*FString::Printf(TEXT("Binding-output insertion succeeds: %s"), *Error.ToString()),
		FVerseClauseEditing::InsertExpression(
			PredicateSession,
			AfterDefinition->Clause,
			AfterDefinition->InsertIndex,
			Consumer,
			Error,
			nullptr,
			TEXT("Value")));
	PredicateGraph = FindFunctionGraph(PredicateSession);
	PredicateIf = PredicateGraph.FindByPredicate(
		[](const FVerseVisualTile& Tile)
		{
			return Tile.ControlKind == EVerseControlKind::If;
		});
	Failable = PredicateIf != nullptr
		? PredicateIf->Children.FindByPredicate(
			[](const FVerseVisualTile& Tile)
			{
				return Tile.Kind == EVerseVisualTileKind::FailableBlock;
			})
		: nullptr;
	TestTrue(TEXT("The failable context expands around the inserted consumer"),
		Failable != nullptr && Failable->Children.Num() == 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseDocumentSessionSaveTest,
	"VerseVisualEditor.Foundation.DocumentSession.Save",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseDocumentSessionSaveTest::RunTest(const FString& Parameters)
{
	using namespace VerseDocumentSessionTests;
	const TArray<uint8> OriginalBytes = FileBytes(
		UTF8TEXTVIEW("Alpha := class {}\r\n# lf follows\n# cr follows\r"),
		true);
	FText Error;
	const TSharedPtr<FVerseDocument> Document = FVerseDocument::CreateFromBytes(OriginalBytes, Error);
	if (!TestTrue(TEXT("BOM and mixed-line-ending document loads"), Document.IsValid()))
	{
		return false;
	}

	FVerseDocumentSession Session(Document.ToSharedRef());
	TestFalse(TEXT("New session begins clean"), Session.IsDirty());
	TestTrue(TEXT("Rename replacement succeeds"),
		Session.Replace(FVerseTextRange(Session.GetRevision(), {0, 5}), UTF8TEXTVIEW("Beta"), Error));
	TestTrue(TEXT("A replacement makes the session dirty"), Session.IsDirty());
	TestFalse(TEXT("Current and saved content states differ while dirty"),
		Session.GetContentStateId() == Session.GetSavedContentStateId());

	const FString TestDirectory = FPaths::Combine(
		FPaths::ProjectSavedDir(),
		TEXT("Automation"),
		FString::Printf(TEXT("VerseVisualEditor-%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	IFileManager::Get().MakeDirectory(*TestDirectory, true);
	const FString TargetPath = FPaths::Combine(TestDirectory, TEXT("Mixed.verse"));
	const TArray<uint8> OldTarget = FileBytes(UTF8TEXTVIEW("old"), false);
	FFileHelper::SaveArrayToFile(OldTarget, *TargetPath);

	TestTrue(TEXT("Same-directory temporary save replaces the target"), Session.SaveToFile(TargetPath, Error));
	TestFalse(TEXT("Successful replacement marks the current content state saved"), Session.IsDirty());
	TArray<uint8> SavedBytes;
	TestTrue(TEXT("Saved Verse file can be read"), FFileHelper::LoadFileToArray(SavedBytes, *TargetPath));
	const TArray<uint8> ExpectedBytes = FileBytes(
		UTF8TEXTVIEW("Beta := class {}\r\n# lf follows\n# cr follows\r"),
		true);
	TestTrue(TEXT("Save restores BOM and preserves every unaffected mixed line-ending byte"),
		BytesEqual(SavedBytes, ExpectedBytes));

	TestTrue(TEXT("A second rename makes the saved session dirty again"),
		Session.Replace(FVerseTextRange(Session.GetRevision(), {0, 4}), UTF8TEXTVIEW("Gamma"), Error));
	const FVerseContentStateId SavedCheckpoint = Session.GetSavedContentStateId();
	const FString DirectoryAsTarget = FPaths::Combine(TestDirectory, TEXT("CannotReplaceDirectory.verse"));
	IFileManager::Get().MakeDirectory(*DirectoryAsTarget);
	TestFalse(TEXT("Save fails when target replacement cannot complete"),
		Session.SaveToFile(DirectoryAsTarget, Error));
	TestTrue(TEXT("Failed save leaves local contents dirty"), Session.IsDirty());
	TestTrue(TEXT("Failed save retains the previous saved checkpoint"),
		Session.GetSavedContentStateId() == SavedCheckpoint);

	IFileManager::Get().DeleteDirectory(*TestDirectory, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVerseRenameAndExternalChangePolicyTest,
	"VerseVisualEditor.Foundation.DocumentSession.RenameAndExternalChangePolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVerseRenameAndExternalChangePolicyTest::RunTest(const FString& Parameters)
{
	using namespace VerseDocumentSessionTests;

	TestTrue(TEXT("Common Verse identifier is accepted"), ValidateVerseIdentifier(TEXT("My_Class2")).IsEmpty());
	TestFalse(TEXT("Empty identifier reports feedback"), ValidateVerseIdentifier(TEXT("")).IsEmpty());
	TestFalse(TEXT("Identifier beginning with a number reports feedback"), ValidateVerseIdentifier(TEXT("2Class")).IsEmpty());
	TestFalse(TEXT("Identifier containing punctuation reports feedback"), ValidateVerseIdentifier(TEXT("My-Class")).IsEmpty());
	TestFalse(TEXT("Reserved Verse identifier reports feedback"), ValidateVerseIdentifier(TEXT("class")).IsEmpty());
	TestFalse(TEXT("Parser-reserved Verse symbol reports feedback"), ValidateVerseIdentifier(TEXT("array")).IsEmpty());
	TestFalse(TEXT("Reserved underscore reports feedback"), ValidateVerseIdentifier(TEXT("_")).IsEmpty());

	const TSharedPtr<FVerseDocument> RenameDocument = MakeDocument(*this, UTF8TEXTVIEW("Original := class {}"));
	if (!RenameDocument.IsValid())
	{
		return false;
	}
	FVerseDocumentSession RenameSession(RenameDocument.ToSharedRef());
	const FVerseDocumentRevision OriginalRevision = RenameSession.GetRevision();
	const FVerseContentStateId OriginalContentState = RenameSession.GetContentStateId();
	FText RenameError;
	TestFalse(TEXT("An invalid identifier is rejected before replacement"),
		TryReplaceWithValidatedVerseIdentifier(
			RenameSession,
			FVerseTextRange(RenameSession.GetRevision(), {0, 8}),
			TEXT("123Invalid"),
			RenameError));
	TestTrue(TEXT("Rejected identifier does not change the document revision"),
		RenameSession.GetRevision() == OriginalRevision);
	TestTrue(TEXT("Rejected identifier does not dirty the document"),
		RenameSession.GetContentStateId() == OriginalContentState && !RenameSession.IsDirty());
	TestEqual(TEXT("Rejected identifier does not change source"),
		View(RenameSession.GetCurrentUtf8()), UTF8TEXTVIEW("Original := class {}"));

	TestTrue(TEXT("A duplicate watcher notification is ignored"),
		DetermineVerseExternalChangeAction(true, false) == EVerseExternalChangeAction::Ignore);
	TestTrue(TEXT("A clean external change reloads immediately"),
		DetermineVerseExternalChangeAction(false, false) == EVerseExternalChangeAction::Reload);
	TestTrue(TEXT("A dirty external change requires reload-or-keep-local choice"),
		DetermineVerseExternalChangeAction(false, true) == EVerseExternalChangeAction::PromptReloadOrKeepLocal);
	return true;
}

#endif
