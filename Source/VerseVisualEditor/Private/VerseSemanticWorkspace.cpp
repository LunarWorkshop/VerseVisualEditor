#include "VerseSemanticWorkspace.h"

#include "Algo/AllOf.h"
#include "Internationalization/Text.h"
#include "ISolarisIde.h"
#include "ISolarisModule.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "SolarisLoadCompilerModule.h"
#include "VerseProjectTypes.h"
#include "uLang/Diagnostics/Diagnostics.h"
#include "uLang/SourceProject/SourceFileProject.h"
#include "uLang/Toolchain/ProgramBuildManager.h"

#define LOCTEXT_NAMESPACE "VerseSemanticWorkspace"

namespace
{
	FVerseSemanticDiagnostic MakeDiagnostic(FText Message, ELogVerbosity::Type Severity)
	{
		FVerseSemanticDiagnostic Result;
		Result.Message = MoveTemp(Message);
		Result.Severity = Severity;
		return Result;
	}

	ELogVerbosity::Type ToLogVerbosity(uLang::EDiagnosticSeverity Severity)
	{
		switch (Severity)
		{
		case uLang::EDiagnosticSeverity::Error:
			return ELogVerbosity::Error;
		case uLang::EDiagnosticSeverity::Warning:
			return ELogVerbosity::Warning;
		case uLang::EDiagnosticSeverity::Info:
		case uLang::EDiagnosticSeverity::Ok:
		default:
			return ELogVerbosity::Log;
		}
	}

	uLang::CSourceFileSnippet* FindFileSnippet(
		const TSharedRef<ISolIdeSourceProject>& Project,
		const FString& FilePath)
	{
		const FTCHARToUTF8 Utf8Path(*FilePath);
		const uLang::CUTF8StringView PathView(Utf8Path.Get(), Utf8Path.Length());
		for (const uLang::CSourceProject::SPackage& Package : Project->GetProject()->_Packages)
		{
			if (!Package._Package.IsValid()
				|| Package._Package->GetOrigin() != uLang::CSourcePackage::FileSystem)
			{
				continue;
			}
			uLang::TOptional<uLang::TSRef<uLang::CSourceFileSnippet>> Snippet =
				Package._Package.As<uLang::CSourceFilePackage>()->FindSnippetByFilePath(PathView);
			if (Snippet.IsSet())
			{
				return Snippet.GetValue().Get();
			}
		}

		FString NormalizedRequestedPath = FPaths::ConvertRelativePathToFull(FilePath);
		FPaths::NormalizeFilename(NormalizedRequestedPath);
		uLang::CSourceFileSnippet* FoundSnippet = nullptr;
		for (const uLang::CSourceProject::SPackage& Package : Project->GetProject()->_Packages)
		{
			if (!Package._Package.IsValid()
				|| Package._Package->GetOrigin() != uLang::CSourcePackage::FileSystem)
			{
				continue;
			}
			Package._Package->_RootModule->VisitAll(
				[&FoundSnippet, &NormalizedRequestedPath](const uLang::CSourceModule& Module)
				{
					for (const uLang::TSRef<uLang::ISourceSnippet>& SourceSnippet : Module._SourceSnippets)
					{
						const uLang::CUTF8String SnippetPath = SourceSnippet->GetPath();
						FString NormalizedSnippetPath = FPaths::ConvertRelativePathToFull(
							UTF8_TO_TCHAR(SnippetPath.AsCString()));
						FPaths::NormalizeFilename(NormalizedSnippetPath);
						if (NormalizedSnippetPath.Equals(
							NormalizedRequestedPath,
							ESearchCase::IgnoreCase))
						{
							FoundSnippet = SourceSnippet.As<uLang::CSourceFileSnippet>().Get();
							return false;
						}
					}
					return true;
				});
			if (FoundSnippet != nullptr)
			{
				return FoundSnippet;
			}
		}
		return nullptr;
	}

