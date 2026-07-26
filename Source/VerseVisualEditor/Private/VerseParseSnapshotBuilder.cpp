#include "VerseParseSnapshotBuilder.h"

#include "uLang/CompilerPasses/CompilerTypes.h"
#include "uLang/Parser/ParserPass.h"
#include "uLang/SourceProject/UploadedAtFNVersion.h"
#include "uLang/SourceProject/VerseVersion.h"
#include "uLang/Syntax/VstNode.h"

const FName VerseSyntaxKind::Module(TEXT("Module"));
const FName VerseSyntaxKind::Class(TEXT("Class"));
const FName VerseSyntaxKind::Struct(TEXT("Struct"));
const FName VerseSyntaxKind::Interface(TEXT("Interface"));
const FName VerseSyntaxKind::Enum(TEXT("Enum"));
const FName VerseSyntaxKind::Function(TEXT("Function"));
const FName VerseSyntaxKind::Variable(TEXT("Variable"));
const FName VerseSyntaxKind::Constant(TEXT("Constant"));
const FName VerseSyntaxKind::TypeAlias(TEXT("TypeAlias"));

namespace VerseParseSnapshotBuilder
{
	class FSourceIndex
	{
	public:
		explicit FSourceIndex(FUtf8StringView InSource)
			: Source(InSource)
		{
			LineStarts.Add(0);
			for (int32 Offset = 0; Offset < Source.Len(); ++Offset)
			{
				if (Source[Offset] == static_cast<UTF8CHAR>('\r'))
				{
					if (Offset + 1 < Source.Len()
						&& Source[Offset + 1] == static_cast<UTF8CHAR>('\n'))
					{
						++Offset;
					}
					LineStarts.Add(Offset + 1);
				}
				else if (Source[Offset] == static_cast<UTF8CHAR>('\n'))
				{
					LineStarts.Add(Offset + 1);
				}
			}
		}

		FVerseByteRange ToRange(const Verse::SLocus& Locus) const
		{
			const int32 Begin = ToOffset(Locus.BeginRow(), Locus.BeginColumn());
			const int32 End = ToOffset(Locus.EndRow(), Locus.EndColumn());
			if (Begin == INDEX_NONE || End == INDEX_NONE || End < Begin)
			{
				return {};
			}
			return FVerseByteRange::FromBounds(Begin, End);
		}

		FUtf8StringView GetSource() const { return Source; }

	private:
		int32 ToOffset(uint32 Row, uint32 ByteColumn) const
		{
			if (Row >= static_cast<uint32>(LineStarts.Num()))
			{
				return INDEX_NONE;
			}
			const uint64 Offset = static_cast<uint64>(LineStarts[Row]) + ByteColumn;
			return Offset <= static_cast<uint64>(Source.Len())
				? static_cast<int32>(Offset)
				: INDEX_NONE;
		}

		FUtf8StringView Source;
		TArray<int32> LineStarts;
	};

	const Verse::Vst::Node* UnwrapSingleClause(const Verse::Vst::Node* Node)
	{
		while (Node != nullptr && Node->IsA<Verse::Vst::Clause>() && Node->GetChildCount() == 1)
		{
			Node = Node->GetChildren()[0].Get();
		}
		return Node;
	}

	EVerseCommentKind ToCommentKind(Verse::Vst::Comment::EType Type)
	{
		switch (Type)
		{
		case Verse::Vst::Comment::EType::line:
			return EVerseCommentKind::Line;
		case Verse::Vst::Comment::EType::block:
			return EVerseCommentKind::Block;
		case Verse::Vst::Comment::EType::ind:
			return EVerseCommentKind::Indented;
		case Verse::Vst::Comment::EType::frag:
			return EVerseCommentKind::Fragment;
		default:
			return EVerseCommentKind::None;
		}
	}

