#pragma once

#include "Containers/Array.h"
#include "Containers/ArrayView.h"
#include "Containers/StringView.h"
#include "Containers/Utf8String.h"
#include "CoreTypes.h"
#include "Templates/SharedPointer.h"

class FText;

/** A half-open UTF-8 byte range relative to document content, excluding a BOM. */
struct FVerseByteRange
{
	int32 BeginByte = INDEX_NONE;
	int32 NumBytes = 0;

	static FVerseByteRange FromBounds(int32 BeginByte, int32 EndByte)
	{
		return {BeginByte, EndByte - BeginByte};
	}

	bool IsSet() const
	{
		return BeginByte != INDEX_NONE;
	}

	int32 EndByte() const
	{
		return BeginByte + NumBytes;
	}

	bool Contains(int32 ByteOffset) const
	{
		return ByteOffset >= BeginByte && ByteOffset < EndByte();
	}

	bool operator==(const FVerseByteRange& Other) const = default;
};

enum class EVerseSourceRegionKind : uint8
{
	/** Source not yet recognized by the visual editor. Its text remains authoritative. */
	Raw,

	/** Source recognized as a Verse construct. SyntaxKind identifies the construct. */
	Syntax,
};

/** A block-facing description of a range in the immutable original source. */
struct FVerseSourceRegion
{
	FVerseByteRange Range;
	EVerseSourceRegionKind Kind = EVerseSourceRegionKind::Raw;
	FName SyntaxKind;
};

enum class EVerseLineEnding : uint8
{
	None,
	Lf,
	CrLf,
	Cr,
	Mixed,
};

/**
 * Lossless immutable Verse source document.
 *
 * Original UTF-8 text never changes during an editing session. The UTF-8 BOM,
 * when present, is retained as file metadata and excluded from source ranges.
 */
class VERSEVISUALEDITOR_API FVerseDocument : public TSharedFromThis<FVerseDocument>
{
public:
	static TSharedPtr<FVerseDocument> CreateFromBytes(
		TConstArrayView<uint8> Bytes,
		FText& OutError);

	static TSharedPtr<FVerseDocument> LoadFromFile(
		const FString& FilePath,
		FText& OutError);

	const FUtf8String& GetOriginalUtf8() const { return OriginalText; }
	FUtf8StringView GetOriginalUtf8View() const;
	FVerseByteRange GetWholeOriginalRange() const;
	FUtf8StringView GetOriginalUtf8View(FVerseByteRange Range) const;
	FString DecodeOriginalRange(FVerseByteRange Range) const;

	const TArray<FVerseSourceRegion>& GetSourceRegions() const { return SourceRegions; }
	bool SetSourceRegions(TArray<FVerseSourceRegion> NewRegions, FText& OutError);

	bool HasUtf8Bom() const { return bHasUtf8Bom; }
	EVerseLineEnding GetLineEnding() const { return LineEnding; }
	int32 GetOriginalLineNumber(int32 ContentByteOffset) const;

private:
	bool Initialize(TConstArrayView<uint8> Bytes, FText& OutError);
	void RebuildOriginalMetadata();

	static bool ValidateUtf8(TConstArrayView<uint8> Bytes, int32& OutInvalidByte);
	static EVerseLineEnding DetectLineEnding(FUtf8StringView Text);
	static FString DecodeUtf8(FUtf8StringView Text);
	static bool RangesOverlap(const FVerseByteRange& Left, const FVerseByteRange& Right);

	FUtf8String OriginalText;
	bool bHasUtf8Bom = false;
	EVerseLineEnding LineEnding = EVerseLineEnding::None;
	TArray<int32> OriginalLineStarts;
	TArray<FVerseSourceRegion> SourceRegions;
};