	bool IsPathInsideDirectory(const FString& FilePath, const FString& Directory)
	{
		FString NormalizedFile = FPaths::ConvertRelativePathToFull(FilePath);
		FString NormalizedDirectory = FPaths::ConvertRelativePathToFull(Directory);
		FPaths::NormalizeFilename(NormalizedFile);
		FPaths::NormalizeDirectoryName(NormalizedDirectory);
		return FPathViews::IsParentPathOf(NormalizedDirectory, NormalizedFile);
	}

	FString MakeNormalizedPathKey(const FString& FilePath)
	{
		FString Key = FPaths::ConvertRelativePathToFull(FilePath);
		FPaths::NormalizeFilename(Key);
		Key.ToLowerInline();
		return Key;
	}

	FString FindOverlayRoot(const FString& FilePath)
	{
		FString BestRoot;
		auto ConsiderRoot = [&FilePath, &BestRoot](const FString& Candidate)
		{
			if (IsPathInsideDirectory(FilePath, Candidate) && Candidate.Len() > BestRoot.Len())
			{
				BestRoot = FPaths::ConvertRelativePathToFull(Candidate);
				FPaths::NormalizeDirectoryName(BestRoot);
			}
		};

		ConsiderRoot(FPaths::ProjectContentDir());
		for (const TSharedRef<IPlugin>& Plugin : IPluginManager::Get().GetEnabledPlugins())
		{
			ConsiderRoot(Plugin->GetContentDir());
		}
		return BestRoot.IsEmpty()
			? FPaths::GetPath(FPaths::ConvertRelativePathToFull(FilePath))
			: BestRoot;
	}

	const FVersePackageContainer* FindSettingsTemplate(
		const FString& Root,
		const TArray<FVersePackageContainer>& Packages)
	{
		const FVersePackageContainer* BestMatch = nullptr;
		for (const FVersePackageContainer& Package : Packages)
		{
			if (Package.PackageType == EVersePackageType::Content
				&& !Package.DirPath.IsEmpty()
				&& IsPathInsideDirectory(Root / TEXT("Overlay.verse"), Package.DirPath)
				&& (!BestMatch || Package.DirPath.Len() > BestMatch->DirPath.Len()))
			{
				BestMatch = &Package;
			}
		}
		if (BestMatch)
		{
			return BestMatch;
		}

		return Packages.FindByPredicate([](const FVersePackageContainer& Package)
		{
			return Package.PackageType == EVersePackageType::Content
				&& Package.Settings.VerseScope == EVersePackageScope::PublicUser;
		});
	}

