#pragma once

#include "Containers/UnrealString.h"
#include "VerseDocumentRevision.h"

class FText;
class FVerseDocumentSession;

/** Returns empty text when the proposed common-form Verse identifier looks valid. */
FText ValidateVerseIdentifier(FStringView Identifier);

/** Validates before replacement and guarantees that rejected identifiers do not mutate the session. */
bool TryReplaceWithValidatedVerseIdentifier(
	FVerseDocumentSession& Session,
	FVerseTextRange Range,
	FStringView Identifier,
	FText& OutError);