	FVerseByteRange TrimClauseDelimiters(
		const Verse::Vst::Clause& Clause,
		FVerseByteRange Range,
		FUtf8StringView Source)
	{
		if (!Range.IsSet() || Range.EndByte() > Source.Len())
		{
			return {};
		}

		FUtf8StringView Text = Source.Mid(Range.BeginByte, Range.NumBytes);
		int32 RelativeBegin = 0;
		int32 RelativeEnd = Text.Len();
		if (Clause.GetPunctuation() == Verse::Vst::Clause::EPunctuation::Braces)
		{
			int32 OpenBrace = INDEX_NONE;
			int32 CloseBrace = INDEX_NONE;
			for (int32 Index = 0; Index < Text.Len(); ++Index)
			{
				if (OpenBrace == INDEX_NONE && Text[Index] == static_cast<UTF8CHAR>('{'))
				{
					OpenBrace = Index;
				}
				if (Text[Index] == static_cast<UTF8CHAR>('}'))
				{
					CloseBrace = Index;
				}
			}
			if (OpenBrace != INDEX_NONE && CloseBrace != INDEX_NONE && CloseBrace >= OpenBrace)
			{
				RelativeBegin = OpenBrace + 1;
				RelativeEnd = CloseBrace;
			}
		}
		else
		{
			while (RelativeBegin < RelativeEnd
				&& (Text[RelativeBegin] == static_cast<UTF8CHAR>(' ')
					|| Text[RelativeBegin] == static_cast<UTF8CHAR>('\t')))
			{
				++RelativeBegin;
			}
			if (RelativeBegin < RelativeEnd && Text[RelativeBegin] == static_cast<UTF8CHAR>(':'))
			{
				++RelativeBegin;
				if (RelativeBegin < RelativeEnd && Text[RelativeBegin] == static_cast<UTF8CHAR>('\r'))
				{
					++RelativeBegin;
				}
				if (RelativeBegin < RelativeEnd && Text[RelativeBegin] == static_cast<UTF8CHAR>('\n'))
				{
					++RelativeBegin;
				}
			}
		}
		return FVerseByteRange::FromBounds(
			Range.BeginByte + RelativeBegin,
			Range.BeginByte + RelativeEnd);
	}

	FVerseByteRange FindBodyRange(
		const Verse::Vst::Node& RightOperand,
		const FSourceIndex& SourceIndex)
	{
		const Verse::Vst::Node* Unwrapped = UnwrapSingleClause(&RightOperand);
		if (const Verse::Vst::Macro* Macro = Unwrapped != nullptr
			? Unwrapped->AsNullable<Verse::Vst::Macro>()
			: nullptr)
		{
			if (Macro->GetChildCount() <= 1)
			{
				return {};
			}
			const Verse::Vst::Clause& BodyClause = *Macro->GetClause(Macro->GetChildCount() - 2);
			return TrimClauseDelimiters(
				BodyClause,
				SourceIndex.ToRange(BodyClause.Whence()),
				SourceIndex.GetSource());
		}

		return Unwrapped != nullptr ? SourceIndex.ToRange(Unwrapped->Whence()) : FVerseByteRange();
	}

	const Verse::Vst::Identifier* FindFirstIdentifier(const Verse::Vst::Node& Node)
	{
		if (const Verse::Vst::Identifier* Identifier = Node.AsNullable<Verse::Vst::Identifier>())
		{
			return Identifier;
		}
		for (const Verse::Vst::TNodeRef<Verse::Vst::Node>& Child : Node.GetChildren())
		{
			if (const Verse::Vst::Identifier* Identifier = FindFirstIdentifier(*Child))
			{
				return Identifier;
			}
		}
		return nullptr;
	}

	bool ContainsNodeType(const Verse::Vst::Node& Node, Verse::Vst::NodeType Type)
	{
		if (Node.GetElementType() == Type)
		{
			return true;
		}
		for (const Verse::Vst::TNodeRef<Verse::Vst::Node>& Child : Node.GetChildren())
		{
			if (ContainsNodeType(*Child, Type))
			{
				return true;
			}
		}
		return false;
	}

