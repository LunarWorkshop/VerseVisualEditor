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
		while (Node != nullptr
			&& (Node->IsA<Verse::Vst::Clause>() || Node->IsA<Verse::Vst::Parens>())
			&& Node->GetChildCount() == 1)
		{
			Node = Node->GetChildren()[0].Get();
		}
		return Node;
	}

	const Verse::Vst::Node* UnwrapSingleClauseAndCollectGrouping(
		const Verse::Vst::Node* Node,
		const FSourceIndex& SourceIndex,
		TArray<FVerseGroupingLayer>& OutLayers)
	{
		const FUtf8StringView Source = SourceIndex.GetSource();
		while (Node != nullptr
			&& (Node->IsA<Verse::Vst::Clause>() || Node->IsA<Verse::Vst::Parens>())
			&& Node->GetChildCount() == 1)
		{
			const FVerseByteRange Range = SourceIndex.ToRange(Node->Whence());
			if (Range.IsSet())
			{
				int32 Left = Range.BeginByte;
				int32 Right = Range.EndByte() - 1;
				while (Left <= Right && (Source[Left] == static_cast<UTF8CHAR>(' ')
					|| Source[Left] == static_cast<UTF8CHAR>('\t')
					|| Source[Left] == static_cast<UTF8CHAR>('\r')
					|| Source[Left] == static_cast<UTF8CHAR>('\n')))
				{
					++Left;
				}
				while (Right >= Left && (Source[Right] == static_cast<UTF8CHAR>(' ')
					|| Source[Right] == static_cast<UTF8CHAR>('\t')
					|| Source[Right] == static_cast<UTF8CHAR>('\r')
					|| Source[Right] == static_cast<UTF8CHAR>('\n')))
				{
					--Right;
				}
				while (Left < Right
					&& Source[Left] == static_cast<UTF8CHAR>('(')
					&& Source[Right] == static_cast<UTF8CHAR>(')'))
				{
					const int32 Opening = Left++;
					const int32 Closing = Right--;
					if (!OutLayers.ContainsByPredicate(
						[Opening](const FVerseGroupingLayer& Layer)
						{
							return Layer.OpeningRange.BeginByte == Opening;
						}))
					{
						OutLayers.Add({FVerseByteRange::FromBounds(Opening, Closing + 1),
							{Opening, 1}, {Closing, 1}});
					}
					while (Left < Right && FChar::IsWhitespace(static_cast<TCHAR>(Source[Left]))) ++Left;
					while (Right > Left && FChar::IsWhitespace(static_cast<TCHAR>(Source[Right]))) --Right;
				}
			}
			Node = Node->GetChildren()[0].Get();
		}
		return Node;
	}

	const Verse::Vst::Clause* FindPunctuatedClauseThroughSingleChildWrappers(
		const Verse::Vst::Node* Node)
	{
		while (Node != nullptr)
		{
			if (const Verse::Vst::Clause* Clause =
				Node->AsNullable<Verse::Vst::Clause>())
			{
				if (Clause->GetPunctuation()
					!= Verse::Vst::Clause::EPunctuation::Unknown)
				{
					return Clause;
				}
			}
			if ((!Node->IsA<Verse::Vst::Clause>()
					&& !Node->IsA<Verse::Vst::Parens>())
				|| Node->GetChildCount() != 1)
			{
				return nullptr;
			}
			Node = Node->GetChildren()[0].Get();
		}
		return nullptr;
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

	void AddCommentDescriptor(
		const Verse::Vst::Node& Node,
		const FSourceIndex& SourceIndex,
		EVerseCommentAttachment Attachment,
		TSet<const Verse::Vst::Node*>& VisitedComments,
		TArray<FVerseCommentDescriptor>& OutComments)
	{
		if (VisitedComments.Contains(&Node))
		{
			return;
		}
		VisitedComments.Add(&Node);

		const Verse::Vst::Comment* Comment = Node.AsNullable<Verse::Vst::Comment>();
		if (Comment == nullptr)
		{
			return;
		}
		const FVerseByteRange Range = SourceIndex.ToRange(Comment->Whence());
		if (!Range.IsSet() || Range.NumBytes <= 0)
		{
			return;
		}

		FVerseCommentDescriptor& Descriptor = OutComments.AddDefaulted_GetRef();
		Descriptor.Range = Range;
		Descriptor.Kind = ToCommentKind(Comment->_Type);
		Descriptor.Attachment = Attachment;
	}

	void CollectItemCommentDescriptors(
		const Verse::Vst::Node& Node,
		const FSourceIndex& SourceIndex,
		bool bAtItemBoundary,
		TSet<const Verse::Vst::Node*>& VisitedComments,
		TArray<FVerseCommentDescriptor>& OutComments)
	{
		// The parser has already decided ownership. Comments on the ordered
		// expression itself are prefix/postfix; comments owned by a descendant
		// are inline with respect to the ordered item. A direct comment child of
		// a clause is deliberately not claimed by either neighboring item.
		for (const Verse::Vst::TNodeRef<Verse::Vst::Node>& Comment : Node.GetPrefixComments())
		{
			AddCommentDescriptor(
				*Comment,
				SourceIndex,
				bAtItemBoundary ? EVerseCommentAttachment::Prefix : EVerseCommentAttachment::Inline,
				VisitedComments,
				OutComments);
		}
		for (const Verse::Vst::TNodeRef<Verse::Vst::Node>& Comment : Node.GetPostfixComments())
		{
			AddCommentDescriptor(
				*Comment,
				SourceIndex,
				bAtItemBoundary ? EVerseCommentAttachment::Postfix : EVerseCommentAttachment::Inline,
				VisitedComments,
				OutComments);
		}

		if (Node.GetAux())
		{
			CollectItemCommentDescriptors(
				*Node.GetAux(), SourceIndex, false, VisitedComments, OutComments);
		}

		int32 NonCommentChildCount = 0;
		for (const Verse::Vst::TNodeRef<Verse::Vst::Node>& Child : Node.GetChildren())
		{
			NonCommentChildCount += Child->IsA<Verse::Vst::Comment>() ? 0 : 1;
		}
		const bool bTransparentSingleClause = bAtItemBoundary
			&& Node.IsA<Verse::Vst::Clause>()
			&& NonCommentChildCount == 1;
		for (const Verse::Vst::TNodeRef<Verse::Vst::Node>& Child : Node.GetChildren())
		{
			if (!Child->IsA<Verse::Vst::Comment>())
			{
				CollectItemCommentDescriptors(
					*Child,
					SourceIndex,
					bTransparentSingleClause,
					VisitedComments,
					OutComments);
			}
		}
	}

	void CollectItemCommentDescriptors(
		const Verse::Vst::Node& Node,
		const FSourceIndex& SourceIndex,
		TArray<FVerseCommentDescriptor>& OutComments)
	{
		TSet<const Verse::Vst::Node*> VisitedComments;
		CollectItemCommentDescriptors(Node, SourceIndex, true, VisitedComments, OutComments);
		OutComments.Sort([](const FVerseCommentDescriptor& Left, const FVerseCommentDescriptor& Right)
		{
			return Left.Range.BeginByte < Right.Range.BeginByte;
		});
	}

	bool TryMakeTypedRegion(
		const Verse::Vst::Node& Node,
		const FSourceIndex& SourceIndex,
		FVerseSourceRegion& OutRegion);
	bool ContainsNodeType(const Verse::Vst::Node& Node, Verse::Vst::NodeType Type);

	int32 FindLastByte(FUtf8StringView Source, UTF8CHAR Byte, int32 Begin, int32 End)
	{
		Begin = FMath::Clamp(Begin, 0, Source.Len());
		End = FMath::Clamp(End, Begin, Source.Len());
		for (int32 Offset = End - 1; Offset >= Begin; --Offset)
		{
			if (Source[Offset] == Byte)
			{
				return Offset;
			}
		}
		return INDEX_NONE;
	}

	FVerseByteRange TrimSourceWhitespace(FUtf8StringView Source, FVerseByteRange Range)
	{
		int32 Begin = FMath::Clamp(Range.BeginByte, 0, Source.Len());
		int32 End = FMath::Clamp(Range.EndByte(), Begin, Source.Len());
		auto IsWhitespace = [](UTF8CHAR Character)
		{
			return Character == static_cast<UTF8CHAR>(' ')
				|| Character == static_cast<UTF8CHAR>('\t')
				|| Character == static_cast<UTF8CHAR>('\r')
				|| Character == static_cast<UTF8CHAR>('\n');
		};
		while (Begin < End && IsWhitespace(Source[Begin])) ++Begin;
		while (End > Begin && IsWhitespace(Source[End - 1])) --End;
		return FVerseByteRange::FromBounds(Begin, End);
	}

	EVerseClauseDelimiter ToClauseDelimiter(
		Verse::Vst::Clause::EPunctuation Punctuation)
	{
		switch (Punctuation)
		{
		case Verse::Vst::Clause::EPunctuation::Braces:
			return EVerseClauseDelimiter::Braces;
		case Verse::Vst::Clause::EPunctuation::Colon:
			return EVerseClauseDelimiter::Colon;
		case Verse::Vst::Clause::EPunctuation::Indentation:
			return EVerseClauseDelimiter::BareIndentation;
		default:
			return EVerseClauseDelimiter::None;
		}
	}

	EVerseLineEnding DetectLineEnding(FUtf8StringView Text)
	{
		bool bLf = false;
		bool bCrLf = false;
		bool bCr = false;
		for (int32 Index = 0; Index < Text.Len(); ++Index)
		{
			if (Text[Index] == static_cast<UTF8CHAR>('\r'))
			{
				if (Index + 1 < Text.Len()
					&& Text[Index + 1] == static_cast<UTF8CHAR>('\n'))
				{
					bCrLf = true;
					++Index;
				}
				else
				{
					bCr = true;
				}
			}
			else if (Text[Index] == static_cast<UTF8CHAR>('\n'))
			{
				bLf = true;
			}
		}
		const int32 Kinds = static_cast<int32>(bLf)
			+ static_cast<int32>(bCrLf) + static_cast<int32>(bCr);
		if (Kinds > 1) return EVerseLineEnding::Mixed;
		if (bCrLf) return EVerseLineEnding::CrLf;
		if (bLf) return EVerseLineEnding::Lf;
		if (bCr) return EVerseLineEnding::Cr;
		return EVerseLineEnding::None;
	}

	FString DecodeUtf8(FUtf8StringView Text)
	{
		if (Text.IsEmpty())
		{
			return FString();
		}
		const FUTF8ToTCHAR Converted(
			reinterpret_cast<const ANSICHAR*>(Text.GetData()), Text.Len());
		return FString(Converted.Length(), Converted.Get());
	}

	FString IndentationAt(FUtf8StringView Source, int32 ByteOffset)
	{
		ByteOffset = FMath::Clamp(ByteOffset, 0, Source.Len());
		int32 LineStart = ByteOffset;
		while (LineStart > 0
			&& Source[LineStart - 1] != static_cast<UTF8CHAR>('\n')
			&& Source[LineStart - 1] != static_cast<UTF8CHAR>('\r'))
		{
			--LineStart;
		}
		int32 End = LineStart;
		while (End < Source.Len()
			&& (Source[End] == static_cast<UTF8CHAR>(' ')
				|| Source[End] == static_cast<UTF8CHAR>('\t')))
		{
			++End;
		}
		return DecodeUtf8(Source.Mid(LineStart, End - LineStart));
	}

	void FinalizeClauseSyntax(
		FVerseClauseDescriptor& Descriptor,
		FUtf8StringView Source,
		FVerseByteRange DefinitionRange,
		int32 FirstChildByte,
		int32 LastChildByte)
	{
		FVerseClauseSyntaxDescriptor& Syntax = Descriptor.Syntax;
		Syntax.OpeningRange = Descriptor.OpeningPunctuationRange;
		Syntax.ClosingRange = Descriptor.ClosingPunctuationRange;
		const int32 InteriorBegin = Descriptor.InteriorRange.IsSet()
			? Descriptor.InteriorRange.BeginByte : DefinitionRange.EndByte();
		const int32 InteriorEnd = Descriptor.InteriorRange.IsSet()
			? Descriptor.InteriorRange.EndByte() : InteriorBegin;
		const int32 ContentBegin = FMath::Clamp(FirstChildByte, InteriorBegin, InteriorEnd);
		const int32 ContentEnd = FMath::Clamp(LastChildByte, ContentBegin, InteriorEnd);
		Syntax.LeadingWhitespaceRange = FVerseByteRange::FromBounds(InteriorBegin, ContentBegin);
		Syntax.TrailingWhitespaceRange = FVerseByteRange::FromBounds(ContentEnd, InteriorEnd);
		const FUtf8StringView Interior = Source.Mid(InteriorBegin, InteriorEnd - InteriorBegin);
		Syntax.LineEnding = DetectLineEnding(Interior);
		Syntax.Layout = Syntax.LineEnding == EVerseLineEnding::None
			? EVerseSyntaxLayout::Inline : EVerseSyntaxLayout::Multiline;
		Syntax.IndentationPrefix = IndentationAt(Source, DefinitionRange.BeginByte);
		if (ContentBegin < InteriorEnd)
		{
			const FString ChildIndent = IndentationAt(Source, ContentBegin);
			Syntax.IndentationUnit = ChildIndent.StartsWith(Syntax.IndentationPrefix)
				? ChildIndent.Mid(Syntax.IndentationPrefix.Len()) : ChildIndent;
		}
		if (Syntax.Delimiter == EVerseClauseDelimiter::Braces)
		{
			const FUtf8StringView BeforeOpen = Source.Mid(
				DefinitionRange.BeginByte,
				FMath::Max(0, Descriptor.OpeningPunctuationRange.BeginByte
					- DefinitionRange.BeginByte));
			Syntax.BracePlacement = DetectLineEnding(BeforeOpen) == EVerseLineEnding::None
				? EVerseBracePlacement::SameLine : EVerseBracePlacement::NextLine;
		}
		const int32 HeaderEnd = Syntax.OpeningRange.IsSet()
			? Syntax.OpeningRange.BeginByte : ContentBegin;
		const int32 HeaderBegin = FMath::Clamp(DefinitionRange.BeginByte, 0, HeaderEnd);
		const FString HeaderText = DecodeUtf8(Source.Mid(
			HeaderBegin, HeaderEnd - HeaderBegin));
		auto RecordKeyword = [&](FStringView Keyword, EVerseClauseKeyword Kind)
		{
			const int32 Relative = HeaderText.Find(
				FString(Keyword), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
			if (Relative != INDEX_NONE)
			{
				Syntax.Keyword = Kind;
				const FTCHARToUTF8 Utf8(*FString(Keyword));
				Syntax.KeywordRange = {
					HeaderBegin + FTCHARToUTF8(*HeaderText.Left(Relative)).Length(),
					Utf8.Length()};
				return true;
			}
			return false;
		};
		if (!RecordKeyword(TEXTVIEW("else"), EVerseClauseKeyword::Else)
			&& !RecordKeyword(TEXTVIEW("then"), EVerseClauseKeyword::Then))
		{
			RecordKeyword(TEXTVIEW("do"), EVerseClauseKeyword::Do);
		}
		const FString Leading = DecodeUtf8(Source.Mid(
			Syntax.LeadingWhitespaceRange.BeginByte,
			Syntax.LeadingWhitespaceRange.NumBytes));
		Syntax.bHasCustomWhitespace = Leading.Contains(TEXT("\t"))
			&& Leading.Contains(TEXT(" "));
	}

	void AccumulateOwnedSourceBounds(
		const Verse::Vst::Node& Node,
		const FSourceIndex& SourceIndex,
		int32& InOutFirstByte,
		int32& InOutLastByte,
		TSet<const Verse::Vst::Node*>& VisitedNodes)
	{
		if (VisitedNodes.Contains(&Node))
		{
			return;
		}
		VisitedNodes.Add(&Node);

		const FVerseByteRange Range = SourceIndex.ToRange(Node.Whence());
		if (Range.IsSet())
		{
			InOutFirstByte = FMath::Min(InOutFirstByte, Range.BeginByte);
			InOutLastByte = FMath::Max(InOutLastByte, Range.EndByte());
		}
		for (const Verse::Vst::TNodeRef<Verse::Vst::Node>& Comment : Node.GetPrefixComments())
		{
			AccumulateOwnedSourceBounds(*Comment, SourceIndex, InOutFirstByte, InOutLastByte, VisitedNodes);
		}
		for (const Verse::Vst::TNodeRef<Verse::Vst::Node>& Comment : Node.GetPostfixComments())
		{
			AccumulateOwnedSourceBounds(*Comment, SourceIndex, InOutFirstByte, InOutLastByte, VisitedNodes);
		}
		if (Node.GetAux())
		{
			AccumulateOwnedSourceBounds(*Node.GetAux(), SourceIndex, InOutFirstByte, InOutLastByte, VisitedNodes);
		}
		for (const Verse::Vst::TNodeRef<Verse::Vst::Node>& Child : Node.GetChildren())
		{
			AccumulateOwnedSourceBounds(*Child, SourceIndex, InOutFirstByte, InOutLastByte, VisitedNodes);
		}
	}

	void SortRegions(TArray<FVerseSourceRegion>& Regions)
	{
		Regions.Sort([](const FVerseSourceRegion& Left, const FVerseSourceRegion& Right)
		{
			return Left.Range.BeginByte == Right.Range.BeginByte
				? Left.Range.NumBytes < Right.Range.NumBytes
				: Left.Range.BeginByte < Right.Range.BeginByte;
		});
	}

	TArray<FVerseSourceRegion> PartitionRange(
		FVerseByteRange ParentRange,
		TArray<FVerseSourceRegion> RecognizedRegions)
	{
		TArray<FVerseSourceRegion> CompleteRegions;
		if (!ParentRange.IsSet())
		{
			return CompleteRegions;
		}

		SortRegions(RecognizedRegions);
		int32 Cursor = ParentRange.BeginByte;
		for (FVerseSourceRegion& RecognizedRegion : RecognizedRegions)
		{
			if (!RecognizedRegion.Range.IsSet()
				|| RecognizedRegion.Range.BeginByte < Cursor
				|| RecognizedRegion.Range.EndByte() > ParentRange.EndByte())
			{
				continue;
			}
			if (Cursor < RecognizedRegion.Range.BeginByte)
			{
				FVerseSourceRegion& Gap = CompleteRegions.AddDefaulted_GetRef();
				Gap.Range = FVerseByteRange::FromBounds(Cursor, RecognizedRegion.Range.BeginByte);
			}
			CompleteRegions.Add(MoveTemp(RecognizedRegion));
			Cursor = CompleteRegions.Last().Range.EndByte();
		}
		if (Cursor < ParentRange.EndByte())
		{
			FVerseSourceRegion& Gap = CompleteRegions.AddDefaulted_GetRef();
			Gap.Range = FVerseByteRange::FromBounds(Cursor, ParentRange.EndByte());
		}
		return CompleteRegions;
	}

	FVerseClauseDescriptor MakeExpressionDescriptor(
		const Verse::Vst::Node& Expression,
		const FSourceIndex& SourceIndex)
	{
		FVerseClauseDescriptor Descriptor;
		Descriptor.InteriorRange = SourceIndex.ToRange(Expression.Whence());
		if (Descriptor.InteriorRange.IsSet())
		{
			Descriptor.EmptyBodyInsertionByte = Descriptor.InteriorRange.EndByte();
			Descriptor.Syntax.Layout = EVerseSyntaxLayout::Inline;
		}
		return Descriptor;
	}

	FVerseClauseDescriptor MakeClauseDescriptor(
		const Verse::Vst::Clause& Clause,
		FVerseByteRange DefinitionRange,
		const FSourceIndex& SourceIndex)
	{
		FVerseClauseDescriptor Descriptor;
		const FUtf8StringView Source = SourceIndex.GetSource();
		if (!DefinitionRange.IsSet() || DefinitionRange.EndByte() > Source.Len())
		{
			return Descriptor;
		}

		int32 FirstChildByte = DefinitionRange.EndByte();
		int32 LastChildByte = DefinitionRange.BeginByte;
		TSet<const Verse::Vst::Node*> VisitedNodes;
		for (const Verse::Vst::TNodeRef<Verse::Vst::Node>& Child : Clause.GetChildren())
		{
			AccumulateOwnedSourceBounds(
				*Child,
				SourceIndex,
				FirstChildByte,
				LastChildByte,
				VisitedNodes);
		}

		const Verse::Vst::Clause::EPunctuation Punctuation = Clause.GetPunctuation();
		if (Punctuation == Verse::Vst::Clause::EPunctuation::Braces)
		{
			const int32 OpenByte = FindLastByte(
				Source,
				static_cast<UTF8CHAR>('{'),
				DefinitionRange.BeginByte,
				FirstChildByte);
			const int32 CloseByte = FindLastByte(
				Source,
				static_cast<UTF8CHAR>('}'),
				FMath::Max(LastChildByte, OpenByte + 1),
				DefinitionRange.EndByte());
			if (OpenByte != INDEX_NONE && CloseByte != INDEX_NONE && OpenByte < CloseByte)
			{
				Descriptor.Syntax.Delimiter = EVerseClauseDelimiter::Braces;
				Descriptor.OpeningPunctuationRange = {OpenByte, 1};
				Descriptor.ClosingPunctuationRange = {CloseByte, 1};
				Descriptor.InteriorRange = FVerseByteRange::FromBounds(OpenByte + 1, CloseByte);
				Descriptor.EmptyBodyInsertionByte = OpenByte + 1;
				FinalizeClauseSyntax(
					Descriptor, Source, DefinitionRange, FirstChildByte, LastChildByte);
				return Descriptor;
			}
		}
		else if (Punctuation == Verse::Vst::Clause::EPunctuation::Colon)
		{
			const int32 ColonByte = FindLastByte(
				Source,
				static_cast<UTF8CHAR>(':'),
				DefinitionRange.BeginByte,
				FirstChildByte);
			if (ColonByte != INDEX_NONE)
			{
				Descriptor.Syntax.Delimiter = EVerseClauseDelimiter::Colon;
				Descriptor.OpeningPunctuationRange = {ColonByte, 1};
				Descriptor.InteriorRange = FVerseByteRange::FromBounds(ColonByte + 1, DefinitionRange.EndByte());
				Descriptor.EmptyBodyInsertionByte = ColonByte + 1;
				FinalizeClauseSyntax(
					Descriptor, Source, DefinitionRange, FirstChildByte, LastChildByte);
				return Descriptor;
			}
		}
		else if (Punctuation == Verse::Vst::Clause::EPunctuation::Indentation)
		{
			Descriptor.Syntax.Delimiter = EVerseClauseDelimiter::BareIndentation;
		}

		const FVerseByteRange ClauseRange = SourceIndex.ToRange(Clause.Whence());
		if (Punctuation == Verse::Vst::Clause::EPunctuation::Unknown
			&& ClauseRange.IsSet())
		{
			const int32 OpenParen = FindLastByte(
				Source, static_cast<UTF8CHAR>('('),
				ClauseRange.BeginByte, FirstChildByte);
			const int32 CloseParen = FindLastByte(
				Source, static_cast<UTF8CHAR>(')'),
				FMath::Max(LastChildByte, OpenParen + 1), ClauseRange.EndByte());
			if (OpenParen != INDEX_NONE && CloseParen != INDEX_NONE
				&& OpenParen < CloseParen)
			{
				Descriptor.Syntax.Delimiter = EVerseClauseDelimiter::Parentheses;
				Descriptor.OpeningPunctuationRange = {OpenParen, 1};
				Descriptor.ClosingPunctuationRange = {CloseParen, 1};
				Descriptor.InteriorRange = FVerseByteRange::FromBounds(
					OpenParen + 1, CloseParen);
				Descriptor.EmptyBodyInsertionByte = OpenParen + 1;
				FinalizeClauseSyntax(
					Descriptor, Source, DefinitionRange, FirstChildByte, LastChildByte);
				return Descriptor;
			}
			const int32 Dot = FindLastByte(
				Source, static_cast<UTF8CHAR>('.'),
				ClauseRange.BeginByte, FirstChildByte);
			if (Dot != INDEX_NONE)
			{
				Descriptor.Syntax.Delimiter = EVerseClauseDelimiter::Dot;
				Descriptor.OpeningPunctuationRange = {Dot, 1};
				Descriptor.InteriorRange = FVerseByteRange::FromBounds(
					Dot + 1, ClauseRange.EndByte());
				Descriptor.EmptyBodyInsertionByte = Dot + 1;
				FinalizeClauseSyntax(
					Descriptor, Source, DefinitionRange, FirstChildByte, LastChildByte);
				return Descriptor;
			}
		}
		Descriptor.InteriorRange = ClauseRange.IsSet()
			? ClauseRange
			: FVerseByteRange::FromBounds(DefinitionRange.EndByte(), DefinitionRange.EndByte());
		Descriptor.EmptyBodyInsertionByte = Descriptor.InteriorRange.BeginByte;
		FinalizeClauseSyntax(
			Descriptor, Source, DefinitionRange, FirstChildByte, LastChildByte);
		return Descriptor;
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

	void CollectIdentifiers(
		const Verse::Vst::Node& Node,
		TArray<const Verse::Vst::Identifier*>& OutIdentifiers)
	{
		if (const Verse::Vst::Identifier* Identifier =
			Node.AsNullable<Verse::Vst::Identifier>())
		{
			OutIdentifiers.Add(Identifier);
			return;
		}
		for (const Verse::Vst::TNodeRef<Verse::Vst::Node>& Child : Node.GetChildren())
		{
			CollectIdentifiers(*Child, OutIdentifiers);
		}
	}

	const Verse::Vst::PrePostCall* FindFunctionCall(const Verse::Vst::Node& Node)
	{
		if (const Verse::Vst::PrePostCall* Call = Node.AsNullable<Verse::Vst::PrePostCall>())
		{
			for (const Verse::Vst::TNodeRef<Verse::Vst::Node>& Child : Call->GetChildren())
			{
				const Verse::Vst::PrePostCall::Op Tag = Child->GetTag<Verse::Vst::PrePostCall::Op>();
				if (Tag == Verse::Vst::PrePostCall::SureCall
					|| Tag == Verse::Vst::PrePostCall::FailCall)
				{
					return Call;
				}
			}
		}
		if (Node.GetAux())
		{
			if (const Verse::Vst::PrePostCall* Call = FindFunctionCall(*Node.GetAux()))
			{
				return Call;
			}
		}
		for (const Verse::Vst::TNodeRef<Verse::Vst::Node>& Child : Node.GetChildren())
		{
			if (const Verse::Vst::PrePostCall* Call = FindFunctionCall(*Child))
			{
				return Call;
			}
		}
		return nullptr;
	}

	bool TryMakeFunctionParameter(
		const Verse::Vst::Node& Node,
		const FSourceIndex& SourceIndex,
		FVerseFunctionParameter& OutParameter)
	{
		const Verse::Vst::Node* ParameterNode = UnwrapSingleClause(&Node);
		if (ParameterNode == nullptr)
		{
			return false;
		}
		if (const Verse::Vst::Definition* DefaultValue = ParameterNode->AsNullable<Verse::Vst::Definition>())
		{
			ParameterNode = DefaultValue->GetOperandLeft().Get();
		}

		const Verse::Vst::Node* NameNode = ParameterNode;
		const Verse::Vst::Node* TypeNode = nullptr;
		if (const Verse::Vst::TypeSpec* TypeSpec = ParameterNode->AsNullable<Verse::Vst::TypeSpec>())
		{
			if (TypeSpec->HasLhs())
			{
				NameNode = TypeSpec->GetLhs().Get();
				TypeNode = TypeSpec->GetRhs().Get();
			}
		}

		const Verse::Vst::Identifier* Name = FindFirstIdentifier(*NameNode);
		if (Name == nullptr)
		{
			return false;
		}
		OutParameter.Range = SourceIndex.ToRange(Node.Whence());
		OutParameter.NameRange = SourceIndex.ToRange(Name->Whence());
		const int32 NameByteLength = Name->GetSourceText().ByteLen();
		if (OutParameter.NameRange.IsSet()
			&& NameByteLength > 0
			&& OutParameter.NameRange.NumBytes >= NameByteLength)
		{
			OutParameter.NameRange = {
				OutParameter.NameRange.EndByte() - NameByteLength,
				NameByteLength};
		}
		OutParameter.TypeRange = TypeNode != nullptr
			? SourceIndex.ToRange(TypeNode->Whence())
			: FVerseByteRange();
		return OutParameter.Range.IsSet() && OutParameter.NameRange.IsSet();
	}

	void CollectFunctionParametersFromArguments(
		const Verse::Vst::Node& Arguments,
		const FSourceIndex& SourceIndex,
		TArray<FVerseFunctionParameter>& OutParameters)
	{
		if (const Verse::Vst::Clause* Clause = Arguments.AsNullable<Verse::Vst::Clause>())
		{
			for (const Verse::Vst::TNodeRef<Verse::Vst::Node>& Child : Clause->GetChildren())
			{
				FVerseFunctionParameter Parameter;
				if (TryMakeFunctionParameter(*Child, SourceIndex, Parameter))
				{
					OutParameters.Add(MoveTemp(Parameter));
				}
				else if (Child->IsA<Verse::Vst::Clause>())
				{
					CollectFunctionParametersFromArguments(*Child, SourceIndex, OutParameters);
				}
			}
			return;
		}

		FVerseFunctionParameter Parameter;
		if (TryMakeFunctionParameter(Arguments, SourceIndex, Parameter))
		{
			OutParameters.Add(MoveTemp(Parameter));
		}
	}

	void CollectIdentifierReferences(
		const Verse::Vst::Node& Node,
		const uLang::CUTF8StringView& Name,
		FVerseByteRange BodyRange,
		const FSourceIndex& SourceIndex,
		TArray<FVerseByteRange>& OutReferences)
	{
		if (const Verse::Vst::Identifier* Identifier = Node.AsNullable<Verse::Vst::Identifier>())
		{
			const FVerseByteRange Range = SourceIndex.ToRange(Identifier->Whence());
			if (Identifier->GetSourceText() == Name
				&& Range.IsSet()
				&& Range.BeginByte >= BodyRange.BeginByte
				&& Range.EndByte() <= BodyRange.EndByte())
			{
				OutReferences.AddUnique(Range);
			}
		}
		if (Node.GetAux())
		{
			CollectIdentifierReferences(*Node.GetAux(), Name, BodyRange, SourceIndex, OutReferences);
		}
		for (const Verse::Vst::TNodeRef<Verse::Vst::Node>& Child : Node.GetChildren())
		{
			CollectIdentifierReferences(*Child, Name, BodyRange, SourceIndex, OutReferences);
		}
	}

	void PopulateFunctionMetadata(
		const Verse::Vst::Node& NameOperand,
		const Verse::Vst::Node& Body,
		const FSourceIndex& SourceIndex,
		FVerseSourceRegion& OutRegion)
	{
		const Verse::Vst::PrePostCall* Call = FindFunctionCall(NameOperand);
		if (Call == nullptr)
		{
			return;
		}
		FVerseByteRange ArgumentRange;
		for (const Verse::Vst::TNodeRef<Verse::Vst::Node>& Child : Call->GetChildren())
		{
			const Verse::Vst::PrePostCall::Op Tag = Child->GetTag<Verse::Vst::PrePostCall::Op>();
			if (Tag == Verse::Vst::PrePostCall::SureCall
				|| Tag == Verse::Vst::PrePostCall::FailCall)
			{
				ArgumentRange = SourceIndex.ToRange(Child->Whence());
				CollectFunctionParametersFromArguments(*Child, SourceIndex, OutRegion.FunctionParameters);
				break;
			}
		}
		for (const FVerseByteRange SpecifierRange : OutRegion.SpecifierRanges)
		{
			if (ArgumentRange.IsSet() && SpecifierRange.BeginByte < ArgumentRange.BeginByte)
			{
				OutRegion.FunctionAccessSpecifierRanges.Add(SpecifierRange);
			}
			else
			{
				OutRegion.FunctionEffectSpecifierRanges.Add(SpecifierRange);
			}
		}
		for (FVerseFunctionParameter& Parameter : OutRegion.FunctionParameters)
		{
			const FUtf8StringView NameView = SourceIndex.GetSource().Mid(
				Parameter.NameRange.BeginByte,
				Parameter.NameRange.NumBytes);
			const uLang::CUTF8StringView ParameterName(
				reinterpret_cast<const char*>(NameView.GetData()),
				NameView.Len());
			CollectIdentifierReferences(
				Body,
				ParameterName,
				OutRegion.BodyRange,
				SourceIndex,
				Parameter.ReferenceRanges);
			Parameter.ReferenceRanges.Sort([](const FVerseByteRange& Left, const FVerseByteRange& Right)
			{
				return Left.BeginByte < Right.BeginByte;
			});
		}
	}

	int32 CountLineBreaks(FUtf8StringView Text)
	{
		int32 Count = 0;
		for (int32 Index = 0; Index < Text.Len(); ++Index)
		{
			if (Text[Index] == static_cast<UTF8CHAR>('\r'))
			{
				++Count;
				if (Index + 1 < Text.Len() && Text[Index + 1] == static_cast<UTF8CHAR>('\n'))
				{
					++Index;
				}
			}
			else if (Text[Index] == static_cast<UTF8CHAR>('\n'))
			{
				++Count;
			}
		}
		return Count;
	}

	FVerseSeparatorDescriptor ClassifySeparator(
		FUtf8StringView Source,
		FVerseByteRange TriviaRange,
		bool bIsFinal)
	{
		FVerseSeparatorDescriptor Result;
		Result.bIsEndOfClause = bIsFinal;
		if (!TriviaRange.IsSet())
		{
			return Result;
		}
		const FUtf8StringView Trivia = Source.Mid(
			TriviaRange.BeginByte, TriviaRange.NumBytes);
		int32 TokenOffset = INDEX_NONE;
		for (int32 Index = 0; Index < Trivia.Len(); ++Index)
		{
			if (Trivia[Index] == static_cast<UTF8CHAR>(';')
				|| Trivia[Index] == static_cast<UTF8CHAR>(','))
			{
				TokenOffset = Index;
				Result.Token = Trivia[Index] == static_cast<UTF8CHAR>(';')
					? EVerseSeparatorToken::Semicolon
					: EVerseSeparatorToken::Comma;
				break;
			}
		}
		if (TokenOffset != INDEX_NONE)
		{
			Result.TokenRange = {TriviaRange.BeginByte + TokenOffset, 1};
		}
		const int32 WhitespaceBegin = TokenOffset == INDEX_NONE
			? TriviaRange.BeginByte : TriviaRange.BeginByte + TokenOffset + 1;
		Result.WhitespaceRange = FVerseByteRange::FromBounds(
			WhitespaceBegin, TriviaRange.EndByte());
		const FUtf8StringView Whitespace = Source.Mid(
			Result.WhitespaceRange.BeginByte, Result.WhitespaceRange.NumBytes);
		const int32 LineBreaks = CountLineBreaks(Whitespace);
		Result.BlankLineCount = FMath::Max(0, LineBreaks - 1);
		if (TokenOffset != INDEX_NONE)
		{
			Result.Layout = LineBreaks > 0
				? EVerseSeparatorLayout::TokenAndNewline
				: EVerseSeparatorLayout::TokenAndSpace;
		}
		else if (LineBreaks > 0)
		{
			Result.Layout = EVerseSeparatorLayout::Newline;
		}
		else if (!Whitespace.IsEmpty())
		{
			bool bSimpleHorizontal = true;
			for (UTF8CHAR Byte : Whitespace)
			{
				bSimpleHorizontal &= Byte == static_cast<UTF8CHAR>(' ')
					|| Byte == static_cast<UTF8CHAR>('\t');
			}
			Result.Layout = bSimpleHorizontal
				? EVerseSeparatorLayout::HorizontalSpace
				: EVerseSeparatorLayout::CustomPreserved;
		}
		return Result;
	}

	FVerseExpressionType FindIdentifierType(
		FUtf8StringView IdentifierText,
		const FSourceIndex& SourceIndex,
		TConstArrayView<FVerseFunctionParameter> Parameters)
	{
		for (const FVerseFunctionParameter& Parameter : Parameters)
		{
			const FUtf8StringView ParameterName = SourceIndex.GetSource().Mid(
				Parameter.NameRange.BeginByte,
				Parameter.NameRange.NumBytes);
			if (IdentifierText == ParameterName)
			{
				return {Parameter.TypeRange, NAME_None,
					EVerseTypeResolutionProvenance::LocallyInferred};
			}
		}
		return {};
	}

	FVerseExpressionDescriptor BuildExpressionDescriptor(
		const Verse::Vst::Node& Node,
		const FSourceIndex& SourceIndex,
		TConstArrayView<FVerseFunctionParameter> Parameters);

	FVerseExpressionDescriptor BuildWrappedExpressionDescriptor(
		const Verse::Vst::Node& Node,
		const FSourceIndex& SourceIndex,
		TConstArrayView<FVerseFunctionParameter> Parameters)
	{
		TArray<FVerseGroupingLayer> GroupingLayers;
		const Verse::Vst::Node* Unwrapped = UnwrapSingleClauseAndCollectGrouping(
			&Node, SourceIndex, GroupingLayers);
		FVerseExpressionDescriptor Result = BuildExpressionDescriptor(
			Unwrapped != nullptr ? *Unwrapped : Node, SourceIndex, Parameters);
		// Parenthesis wrappers are not consistently materialized as Parens nodes by
		// the UE6 VST. Recover source-exact grouping immediately around the compiler
		// expression, while excluding call/control delimiters (whose opening token is
		// immediately preceded by an identifier/keyword).
		if (Result.Range.IsSet())
		{
			const FUtf8StringView Source = SourceIndex.GetSource();
			int32 InnerBegin = Result.Range.BeginByte;
			int32 InnerEnd = Result.Range.EndByte();
			for (;;)
			{
				int32 Open = InnerBegin - 1;
				while (Open >= 0 && FChar::IsWhitespace(static_cast<TCHAR>(Source[Open]))) --Open;
				int32 Close = InnerEnd;
				while (Close < Source.Len() && FChar::IsWhitespace(static_cast<TCHAR>(Source[Close]))) ++Close;
				if (Open < 0 || Close >= Source.Len()
					|| Source[Open] != static_cast<UTF8CHAR>('(')
					|| Source[Close] != static_cast<UTF8CHAR>(')'))
				{
					break;
				}
				int32 BeforeOpen = Open - 1;
				while (BeforeOpen >= 0
					&& FChar::IsWhitespace(static_cast<TCHAR>(Source[BeforeOpen]))) --BeforeOpen;
				if (BeforeOpen >= 0)
				{
					const UTF8CHAR Before = Source[BeforeOpen];
					const bool bCallOrKeywordDelimiter =
						(Before >= static_cast<UTF8CHAR>('a') && Before <= static_cast<UTF8CHAR>('z'))
						|| (Before >= static_cast<UTF8CHAR>('A') && Before <= static_cast<UTF8CHAR>('Z'))
						|| (Before >= static_cast<UTF8CHAR>('0') && Before <= static_cast<UTF8CHAR>('9'))
						|| Before == static_cast<UTF8CHAR>('_');
					if (bCallOrKeywordDelimiter)
					{
						break;
					}
				}
				if (!GroupingLayers.ContainsByPredicate(
					[Open](const FVerseGroupingLayer& Layer)
					{
						return Layer.OpeningRange.BeginByte == Open;
					}))
				{
					GroupingLayers.Add({FVerseByteRange::FromBounds(Open, Close + 1),
						{Open, 1}, {Close, 1}});
				}
				InnerBegin = Open;
				InnerEnd = Close + 1;
			}
		}
		if (!GroupingLayers.IsEmpty())
		{
			GroupingLayers.Sort([](const FVerseGroupingLayer& Left, const FVerseGroupingLayer& Right)
			{
				return Left.OpeningRange.BeginByte < Right.OpeningRange.BeginByte;
			});
			Result.GroupingLayers = MoveTemp(GroupingLayers);
			Result.Range = Result.GroupingLayers[0].Range;
		}
		return Result;
	}

	FVerseExpressionDescriptor BuildExpressionDescriptor(
		const Verse::Vst::Node& Node,
		const FSourceIndex& SourceIndex,
		TConstArrayView<FVerseFunctionParameter> Parameters)
	{
		FVerseExpressionDescriptor Result;
		Result.Range = SourceIndex.ToRange(Node.Whence());
		Result.VstNodeType = static_cast<uint8>(Node.GetElementType());
		Result.VstTag = Node.GetTag<uint8>();
		auto AppendControlRegion = [&](const Verse::Vst::Clause& Clause,
			EVerseControlRegionKind Kind)
		{
			FVerseExpressionControlRegion& Region =
				Result.ControlRegions.AddDefaulted_GetRef();
			Region.Range = SourceIndex.ToRange(Clause.Whence());
			Region.Kind = Kind;
			const FVerseClauseDescriptor ClauseDescriptor =
				MakeClauseDescriptor(Clause, Region.Range, SourceIndex);
			Region.InteriorRange = ClauseDescriptor.InteriorRange;
			Region.OpeningPunctuationRange = ClauseDescriptor.OpeningPunctuationRange;
			Region.ClosingPunctuationRange = ClauseDescriptor.ClosingPunctuationRange;
			Region.Syntax = ClauseDescriptor.Syntax;
			Region.EmptyBodyInsertionByte = ClauseDescriptor.EmptyBodyInsertionByte;
			Region.FirstOperandIndex = Result.Operands.Num();
			for (const Verse::Vst::TNodeRef<Verse::Vst::Node>& Child : Clause.GetChildren())
			{
				const Verse::Vst::Node* Expression = UnwrapSingleClause(Child.Get());
				if (Expression != nullptr && !Expression->IsA<Verse::Vst::Comment>())
				{
					FVerseExpressionDescriptor Operand = BuildWrappedExpressionDescriptor(
						*Child, SourceIndex, Parameters);
					// The official VST represents a for generator (`Item : Iterable`)
					// as a top-level TypeSpec rather than a Definition. Translate that
					// syntax at this parser boundary so later graph passes receive a
					// definition-shaped operand with an explicit generator identity.
					if (Result.ControlKind == EVerseControlKind::For
						&& Kind == EVerseControlRegionKind::Condition)
					{
						const Verse::Vst::TypeSpec* Generator =
							Expression->AsNullable<Verse::Vst::TypeSpec>();
						if (Generator != nullptr && Generator->HasLhs())
						{
							Operand.Kind = EVerseExpressionKind::Definition;
							Operand.bForGenerator = true;
							Operand.DefinitionKind = VerseSyntaxKind::Constant;
							TArray<const Verse::Vst::Identifier*> BindingNames;
							CollectIdentifiers(*Generator->GetLhs(), BindingNames);
							if (!BindingNames.IsEmpty())
							{
								const Verse::Vst::Identifier* Name = BindingNames.Pop();
								Operand.NameRange = TrimSourceWhitespace(
									SourceIndex.GetSource(), SourceIndex.ToRange(Name->Whence()));
								for (const Verse::Vst::Identifier* AdditionalName : BindingNames)
								{
									Operand.AdditionalBindingNameRanges.Add(
										TrimSourceWhitespace(
											SourceIndex.GetSource(),
											SourceIndex.ToRange(AdditionalName->Whence())));
								}
							}
							Operand.DeclaredTypeRange = {};
							Operand.Operands.Reset();
							Operand.Operands.Add(BuildWrappedExpressionDescriptor(
								*Generator->GetRhs(), SourceIndex, Parameters));
						}
						}
						Result.Operands.Add(MoveTemp(Operand));
						FVerseExpressionControlItem& ParsedItem = Region.Items.AddDefaulted_GetRef();
						CollectItemCommentDescriptors(*Child, SourceIndex, ParsedItem.Comments);
					}
				}
				Region.OperandCount = Result.Operands.Num() - Region.FirstOperandIndex;
			for (int32 Offset = 0; Offset < Region.OperandCount; ++Offset)
			{
				const int32 OperandIndex = Region.FirstOperandIndex + Offset;
				const FVerseByteRange ExpressionRange = Result.Operands[OperandIndex].Range;
					check(Region.Items.IsValidIndex(Offset));
					FVerseExpressionControlItem& Item = Region.Items[Offset];
				Item.ExpressionRange = ExpressionRange;
				const int32 LeadingBegin = Offset == 0
					? Region.InteriorRange.BeginByte
					: Result.Operands[OperandIndex - 1].Range.EndByte();
				if (Region.InteriorRange.IsSet()
					&& LeadingBegin < ExpressionRange.BeginByte)
				{
					Item.LeadingTriviaRange = FVerseByteRange::FromBounds(
						LeadingBegin,
						ExpressionRange.BeginByte);
				}
				const int32 TrailingEnd = Offset + 1 < Region.OperandCount
					? Result.Operands[OperandIndex + 1].Range.BeginByte
					: Region.InteriorRange.EndByte();
				if (ExpressionRange.EndByte() < TrailingEnd)
				{
					Item.TrailingTriviaRange = FVerseByteRange::FromBounds(
						ExpressionRange.EndByte(),
						TrailingEnd);
				}
				const FUtf8StringView Trivia = Item.TrailingTriviaRange.IsSet()
					? SourceIndex.GetSource().Mid(
						Item.TrailingTriviaRange.BeginByte,
						Item.TrailingTriviaRange.NumBytes)
					: FUtf8StringView();
				Item.Separator = ClassifySeparator(
					SourceIndex.GetSource(), Item.TrailingTriviaRange,
					Offset == Region.OperandCount - 1);
			}
		};

		if (const Verse::Vst::FlowIf* FlowIf = Node.AsNullable<Verse::Vst::FlowIf>())
		{
			Result.Kind = EVerseExpressionKind::Control;
			Result.ControlKind = EVerseControlKind::If;
			for (const Verse::Vst::TNodeRef<Verse::Vst::Node>& Child : FlowIf->GetChildren())
			{
				const Verse::Vst::Clause* Clause = Child->AsNullable<Verse::Vst::Clause>();
				if (Clause == nullptr)
				{
					continue;
				}
				switch (Child->GetTag<Verse::Vst::FlowIf::ClauseTag>())
				{
				case Verse::Vst::FlowIf::ClauseTag::condition:
					AppendControlRegion(*Clause, EVerseControlRegionKind::Condition);
					break;
				case Verse::Vst::FlowIf::ClauseTag::then_body:
					AppendControlRegion(*Clause, EVerseControlRegionKind::Body);
					break;
				case Verse::Vst::FlowIf::ClauseTag::else_body:
					AppendControlRegion(*Clause, EVerseControlRegionKind::Else);
					break;
				default:
					break;
				}
			}
			return Result;
		}

		if (const Verse::Vst::Macro* Macro = Node.AsNullable<Verse::Vst::Macro>())
		{
			const Verse::Vst::Identifier* Name = FindFirstIdentifier(*Macro->GetName());
			const FVerseByteRange NameRange = Name != nullptr
				? TrimSourceWhitespace(SourceIndex.GetSource(), SourceIndex.ToRange(Name->Whence()))
				: FVerseByteRange();
			const FUtf8StringView NameText = NameRange.IsSet()
				? SourceIndex.GetSource().Mid(NameRange.BeginByte, NameRange.NumBytes)
				: FUtf8StringView();
			if (NameText == UTF8TEXTVIEW("for")
					|| NameText == UTF8TEXTVIEW("loop")
					|| NameText == UTF8TEXTVIEW("sync")
					|| NameText == UTF8TEXTVIEW("block"))
			{
				Result.Kind = EVerseExpressionKind::Control;
				if (NameText == UTF8TEXTVIEW("for"))
				{
					Result.ControlKind = EVerseControlKind::For;
				}
				else if (NameText == UTF8TEXTVIEW("loop"))
				{
					Result.ControlKind = EVerseControlKind::Loop;
				}
				else if (NameText == UTF8TEXTVIEW("sync"))
				{
					Result.ControlKind = EVerseControlKind::Sync;
				}
				else
				{
					Result.ControlKind = EVerseControlKind::Block;
				}
				Result.OperatorRange = NameRange;
				const int32 ClauseCount = Macro->GetChildCount() - 1;
				for (int32 ClauseIndex = 0; ClauseIndex < ClauseCount; ++ClauseIndex)
				{
					AppendControlRegion(
						*Macro->GetClause(ClauseIndex),
						Result.ControlKind == EVerseControlKind::For && ClauseIndex == 0
								? EVerseControlRegionKind::Condition
								: EVerseControlRegionKind::Body);
				}
				return Result;
			}
		}

		if (const Verse::Vst::Definition* Definition = Node.AsNullable<Verse::Vst::Definition>();
			Definition != nullptr && Definition->GetChildCount() == 2)
		{
			const Verse::Vst::Node& Left = *Definition->GetOperandLeft();
			const Verse::Vst::Node* NameNode = &Left;
			const Verse::Vst::Node* TypeNode = nullptr;
			if (const Verse::Vst::TypeSpec* TypeSpec = Left.AsNullable<Verse::Vst::TypeSpec>();
				TypeSpec != nullptr && TypeSpec->HasLhs())
			{
				NameNode = TypeSpec->GetLhs().Get();
				TypeNode = TypeSpec->GetRhs().Get();
			}
			if (const Verse::Vst::Identifier* Name = FindFirstIdentifier(*NameNode))
			{
				Result.Kind = EVerseExpressionKind::Definition;
				Result.DefinitionKind = ContainsNodeType(
					*NameNode, Verse::Vst::NodeType::Mutation)
					? VerseSyntaxKind::Variable
					: VerseSyntaxKind::Constant;
				Result.NameRange = SourceIndex.ToRange(Name->Whence());
				const int32 NameLength = Name->GetSourceText().ByteLen();
				if (NameLength > 0 && Result.NameRange.NumBytes >= NameLength)
				{
					Result.NameRange = {
						Result.NameRange.EndByte() - NameLength, NameLength};
				}
				Result.DeclaredTypeRange = TypeNode != nullptr
					? SourceIndex.ToRange(TypeNode->Whence())
					: FVerseByteRange();
				const Verse::Vst::Node* Value = UnwrapSingleClause(
					Definition->GetOperandRight().Get());
				if (Value != nullptr)
				{
					Result.Operands.Add(BuildWrappedExpressionDescriptor(
						*Definition->GetOperandRight(), SourceIndex, Parameters));
				}
				return Result;
			}
		}
		if (const Verse::Vst::Identifier* Identifier = Node.AsNullable<Verse::Vst::Identifier>())
		{
			const int32 IdentifierLength = Identifier->GetSourceText().ByteLen();
			if (IdentifierLength > 0 && Result.Range.NumBytes >= IdentifierLength)
			{
				Result.Range = {Result.Range.EndByte() - IdentifierLength, IdentifierLength};
			}
			const FUtf8StringView IdentifierText = SourceIndex.GetSource().Mid(
				Result.Range.BeginByte,
				Result.Range.NumBytes);
			if (IdentifierText == UTF8TEXTVIEW("true") || IdentifierText == UTF8TEXTVIEW("false"))
			{
				Result.Kind = EVerseExpressionKind::Literal;
				Result.LiteralKind = EVerseLiteralKind::Logic;
				Result.Type = {{}, TEXT("logic"), EVerseTypeResolutionProvenance::LocallyInferred};
				return Result;
			}
			Result.Kind = EVerseExpressionKind::Identifier;
			Result.Type = FindIdentifierType(
				IdentifierText,
				SourceIndex,
				Parameters);
			return Result;
		}
		if (Node.IsA<Verse::Vst::IntLiteral>())
		{
			Result.Kind = EVerseExpressionKind::Literal;
			Result.LiteralKind = EVerseLiteralKind::Integer;
			Result.Type = {{}, TEXT("int"), EVerseTypeResolutionProvenance::LocallyInferred};
			return Result;
		}
		if (Node.IsA<Verse::Vst::FloatLiteral>())
		{
			Result.Kind = EVerseExpressionKind::Literal;
			Result.LiteralKind = EVerseLiteralKind::Float;
			Result.Type = {{}, TEXT("float"), EVerseTypeResolutionProvenance::LocallyInferred};
			return Result;
		}
		if (Node.IsA<Verse::Vst::StringLiteral>())
		{
			const FUtf8StringView Source = SourceIndex.GetSource();
			if (Result.Range.BeginByte > 0
				&& Result.Range.EndByte() < Source.Len()
				&& Source[Result.Range.BeginByte - 1] == static_cast<UTF8CHAR>('"')
				&& Source[Result.Range.EndByte()] == static_cast<UTF8CHAR>('"'))
			{
				// The VST locus for a plain string covers its contents, unlike the
				// other literal nodes. The visual expression owns the complete source
				// spelling so localized replacement also replaces both delimiters.
				Result.Range = FVerseByteRange::FromBounds(
					Result.Range.BeginByte - 1,
					Result.Range.EndByte() + 1);
			}
			Result.Kind = EVerseExpressionKind::Literal;
			Result.LiteralKind = EVerseLiteralKind::String;
			Result.Type = {{}, TEXT("string"), EVerseTypeResolutionProvenance::LocallyInferred};
			return Result;
		}
		if (Node.IsA<Verse::Vst::CharLiteral>())
		{
			Result.Kind = EVerseExpressionKind::Literal;
			Result.LiteralKind = EVerseLiteralKind::Character;
			Result.Type = {{}, TEXT("char"), EVerseTypeResolutionProvenance::LocallyInferred};
			return Result;
		}

		const Verse::Vst::BinaryOpAddSub* Add = Node.AsNullable<Verse::Vst::BinaryOpAddSub>();
		if (Add != nullptr && Add->GetChildCount() == 2)
		{
			const Verse::Vst::Node& OperatorNode = *Add->GetChildren()[0];
			const Verse::Vst::Node& OperandNode = *Add->GetChildren()[1];
			const Verse::Vst::Operator* Operator = OperatorNode.AsNullable<Verse::Vst::Operator>();
			if (OperatorNode.GetTag<Verse::Vst::BinaryOp::op>() == Verse::Vst::BinaryOp::op::Operator
				&& OperandNode.GetTag<Verse::Vst::BinaryOp::op>() == Verse::Vst::BinaryOp::op::Operand
				&& Operator != nullptr
				&& Operator->GetSourceText().ByteLen() == 1
				&& (Operator->GetSourceText()[0] == u'-' || Operator->GetSourceText()[0] == u'+'))
			{
				FVerseExpressionDescriptor SignedOperand =
					BuildWrappedExpressionDescriptor(OperandNode, SourceIndex, Parameters);
				if (SignedOperand.LiteralKind == EVerseLiteralKind::Integer
					|| SignedOperand.LiteralKind == EVerseLiteralKind::Float)
				{
					const FVerseByteRange OperatorLocus = SourceIndex.ToRange(OperatorNode.Whence());
					const int32 SignByte = FindLastByte(
						SourceIndex.GetSource(),
						static_cast<UTF8CHAR>(Operator->GetSourceText()[0]),
						OperatorLocus.BeginByte,
						OperatorLocus.EndByte());
					SignedOperand.Range = SignByte != INDEX_NONE
						? FVerseByteRange::FromBounds(SignByte, SignedOperand.Range.EndByte())
						: Result.Range;
					return SignedOperand;
				}
			}
		}
		auto BuildBinary = [&](const Verse::Vst::Node& Left,
			const Verse::Vst::Node& Right,
			FVerseByteRange OperatorRange,
			FString OperatorSpelling)
		{
			Result.Kind = EVerseExpressionKind::BinaryOperator;
			Result.OperatorRange = TrimSourceWhitespace(SourceIndex.GetSource(), OperatorRange);
			Result.OperatorSpelling = MoveTemp(OperatorSpelling);
			Result.Operands.Add(BuildWrappedExpressionDescriptor(
				Left, SourceIndex, Parameters));
			Result.Operands.Add(BuildWrappedExpressionDescriptor(
				Right, SourceIndex, Parameters));
		};

		auto TryBuildTaggedBinary = [&](const Verse::Vst::BinaryOp* Binary) -> bool
		{
			if (Binary == nullptr || Binary->GetChildCount() != 3)
			{
				return false;
			}
			const Verse::Vst::Node& Left = *Binary->GetChildren()[0];
			const Verse::Vst::Node& OperatorNode = *Binary->GetChildren()[1];
			const Verse::Vst::Node& Right = *Binary->GetChildren()[2];
			const Verse::Vst::Operator* Operator = OperatorNode.AsNullable<Verse::Vst::Operator>();
			if (Left.GetTag<Verse::Vst::BinaryOp::op>() != Verse::Vst::BinaryOp::op::Operand
				|| OperatorNode.GetTag<Verse::Vst::BinaryOp::op>() != Verse::Vst::BinaryOp::op::Operator
				|| Right.GetTag<Verse::Vst::BinaryOp::op>() != Verse::Vst::BinaryOp::op::Operand
				|| Operator == nullptr || Operator->GetSourceText().ByteLen() != 1)
			{
				return false;
			}
			const FVerseByteRange OperatorLocus =
				SourceIndex.ToRange(OperatorNode.Whence());
			const UTF8CHAR OperatorByte =
				static_cast<UTF8CHAR>(Operator->GetSourceText()[0]);
			const FUTF8ToTCHAR ConvertedOperator(
				reinterpret_cast<const ANSICHAR*>(Operator->GetSourceText().AsUTF8()),
				Operator->GetSourceText().ByteLen());
			const int32 OperatorByteIndex = FindLastByte(
				SourceIndex.GetSource(),
				OperatorByte,
				OperatorLocus.BeginByte,
				OperatorLocus.EndByte());
			BuildBinary(
				Left,
				Right,
				OperatorByteIndex != INDEX_NONE
					? FVerseByteRange{OperatorByteIndex, 1}
					: OperatorLocus,
				FString(ConvertedOperator.Length(), ConvertedOperator.Get()));
			return true;
		};

		if (TryBuildTaggedBinary(Add)
			|| TryBuildTaggedBinary(Node.AsNullable<Verse::Vst::BinaryOpMulDivInfix>()))
		{
			return Result;
		}

		if (const Verse::Vst::BinaryOpCompare* Compare = Node.AsNullable<Verse::Vst::BinaryOpCompare>();
			Compare != nullptr && Compare->GetChildCount() == 2)
		{
			const Verse::Vst::Node& Left = *Compare->GetOperandLeft();
			const Verse::Vst::Node& Right = *Compare->GetOperandRight();
			const FVerseByteRange LeftRange = SourceIndex.ToRange(Left.Whence());
			const FVerseByteRange RightRange = SourceIndex.ToRange(Right.Whence());
			FString OperatorSpelling;
			switch (Compare->GetOp())
			{
			case Verse::Vst::BinaryOpCompare::op::lt: OperatorSpelling = TEXT("<"); break;
			case Verse::Vst::BinaryOpCompare::op::lteq: OperatorSpelling = TEXT("<="); break;
			case Verse::Vst::BinaryOpCompare::op::gt: OperatorSpelling = TEXT(">"); break;
			case Verse::Vst::BinaryOpCompare::op::gteq: OperatorSpelling = TEXT(">="); break;
			case Verse::Vst::BinaryOpCompare::op::eq: OperatorSpelling = TEXT("="); break;
			case Verse::Vst::BinaryOpCompare::op::noteq: OperatorSpelling = TEXT("<>"); break;
			default: checkNoEntry(); break;
			}
			BuildBinary(
				Left,
				Right,
				FVerseByteRange::FromBounds(LeftRange.EndByte(), RightRange.BeginByte),
				MoveTemp(OperatorSpelling));
			return Result;
		}

		auto TryBuildTwoOperandGapOperator = [&](bool bMatches,
			const TCHAR* OperatorSpelling) -> bool
		{
			if (!bMatches || Node.GetChildCount() != 2)
			{
				return false;
			}
			const Verse::Vst::Node& Left = *Node.GetChildren()[0];
			const Verse::Vst::Node& Right = *Node.GetChildren()[1];
			const FVerseByteRange LeftRange = SourceIndex.ToRange(Left.Whence());
			const FVerseByteRange RightRange = SourceIndex.ToRange(Right.Whence());
			BuildBinary(
				Left,
				Right,
				FVerseByteRange::FromBounds(LeftRange.EndByte(), RightRange.BeginByte),
				OperatorSpelling);
			return true;
		};
		if (TryBuildTwoOperandGapOperator(
				Node.IsA<Verse::Vst::BinaryOpLogicalAnd>(), TEXT("and"))
			|| TryBuildTwoOperandGapOperator(
				Node.IsA<Verse::Vst::BinaryOpLogicalOr>(), TEXT("or"))
			|| TryBuildTwoOperandGapOperator(
				Node.IsA<Verse::Vst::Assignment>(), TEXT("=")))
		{
			return Result;
		}

		if (const Verse::Vst::PrefixOpLogicalNot* LogicalNot =
			Node.AsNullable<Verse::Vst::PrefixOpLogicalNot>();
			LogicalNot != nullptr && LogicalNot->GetChildCount() == 1)
		{
			const Verse::Vst::Node& Operand = *LogicalNot->GetChildren()[0];
			const FVerseByteRange OperandRange = SourceIndex.ToRange(Operand.Whence());
			Result.Kind = EVerseExpressionKind::UnaryOperator;
			Result.OperatorSpelling = TEXT("not");
			Result.OperatorRange = TrimSourceWhitespace(
				SourceIndex.GetSource(),
				FVerseByteRange::FromBounds(Result.Range.BeginByte, OperandRange.BeginByte));
			Result.Operands.Add(BuildWrappedExpressionDescriptor(Operand, SourceIndex, Parameters));
			return Result;
		}

		if (Add != nullptr && Add->GetChildCount() == 2)
		{
			const Verse::Vst::Node& OperatorNode = *Add->GetChildren()[0];
			const Verse::Vst::Node& Operand = *Add->GetChildren()[1];
			Result.Kind = EVerseExpressionKind::UnaryOperator;
			if (const Verse::Vst::Operator* PrefixOperator =
				OperatorNode.AsNullable<Verse::Vst::Operator>())
			{
				const FUTF8ToTCHAR ConvertedOperator(
					reinterpret_cast<const ANSICHAR*>(PrefixOperator->GetSourceText().AsUTF8()),
					PrefixOperator->GetSourceText().ByteLen());
				Result.OperatorSpelling = FString(
					ConvertedOperator.Length(), ConvertedOperator.Get());
			}
			Result.OperatorRange = TrimSourceWhitespace(
				SourceIndex.GetSource(), SourceIndex.ToRange(OperatorNode.Whence()));
			Result.Operands.Add(BuildWrappedExpressionDescriptor(Operand, SourceIndex, Parameters));
			return Result;
		}

		// The official Verse parser deliberately represents postfix `?` as a
		// PrePostCall chain whose trailing synthetic Clause has the Option tag; it
		// does not use the ordinary prefix/binary operator nodes. This is a VST-to-
		// editor translation, not the semantic `_OpNameQuery` compatibility shim in
		// VerseSemanticCandidates. If Epic changes this VST shape, the source-exact
		// query assertions in FVerseLiteralTilePresentationTest should fail and this
		// recognizer must be updated to the new official representation.
		if (const Verse::Vst::PrePostCall* Query = Node.AsNullable<Verse::Vst::PrePostCall>();
			Query != nullptr
			&& Query->GetChildCount() == 2
			&& Query->GetChildren()[0]->GetTag<Verse::Vst::PrePostCall::Op>()
				== Verse::Vst::PrePostCall::Expression
			&& Query->GetChildren()[1]->GetTag<Verse::Vst::PrePostCall::Op>()
				== Verse::Vst::PrePostCall::Option)
		{
			const Verse::Vst::Node& Operand = *Query->GetChildren()[0];
			const Verse::Vst::Node& Operator = *Query->GetChildren()[1];
			FVerseExpressionDescriptor OperandDescriptor =
				BuildWrappedExpressionDescriptor(Operand, SourceIndex, Parameters);
			Result.Kind = EVerseExpressionKind::UnaryOperator;
			Result.OperatorSpelling = TEXT("?");
			Result.OperatorRange = TrimSourceWhitespace(
				SourceIndex.GetSource(), SourceIndex.ToRange(Operator.Whence()));
			// Parameter references currently retain their declared type only as a
			// source range, while literals carry a canonical intrinsic name. Query needs
			// the canonical name now so its pins and failable result use the logic color.
			// FVerseQuerySyntaxTypeBridgeMaintenanceTest contains an intentional
			// maintenance
			// tripwire proving this range-only limitation still exists. When that test
			// fails because parameter references acquire canonical semantic type names,
			// remove this source-text fallback and consume the canonical type directly.
			// The option overload remains for the later option-expression work.
			const FVerseByteRange OperandTypeRange = TrimSourceWhitespace(
				SourceIndex.GetSource(), OperandDescriptor.Type.SourceRange);
			const bool bLogicOperand = OperandDescriptor.Type.IntrinsicName == TEXT("logic")
				|| (OperandTypeRange.IsSet()
					&& SourceIndex.GetSource().Mid(
						OperandTypeRange.BeginByte, OperandTypeRange.NumBytes)
						== UTF8TEXTVIEW("logic"));
			if (bLogicOperand)
			{
				OperandDescriptor.Type.IntrinsicName = TEXT("logic");
				Result.Type = OperandDescriptor.Type;
			}
			Result.Operands.Add(MoveTemp(OperandDescriptor));
			return Result;
		}

		if (const Verse::Vst::PrePostCall* Call = Node.AsNullable<Verse::Vst::PrePostCall>();
			Call != nullptr && Call->IsSimpleCall())
		{
			Result.Kind = EVerseExpressionKind::Call;
			const Verse::Vst::Node& Callee = *Call->GetChildren()[0];
			const Verse::Vst::Node& Arguments = *Call->GetChildren()[1];
			Result.OperatorRange = SourceIndex.ToRange(Callee.Whence());
			if (const Verse::Vst::Clause* Clause = Arguments.AsNullable<Verse::Vst::Clause>())
			{
				for (const Verse::Vst::TNodeRef<Verse::Vst::Node>& Argument : Clause->GetChildren())
				{
					// A named argument is represented by the official VST as a
					// Definition (`?Name := Value`). The call operand is Value; the
					// selected semantic signature supplies the socket's parameter name.
					const Verse::Vst::Node* Value = Argument.Get();
					if (const Verse::Vst::Definition* Named =
						Argument->AsNullable<Verse::Vst::Definition>())
					{
						Value = Named->GetOperandRight().Get();
					}
					Result.Operands.Add(BuildWrappedExpressionDescriptor(*Value, SourceIndex, Parameters));
				}
			}
			return Result;
		}
		return Result;
	}

	void BuildFunctionClauseItems(
		const Verse::Vst::Node& Body,
		const FSourceIndex& SourceIndex,
		FVerseSourceRegion& OutRegion)
	{
		TArray<const Verse::Vst::Node*> RootExpressions;
		if (const Verse::Vst::Clause* Clause = Body.AsNullable<Verse::Vst::Clause>())
		{
			for (const Verse::Vst::TNodeRef<Verse::Vst::Node>& Child : Clause->GetChildren())
			{
				const Verse::Vst::Node* Candidate = UnwrapSingleClause(Child.Get());
				if (Candidate != nullptr && !Candidate->IsA<Verse::Vst::Comment>())
				{
					RootExpressions.Add(Child.Get());
				}
			}
		}
		else if (!Body.IsA<Verse::Vst::Comment>())
		{
			RootExpressions.Add(&Body);
		}

		RootExpressions.Sort([&SourceIndex](const Verse::Vst::Node& Left, const Verse::Vst::Node& Right)
		{
			return SourceIndex.ToRange(Left.Whence()).BeginByte
				< SourceIndex.ToRange(Right.Whence()).BeginByte;
		});

		for (const Verse::Vst::Node* Expression : RootExpressions)
		{
			const FVerseByteRange ExpressionRange = SourceIndex.ToRange(Expression->Whence());
			if (!ExpressionRange.IsSet()
				|| ExpressionRange.NumBytes <= 0
				|| ExpressionRange.BeginByte < OutRegion.BodyRange.BeginByte
				|| ExpressionRange.EndByte() > OutRegion.BodyRange.EndByte())
			{
				continue;
			}

			FVerseClauseItemDescriptor& Item = OutRegion.BodyClause.Items.AddDefaulted_GetRef();
			Item.Expression = BuildWrappedExpressionDescriptor(
				*Expression,
				SourceIndex,
				OutRegion.FunctionParameters);
			CollectItemCommentDescriptors(*Expression, SourceIndex, Item.Comments);
		}

		TArray<FVerseClauseItemDescriptor>& Items = OutRegion.BodyClause.Items;
		for (int32 Index = 0; Index < Items.Num(); ++Index)
		{
			FVerseClauseItemDescriptor& Item = Items[Index];
			if (Index == 0 && OutRegion.BodyRange.BeginByte < Item.Expression.Range.BeginByte)
			{
				Item.LeadingTriviaRange = FVerseByteRange::FromBounds(
					OutRegion.BodyRange.BeginByte,
					Item.Expression.Range.BeginByte);
			}
			const int32 TrailingEnd = Index + 1 < Items.Num()
				? Items[Index + 1].Expression.Range.BeginByte
				: OutRegion.BodyRange.EndByte();
			if (Item.Expression.Range.EndByte() < TrailingEnd)
			{
				Item.TrailingTriviaRange = FVerseByteRange::FromBounds(
					Item.Expression.Range.EndByte(),
					TrailingEnd);
			}
			const FUtf8StringView Trivia = Item.TrailingTriviaRange.IsSet()
				? SourceIndex.GetSource().Mid(
					Item.TrailingTriviaRange.BeginByte,
					Item.TrailingTriviaRange.NumBytes)
				: FUtf8StringView();
			Item.bIsFinalValuePosition = Index == Items.Num() - 1;
			Item.Separator = ClassifySeparator(
				SourceIndex.GetSource(),
				Item.TrailingTriviaRange,
				Item.bIsFinalValuePosition);
			Item.ExtraBlankLineCount = FMath::Max(0, CountLineBreaks(Trivia) - 1);

		}
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

	void CollectAppendSpecifierRanges(
		const Verse::Vst::Node& Node,
		const FSourceIndex& SourceIndex,
		TArray<FVerseByteRange>& OutRanges)
	{
		if (const Verse::Vst::Clause* Clause = Node.AsNullable<Verse::Vst::Clause>();
			Clause != nullptr
			&& Clause->GetForm() == Verse::Vst::Clause::EForm::IsAppendAttributeHolder)
		{
			const FVerseByteRange Range = SourceIndex.ToRange(Clause->Whence());
			if (Range.IsSet() && Range.NumBytes > 0)
			{
				OutRanges.AddUnique(Range);
			}
			return;
		}

		if (Node.GetAux())
		{
			CollectAppendSpecifierRanges(*Node.GetAux(), SourceIndex, OutRanges);
		}
		for (const Verse::Vst::TNodeRef<Verse::Vst::Node>& Child : Node.GetChildren())
		{
			CollectAppendSpecifierRanges(*Child, SourceIndex, OutRanges);
		}
	}

	void CollectCommentRegions(
		const Verse::Vst::Node& Node,
		const FSourceIndex& SourceIndex,
		TSet<const Verse::Vst::Node*>& VisitedNodes,
		TArray<FVerseSourceRegion>& OutRegions,
		EVerseCommentAttachment DirectAttachment = EVerseCommentAttachment::Unattached,
		FVerseByteRange DirectOwnerRange = {})
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
				Region.CommentAttachment = DirectAttachment;
				Region.CommentOwnerRange = DirectAttachment == EVerseCommentAttachment::Unattached
					? FVerseByteRange()
					: DirectOwnerRange;
			}
			return;
		}

		const FVerseByteRange OwnerRange = SourceIndex.ToRange(Node.Whence());

		for (const Verse::Vst::TNodeRef<Verse::Vst::Node>& Comment : Node.GetPrefixComments())
		{
			CollectCommentRegions(
				*Comment,
				SourceIndex,
				VisitedNodes,
				OutRegions,
				EVerseCommentAttachment::Prefix,
				OwnerRange);
		}
		for (const Verse::Vst::TNodeRef<Verse::Vst::Node>& Comment : Node.GetPostfixComments())
		{
			CollectCommentRegions(
				*Comment,
				SourceIndex,
				VisitedNodes,
				OutRegions,
				EVerseCommentAttachment::Postfix,
				OwnerRange);
		}
		if (Node.GetAux())
		{
			CollectCommentRegions(
				*Node.GetAux(),
				SourceIndex,
				VisitedNodes,
				OutRegions,
				EVerseCommentAttachment::Inline,
				OwnerRange);
		}
		for (const Verse::Vst::TNodeRef<Verse::Vst::Node>& Child : Node.GetChildren())
		{
			CollectCommentRegions(
				*Child,
				SourceIndex,
				VisitedNodes,
				OutRegions,
				EVerseCommentAttachment::Unattached,
				{});
		}
	}

	TArray<FVerseSourceRegion> BuildClauseChildren(
		const Verse::Vst::Clause& Clause,
		const FVerseClauseDescriptor& Descriptor,
		const FSourceIndex& SourceIndex)
	{
		TArray<FVerseSourceRegion> RecognizedChildren;
		for (const Verse::Vst::TNodeRef<Verse::Vst::Node>& Child : Clause.GetChildren())
		{
			const Verse::Vst::Node* Candidate = UnwrapSingleClause(Child.Get());
			FVerseSourceRegion ChildRegion;
			if (Candidate != nullptr && TryMakeTypedRegion(*Candidate, SourceIndex, ChildRegion))
			{
				RecognizedChildren.Add(MoveTemp(ChildRegion));
			}
		}

		TSet<const Verse::Vst::Node*> VisitedComments;
		CollectCommentRegions(Clause, SourceIndex, VisitedComments, RecognizedChildren);
		return PartitionRange(Descriptor.InteriorRange, MoveTemp(RecognizedChildren));
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
		CollectAppendSpecifierRanges(*NameOperand, SourceIndex, OutRegion.SpecifierRanges);
		OutRegion.SpecifierRanges.Sort([](const FVerseByteRange& Left, const FVerseByteRange& Right)
		{
			return Left.BeginByte < Right.BeginByte;
		});
		const Verse::Vst::Clause* FunctionBodyClause =
			SyntaxKind == VerseSyntaxKind::Function
				? FindPunctuatedClauseThroughSingleChildWrappers(&RightOperand)
				: nullptr;
		const Verse::Vst::Node* UnwrappedRight = UnwrapSingleClause(&RightOperand);
		if (FunctionBodyClause != nullptr)
		{
			OutRegion.BodyClause = MakeClauseDescriptor(
				*FunctionBodyClause,
				DefinitionRange,
				SourceIndex);
			OutRegion.BodyRange = OutRegion.BodyClause.InteriorRange;
		}
		else if (const Verse::Vst::Macro* Macro = UnwrappedRight != nullptr
			? UnwrappedRight->AsNullable<Verse::Vst::Macro>()
			: nullptr;
			Macro != nullptr
			&& Macro->GetChildCount() > 1
			&& !ClassifyMacro(*Macro).IsNone())
		{
			const Verse::Vst::Clause& BodyClause = *Macro->GetClause(Macro->GetChildCount() - 2);
			OutRegion.BodyClause = MakeClauseDescriptor(BodyClause, DefinitionRange, SourceIndex);
			OutRegion.BodyRange = OutRegion.BodyClause.InteriorRange;
			OutRegion.Children = BuildClauseChildren(BodyClause, OutRegion.BodyClause, SourceIndex);
		}
		else if (const Verse::Vst::Clause* BodyClause = UnwrappedRight != nullptr
			? UnwrappedRight->AsNullable<Verse::Vst::Clause>()
			: nullptr)
		{
			OutRegion.BodyClause = MakeClauseDescriptor(
				*BodyClause,
				DefinitionRange,
				SourceIndex);
			OutRegion.BodyRange = OutRegion.BodyClause.InteriorRange;
		}
		else if (UnwrappedRight != nullptr)
		{
			OutRegion.BodyClause = MakeExpressionDescriptor(*UnwrappedRight, SourceIndex);
			OutRegion.BodyRange = OutRegion.BodyClause.InteriorRange;
		}
		if (SyntaxKind == VerseSyntaxKind::Function && OutRegion.BodyRange.IsSet())
		{
			PopulateFunctionMetadata(*NameOperand, RightOperand, SourceIndex, OutRegion);
			const Verse::Vst::Node* FunctionItemsRoot = FunctionBodyClause != nullptr
				? static_cast<const Verse::Vst::Node*>(FunctionBodyClause)
				: UnwrappedRight;
			if (FunctionItemsRoot != nullptr)
			{
				BuildFunctionClauseItems(*FunctionItemsRoot, SourceIndex, OutRegion);
			}
			FVerseSourceRegion& RawBody = OutRegion.Children.AddDefaulted_GetRef();
			RawBody.Range = OutRegion.BodyRange;
		}
		if (OutRegion.BodyClause.OpeningPunctuationRange.IsSet())
		{
			OutRegion.HeaderRange = FVerseByteRange::FromBounds(
				DefinitionRange.BeginByte,
				OutRegion.BodyClause.OpeningPunctuationRange.BeginByte);
		}
		else if (OutRegion.BodyRange.IsSet())
		{
			OutRegion.HeaderRange = FVerseByteRange::FromBounds(
				DefinitionRange.BeginByte,
				OutRegion.BodyRange.BeginByte);
		}
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

	TArray<FVerseSourceRegion> CompleteRegions = VerseParseSnapshotBuilder::PartitionRange(
		Document->GetWholeOriginalRange(),
		MoveTemp(RecognizedRegions));

	return CompleteRegions.IsEmpty()
		? FVerseParseSnapshot::CreateRaw(MoveTemp(Document))
		: FVerseParseSnapshot::CreateRecognized(MoveTemp(Document), MoveTemp(CompleteRegions));
}
