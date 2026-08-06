#include "Slate/VerseVisualEditorStyle.h"

#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "GraphEditorSettings.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "VerseParseSnapshotBuilder.h"
#include "VisualModel/VerseVisualTile.h"

namespace VerseVisualEditorStyle
{
	namespace
	{
		TSharedPtr<FSlateStyleSet> Style;

		constexpr float OuterRadius = 7.0f;
		constexpr float InnerRadius = 6.0f;

		FLinearColor DarkenTitleColor(
			FLinearColor BlueprintColor,
			float Brightness,
			float Opacity)
		{
			BlueprintColor.R *= Brightness;
			BlueprintColor.G *= Brightness;
			BlueprintColor.B *= Brightness;
			BlueprintColor.A = Opacity;
			return BlueprintColor;
		}
	}

	void Initialize()
	{
		if (Style.IsValid())
		{
			return;
		}

		Style = MakeShared<FSlateStyleSet>(TEXT("VerseVisualEditorStyle"));
		Style->Set(TEXT("Tile.Shadow"), new FSlateRoundedBoxBrush(
			FLinearColor::White, OuterRadius));
		Style->Set(TEXT("Tile.Outline"), new FSlateRoundedBoxBrush(
			FLinearColor::White, OuterRadius));
		Style->Set(TEXT("Tile.Surface"), new FSlateRoundedBoxBrush(
			FLinearColor(0.030f, 0.034f, 0.041f, 0.96f), InnerRadius));
		Style->Set(TEXT("Tile.Identity"), new FSlateRoundedBoxBrush(
			FLinearColor::White,
			FVector4(InnerRadius, InnerRadius, 0.0f, 0.0f)));
		Style->Set(TEXT("Tile.SourcePreview"), new FSlateColorBrush(
			FLinearColor(0.018f, 0.021f, 0.027f, 0.82f)));
		Style->Set(TEXT("Tile.Diagnostic"), new FSlateColorBrush(
			FLinearColor(0.16f, 0.025f, 0.018f, 0.72f)));
		Style->Set(TEXT("Tile.BodyOverlay"), new FSlateRoundedBoxBrush(
			FLinearColor::White,
			FVector4(0.0f, 0.0f, InnerRadius, InnerRadius)));
		Style->Set(TEXT("Tile.Separator"), new FSlateColorBrush(
			FLinearColor(0.0f, 0.0f, 0.0f, 0.32f)));
		Style->Set(TEXT("Color.PrimaryText"),
			FLinearColor(0.96f, 0.97f, 0.985f, 1.0f));
		Style->Set(TEXT("Color.SecondaryText"),
			FLinearColor(0.76f, 0.80f, 0.86f, 1.0f));
		Style->Set(TEXT("Color.MetadataText"),
			FLinearColor(0.58f, 0.63f, 0.70f, 1.0f));
		Style->Set(TEXT("Color.NeutralTitle"),
			FLinearColor(0.115f, 0.135f, 0.165f, 0.94f));
		Style->Set(TEXT("Color.IdentifierTitle"),
			FLinearColor(0.020f, 0.025f, 0.035f, 0.96f));
		Style->Set(TEXT("Color.FailureTitle"),
			FLinearColor(0.18f, 0.155f, 0.055f, 0.88f));
		Style->Set(TEXT("Color.FunctionBlue"),
			FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("3d637d")))
			.CopyWithNewOpacity(0.90f));
		Style->Set(TEXT("Color.HeaderGlossTop"),
			FLinearColor(1.0f, 1.0f, 1.0f, 0.095f));
		Style->Set(TEXT("Color.HeaderGlossMiddle"),
			FLinearColor(1.0f, 1.0f, 1.0f, 0.012f));
		Style->Set(TEXT("Color.HeaderGlossBottom"),
			FLinearColor(0.0f, 0.0f, 0.0f, 0.115f));
		Style->Set(TEXT("Color.BodyGradientTop"),
			FLinearColor(1.0f, 1.0f, 1.0f, 0.045f));
		Style->Set(TEXT("Color.BodyGradientMiddle"),
			FLinearColor(1.0f, 1.0f, 1.0f, 0.006f));
		Style->Set(TEXT("Color.BodyGradientBottom"),
			FLinearColor(0.0f, 0.0f, 0.0f, 0.14f));
		Style->Set(TEXT("Color.Shadow"),
			FLinearColor(0.0f, 0.0f, 0.0f, 0.34f));
		Style->Set(TEXT("Color.Selection"),
			FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("3da5ff"))));
		Style->Set(TEXT("Color.SelectedShadow"),
			FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("3da5ff")))
			.CopyWithNewOpacity(0.25f));
		Style->Set(TEXT("Color.FailureGlass"),
			FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("2e2a14")))
			.CopyWithNewOpacity(0.72f));
	Style->Set(TEXT("Color.FailurePattern"),
		FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("4d451b")))
		.CopyWithNewOpacity(0.34f));
	Style->Set(TEXT("Color.SynchronizationGlass"),
		FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("18283d")))
		.CopyWithNewOpacity(0.76f));
	Style->Set(TEXT("Color.SynchronizationThread"),
		FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("7894bd")))
		.CopyWithNewOpacity(0.18f));
		Style->Set(TEXT("Metric.TileCornerRadius"), InnerRadius);

		FSlateStyleRegistry::RegisterSlateStyle(*Style);
	}

	void Shutdown()
	{
		if (!Style.IsValid())
		{
			return;
		}
		FSlateStyleRegistry::UnRegisterSlateStyle(*Style);
		ensure(Style.IsUnique());
		Style.Reset();
	}

	bool IsInitialized()
	{
		return Style.IsValid();
	}

	const ISlateStyle& Get()
	{
		check(Style.IsValid());
		return *Style;
	}

	FLinearColor GetTypeColor(const FString& VerseType)
	{
		const UGraphEditorSettings* Settings = GetDefault<UGraphEditorSettings>();
		FString Type = VerseType.TrimStartAndEnd().ToLower();
		while (Type.RemoveFromStart(TEXT("?")) || Type.RemoveFromStart(TEXT("[]")))
		{
		}
		if (Type == TEXT("logic")) return Settings->BooleanPinTypeColor;
		if (Type == TEXT("int")) return Settings->IntPinTypeColor;
		if (Type == TEXT("float")) return Settings->FloatPinTypeColor;
		if (Type == TEXT("string")) return Settings->StringPinTypeColor;
		if (Type == TEXT("message")) return Settings->TextPinTypeColor;
		if (Type == TEXT("char")) return Settings->BytePinTypeColor;
		if (Type == TEXT("type")) return Settings->ClassPinTypeColor;
		if (Type == TEXT("void")) return Settings->DefaultPinTypeColor;
		return Settings->ObjectPinTypeColor;
	}

	FLinearColor GetTileTitleColor(const FVerseVisualTile& Tile)
	{
		const UGraphEditorSettings* Settings = GetDefault<UGraphEditorSettings>();
		if (Tile.Kind == EVerseVisualTileKind::FailableBlock)
		{
			return Get().GetColor(TEXT("Color.FailureTitle"));
		}
		if (Tile.Kind == EVerseVisualTileKind::FunctionEntry
			|| Tile.Kind == EVerseVisualTileKind::FunctionReturn)
		{
			return DarkenTitleColor(
				Settings->FunctionTerminatorNodeTitleColor, 0.58f, 0.92f);
		}
		if (Tile.Kind == EVerseVisualTileKind::Definition
			&& Tile.DefinitionKind == VerseSyntaxKind::Function)
		{
			return Get().GetColor(TEXT("Color.FunctionBlue"));
		}
		if (Tile.Kind == EVerseVisualTileKind::Comment)
		{
			return Settings->DefaultCommentNodeTitleColor.CopyWithNewOpacity(0.88f);
		}
		if (Tile.Kind == EVerseVisualTileKind::Unknown)
		{
			return Get().GetColor(TEXT("Color.NeutralTitle"));
		}
		if (Tile.Kind == EVerseVisualTileKind::Expression)
		{
			if (Tile.ExpressionKind == EVerseExpressionKind::Call)
			{
				return Get().GetColor(TEXT("Color.FunctionBlue"));
			}
			if (Tile.ExpressionKind == EVerseExpressionKind::Control)
			{
				if (Tile.ControlKind == EVerseControlKind::Sync)
				{
					return FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("334f78")));
				}
				return Get().GetColor(TEXT("Color.NeutralTitle"));
			}
			if (Tile.ExpressionKind == EVerseExpressionKind::Identifier)
			{
				return Get().GetColor(TEXT("Color.IdentifierTitle"));
			}
		}
		return Get().GetColor(TEXT("Color.NeutralTitle"));
	}

	FLinearColor GetPrimaryTextColor()
	{
		return Get().GetColor(TEXT("Color.PrimaryText"));
	}

	FLinearColor GetSecondaryTextColor()
	{
		return Get().GetColor(TEXT("Color.SecondaryText"));
	}

	FLinearColor GetMetadataTextColor()
	{
		return Get().GetColor(TEXT("Color.MetadataText"));
	}
}
