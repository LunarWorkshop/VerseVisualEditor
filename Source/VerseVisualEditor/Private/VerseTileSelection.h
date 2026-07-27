#pragma once

#include "VerseDocument.h"

/** Single visual-tile selection state for one document canvas. */
class FVerseTileSelection
{
public:
	void Select(FVerseByteRange Range)
	{
		SelectedRange = Range;
	}

	void Clear()
	{
		SelectedRange.Reset();
	}

	bool IsSelected(FVerseByteRange Range) const
	{
		return SelectedRange.IsSet() && SelectedRange.GetValue() == Range;
	}

	const TOptional<FVerseByteRange>& GetSelectedRange() const
	{
		return SelectedRange;
	}

private:
	TOptional<FVerseByteRange> SelectedRange;
};
