#include "Editing/VerseProvisionalState.h"

void FVerseProvisionalState::Reset()
{
	Ranges.Reset();
	SourceSnapshot.Reset();
	bHasSnapshot = false;
}

void FVerseProvisionalState::Add(
	FVerseTextRange Range,
	FUtf8StringView CurrentSource)
{
	Ranges.AddUnique(Range);
	SourceSnapshot = FUtf8String(CurrentSource);
	SnapshotRevision = Range.Revision;
	bHasSnapshot = true;
}

void FVerseProvisionalState::Adopt(FVerseTextRange Range)
{
	Ranges.Remove(Range);
	if (Ranges.IsEmpty())
	{
		SourceSnapshot.Reset();
		bHasSnapshot = false;
	}
}

void FVerseProvisionalState::AdoptContaining(FVerseTextRange EditedRange)
{
	Ranges.RemoveAll([EditedRange](const FVerseTextRange& Provisional)
	{
		return Provisional.Revision == EditedRange.Revision
			&& EditedRange.BeginByte >= Provisional.BeginByte
			&& EditedRange.EndByte() <= Provisional.EndByte();
	});
	if (Ranges.IsEmpty())
	{
		SourceSnapshot.Reset();
		bHasSnapshot = false;
	}
}

void FVerseProvisionalState::Rebase(
	FUtf8StringView CurrentSource,
	FVerseDocumentRevision CurrentRevision)
{
	if (Ranges.IsEmpty())
	{
		SourceSnapshot.Reset();
		bHasSnapshot = false;
		return;
	}
	if (!bHasSnapshot)
	{
		SourceSnapshot = FUtf8String(CurrentSource);
		SnapshotRevision = CurrentRevision;
		for (FVerseTextRange& Range : Ranges)
		{
			Range.Revision = CurrentRevision;
		}
		bHasSnapshot = true;
		return;
	}
	if (SnapshotRevision == CurrentRevision)
	{
		return;
	}

	const FUtf8StringView PreviousSource(*SourceSnapshot, SourceSnapshot.Len());
	const int32 PreviousLength = PreviousSource.Len();
	const int32 CurrentLength = CurrentSource.Len();
	int32 CommonPrefix = 0;
	while (CommonPrefix < PreviousLength
		&& CommonPrefix < CurrentLength
		&& PreviousSource[CommonPrefix] == CurrentSource[CommonPrefix])
	{
		++CommonPrefix;
	}
	int32 CommonSuffix = 0;
	while (CommonSuffix < PreviousLength - CommonPrefix
		&& CommonSuffix < CurrentLength - CommonPrefix
		&& PreviousSource[PreviousLength - CommonSuffix - 1]
			== CurrentSource[CurrentLength - CommonSuffix - 1])
	{
		++CommonSuffix;
	}

	const int32 PreviousChangeEnd = PreviousLength - CommonSuffix;
	const int32 CurrentChangeEnd = CurrentLength - CommonSuffix;
	const int32 Delta = CurrentChangeEnd - PreviousChangeEnd;
	for (int32 Index = Ranges.Num() - 1; Index >= 0; --Index)
	{
		FVerseTextRange& Range = Ranges[Index];
		if (Range.Revision != SnapshotRevision)
		{
			Ranges.RemoveAt(Index);
			continue;
		}
		if (Range.EndByte() <= CommonPrefix)
		{
			Range.Revision = CurrentRevision;
			continue;
		}
		if (Range.BeginByte >= PreviousChangeEnd)
		{
			Range.BeginByte += Delta;
			Range.Revision = CurrentRevision;
			continue;
		}

		// The source represented by this provisional tile was edited directly.
		Ranges.RemoveAt(Index);
	}

	SourceSnapshot = FUtf8String(CurrentSource);
	SnapshotRevision = CurrentRevision;
	bHasSnapshot = !Ranges.IsEmpty();
	if (!bHasSnapshot)
	{
		SourceSnapshot.Reset();
	}
}
