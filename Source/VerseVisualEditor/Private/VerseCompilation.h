#pragma once

#include "Containers/Array.h"
#include "Containers/ArrayView.h"
#include "Containers/UnrealString.h"
#include "Containers/Utf8String.h"
#include "SolBuildDiagnostic.h"
#include "VerseDocumentRevision.h"
#include "VerseVisualEditorSettings.h"

struct FVerseVisualTile;
enum class EVerseDiagnosticSeverity : uint8
{
	Info,
	Warning,
	Error,
};

struct FVerseCompilationDiagnostic
{
	EVerseDiagnosticSeverity Severity = EVerseDiagnosticSeverity::Error;
	uint16 ReferenceCode = 0;
	FString Message;
	FVerseTextRange Range;
	TArray<int32> AffectedTileIndices;
};

struct FVerseCompilationResult
{
	FVerseDocumentRevision Revision;
	bool bSucceeded = false;
	TArray<FVerseCompilationDiagnostic> Diagnostics;
};

namespace VerseCompilation
{
	/** Runs the official Verse parser and captures its structured diagnostics. */
	FVerseCompilationResult Compile(
		FUtf8String Source,
		FVerseDocumentRevision Revision,
		FString SourcePath);

	/** Converts full-project Solaris diagnostics for one source file. */
	FVerseCompilationResult FromProjectBuildDiagnostics(
		FUtf8StringView Source,
		FVerseDocumentRevision Revision,
		TConstArrayView<FSolDiagnostic> Diagnostics);

	/** Maps a result to tiles only if it still belongs to the current revision. */
	bool TryAcceptResult(
		FVerseCompilationResult Result,
		FVerseDocumentRevision CurrentRevision,
		TConstArrayView<FVerseVisualTile> CurrentTiles,
		FVerseCompilationResult& OutAcceptedResult);
}
