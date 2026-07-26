#include "VerseDocument.h"

#include "Containers/StringConv.h"
#include "Misc/FileHelper.h"

#define LOCTEXT_NAMESPACE "VerseDocument"

namespace VerseDocument
{
	constexpr uint8 Utf8Bom[] = {0xEF, 0xBB, 0xBF};

	bool IsContinuationByte(uint8 Byte)
	{
		return (Byte & 0xC0) == 0x80;
	}
}

TSharedPtr<FVerseDocument> FVerseDocument::CreateFromBytes(
	TConstArrayView<uint8> Bytes,
	FText& OutError)
{
	TSharedPtr<FVerseDocument> Document = MakeShared<FVerseDocument>();
	if (!Document->Initialize(Bytes, OutError))
	{
		return nullptr;
	}
	return Document;
}

TSharedPtr<FVerseDocument> FVerseDocument::LoadFromFile(
	const FString& FilePath,
	FText& OutError)
{
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *FilePath))
	{
		OutError = FText::Format(
			LOCTEXT("LoadFailed", "Could not read Verse file: {0}"),
			FText::FromString(FilePath));
		return nullptr;
	}

	return CreateFromBytes(Bytes, OutError);
}

bool FVerseDocument::Initialize(TConstArrayView<uint8> Bytes, FText& OutError)
{
	if (Bytes.Num() >= 2
		&& ((Bytes[0] == 0xFF && Bytes[1] == 0xFE)
			|| (Bytes[0] == 0xFE && Bytes[1] == 0xFF)))
	{
		OutError = LOCTEXT("Utf16NotSupported", "Only UTF-8 Verse files are currently supported.");
		return false;
	}

	bHasUtf8Bom = Bytes.Num() >= 3
		&& Bytes[0] == VerseDocument::Utf8Bom[0]
		&& Bytes[1] == VerseDocument::Utf8Bom[1]
		&& Bytes[2] == VerseDocument::Utf8Bom[2];

	const TConstArrayView<uint8> ContentBytes = Bytes.RightChop(bHasUtf8Bom ? 3 : 0);
	int32 InvalidByte = INDEX_NONE;
	if (!ValidateUtf8(ContentBytes, InvalidByte))
	{
		OutError = FText::Format(
			LOCTEXT("InvalidUtf8", "The Verse file is not valid UTF-8 at content byte {0}."),
			FText::AsNumber(InvalidByte));
		return false;
	}

	OriginalText = ContentBytes.IsEmpty()
		? FUtf8String()
		: FUtf8String(FUtf8StringView(
			reinterpret_cast<const UTF8CHAR*>(ContentBytes.GetData()),
			ContentBytes.Num()));
	RebuildOriginalMetadata();
	OutError = FText::GetEmpty();
	return true;
}

void FVerseDocument::RebuildOriginalMetadata()
{
	const FUtf8StringView OriginalView = GetOriginalUtf8View();
	LineEnding = DetectLineEnding(OriginalView);

	OriginalLineStarts.Reset();
	OriginalLineStarts.Add(0);
	for (int32 ByteIndex = 0; ByteIndex < OriginalView.Len(); ++ByteIndex)
	{
		if (OriginalView[ByteIndex] == static_cast<UTF8CHAR>('\r'))
		{
			if (ByteIndex + 1 < OriginalView.Len()
				&& OriginalView[ByteIndex + 1] == static_cast<UTF8CHAR>('\n'))
			{
				++ByteIndex;
			}
			OriginalLineStarts.Add(ByteIndex + 1);
		}
		else if (OriginalView[ByteIndex] == static_cast<UTF8CHAR>('\n'))
		{
			OriginalLineStarts.Add(ByteIndex + 1);
		}
	}

	SourceRegions.Reset();
	if (!OriginalView.IsEmpty())
	{
		SourceRegions.Add({{0, OriginalView.Len()}, EVerseSourceRegionKind::Raw, NAME_None});
	}
}

FUtf8StringView FVerseDocument::GetOriginalUtf8View() const
{
	return FUtf8StringView(*OriginalText, OriginalText.Len());
}

FVerseByteRange FVerseDocument::GetWholeOriginalRange() const
{
	return {0, OriginalText.Len()};
}

FUtf8StringView FVerseDocument::GetOriginalUtf8View(FVerseByteRange Range) const
{
	const FVerseByteRange WholeRange = GetWholeOriginalRange();
	if (!Range.IsSet() || Range.BeginByte < 0 || Range.NumBytes < 0 || Range.EndByte() > WholeRange.NumBytes)
	{
		return {};
	}

	return FUtf8StringView(*OriginalText + Range.BeginByte, Range.NumBytes);
}

FString FVerseDocument::DecodeOriginalRange(FVerseByteRange Range) const
{
	return DecodeUtf8(GetOriginalUtf8View(Range));
}

