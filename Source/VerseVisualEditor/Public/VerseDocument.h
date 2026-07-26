#pragma once

#include "Containers/Array.h"
#include "Containers/ArrayView.h"
#include "Containers/StringView.h"
#include "CoreTypes.h"
#include "Misc/DateTime.h"
#include "Templates/SharedPointer.h"

class FText;

/** A half-open UTF-8 byte range relative to the document content, excluding a BOM. */
struct FVerseSourceRange
{
	int32 BeginByte = INDEX_NONE;
	int32 NumBytes = 0;

	static FVerseSourceRange FromBounds(int32 BeginByte, int32 EndByte)
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

	bool operator==(const FVerseSourceRange& Other) const = default;
};

enum class EVerseSourceRegionKind : uint8
{
	/** Source not yet recognized by the visual editor. Its bytes remain authoritative. */
	Raw,

	/** Source recognized as a Verse construct. SyntaxKind identifies the construct. */
	Syntax,
};

/** A block-facing description of a range in the immutable original source. */
struct FVerseSourceRegion
{
	FVerseSourceRange Range;
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
 * Lossless Verse source document.
 *
 * Original UTF-8 bytes never change during an editing session. Source blocks
 * keep FVerseSourceRange values into that buffer. User edits are represented
 * by a piece-table overlay and own only their replacement UTF-8 bytes.
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

	const TArray<uint8>& GetOriginalBytes() const { return OriginalBytes; }
	FVerseSourceRange GetWholeOriginalRange() const;
	FUtf8StringView GetOriginalUtf8View(FVerseSourceRange Range) const;
	FString DecodeOriginalRange(FVerseSourceRange Range) const;

	const TArray<FVerseSourceRegion>& GetSourceRegions() const { return SourceRegions; }
	bool SetSourceRegions(TArray<FVerseSourceRegion> NewRegions, FText& OutError);

	bool HasUtf8Bom() const { return OriginalContentOffset == 3; }
	EVerseLineEnding GetLineEnding() const { return LineEnding; }
	int32 GetOriginalLineNumber(int32 ContentByteOffset) const;

private:
	bool Initialize(TConstArrayView<uint8> Bytes, FText& OutError);
	void RebuildOriginalMetadata();

	static bool ValidateUtf8(TConstArrayView<uint8> Bytes, int32& OutInvalidByte);
	static EVerseLineEnding DetectLineEnding(TConstArrayView<uint8> ContentBytes);
	static FString DecodeUtf8(TConstArrayView<uint8> Bytes);
	static bool RangesOverlap(const FVerseSourceRange& Left, const FVerseSourceRange& Right);

	TArray<uint8> OriginalBytes;
	int32 OriginalContentOffset = 0;
	EVerseLineEnding LineEnding = EVerseLineEnding::None;
	TArray<int32> OriginalLineStarts;
	TArray<FVerseSourceRegion> SourceRegions;
};
