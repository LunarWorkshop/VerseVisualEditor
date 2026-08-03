#include "Semantics/VerseCompilation.h"

#include "Containers/StringConv.h"
#include "SolBuildResults.h"
#include "VisualModel/VerseVisualTile.h"
#include "uLang/CompilerPasses/CompilerTypes.h"
#include "uLang/Diagnostics/Diagnostics.h"
#include "uLang/Diagnostics/Glitch.h"
#include "uLang/Parser/ParserPass.h"
#include "uLang/SourceProject/UploadedAtFNVersion.h"
#include "uLang/SourceProject/VerseVersion.h"
#include "uLang/Syntax/VstNode.h"

namespace
{
	FString ToFString(const uLang::CUTF8String& Text)
	{
		const FUTF8ToTCHAR Converted(Text.AsCString(), Text.ByteLen());
		return FString(Converted.Length(), Converted.Get());
	}

	EVerseDiagnosticSeverity ToSeverity(uLang::EDiagnosticSeverity Severity)
	{
		switch (Severity)
		{
		case uLang::EDiagnosticSeverity::Warning:
			return EVerseDiagnosticSeverity::Warning;
		case uLang::EDiagnosticSeverity::Error:
			return EVerseDiagnosticSeverity::Error;
		case uLang::EDiagnosticSeverity::Ok:
		case uLang::EDiagnosticSeverity::Info:
		default:
			return EVerseDiagnosticSeverity::Info;
		}
	}

	EVerseDiagnosticSeverity ToSeverity(ELogVerbosity::Type Severity)
	{
		if (Severity == ELogVerbosity::Error || Severity == ELogVerbosity::Fatal)
		{
			return EVerseDiagnosticSeverity::Error;
		}
		return Severity == ELogVerbosity::Warning
			? EVerseDiagnosticSeverity::Warning
			: EVerseDiagnosticSeverity::Info;
	}

	FVerseTextRange ToRevisionedRange(
		const uLang::CUTF8StringView& Source,
		const uLang::STextRange& CompilerRange,
		FVerseDocumentRevision Revision)
	{
		if (!CompilerRange.IsValid())
		{
			return {};
		}

		const uLang::TOptional<int32_t> Begin = uLang::ScanToRowCol(Source, CompilerRange.GetBegin());
		const uLang::TOptional<int32_t> End = uLang::ScanToRowCol(Source, CompilerRange.GetEnd());
		if (!Begin || !End || End.GetValue() < Begin.GetValue())
		{
			return {};
		}

		return FVerseTextRange(
			Revision,
			FVerseByteRange::FromBounds(Begin.GetValue(), End.GetValue()));
	}

	bool DiagnosticTouchesTile(const FVerseTextRange& Diagnostic, const FVerseTextRange& Tile)
	{
		if (!Diagnostic.IsSet() || Diagnostic.Revision != Tile.Revision)
		{
			return false;
		}
		if (Diagnostic.NumBytes == 0)
		{
			return Diagnostic.BeginByte >= Tile.BeginByte && Diagnostic.BeginByte <= Tile.EndByte();
		}
		return Diagnostic.BeginByte < Tile.EndByte() && Tile.BeginByte < Diagnostic.EndByte();
	}
}

FVerseCompilationResult VerseCompilation::FromProjectBuildDiagnostics(
	FUtf8StringView Source,
	FVerseDocumentRevision Revision,
	TConstArrayView<FSolDiagnostic> Diagnostics)
{
	FVerseCompilationResult Result;
	Result.Revision = Revision;
	Result.bSucceeded = true;
	const uLang::CUTF8StringView CompilerSource(
		reinterpret_cast<const char*>(Source.GetData()),
		Source.Len());

	for (const FSolDiagnostic& ProjectDiagnostic : Diagnostics)
	{
		FVerseCompilationDiagnostic& Diagnostic = Result.Diagnostics.AddDefaulted_GetRef();
		Diagnostic.Severity = ToSeverity(ProjectDiagnostic.Info.Severity);
		Diagnostic.ReferenceCode = ProjectDiagnostic.Info.ReferenceCode;
		Diagnostic.Message = ProjectDiagnostic.Info.Message;
		Result.bSucceeded &= Diagnostic.Severity != EVerseDiagnosticSeverity::Error;

		const Verse::FDiagnosticLocus& Locus = ProjectDiagnostic.Location;
		if (Locus.RowSpan.X > 0 && Locus.ColSpan.X > 0
			&& Locus.RowSpan.Y > 0 && Locus.ColSpan.Y > 0)
		{
			const uLang::STextRange CompilerRange(
				Locus.RowSpan.X - 1,
				Locus.ColSpan.X - 1,
				Locus.RowSpan.Y - 1,
				Locus.ColSpan.Y - 1);
			Diagnostic.Range = ToRevisionedRange(CompilerSource, CompilerRange, Revision);
		}
	}
	return Result;
}

FVerseCompilationResult VerseCompilation::Compile(
	FUtf8String Source,
	FVerseDocumentRevision Revision,
	FString SourcePath)
{
	FVerseCompilationResult Result;
	Result.Revision = Revision;

	const uLang::CUTF8StringView CompilerSource(
		reinterpret_cast<const char*>(*Source),
		Source.Len());
	const FTCHARToUTF8 Utf8Path(*SourcePath);
	Verse::Vst::TNodeRef<Verse::Vst::Snippet> Snippet =
		Verse::Vst::TNodeRef<Verse::Vst::Snippet>::New(
			uLang::CUTF8StringView(Utf8Path.Get(), Utf8Path.Length()));
	uLang::SBuildContext BuildContext;
	uLang::CParserPass Parser;
	Parser.ProcessSnippet(
		Snippet,
		CompilerSource,
		BuildContext,
		Verse::Version::Default,
		VerseFN::UploadedAtFNVersion::Latest);

	Result.bSucceeded = !BuildContext._Diagnostics->HasErrors();
	for (const uLang::TSRef<uLang::SGlitch>& Glitch : BuildContext._Diagnostics->GetGlitches())
	{
		FVerseCompilationDiagnostic& Diagnostic = Result.Diagnostics.AddDefaulted_GetRef();
		Diagnostic.Severity = ToSeverity(Glitch->_Result.GetInfo().Severity);
		Diagnostic.ReferenceCode = Glitch->_Result.GetInfo().ReferenceCode;
		Diagnostic.Message = ToFString(Glitch->_Result._Message);
		Diagnostic.Range = ToRevisionedRange(CompilerSource, Glitch->_Locus._Range, Revision);
	}

	return Result;
}

bool VerseCompilation::TryAcceptResult(
	FVerseCompilationResult Result,
	FVerseDocumentRevision CurrentRevision,
	TConstArrayView<FVerseVisualTile> CurrentTiles,
	FVerseCompilationResult& OutAcceptedResult)
{
	if (Result.Revision != CurrentRevision)
	{
		return false;
	}

	for (FVerseCompilationDiagnostic& Diagnostic : Result.Diagnostics)
	{
		Diagnostic.AffectedTileIndices.Reset();
		for (int32 TileIndex = 0; TileIndex < CurrentTiles.Num(); ++TileIndex)
		{
			if (DiagnosticTouchesTile(Diagnostic.Range, CurrentTiles[TileIndex].Range))
			{
				Diagnostic.AffectedTileIndices.Add(TileIndex);
			}
		}
	}

	OutAcceptedResult = MoveTemp(Result);
	return true;
}
