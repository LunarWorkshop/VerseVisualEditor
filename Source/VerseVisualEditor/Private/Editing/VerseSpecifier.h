#pragma once

#include "Containers/UnrealString.h"
#include "VerseDocumentRevision.h"

class FText;
class FVerseDocumentSession;

/** Validates zero or more canonical <identifier> groups and returns normalized source text. */
FText NormalizeVerseSpecifiers(FStringView ProposedText, FString& OutNormalizedText);

/** Validates before replacement so malformed specifier syntax can never reach the document. */
bool TryReplaceWithValidatedVerseSpecifiers(
	FVerseDocumentSession& Session,
	FVerseTextRange Range,
	FStringView ProposedText,
	FText& OutError);