bool FVerseDocument::SetSourceRegions(TArray<FVerseSourceRegion> NewRegions, FText& OutError)
{
	const FVerseByteRange WholeRange = GetWholeOriginalRange();
	NewRegions.Sort([](const FVerseSourceRegion& Left, const FVerseSourceRegion& Right)
	{
		return Left.Range.BeginByte < Right.Range.BeginByte;
	});

	for (int32 RegionIndex = 0; RegionIndex < NewRegions.Num(); ++RegionIndex)
	{
		const FVerseByteRange Range = NewRegions[RegionIndex].Range;
		if (!Range.IsSet() || Range.BeginByte < 0 || Range.NumBytes < 0 || Range.EndByte() > WholeRange.EndByte())
		{
			OutError = LOCTEXT("InvalidSourceRegion", "A source region is outside the original document.");
			return false;
		}
		if (RegionIndex > 0 && RangesOverlap(NewRegions[RegionIndex - 1].Range, Range))
		{
			OutError = LOCTEXT("OverlappingSourceRegions", "Source regions may not overlap.");
			return false;
		}
	}

	SourceRegions = MoveTemp(NewRegions);
	OutError = FText::GetEmpty();
	return true;
}

int32 FVerseDocument::GetOriginalLineNumber(int32 ContentByteOffset) const
{
	if (ContentByteOffset < 0 || ContentByteOffset > GetWholeOriginalRange().NumBytes)
	{
		return INDEX_NONE;
	}

	int32 Lower = 0;
	int32 Upper = OriginalLineStarts.Num();
	while (Lower < Upper)
	{
		const int32 Middle = Lower + (Upper - Lower) / 2;
		if (OriginalLineStarts[Middle] <= ContentByteOffset)
		{
			Lower = Middle + 1;
		}
		else
		{
			Upper = Middle;
		}
	}
	return FMath::Max(1, Lower);
}

bool FVerseDocument::ValidateUtf8(TConstArrayView<uint8> Bytes, int32& OutInvalidByte)
{
	OutInvalidByte = INDEX_NONE;
	for (int32 Index = 0; Index < Bytes.Num();)
	{
		const uint8 First = Bytes[Index];
		int32 SequenceLength = 0;
		if (First <= 0x7F)
		{
			SequenceLength = 1;
		}
		else if (First >= 0xC2 && First <= 0xDF)
		{
			SequenceLength = 2;
		}
		else if (First >= 0xE0 && First <= 0xEF)
		{
			SequenceLength = 3;
		}
		else if (First >= 0xF0 && First <= 0xF4)
		{
			SequenceLength = 4;
		}
		else
		{
			OutInvalidByte = Index;
			return false;
		}

		if (Index + SequenceLength > Bytes.Num())
		{
			OutInvalidByte = Index;
			return false;
		}
		for (int32 ContinuationIndex = 1; ContinuationIndex < SequenceLength; ++ContinuationIndex)
		{
			if (!VerseDocument::IsContinuationByte(Bytes[Index + ContinuationIndex]))
			{
				OutInvalidByte = Index + ContinuationIndex;
				return false;
			}
		}

		if (SequenceLength == 3)
		{
			const uint8 Second = Bytes[Index + 1];
			if ((First == 0xE0 && Second < 0xA0) || (First == 0xED && Second > 0x9F))
			{
				OutInvalidByte = Index;
				return false;
			}
		}
		else if (SequenceLength == 4)
		{
			const uint8 Second = Bytes[Index + 1];
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

EVerseLineEnding FVerseDocument::DetectLineEnding(FUtf8StringView Text)
{
	bool bSawLf = false;
	bool bSawCrLf = false;
	bool bSawCr = false;

	for (int32 Index = 0; Index < Text.Len(); ++Index)
	{
		if (Text[Index] == static_cast<UTF8CHAR>('\r'))
		{
			if (Index + 1 < Text.Len() && Text[Index + 1] == static_cast<UTF8CHAR>('\n'))
			{
				bSawCrLf = true;
				++Index;
			}
			else
			{
				bSawCr = true;
			}
		}
		else if (Text[Index] == static_cast<UTF8CHAR>('\n'))
		{
			bSawLf = true;
		}
	}

	const int32 Kinds = static_cast<int32>(bSawLf)
		+ static_cast<int32>(bSawCrLf)
		+ static_cast<int32>(bSawCr);
	if (Kinds > 1)
	{
		return EVerseLineEnding::Mixed;
	}
	if (bSawCrLf)
	{
		return EVerseLineEnding::CrLf;
	}
	if (bSawLf)
	{
		return EVerseLineEnding::Lf;
	}
	if (bSawCr)
	{
		return EVerseLineEnding::Cr;
	}
	return EVerseLineEnding::None;
}

FString FVerseDocument::DecodeUtf8(FUtf8StringView Text)
{
	if (Text.IsEmpty())
	{
		return FString();
	}

	const FUTF8ToTCHAR Converted(Text.GetData(), Text.Len());
	return FString(Converted.Length(), Converted.Get());
}

bool FVerseDocument::RangesOverlap(
	const FVerseByteRange& Left,
	const FVerseByteRange& Right)
{
	return Left.BeginByte < Right.EndByte() && Right.BeginByte < Left.EndByte();
}

#undef LOCTEXT_NAMESPACE
