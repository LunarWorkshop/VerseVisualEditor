#pragma once

#include "Containers/Map.h"
#include "Containers/Set.h"
#include "Containers/UnrealString.h"
#include "Containers/Utf8String.h"
#include "Internationalization/Text.h"
#include "Logging/LogVerbosity.h"
#include "Math/IntPoint.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"
#include "VerseDocumentRevision.h"
#include "uLang/Common/Containers/SharedPointer.h"
#include "uLang/Syntax/VstNode.h"

class ISolarisIde;
class ISolIdeSourceProject;

namespace uLang
{
	class CSemanticProgram;
}

struct FVerseSemanticDocumentInput
{
	FString FilePath;
	FUtf8String Source;
	FVerseDocumentRevision Revision;
};

struct FVerseSemanticDiagnostic
{
	FText Message;
	ELogVerbosity::Type Severity = ELogVerbosity::Log;
	/** Stable compiler diagnostic identity; zero is reserved for editor-authored diagnostics. */
	uint16 ReferenceCode = 0;
	FString FilePath;
	/** One-based compiler source location; INDEX_NONE means unavailable. */
	FIntPoint RowSpan = FIntPoint(INDEX_NONE, INDEX_NONE);
	FIntPoint ColumnSpan = FIntPoint(INDEX_NONE, INDEX_NONE);
	/** Exact UTF-8 locus in SourceRevision when the diagnostic came from an editor buffer. */
	FVerseByteRange SourceRange;
	FVerseDocumentRevision SourceRevision;

	bool AppliesToFile(const FString& CandidateFilePath) const;
};

/** Immutable compiler-owned semantic state for a known set of document revisions. */
class FVerseSemanticSnapshot
{
public:
	FVerseSemanticSnapshot();
	~FVerseSemanticSnapshot();

	bool Describes(const FString& FilePath, FVerseDocumentRevision Revision) const;
	const uLang::TSPtr<uLang::CSemanticProgram>& GetProgram() const { return Program; }
	const Verse::Vst::TNodePtr<Verse::Vst::Project>& GetProjectVst() const { return ProjectVst; }

	static TSharedRef<FVerseSemanticSnapshot> CreateForTesting(
		TConstArrayView<FVerseSemanticDocumentInput> Documents);

private:
	friend class FVerseSemanticWorkspace;

	static FString MakeDocumentKey(const FString& FilePath);
	void AddDocuments(TConstArrayView<FVerseSemanticDocumentInput> Documents);

	uLang::TSPtr<uLang::CSemanticProgram> Program;
	Verse::Vst::TNodePtr<Verse::Vst::Project> ProjectVst;
	TMap<FString, FVerseDocumentRevision> DocumentRevisions;
};

enum class EVerseSemanticWorkspaceState : uint8
{
	Unavailable,
	Debouncing,
	Analyzing,
	Ready,
	Failed,
};

/** Controls which existing packages an in-memory overlay may depend on. */
enum class EVerseSemanticDependencyPolicy : uint8
{
	ProjectVisible,
	PublicApiOnly,
};

struct FVerseSemanticAnalysisResult
{
	bool bSucceeded = false;
	/** May contain useful compiler state even when local analysis reports errors. */
	TSharedPtr<const FVerseSemanticSnapshot> Snapshot;
	TArray<FVerseSemanticDiagnostic> Diagnostics;
};

/**
 * Owns the compiled semantic baseline and an isolated Solaris environment used
 * only to analyze unsaved editor buffers.
 */
class FVerseSemanticWorkspace
{
public:
	using FAnalysisFunction =
		TFunction<FVerseSemanticAnalysisResult(TConstArrayView<FVerseSemanticDocumentInput>)>;

	explicit FVerseSemanticWorkspace(double InDebounceSeconds = 0.25);
	FVerseSemanticWorkspace(
		EVerseSemanticDependencyPolicy InDependencyPolicy,
		double InDebounceSeconds = 0.25);
	FVerseSemanticWorkspace(FAnalysisFunction InAnalysisFunction, double InDebounceSeconds);
	~FVerseSemanticWorkspace();