	void CollectCommentRegions(
		const Verse::Vst::Node& Node,
		const FSourceIndex& SourceIndex,
		TSet<const Verse::Vst::Node*>& VisitedNodes,
		TArray<FVerseSourceRegion>& OutRegions)
	{
		if (VisitedNodes.Contains(&Node))
		{
			return;
		}
		VisitedNodes.Add(&Node);

		if (const Verse::Vst::Comment* Comment = Node.AsNullable<Verse::Vst::Comment>())
		{
			const FVerseByteRange Range = SourceIndex.ToRange(Node.Whence());
			if (Range.IsSet() && Range.NumBytes > 0)
			{
				FVerseSourceRegion& Region = OutRegions.AddDefaulted_GetRef();
				Region.Range = Range;
				Region.Kind = EVerseSourceRegionKind::Comment;
				Region.BodyRange = Range;
				Region.CommentKind = ToCommentKind(Comment->_Type);
			}
			return;
		}

		for (const Verse::Vst::TNodeRef<Verse::Vst::Node>& Comment : Node.GetPrefixComments())
		{
			CollectCommentRegions(*Comment, SourceIndex, VisitedNodes, OutRegions);
		}
		for (const Verse::Vst::TNodeRef<Verse::Vst::Node>& Comment : Node.GetPostfixComments())
		{
			CollectCommentRegions(*Comment, SourceIndex, VisitedNodes, OutRegions);
		}
		if (Node.GetAux())
		{
			CollectCommentRegions(*Node.GetAux(), SourceIndex, VisitedNodes, OutRegions);
		}
		for (const Verse::Vst::TNodeRef<Verse::Vst::Node>& Child : Node.GetChildren())
		{
			CollectCommentRegions(*Child, SourceIndex, VisitedNodes, OutRegions);
		}
	}

	FName ClassifyMacro(const Verse::Vst::Node& RightOperand)
	{
		const Verse::Vst::Node* Unwrapped = UnwrapSingleClause(&RightOperand);
		const Verse::Vst::Macro* Macro = Unwrapped != nullptr
			? Unwrapped->AsNullable<Verse::Vst::Macro>()
			: nullptr;
		const Verse::Vst::Identifier* MacroName = Macro != nullptr
			? Macro->GetName()->AsNullable<Verse::Vst::Identifier>()
			: nullptr;
		if (MacroName == nullptr)
		{
			return NAME_None;
		}

		const uLang::CUTF8String& Name = MacroName->GetSourceText();
		if (Name == "module")
		{
			return VerseSyntaxKind::Module;
		}
		if (Name == "class")
		{
			return VerseSyntaxKind::Class;
		}
		if (Name == "struct")
		{
			return VerseSyntaxKind::Struct;
		}
		if (Name == "interface")
		{
			return VerseSyntaxKind::Interface;
		}
		if (Name == "enum")
		{
			return VerseSyntaxKind::Enum;
		}
		if (Name == "type")
		{
			return VerseSyntaxKind::TypeAlias;
		}
		return NAME_None;
	}

	bool TryMakeTypedRegion(
		const Verse::Vst::Node& Node,
		const FSourceIndex& SourceIndex,
		FVerseSourceRegion& OutRegion)
	{
		const Verse::Vst::Definition* Definition = Node.AsNullable<Verse::Vst::Definition>();
		if (Definition == nullptr || Definition->GetChildCount() != 2)
		{
			return false;
		}

		const Verse::Vst::Node& LeftOperand = *Definition->GetOperandLeft();
		const Verse::Vst::Node& RightOperand = *Definition->GetOperandRight();
		const Verse::Vst::Node* NameOperand = &LeftOperand;
		const Verse::Vst::Node* TypeOperand = nullptr;
		if (const Verse::Vst::TypeSpec* TypeSpec = LeftOperand.AsNullable<Verse::Vst::TypeSpec>())
		{
			if (TypeSpec->HasLhs())
			{
				NameOperand = TypeSpec->GetLhs().Get();
				TypeOperand = TypeSpec->GetRhs().Get();
			}
		}

		const Verse::Vst::Identifier* Name = FindFirstIdentifier(*NameOperand);
		if (Name == nullptr)
		{
			return false;
		}

		FName SyntaxKind = ClassifyMacro(RightOperand);
		if (SyntaxKind.IsNone())
		{
			if (ContainsNodeType(*NameOperand, Verse::Vst::NodeType::Mutation))
			{
				SyntaxKind = VerseSyntaxKind::Variable;
			}
			else if (ContainsNodeType(*NameOperand, Verse::Vst::NodeType::PrePostCall))
			{
				SyntaxKind = VerseSyntaxKind::Function;
			}
			else
			{
				SyntaxKind = VerseSyntaxKind::Constant;
			}
		}

		const FVerseByteRange DefinitionRange = SourceIndex.ToRange(Definition->Whence());
		FVerseByteRange NameRange = SourceIndex.ToRange(Name->Whence());
		const int32 NameByteLength = Name->GetSourceText().ByteLen();
		if (NameRange.IsSet() && NameByteLength > 0 && NameRange.NumBytes >= NameByteLength)
		{
			NameRange = {NameRange.EndByte() - NameByteLength, NameByteLength};
		}
		if (!DefinitionRange.IsSet()
			|| DefinitionRange.NumBytes <= 0
			|| !NameRange.IsSet()
			|| NameRange.BeginByte < DefinitionRange.BeginByte
			|| NameRange.EndByte() > DefinitionRange.EndByte())
		{
			return false;
		}

		OutRegion.Range = DefinitionRange;
		OutRegion.Kind = EVerseSourceRegionKind::Syntax;
		OutRegion.SyntaxKind = SyntaxKind;
		OutRegion.NameRange = NameRange;
		OutRegion.TypeRange = TypeOperand != nullptr
			? SourceIndex.ToRange(TypeOperand->Whence())
			: FVerseByteRange();
		OutRegion.BodyRange = FindBodyRange(RightOperand, SourceIndex);
		return true;
	}
}

