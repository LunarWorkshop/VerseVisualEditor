#pragma once

#include "CoreMinimal.h"
#include "VerseParseSnapshot.h"
#include "VerseVisualEditorSettings.h"

struct FVerseVisualClauseDescriptor;

/** Fully resolved conventions used for newly emitted or explicitly reformatted source. */
struct FVerseFormattingStyleProfile
{
	EVerseClauseDelimiter BodyDelimiter = EVerseClauseDelimiter::Colon;
	EVerseLineEnding LineEnding = EVerseLineEnding::Lf;
	EVerseBracePlacement BracePlacement = EVerseBracePlacement::SameLine;
	EVerseSeparatorToken StatementSeparatorToken = EVerseSeparatorToken::None;
	EVerseSeparatorLayout StatementSeparatorLayout = EVerseSeparatorLayout::Newline;
	EVerseSeparatorToken FailureSeparatorToken = EVerseSeparatorToken::Semicolon;
	EVerseSeparatorLayout FailureSeparatorLayout = EVerseSeparatorLayout::TokenAndSpace;
	FString IndentationUnit = TEXT("    ");
	bool bSpaceAroundOperators = true;
	bool bSpaceAfterComma = true;
	bool bSpaceAfterSemicolon = true;
	bool bSpaceInsideParentheses = false;
};

/** Dominant source conventions. Unset axes deliberately defer to settings. */
struct FVerseFormattingStyleEvidence
{
	TOptional<EVerseClauseDelimiter> BodyDelimiter;
	TOptional<EVerseLineEnding> LineEnding;
	TOptional<EVerseBracePlacement> BracePlacement;
	TOptional<FString> IndentationUnit;
	TOptional<FVerseSeparatorDescriptor> StatementSeparator;
	TOptional<bool> SpaceAroundOperators;
};

class FVerseFormattingStyleAnalyzer
{
public:
	static FVerseFormattingStyleEvidence Analyze(
		const FVerseDocument& Document,
		const FVerseParseSnapshot& Snapshot);
};

class FVerseFormattingStyleResolver
{
public:
	static FVerseFormattingStyleProfile ResolveDefaults();
	static FVerseFormattingStyleProfile Resolve(
		const FVerseDocument& Document,
		const FVerseParseSnapshot& Snapshot,
		const FVerseClauseSyntaxDescriptor* DestinationClause = nullptr);
	static FVerseFormattingStyleProfile Resolve(
		const FVerseDocument& Document,
		const FVerseParseSnapshot& Snapshot,
		const FVerseVisualClauseDescriptor& DestinationClause);
};

/** The only source of generated formatting punctuation and horizontal whitespace. */
class FVerseSyntaxEmitter
{
public:
	static FString LineEnding(const FVerseFormattingStyleProfile& Style);
	static FString Separator(
		EVerseSeparatorToken Token,
		EVerseSeparatorLayout Layout,
		int32 BlankLineCount,
		const FVerseFormattingStyleProfile& Style,
		FStringView FollowingIndentation = {});
	static FString Infix(
		FStringView Left,
		FStringView Operator,
		FStringView Right,
		const FVerseFormattingStyleProfile& Style);
	static FString Prefix(
		FStringView Operator,
		FStringView Operand,
		const FVerseFormattingStyleProfile& Style);
	static FString Arguments(
		TConstArrayView<FString> Arguments,
		bool bFailureCall,
		const FVerseFormattingStyleProfile& Style);
	static FString Definition(
		bool bMutable,
		FStringView Name,
		FStringView Type,
		FStringView Initializer,
		const FVerseFormattingStyleProfile& Style);
	/** Complete, source-safe initial if expression for the selected project body syntax. */
	static FString IfTemplate(const FVerseFormattingStyleProfile& Style);
	/** Complete sync expression with the compiler-required two provisional arms. */
	static FString SyncTemplate(const FVerseFormattingStyleProfile& Style);
};
