#pragma once

#include "CoreTypes.h"
#include "VerseDocument.h"

/** Monotonically increasing identity for one document-session revision. */
struct FVerseDocumentRevision
{
	uint64 Value = 0;

	bool operator==(const FVerseDocumentRevision& Other) const = default;
};

/** A UTF-8 byte range whose offsets are valid only in one document revision. */
struct FVerseTextRange : FVerseByteRange
{
	FVerseDocumentRevision Revision;

	FVerseTextRange() = default;
	FVerseTextRange(FVerseDocumentRevision InRevision, FVerseByteRange InRange)
		: FVerseByteRange(InRange)
		, Revision(InRevision)
	{
	}

	bool operator==(const FVerseTextRange& Other) const
	{
		return Revision == Other.Revision
			&& static_cast<const FVerseByteRange&>(*this) == static_cast<const FVerseByteRange&>(Other);
	}
};
