#include "VerseEditBuffer.h"

#include "Internationalization/Text.h"

#define LOCTEXT_NAMESPACE "VerseEditBuffer"

namespace
{
	bool IsContinuationByte(UTF8CHAR Byte)
	{
		return (static_cast<uint8>(Byte) & 0xC0) == 0x80;
	}
}

FVerseEditBuffer::FVerseEditBuffer(TSharedRef<const FVerseDocument> InOriginalDocument)
	: OriginalDocument(MoveTemp(InOriginalDocument))
	, CurrentLength(OriginalDocument->GetOriginalUtf8View().Len())
{
	if (CurrentLength > 0)
	{
		Spans.Add({EVerseEditSpanSource::Original, 0, CurrentLength});
	}
}

bool FVerseEditBuffer::Replace(
	FVerseByteRange CurrentRange,
	FUtf8StringView Replacement,
	FText& OutError)
{
	const int64 EndByte = static_cast<int64>(CurrentRange.BeginByte) + CurrentRange.NumBytes;
	if (!CurrentRange.IsSet()
		|| CurrentRange.BeginByte < 0
		|| CurrentRange.NumBytes < 0
		|| EndByte > CurrentLength)
	{
		OutError = LOCTEXT("InvalidRange", "The edit range is outside the current document.");
		return false;
	}
	if (!IsCodePointBoundary(CurrentRange.BeginByte)
		|| !IsCodePointBoundary(static_cast<int32>(EndByte)))
	{
		OutError = LOCTEXT("SplitCodePoint", "The edit range splits a UTF-8 code point.");
		return false;
	}

	int32 InvalidByte = INDEX_NONE;
	if (!ValidateUtf8(Replacement, InvalidByte))
	{
		OutError = FText::Format(
			LOCTEXT("InvalidReplacementUtf8", "Replacement text is not valid UTF-8 at byte {0}."),
			FText::AsNumber(InvalidByte));
		return false;
	}

	TArray<FVerseEditSpan> NewSpans;
	NewSpans.Reserve(Spans.Num() + 2);
	AppendCurrentSlice(NewSpans, 0, CurrentRange.BeginByte);
	if (!Replacement.IsEmpty())
	{
		const int32 AddedBegin = AddedText.Len();
		AddedText.Append(Replacement);
		AppendCoalesced(
			NewSpans,
			{EVerseEditSpanSource::Added, AddedBegin, Replacement.Len()});
	}
	AppendCurrentSlice(NewSpans, static_cast<int32>(EndByte), CurrentLength);

	Spans = MoveTemp(NewSpans);
	CurrentLength = CurrentLength - CurrentRange.NumBytes + Replacement.Len();
	OutError = FText::GetEmpty();
	return true;
}

FUtf8String FVerseEditBuffer::Materialize() const
{
	FUtf8String Result;
	Result.Reserve(CurrentLength);
	for (const FVerseEditSpan& Span : Spans)
	{
		Result.Append(GetBackingView(Span));
	}
	return Result;
}

bool FVerseEditBuffer::IsCodePointBoundary(int32 CurrentOffset) const
{
	if (CurrentOffset == 0 || CurrentOffset == CurrentLength)
	{
		return true;
	}
	if (CurrentOffset < 0 || CurrentOffset > CurrentLength)
	{
		return false;
	}

	int32 SpanCurrentBegin = 0;
	for (const FVerseEditSpan& Span : Spans)
	{
		const int32 SpanCurrentEnd = SpanCurrentBegin + Span.NumBytes;
		if (CurrentOffset == SpanCurrentBegin || CurrentOffset == SpanCurrentEnd)
		{
			return true;
		}
		if (CurrentOffset < SpanCurrentEnd)
		{
			return !IsContinuationByte(GetBackingView(Span)[CurrentOffset - SpanCurrentBegin]);
		}
		SpanCurrentBegin = SpanCurrentEnd;
	}
	return false;
}

FUtf8StringView FVerseEditBuffer::GetBackingView(const FVerseEditSpan& Span) const
{
	const FUtf8StringView Backing = Span.Source == EVerseEditSpanSource::Original
		? OriginalDocument->GetOriginalUtf8View()
		: FUtf8StringView(*AddedText, AddedText.Len());
	return Backing.Mid(Span.BeginByte, Span.NumBytes);
}

void FVerseEditBuffer::AppendCurrentSlice(
	TArray<FVerseEditSpan>& OutSpans,
	int32 SliceBegin,
	int32 SliceEnd) const
{
	if (SliceBegin >= SliceEnd)
	{
		return;
	}

	int32 SpanCurrentBegin = 0;
	for (const FVerseEditSpan& Span : Spans)
	{
		const int32 SpanCurrentEnd = SpanCurrentBegin + Span.NumBytes;
		const int32 IntersectionBegin = FMath::Max(SliceBegin, SpanCurrentBegin);
		const int32 IntersectionEnd = FMath::Min(SliceEnd, SpanCurrentEnd);
		if (IntersectionBegin < IntersectionEnd)
		{
			AppendCoalesced(
				OutSpans,
				{
					Span.Source,
					Span.BeginByte + IntersectionBegin - SpanCurrentBegin,
					IntersectionEnd - IntersectionBegin,
				});
		}
		if (SpanCurrentEnd >= SliceEnd)
		{
			break;
		}
		SpanCurrentBegin = SpanCurrentEnd;
	}
}

void FVerseEditBuffer::AppendCoalesced(TArray<FVerseEditSpan>& OutSpans, FVerseEditSpan Span)
{
	if (Span.NumBytes <= 0)
	{
		return;
	}
	if (!OutSpans.IsEmpty()
		&& OutSpans.Last().Source == Span.Source
		&& OutSpans.Last().EndByte() == Span.BeginByte)
	{
		OutSpans.Last().NumBytes += Span.NumBytes;
		return;
	}
	OutSpans.Add(Span);
}

bool FVerseEditBuffer::ValidateUtf8(FUtf8StringView Text, int32& OutInvalidByte)
{
	OutInvalidByte = INDEX_NONE;
	for (int32 Index = 0; Index < Text.Len();)
	{
		const uint8 First = static_cast<uint8>(Text[Index]);
		int32 SequenceLength = 0;
		if (First <= 0x7F) SequenceLength = 1;
		else if (First >= 0xC2 && First <= 0xDF) SequenceLength = 2;
		else if (First >= 0xE0 && First <= 0xEF) SequenceLength = 3;
		else if (First >= 0xF0 && First <= 0xF4) SequenceLength = 4;
		else
		{
			OutInvalidByte = Index;
			return false;
		}

		if (Index + SequenceLength > Text.Len())
		{
			OutInvalidByte = Index;
			return false;
		}
		for (int32 ContinuationIndex = 1; ContinuationIndex < SequenceLength; ++ContinuationIndex)
		{
			if (!IsContinuationByte(Text[Index + ContinuationIndex]))
			{
				OutInvalidByte = Index + ContinuationIndex;
				return false;
			}
		}
		if (SequenceLength == 3)
		{
			const uint8 Second = static_cast<uint8>(Text[Index + 1]);
			if ((First == 0xE0 && Second < 0xA0) || (First == 0xED && Second > 0x9F))
			{
				OutInvalidByte = Index;
				return false;
			}
		}
		else if (SequenceLength == 4)
		{
			const uint8 Second = static_cast<uint8>(Text[Index + 1]);
			if ((First == 0xF0 && Second < 0x90) || (First == 0xF4 && Second > 0x8F))
			{
				OutInvalidByte = Index;
				return false;
			}
		}
		Index += SequenceLength;
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
