#pragma once

#include "Containers/Utf8String.h"
#include "Templates/SharedPointer.h"
#include "VerseDocument.h"

class FText;

enum class EVerseEditSpanSource : uint8
{
	Original,
	Added,
};

/** One current-source span into either immutable original text or append-only added text. */
struct FVerseEditSpan
{
	EVerseEditSpanSource Source = EVerseEditSpanSource::Original;
	int32 BeginByte = 0;
	int32 NumBytes = 0;

	int32 EndByte() const { return BeginByte + NumBytes; }
	bool operator==(const FVerseEditSpan& Other) const = default;
};

/** Piece-style editable UTF-8 source that never rewrites either backing buffer. */
class FVerseEditBuffer
{
public:
	explicit FVerseEditBuffer(TSharedRef<const FVerseDocument> InOriginalDocument);

	bool Replace(FVerseByteRange CurrentRange, FUtf8StringView Replacement, FText& OutError);
	FUtf8String Materialize() const;

	int32 Len() const { return CurrentLength; }
	const TArray<FVerseEditSpan>& GetSpans() const { return Spans; }
	const FUtf8String& GetAddedText() const { return *AddedText; }

private:
	bool IsCodePointBoundary(int32 CurrentOffset) const;
	FUtf8StringView GetBackingView(const FVerseEditSpan& Span) const;
	void AppendCurrentSlice(
		TArray<FVerseEditSpan>& OutSpans,
		int32 SliceBegin,
		int32 SliceEnd) const;
	static void AppendCoalesced(TArray<FVerseEditSpan>& OutSpans, FVerseEditSpan Span);
	static bool ValidateUtf8(FUtf8StringView Text, int32& OutInvalidByte);

	TSharedRef<const FVerseDocument> OriginalDocument;
	/** One append-only backing store shared by all history snapshots. */
	TSharedRef<FUtf8String> AddedText;
	TArray<FVerseEditSpan> Spans;
	int32 CurrentLength = 0;
};
