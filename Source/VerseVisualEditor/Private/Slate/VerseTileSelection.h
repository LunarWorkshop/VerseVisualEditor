#pragma once

#include "VerseDocumentRevision.h"

/** Single visual-tile selection state for one document canvas. */
class FVerseTileSelection
{
public:
	void Select(FVerseTextRange Range)
	{
		SelectedRange = Range;
	}

	void Clear()
	{
		SelectedRange.Reset();
	}

	bool IsSelected(FVerseTextRange Range) const
	{
		return SelectedRange.IsSet() && SelectedRange.GetValue() == Range;
	}

	const TOptional<FVerseTextRange>& GetSelectedRange() const
	{
		return SelectedRange;
	}

private:
	TOptional<FVerseTextRange> SelectedRange;
};
