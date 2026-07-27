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

	bool TryMakeTypedRegion(
		const Verse::Vst::Node& Node,
		const FSourceIndex& SourceIndex,
		FVerseSourceRegion& OutRegion);

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
				Descriptor.PunctuationStyle = EVerseClausePunctuationStyle::Braces;
				Descriptor.OpeningPunctuationRange = {OpenByte, 1};
				Descriptor.ClosingPunctuationRange = {CloseByte, 1};
				Descriptor.InteriorRange = FVerseByteRange::FromBounds(OpenByte + 1, CloseByte);
				Descriptor.EmptyBodyInsertionByte = OpenByte + 1;
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
				Descriptor.PunctuationStyle = EVerseClausePunctuationStyle::ColonOrIndentation;
				Descriptor.OpeningPunctuationRange = {ColonByte, 1};
				Descriptor.InteriorRange = FVerseByteRange::FromBounds(ColonByte + 1, DefinitionRange.EndByte());
				Descriptor.EmptyBodyInsertionByte = ColonByte + 1;
				return Descriptor;
			}
		}
		else if (Punctuation == Verse::Vst::Clause::EPunctuation::Indentation)
		{
			Descriptor.PunctuationStyle = EVerseClausePunctuationStyle::ColonOrIndentation;
		}

		const FVerseByteRange ClauseRange = SourceIndex.ToRange(Clause.Whence());
		Descriptor.InteriorRange = ClauseRange.IsSet()
			? ClauseRange
			: FVerseByteRange::FromBounds(DefinitionRange.EndByte(), DefinitionRange.EndByte());
		Descriptor.EmptyBodyInsertionByte = Descriptor.InteriorRange.BeginByte;
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
		const Verse::Vst::Node* UnwrappedRight = UnwrapSingleClause(&RightOperand);
		if (const Verse::Vst::Macro* Macro = UnwrappedRight != nullptr
			? UnwrappedRight->AsNullable<Verse::Vst::Macro>()
			: nullptr;
			Macro != nullptr && Macro->GetChildCount() > 1)
		{
			const Verse::Vst::Clause& BodyClause = *Macro->GetClause(Macro->GetChildCount() - 2);
			OutRegion.BodyClause = MakeClauseDescriptor(BodyClause, DefinitionRange, SourceIndex);
			OutRegion.BodyRange = OutRegion.BodyClause.InteriorRange;
			OutRegion.Children = BuildClauseChildren(BodyClause, OutRegion.BodyClause, SourceIndex);
		}
		else if (UnwrappedRight != nullptr)
		{
			OutRegion.BodyClause = MakeExpressionDescriptor(*UnwrappedRight, SourceIndex);
			OutRegion.BodyRange = OutRegion.BodyClause.InteriorRange;
		}
		if (SyntaxKind == VerseSyntaxKind::Function && OutRegion.BodyRange.IsSet())
		{
			PopulateFunctionMetadata(*NameOperand, RightOperand, SourceIndex, OutRegion);
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
