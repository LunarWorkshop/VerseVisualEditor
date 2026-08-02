#pragma once

#include "Containers/Array.h"
#include "Containers/Utf8String.h"
#include "VerseDocumentRevision.h"

/** Transient, revision-aware ownership of provisional visual tiles. */
class FVerseProvisionalState
{
public:
	void Add(FVerseTextRange Range, FUtf8StringView CurrentSource);
	void Reset();
	void Adopt(FVerseTextRange Range);
	void AdoptContaining(FVerseTextRange EditedRange);
	void Rebase(FUtf8StringView CurrentSource, FVerseDocumentRevision CurrentRevision);

	bool Contains(FVerseTextRange Range) const { return Ranges.Contains(Range); }
	TConstArrayView<FVerseTextRange> GetRanges() const { return Ranges; }
	bool IsEmpty() const { return Ranges.IsEmpty(); }

private:
	TArray<FVerseTextRange> Ranges;
	FUtf8String SourceSnapshot;
	FVerseDocumentRevision SnapshotRevision;
	bool bHasSnapshot = false;
};
