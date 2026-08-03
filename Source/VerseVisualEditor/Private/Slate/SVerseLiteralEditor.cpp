#include "Slate/SVerseLiteralEditor.h"

#include "Containers/StringConv.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "VerseLiteralEditor"

namespace
{
	FString DecodeQuotedContent(const FString& Source, TCHAR Quote)
	{
		const FString Content = Source.Len() >= 2
			&& Source[0] == Quote
			&& Source[Source.Len() - 1] == Quote
			? Source.Mid(1, Source.Len() - 2)
			: Source;
		FString Decoded;
		for (int32 Index = 0; Index < Content.Len(); ++Index)
		{
			if (Content[Index] != TEXT('\\') || Index + 1 >= Content.Len())
			{
				Decoded += Content[Index];
				continue;
			}
			const TCHAR Escaped = Content[++Index];
			switch (Escaped)
			{
			case TEXT('n'): Decoded += TEXT('\n'); break;
			case TEXT('r'): Decoded += TEXT('\r'); break;
			case TEXT('t'): Decoded += TEXT('\t'); break;
			case TEXT('\\'): Decoded += TEXT('\\'); break;
			default: Decoded += Escaped; break;
			}
		}
		return Decoded;
	}

	FString EscapeQuotedContent(const FString& Content, TCHAR Quote)
	{
		FString Escaped;
		Escaped.Reserve(Content.Len() + 2);
		for (const TCHAR Character : Content)
		{
			switch (Character)
			{
			case TEXT('\\'): Escaped += TEXT("\\\\"); break;
			case TEXT('\n'): Escaped += TEXT("\\n"); break;
			case TEXT('\r'): Escaped += TEXT("\\r"); break;
			case TEXT('\t'): Escaped += TEXT("\\t"); break;
			default:
				if (Character == Quote)
				{
					Escaped += TEXT('\\');
				}
				Escaped += Character;
				break;
			}
		}
		return FString::Chr(Quote) + Escaped + FString::Chr(Quote);
	}

	bool IsOneCharacter(const FString& Text)
	{
		return Text.Len() == 1
			|| (Text.Len() == 2
				&& StringConv::IsHighSurrogate(Text[0])
				&& StringConv::IsLowSurrogate(Text[1]));
	}
}

void SVerseLiteralEditor::Construct(const FArguments& InArgs)
{
	LiteralKind = InArgs._LiteralKind;
	LiteralRange = InArgs._LiteralRange;
	SourceText = InArgs._SourceText.TrimStartAndEnd();
	OnSourceCommitted = InArgs._OnSourceCommitted;

	TSharedRef<SWidget> Editor = SNullWidget::NullWidget;
	switch (LiteralKind)
	{
	case EVerseLiteralKind::Integer:
	{
		int64 Value = 0;
		LexTryParseString(Value, *SourceText);
		Editor = SNew(SSpinBox<int64>)
			.MinDesiredWidth(38.0f)
			.Value(Value)
			.OnValueCommitted_Lambda([this](int64 NewValue, ETextCommit::Type)
			{
				CommitSource(LexToString(NewValue));
			});
		break;
	}
	case EVerseLiteralKind::Float:
		Editor = BuildFloatEditor();
		break;
	case EVerseLiteralKind::String:
		Editor = SNew(SEditableTextBox)
			.MinDesiredWidth(56.0f)
			.Text(FText::FromString(DecodeQuotedContent(SourceText, TEXT('"'))))
			.OnTextCommitted_Lambda([this](const FText& NewText, ETextCommit::Type)
			{
				CommitSource(EscapeQuotedContent(NewText.ToString(), TEXT('"')));
			});
		break;
	case EVerseLiteralKind::Character:
		Editor = SNew(SEditableTextBox)
			.MinDesiredWidth(30.0f)
			.Text(FText::FromString(DecodeQuotedContent(SourceText, TEXT('\''))))
			.OnVerifyTextChanged_Lambda([](const FText& NewText, FText& OutError)
			{
				if (NewText.IsEmpty() || IsOneCharacter(NewText.ToString()))
				{
					return true;
				}
				OutError = LOCTEXT("OneCharacterRequired", "Enter exactly one character.");
				return false;
			})
			.OnTextCommitted_Lambda([this](const FText& NewText, ETextCommit::Type)
			{
				if (IsOneCharacter(NewText.ToString()))
				{
					CommitSource(EscapeQuotedContent(NewText.ToString(), TEXT('\'')));
				}
			});
		break;
	case EVerseLiteralKind::Logic:
	{
		const bool bValue = SourceText.Equals(TEXT("true"), ESearchCase::IgnoreCase);
		Editor = SNew(SCheckBox)
			.IsChecked(bValue ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
			.OnCheckStateChanged_Lambda([this](ECheckBoxState State)
			{
				CommitSource(State == ECheckBoxState::Checked ? TEXT("true") : TEXT("false"));
			})
			[
				SNew(STextBlock)
				.Text(bValue ? LOCTEXT("LogicTrue", "True") : LOCTEXT("LogicFalse", "False"))
			];
		break;
	}
	default:
		Editor = SNew(STextBlock).Text(FText::FromString(SourceText));
		break;
	}

	ChildSlot
	[
		Editor
	];
}

TSharedRef<SWidget> SVerseLiteralEditor::BuildFloatEditor()
{
	double Value = 0.0;
	LexTryParseString(Value, *SourceText);
	return SNew(SSpinBox<double>)
		.MinDesiredWidth(42.0f)
		.Value(Value)
		.OnValueCommitted_Lambda([this](double NewValue, ETextCommit::Type)
		{
			CommitSource(FString::SanitizeFloat(NewValue));
		});
}

void SVerseLiteralEditor::CommitSource(FString Source)
{
	OnSourceCommitted.ExecuteIfBound(LiteralRange, FText::FromString(MoveTemp(Source)));
}

#undef LOCTEXT_NAMESPACE