	FVerseProjectContainer BuildInMemoryOverlayPackages(
		const TSharedRef<ISolIdeSourceProject>& MainProject,
		TConstArrayView<FVerseSemanticDocumentInput> Documents,
		TSet<FString>& OutInMemoryDocumentKeys)
	{
		TMap<FString, TArray<const FVerseSemanticDocumentInput*>> DocumentsByRoot;
		for (const FVerseSemanticDocumentInput& Document : Documents)
		{
			if (FindFileSnippet(MainProject, Document.FilePath) == nullptr)
			{
				DocumentsByRoot.FindOrAdd(FindOverlayRoot(Document.FilePath)).Add(&Document);
				OutInMemoryDocumentKeys.Add(MakeNormalizedPathKey(Document.FilePath));
			}
		}

		FVerseProjectContainer ExistingPackages;
		ISolarisModule::Get().GetVersePackages(
			ExistingPackages,
			[](FVersePackageContainer&) { return true; });

		FVerseProjectContainer Result;
		for (const TPair<FString, TArray<const FVerseSemanticDocumentInput*>>& Entry : DocumentsByRoot)
		{
			FVersePackageContainer& Overlay = Result.Packages.AddDefaulted_GetRef();
			const uint32 RootHash = GetTypeHash(Entry.Key);
			Overlay.Name = FString::Printf(TEXT("VerseVisualEditorOverlay_%08X"), RootHash);
			Overlay.DirPath = Entry.Key;
			Overlay.PackageType = EVersePackageType::Content;

			if (const FVersePackageContainer* Template = FindSettingsTemplate(Entry.Key, ExistingPackages.Packages))
			{
				Overlay.Settings = Template->Settings;
			}
			Overlay.Settings.VerseRole = EVersePackageRole::Source;
			Overlay.Settings.VersePath = FString::Printf(
				TEXT("/localhost/VerseVisualEditorOverlay_%08X"), RootHash);
			Overlay.Settings.DependencyPackages.Reset();

			for (const FVersePackageContainer& Existing : ExistingPackages.Packages)
			{
				const bool bVisibleToOverlay =
					Overlay.Settings.VerseScope == EVersePackageScope::InternalUser
					|| Existing.Settings.VerseScope == EVersePackageScope::PublicAPI
					|| Existing.Settings.VerseScope == EVersePackageScope::PublicUser;
				if (bVisibleToOverlay && Existing.Name != Overlay.Name)
				{
					Overlay.Settings.DependencyPackages.AddUnique(Existing.Name);
				}
			}

			for (const FVerseSemanticDocumentInput* Document : Entry.Value)
			{
				FVerseSourceFile& SourceFile = Overlay.SourceFiles.AddDefaulted_GetRef();
				SourceFile.Filename = FPaths::ConvertRelativePathToFull(Document->FilePath);
				SourceFile.SetSourceCode(TConstArrayView<uint8>(
					reinterpret_cast<const uint8*>(*Document->Source),
					Document->Source.Len()));
			}
		}
		return Result;
	}
}

FString FVerseSemanticSnapshot::MakeDocumentKey(const FString& FilePath)
{
	return MakeNormalizedPathKey(FilePath);
}

void FVerseSemanticSnapshot::AddDocuments(
	TConstArrayView<FVerseSemanticDocumentInput> Documents)
{
	for (const FVerseSemanticDocumentInput& Document : Documents)
	{
		DocumentRevisions.Add(MakeDocumentKey(Document.FilePath), Document.Revision);
	}
}

bool FVerseSemanticSnapshot::Describes(
	const FString& FilePath,
	FVerseDocumentRevision Revision) const
{
	const FVerseDocumentRevision* Found = DocumentRevisions.Find(MakeDocumentKey(FilePath));
	return Found != nullptr && *Found == Revision;
}

TSharedRef<FVerseSemanticSnapshot> FVerseSemanticSnapshot::CreateForTesting(
	TConstArrayView<FVerseSemanticDocumentInput> Documents)
{
	TSharedRef<FVerseSemanticSnapshot> Snapshot = MakeShared<FVerseSemanticSnapshot>();
	Snapshot->AddDocuments(Documents);
	return Snapshot;
}

FVerseSemanticWorkspace::FVerseSemanticWorkspace(double InDebounceSeconds)
	: DebounceSeconds(InDebounceSeconds)
{
}

FVerseSemanticWorkspace::FVerseSemanticWorkspace(
	FAnalysisFunction InAnalysisFunction,
	double InDebounceSeconds)
	: AnalysisFunction(MoveTemp(InAnalysisFunction))
	, DebounceSeconds(InDebounceSeconds)
{
}

void FVerseSemanticWorkspace::RequestAnalysis(
	TArray<FVerseSemanticDocumentInput> Documents,
	double CurrentTimeSeconds,
	bool bDebounce)
{
	++LatestRequestId;
	PendingRequestId = LatestRequestId;
	PendingDocuments = MoveTemp(Documents);
	MutationSnapshot.Reset();
	Diagnostics.Reset();
	if (PendingDocuments.IsEmpty())
	{
		LastSuccessfulSnapshot = CompiledBaseline;
		MutationSnapshot = CompiledBaseline;
		State = CompiledBaseline.IsValid()
			? EVerseSemanticWorkspaceState::Ready
			: EVerseSemanticWorkspaceState::Unavailable;
		return;
	}

	if (CompiledBaselineDescribesAll(PendingDocuments))
	{
		LastSuccessfulSnapshot = CompiledBaseline;
		MutationSnapshot = CompiledBaseline;
		PendingDocuments.Reset();
		State = EVerseSemanticWorkspaceState::Ready;
		return;
	}

	AnalyzeAfterSeconds = CurrentTimeSeconds + (bDebounce ? DebounceSeconds : 0.0);
	State = EVerseSemanticWorkspaceState::Debouncing;
}

