#pragma once

#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "Templates/SharedPointer.h"

class FVerseDocument;
class FVerseSemanticSnapshot;

namespace uLang
{
	class CDataDefinition;
	class CFunction;
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
	FString DisplayName;
	FText BlueprintDisplayName;
	/** Category metadata resolved from the Verse definition, then intrinsic presentation. */
	FText Category;
	/** Semantic module hierarchy, independent of presentation category metadata. */
	FText ModuleCategory;
	FString ResultTypeName;
	FString SourceSpelling;
	bool bUsesFailureCallSyntax = false;
	int32 BoundInputIndex = INDEX_NONE;
	TArray<FString> UnboundInputDefaults;
	const uLang::CDataDefinition* DataDefinition = nullptr;
	const uLang::CFunction* Function = nullptr;
	const uLang::CTypeBase* ResultType = nullptr;
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
};
