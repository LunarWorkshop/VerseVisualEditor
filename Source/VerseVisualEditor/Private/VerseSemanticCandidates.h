#pragma once

#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "Templates/SharedPointer.h"

class FVerseDocument;
class FVerseSemanticSnapshot;
struct FVerseVisualTile;

namespace uLang
{
	class CDataDefinition;
	class CFunction;
	class CFunctionType;
}

enum class EVerseSemanticCandidateKind : uint8
{
	Identifier,
	Function,
	InfixOperator,
	PrefixOperator,
	PostfixOperator,
};

/** A compiler-owned expression candidate kept alive by its semantic snapshot. */
struct FVerseSemanticCandidate
{
	EVerseSemanticCandidateKind Kind = EVerseSemanticCandidateKind::Identifier;
	const uLang::CDataDefinition* DataDefinition = nullptr;
	const uLang::CFunction* Function = nullptr;
	/** The instantiated overload selected while matching the dragged socket. */
	const uLang::CFunctionType* InstantiatedFunctionType = nullptr;
	int32 BoundInputIndex = INDEX_NONE;
	/** Owns the compiler program containing the pointers above. */
	TSharedPtr<const FVerseSemanticSnapshot> Snapshot;
};

class FVerseSemanticCandidateProvider
{
public:
	static TArray<FVerseSemanticCandidate> Build(
		TConstArrayView<TSharedPtr<const FVerseSemanticSnapshot>> Snapshots,
		const FString& FilePath,
		int32 ExpressionBeginByte,
		bool bDraggingFromOutput,
		const FVerseDocument& Document);
	/** Returns every source-creatable expression visible at an untyped clause insertion point. */
	static TArray<FVerseSemanticCandidate> BuildAll(
		TConstArrayView<TSharedPtr<const FVerseSemanticSnapshot>> Snapshots,
		const FString& FilePath,
		int32 ExpressionBeginByte,
		const FVerseDocument& Document);

	/** Bind existing expression tiles to the exact compiler-owned syntax/semantic graph. */
	static void BindFunctionGraph(
		TArray<FVerseVisualTile>& GraphTiles,
		const TSharedPtr<const FVerseSemanticSnapshot>& Snapshot,
		const FString& FilePath,
		const FVerseDocument& Document);
};