void FVerseSemanticWorkspace::Tick(double CurrentTimeSeconds)
{
	if (State != EVerseSemanticWorkspaceState::Debouncing
		|| CurrentTimeSeconds < AnalyzeAfterSeconds)
	{
		return;
	}

	const uint64 RequestId = PendingRequestId;
	const TArray<FVerseSemanticDocumentInput> Documents = PendingDocuments;
	State = EVerseSemanticWorkspaceState::Analyzing;
	FVerseSemanticAnalysisResult Result = AnalysisFunction
		? AnalysisFunction(Documents)
		: AnalyzeWithPrivateEnvironment(Documents);
	TryPublishResult(RequestId, MoveTemp(Result));
}

void FVerseSemanticWorkspace::RefreshCompiledBaseline(
	TConstArrayView<FVerseSemanticDocumentInput> CompiledDocuments)
{
	if (!ISolarisLoadCompilerModule::IsLoaded())
	{
		return;
	}
	const TSharedPtr<ISolarisIde> EditorIde =
		ISolarisLoadCompilerModule::Get().GetEditorIde();
	if (!EditorIde.IsValid())
	{
		return;
	}
	const uLang::TSPtr<uLang::CProgramBuildManager> BuildManager = EditorIde->GetBuildManager();
	if (!BuildManager.IsValid()
		|| !BuildManager->GetProgramContext()._Program.IsValid()
		|| !BuildManager->GetProjectVst().IsValid())
	{
		return;
	}

	TSharedRef<FVerseSemanticSnapshot> Snapshot = MakeShared<FVerseSemanticSnapshot>();
	Snapshot->Program = BuildManager->GetProgramContext()._Program;
	Snapshot->ProjectVst = BuildManager->GetProjectVst();
	Snapshot->AddDocuments(CompiledDocuments);
	CompiledBaseline = Snapshot;
	PrivateIde.Reset();
	PrivateProject.Reset();
}

void FVerseSemanticWorkspace::InvalidateCompiledBaseline()
{
	CompiledBaseline.Reset();
	MutationSnapshot.Reset();
	PrivateIde.Reset();
	PrivateProject.Reset();
	State = EVerseSemanticWorkspaceState::Unavailable;
}

bool FVerseSemanticWorkspace::HasExactSnapshot(
	const FString& FilePath,
	FVerseDocumentRevision Revision) const
{
	return MutationSnapshot.IsValid()
		&& MutationSnapshot->Describes(FilePath, Revision);
}

FText FVerseSemanticWorkspace::GetMutationUnavailableReason(
	const FString& FilePath,
	FVerseDocumentRevision Revision) const
{
	if (HasExactSnapshot(FilePath, Revision))
	{
		return FText::GetEmpty();
	}
	if (State == EVerseSemanticWorkspaceState::Debouncing
		|| State == EVerseSemanticWorkspaceState::Analyzing)
	{
		return LOCTEXT(
			"SemanticAnalysisPending",
			"Verse semantic analysis is still updating for this document.");
	}
	if (State == EVerseSemanticWorkspaceState::Failed)
	{
		return Diagnostics.IsEmpty()
			? LOCTEXT("SemanticAnalysisFailed", "Verse semantic analysis failed for this document.")
			: Diagnostics[0].Message;
	}
	return LOCTEXT(
		"SemanticAnalysisUnavailable",
		"Verse semantic information is not available for this document revision.");
}