	void RequestAnalysis(
		TArray<FVerseSemanticDocumentInput> Documents,
		double CurrentTimeSeconds,
		bool bDebounce);
	void Tick(double CurrentTimeSeconds);

	/** Refreshes the engine-owned compiled baseline after a successful project build. */
	void RefreshCompiledBaseline(TConstArrayView<FVerseSemanticDocumentInput> CompiledDocuments);
	void InvalidateCompiledBaseline();

	EVerseSemanticWorkspaceState GetState() const { return State; }
	const TSharedPtr<const FVerseSemanticSnapshot>& GetLastSuccessfulSnapshot() const
	{
		return LastSuccessfulSnapshot;
	}
	/** Read-only semantic states to union for degraded candidate discovery. */
	TArray<TSharedPtr<const FVerseSemanticSnapshot>> GetCandidateSnapshots() const
	{
		TArray<TSharedPtr<const FVerseSemanticSnapshot>> Result;
		if (DiscoverySnapshot.IsValid())
		{
			Result.Add(DiscoverySnapshot);
		}
		if (LastSuccessfulSnapshot.IsValid())
		{
			Result.AddUnique(LastSuccessfulSnapshot);
		}
		if (CompiledBaseline.IsValid() && CompiledBaseline != LastSuccessfulSnapshot)
		{
			Result.AddUnique(CompiledBaseline);
		}
		return Result;
	}
	const TArray<FVerseSemanticDiagnostic>& GetDiagnostics() const { return Diagnostics; }
	/** True when the latest completed analysis, successful or not, consumed this exact buffer. */
	bool LatestAnalysisDescribes(
		const FString& FilePath,
		FVerseDocumentRevision Revision) const;
	bool HasExactSnapshot(const FString& FilePath, FVerseDocumentRevision Revision) const;
	FText GetMutationUnavailableReason(
		const FString& FilePath,
		FVerseDocumentRevision Revision) const;
	/** Validates prospective buffers without publishing them or changing workspace state. */
	bool ValidateProspectiveDocuments(
		TConstArrayView<FVerseSemanticDocumentInput> Documents,
		FText& OutError);

private:
	FVerseSemanticAnalysisResult AnalyzeWithPrivateEnvironment(
		TConstArrayView<FVerseSemanticDocumentInput> Documents);
	bool RebuildPrivateEnvironment(
		TConstArrayView<FVerseSemanticDocumentInput> Documents,
		TSet<FString>& OutInMemoryDocumentKeys,
		TArray<FVerseSemanticDiagnostic>& OutDiagnostics);
	bool TryPublishResult(uint64 RequestId, FVerseSemanticAnalysisResult Result);
	bool CompiledBaselineDescribesAll(
		TConstArrayView<FVerseSemanticDocumentInput> Documents) const;

	FAnalysisFunction AnalysisFunction;
	EVerseSemanticDependencyPolicy DependencyPolicy =
		EVerseSemanticDependencyPolicy::ProjectVisible;
	double DebounceSeconds = 0.25;
	double AnalyzeAfterSeconds = 0.0;
	uint64 LatestRequestId = 0;
	uint64 PendingRequestId = 0;
	TArray<FVerseSemanticDocumentInput> PendingDocuments;
	TArray<FVerseSemanticDocumentInput> LastAnalyzedDocuments;
	EVerseSemanticWorkspaceState State = EVerseSemanticWorkspaceState::Unavailable;
	TSharedPtr<const FVerseSemanticSnapshot> CompiledBaseline;
	/** Latest partial or complete compiler state, used only for candidate discovery. */
	TSharedPtr<const FVerseSemanticSnapshot> DiscoverySnapshot;
	TSharedPtr<const FVerseSemanticSnapshot> LastSuccessfulSnapshot;
	TSharedPtr<const FVerseSemanticSnapshot> MutationSnapshot;
	TArray<FVerseSemanticDiagnostic> Diagnostics;
	TSharedPtr<ISolarisIde> PrivateIde;
	TSharedPtr<ISolIdeSourceProject> PrivateProject;
};
