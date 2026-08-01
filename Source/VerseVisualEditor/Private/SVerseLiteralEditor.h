#pragma once

#include "VerseVisualTile.h"
#include "Widgets/SCompoundWidget.h"

DECLARE_DELEGATE_TwoParams(FOnVerseLiteralSourceCommitted, FVerseTextRange, FText);

/** Type-appropriate, syntax-safe editor for one source-exact Verse literal. */
class SVerseLiteralEditor final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SVerseLiteralEditor) {}
		SLATE_ARGUMENT(EVerseLiteralKind, LiteralKind)
		SLATE_ARGUMENT(FVerseTextRange, LiteralRange)
		SLATE_ARGUMENT(FString, SourceText)
		SLATE_EVENT(FOnVerseLiteralSourceCommitted, OnSourceCommitted)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	TSharedRef<SWidget> BuildFloatEditor();
	void CommitSource(FString Source);

	EVerseLiteralKind LiteralKind = EVerseLiteralKind::None;
	FVerseTextRange LiteralRange;
	FString SourceText;
	FOnVerseLiteralSourceCommitted OnSourceCommitted;
};