bool FVerseSemanticWorkspace::RebuildPrivateEnvironment(
	TConstArrayView<FVerseSemanticDocumentInput> Documents,
	TSet<FString>& OutInMemoryDocumentKeys,
	TArray<FVerseSemanticDiagnostic>& OutDiagnostics)
{
	PrivateIde.Reset();
	PrivateProject.Reset();
	OutInMemoryDocumentKeys.Reset();
	if (!ISolarisLoadCompilerModule::IsLoaded())
	{
		OutDiagnostics.Add(MakeDiagnostic(
			LOCTEXT("CompilerModuleUnavailable", "The Solaris compiler module is unavailable."),
			ELogVerbosity::Error));
		return false;
	}
	const TSharedPtr<ISolIdeSourceProject> MainProject =
		ISolarisLoadCompilerModule::Get().GetSourceProject();
	if (!MainProject.IsValid())
	{
		OutDiagnostics.Add(MakeDiagnostic(
			LOCTEXT("SourceProjectUnavailable", "The current Verse source project is unavailable."),
			ELogVerbosity::Error));
		return false;
	}
	FVerseProjectContainer InMemoryPackages = BuildInMemoryOverlayPackages(
		MainProject.ToSharedRef(), Documents, OutInMemoryDocumentKeys);

	TSharedRef<ISolarisIdeDiagnostics> IdeDiagnostics = MakeIdeDiagnostics(
		[&OutDiagnostics](const FSolDiagnostic& Diagnostic)
		{
			OutDiagnostics.Add(MakeDiagnostic(
				FText::FromString(Diagnostic.Info.Message),
				Diagnostic.Info.Severity));
		});
	const TOptional<TSharedRef<ISolIdeSourceProject>> IndependentProject =
		ISolarisModule::Get().CreateProjectSource(
			*MainProject->GetProjectName(),
			ISolarisModule::EBuildMode::Incremental,
			IdeDiagnostics,
			InMemoryPackages.Packages.IsEmpty() ? nullptr : &InMemoryPackages);
	if (!IndependentProject.IsSet())
	{
		if (OutDiagnostics.IsEmpty())
		{
			OutDiagnostics.Add(MakeDiagnostic(
				LOCTEXT("SourceProjectCreationFailed", "Could not create an isolated Verse source project."),
				ELogVerbosity::Error));
		}
		return false;
	}

	FSolIdeConfig Config;
	Config.Flags = ESolIdeFlags::WithNoBackend;
	PrivateIde = ISolarisModule::Get().MakeDevEnvironment(Config);
	PrivateProject = IndependentProject.GetValue();
	if (!PrivateIde->SetSourceProject(PrivateProject.ToSharedRef()))
	{
		OutDiagnostics.Add(MakeDiagnostic(
			LOCTEXT("SetSourceProjectFailed", "Could not initialize the isolated Verse semantic environment."),
			ELogVerbosity::Error));
		PrivateIde.Reset();
		PrivateProject.Reset();
		return false;
	}
	return true;
}