FVerseParseSnapshot FVerseParseSnapshotBuilder::Build(
	TSharedRef<const FVerseDocument> Document)
{
	const FUtf8StringView Source = Document->GetOriginalUtf8View();
	if (Source.IsEmpty())
	{
		return FVerseParseSnapshot::CreateRaw(MoveTemp(Document));
	}

	const uLang::CUTF8StringView CompilerSource(
		reinterpret_cast<const char*>(Source.GetData()),
		Source.Len());
	Verse::Vst::TNodeRef<Verse::Vst::Snippet> Snippet =
		Verse::Vst::TNodeRef<Verse::Vst::Snippet>::New(
			uLang::CUTF8StringView("VerseVisualEditor"));
	uLang::SBuildContext BuildContext;
	uLang::CParserPass Parser;
	Parser.ProcessSnippet(
		Snippet,
		CompilerSource,
		BuildContext,
		Verse::Version::Default,
		VerseFN::UploadedAtFNVersion::Latest);

	const VerseParseSnapshotBuilder::FSourceIndex SourceIndex(Source);
	TArray<FVerseSourceRegion> RecognizedRegions;
	for (const Verse::Vst::TNodeRef<Verse::Vst::Node>& Node : Snippet->GetChildren())
	{
		FVerseSourceRegion Region;
		if (VerseParseSnapshotBuilder::TryMakeTypedRegion(*Node, SourceIndex, Region))
		{
			RecognizedRegions.Add(MoveTemp(Region));
		}
	}
	TSet<const Verse::Vst::Node*> VisitedNodes;
	VerseParseSnapshotBuilder::CollectCommentRegions(
		*Snippet,
		SourceIndex,
		VisitedNodes,
		RecognizedRegions);

	if (RecognizedRegions.IsEmpty())
	{
		return FVerseParseSnapshot::CreateRaw(MoveTemp(Document));
	}

	RecognizedRegions.Sort([](const FVerseSourceRegion& Left, const FVerseSourceRegion& Right)
	{
		return Left.Range.BeginByte == Right.Range.BeginByte
			? Left.Range.NumBytes < Right.Range.NumBytes
			: Left.Range.BeginByte < Right.Range.BeginByte;
	});

	TArray<FVerseSourceRegion> CompleteRegions;
	int32 Cursor = 0;
	for (FVerseSourceRegion& RecognizedRegion : RecognizedRegions)
	{
		if (RecognizedRegion.Range.BeginByte < Cursor || RecognizedRegion.Range.EndByte() > Source.Len())
		{
			continue;
		}
		if (Cursor < RecognizedRegion.Range.BeginByte)
		{
			CompleteRegions.Add({
				FVerseByteRange::FromBounds(Cursor, RecognizedRegion.Range.BeginByte),
				EVerseSourceRegionKind::Raw,
				NAME_None});
		}
		CompleteRegions.Add(MoveTemp(RecognizedRegion));
		Cursor = CompleteRegions.Last().Range.EndByte();
	}
	if (Cursor < Source.Len())
	{
		CompleteRegions.Add({
			FVerseByteRange::FromBounds(Cursor, Source.Len()),
			EVerseSourceRegionKind::Raw,
			NAME_None});
	}

	return CompleteRegions.IsEmpty()
		? FVerseParseSnapshot::CreateRaw(MoveTemp(Document))
		: FVerseParseSnapshot::CreateRecognized(MoveTemp(Document), MoveTemp(CompleteRegions));
}
