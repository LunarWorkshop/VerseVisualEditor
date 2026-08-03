#pragma once

#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "Templates/SharedPointer.h"

class FVerseDocument;
struct FVerseSemanticDiagnostic;
class FVerseSemanticSnapshot;
struct FVerseVisualTile;
struct FVerseVisualSocket;

namespace uLang
{
	class CDataDefinition;
	class CFunction;
	class CFunctionType;
	class CTypeBase;
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
	/** Concrete compiler type at the socket which authorized this typed match. */
	const uLang::CTypeBase* MatchedSocketType = nullptr;
	int32 BoundInputIndex = INDEX_NONE;
	/** Owns the compiler program containing the pointers above. */
	TSharedPtr<const FVerseSemanticSnapshot> Snapshot;
};

/** One concrete, source-selectable overload of an operator visible at a tile. */
struct FVerseOperatorSignature
{
	FString DisplayText;
	TArray<FString> OperandTypeNames;
	FString ResultTypeName;
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
		const FVerseDocument& Document,
		const FVerseVisualSocket* DraggedSocket = nullptr);
	/** Returns every source-creatable expression visible at an untyped clause insertion point. */
	static TArray<FVerseSemanticCandidate> BuildAll(
		TConstArrayView<TSharedPtr<const FVerseSemanticSnapshot>> Snapshots,
		const FString& FilePath,
		int32 ExpressionBeginByte,
		const FVerseDocument& Document);
	/** Returns source-spellable named types visible at the lexical insertion point. */
	static TArray<FString> BuildVisibleTypeNames(
		TConstArrayView<TSharedPtr<const FVerseSemanticSnapshot>> Snapshots,
		const FString& FilePath,
		int32 ExpressionBeginByte,
		const FVerseDocument& Document);
	/** Builds concrete overloads and generic instantiations for an existing operator. */
	static TArray<FVerseOperatorSignature> BuildOperatorSignatures(
		TConstArrayView<TSharedPtr<const FVerseSemanticSnapshot>> Snapshots,
		const FString& FilePath,
		int32 ExpressionBeginByte,
		const FVerseDocument& Document,
		FStringView OperatorSpelling,
		int32 OperandCount,
		TConstArrayView<const FVerseVisualSocket*> ConnectedOperands,
		TConstArrayView<const FVerseVisualSocket*> OutputConsumers);

	/** Bind existing expression tiles to the exact compiler-owned syntax/semantic graph. */
	static void BindFunctionGraph(
		TArray<FVerseVisualTile>& GraphTiles,
		const TSharedPtr<const FVerseSemanticSnapshot>& Snapshot,
		const FString& FilePath,
		const FVerseDocument& Document,
		TConstArrayView<FVerseSemanticDiagnostic> Diagnostics = {});
};