FVerseSemanticAnalysisResult FVerseSemanticWorkspace::AnalyzeWithPrivateEnvironment(
	TConstArrayView<FVerseSemanticDocumentInput> Documents)
{
	FVerseSemanticAnalysisResult Result;
	TSet<FString> InMemoryDocumentKeys;
	if (!RebuildPrivateEnvironment(Documents, InMemoryDocumentKeys, Result.Diagnostics))
	{
		return Result;
	}

	for (const FVerseSemanticDocumentInput& Document : Documents)
	{
		if (InMemoryDocumentKeys.Contains(MakeNormalizedPathKey(Document.FilePath)))
		{
			continue;
		}
		uLang::CSourceFileSnippet* Snippet =
			FindFileSnippet(PrivateProject.ToSharedRef(), Document.FilePath);
		if (Snippet == nullptr)
		{
			Result.Diagnostics.Add(MakeDiagnostic(
				FText::Format(
					LOCTEXT("PrivateSnippetUnavailable", "Could not prepare {0} for private Verse semantic analysis."),
					FText::FromString(Document.FilePath)),
				ELogVerbosity::Error));
			return Result;
		}
		const uLang::CUTF8StringView SourceView(
			reinterpret_cast<const char*>(*Document.Source),
			Document.Source.Len());
		Snippet->SetModifiedText(SourceView);
		Snippet->MarkDirty(1);
	}

	const uLang::TSPtr<uLang::CProgramBuildManager> BuildManager = PrivateIde->GetBuildManager();
	if (!BuildManager.IsValid())
	{
		Result.Diagnostics.Add(MakeDiagnostic(
			LOCTEXT("PrivateBuildManagerUnavailable", "The isolated Verse build manager is unavailable."),
			ELogVerbosity::Error));
		return Result;
	}

	const uLang::TSRef<uLang::CDiagnostics> CompilerDiagnostics =
		uLang::TSRef<uLang::CDiagnostics>::New();
	uLang::SBuildParams BuildParams;
	BuildParams._LinkType = uLang::SBuildParams::ELinkParam::Skip;
	BuildParams._bSemanticAnalysisOnly = true;
	BuildParams._bGenerateDigests = false;
	BuildParams._bGenerateCode = false;
	const uLang::SBuildResults BuildResults =
		BuildManager->Build(BuildParams, CompilerDiagnostics);
	for (const uLang::TSRef<uLang::SGlitch>& Glitch : CompilerDiagnostics->GetGlitches())
	{
		Result.Diagnostics.Add(MakeDiagnostic(
			FText::FromString(UTF8_TO_TCHAR(Glitch->_Result._Message.AsCString())),
			ToLogVerbosity(Glitch->_Result.GetInfo().Severity)));
	}
	if (BuildResults.HasFailure()
		|| !BuildManager->GetProgramContext()._Program.IsValid()
		|| !BuildManager->GetProjectVst().IsValid())
	{
		if (Result.Diagnostics.IsEmpty())
		{
			Result.Diagnostics.Add(MakeDiagnostic(
				LOCTEXT("SemanticBuildFailed", "Verse semantic analysis failed."),
				ELogVerbosity::Error));
		}
		return Result;
	}

	TSharedRef<FVerseSemanticSnapshot> Snapshot = MakeShared<FVerseSemanticSnapshot>();
	Snapshot->Program = BuildManager->GetProgramContext()._Program;
	Snapshot->ProjectVst = BuildManager->GetProjectVst();
	Snapshot->AddDocuments(Documents);
	Result.bSucceeded = true;
	Result.Snapshot = Snapshot;
	return Result;
}

bool FVerseSemanticWorkspace::TryPublishResult(
	uint64 RequestId,
	FVerseSemanticAnalysisResult Result)
{
	if (RequestId != LatestRequestId)
	{
		return false;
	}
	PendingDocuments.Reset();
	Diagnostics = MoveTemp(Result.Diagnostics);
	if (Result.bSucceeded && Result.Snapshot.IsValid())
	{
		LastSuccessfulSnapshot = MoveTemp(Result.Snapshot);
		MutationSnapshot = LastSuccessfulSnapshot;
		State = EVerseSemanticWorkspaceState::Ready;
		return true;
	}
	State = EVerseSemanticWorkspaceState::Failed;
	return false;
}

bool FVerseSemanticWorkspace::CompiledBaselineDescribesAll(
	TConstArrayView<FVerseSemanticDocumentInput> Documents) const
{
	if (!CompiledBaseline.IsValid() || Documents.IsEmpty())
	{
		return false;
	}
	return Algo::AllOf(Documents, [this](const FVerseSemanticDocumentInput& Document)
	{
		return CompiledBaseline->Describes(Document.FilePath, Document.Revision);
	});
}

#undef LOCTEXT_NAMESPACE
