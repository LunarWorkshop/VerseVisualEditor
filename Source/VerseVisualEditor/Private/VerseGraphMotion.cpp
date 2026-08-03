#include "VerseGraphMotion.h"

#include "VerseVisualEditorSettings.h"
#include "VerseDocument.h"
#include "VerseVisualTile.h"

#include "HAL/PlatformTime.h"

FVector2D ApplyVerseGraphDragResistance(FVector2D ScreenDisplacement)
{
	const float Radius = ScreenDisplacement.Size();
	if (Radius <= 100.0f || Radius <= UE_SMALL_NUMBER)
	{
		return ScreenDisplacement;
	}
	const float DisplayedRadius = 100.0f
		+ 200.0f * (1.0f - FMath::Exp(-(Radius - 100.0f) / 230.0f));
	return ScreenDisplacement * (DisplayedRadius / Radius);
}

float EvaluateVerseGraphEaseOut(float Alpha)
{
	const float Remaining = 1.0f - FMath::Clamp(Alpha, 0.0f, 1.0f);
	return 1.0f - Remaining * Remaining * Remaining;
}

FString BuildVerseGraphMotionKeyBase(
	const FVerseVisualTile& Tile,
	const FVerseDocument& Document)
{
	FString Identity;
	if (Tile.NameRange.IsSet())
	{
		Identity = Document.DecodeOriginalRange(Tile.NameRange);
	}
	else if (!Tile.OperatorSpelling.IsEmpty())
	{
		Identity = Tile.OperatorSpelling;
	}
	else if (Tile.Range.IsSet())
	{
		Identity = Document.DecodeOriginalRange(Tile.Range).TrimStartAndEnd();
	}
	return FString::Printf(
		TEXT("%d|%d|%s|%s"),
		static_cast<int32>(Tile.Kind),
		static_cast<int32>(Tile.ExpressionKind),
		*Tile.DefinitionKind.ToString(),
		*Identity);
}

FVector2D ComputeVerseAnchorRelativeGraphPosition(
	FVector2D SurfaceLocalPosition,
	FVector2D AnchorSurfaceLocalPosition)
{
	return SurfaceLocalPosition - AnchorSurfaceLocalPosition;
}

void FVerseGraphMotionController::BeginBuild(bool bAnimateChanges)
{
	PreviousPoses = Poses;
	Poses.Reset();
	KeyOccurrences.Reset();
	bAnimateCurrentBuild = bAnimateChanges;
}

FString FVerseGraphMotionController::AllocateKey(const FString& BaseKey)
{
	int32& Occurrence = KeyOccurrences.FindOrAdd(BaseKey);
	return FString::Printf(TEXT("%s#%d"), *BaseKey, Occurrence++);
}

void FVerseGraphMotionController::SetSurfaceGeometry(
	const FGeometry& InGeometry,
	float InZoom,
	bool bInRequiresCurrentAnchor)
{
	SurfaceGeometry = InGeometry;
	Zoom = FMath::Max(InZoom, UE_SMALL_NUMBER);
	bHasSurfaceGeometry = true;
	bRequiresCurrentAnchor = bInRequiresCurrentAnchor;
	bHasCurrentGraphOrigin = !bRequiresCurrentAnchor;
	if (!bRequiresCurrentAnchor)
	{
		GraphOrigin = FVector2D::ZeroVector;
	}
}

void FVerseGraphMotionController::EstablishCurrentAnchor(
	FVector2D AnchorDesktopPosition)
{
	if (!bHasSurfaceGeometry || !bRequiresCurrentAnchor)
	{
		return;
	}
	GraphOrigin = SurfaceGeometry.AbsoluteToLocal(AnchorDesktopPosition);
	bHasCurrentGraphOrigin = true;
}

void FVerseGraphMotionController::InvalidateSurfaceGeometry()
{
	bHasSurfaceGeometry = false;
	bHasCurrentGraphOrigin = false;
}

TOptional<FVerseGraphMotionPose> FVerseGraphMotionController::FindPreviousPose(
	const FString& Key) const
{
	if (const FVerseGraphMotionPose* Pose = PreviousPoses.Find(Key))
	{
		return *Pose;
	}
	return {};
}

TOptional<FVerseGraphMotionPose> FVerseGraphMotionController::FindCurrentPose(
	const FString& Key) const
{
	if (const FVerseGraphMotionPose* Pose = Poses.Find(Key))
	{
		return *Pose;
	}
	return {};
}

void FVerseGraphMotionController::PublishPose(
	const FString& Key,
	const FVerseGraphMotionPose& Pose)
{
	Poses.Add(Key, Pose);
}

FVector2D FVerseGraphMotionController::DesktopToGraph(FVector2D DesktopPosition) const
{
	return bHasSurfaceGeometry
		? ComputeVerseAnchorRelativeGraphPosition(
			SurfaceGeometry.AbsoluteToLocal(DesktopPosition), GraphOrigin)
		: FVector2D::ZeroVector;
}

void SVerseGraphMotionWidget::Construct(const FArguments& InArgs)
{
	Controller = InArgs._Controller;
	MotionKey = InArgs._MotionKey;
	ParentMotionKey = InArgs._ParentMotionKey;
	Entrance = InArgs._Entrance;
	bIsGraphAnchor = InArgs._IsGraphAnchor;
	SetCanTick(true);
	ChildSlot[InArgs._Content.Widget];
}

void SVerseGraphMotionWidget::BeginElasticDrag(FVector2D DesktopPosition)
{
	DragStartDesktop = DesktopPosition;
	bDragging = true;
	bAnimatingReturn = false;
}

void SVerseGraphMotionWidget::UpdateElasticDrag(FVector2D DesktopPosition)
{
	if (!bDragging || !Controller.IsValid())
	{
		return;
	}
	const FVector2D ResistantScreenOffset =
		ApplyVerseGraphDragResistance(DesktopPosition - DragStartDesktop);
	DragOffset = ResistantScreenOffset / FMath::Max(Controller->GetZoom(), UE_SMALL_NUMBER);
	if (bInitialized)
	{
		Controller->PublishPose(MotionKey, {
			LastTargetGraphPosition,
			LastTargetGraphPosition + ReflowOffset + DragOffset,
			CurrentOpacity});
	}
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SVerseGraphMotionWidget::EndElasticDrag()
{
	if (!bDragging)
	{
		return;
	}
	bDragging = false;
	StartReturnAnimation(FPlatformTime::Seconds());
}

float SVerseGraphMotionWidget::GetDuration() const
{
	return FMath::Max(
		0.0f,
		GetDefault<UVerseVisualEditorSettings>()->GraphMotionDurationSeconds);
}

void SVerseGraphMotionWidget::StartReturnAnimation(double CurrentTime)
{
	ReturnStartOffset = DragOffset;
	ReturnStartTime = CurrentTime;
	bAnimatingReturn = !ReturnStartOffset.IsNearlyZero() && GetDuration() > 0.0f;
	if (!bAnimatingReturn)
	{
		DragOffset = FVector2D::ZeroVector;
	}
}

void SVerseGraphMotionWidget::Tick(
	const FGeometry& AllottedGeometry,
	double InCurrentTime,
	float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	if (!Controller.IsValid())
	{
		return;
	}
	if (bIsGraphAnchor)
	{
		// The surface ticks before its descendants, so cached anchor geometry belongs
		// to the preceding layout epoch. The anchor's allotted geometry is current.
		Controller->EstablishCurrentAnchor(AllottedGeometry.GetAbsolutePosition());
	}
	if (!Controller->CanResolveGraphPositions())
	{
		return;
	}

	const FVector2D Target = Controller->DesktopToGraph(
		AllottedGeometry.GetAbsolutePosition());
	LastTargetGraphPosition = Target;
	const float Duration = GetDuration();
	if (!bInitialized)
	{
		bInitialized = true;
		if (Controller->ShouldAnimateBuild() && Duration > 0.0f)
		{
			if (const TOptional<FVerseGraphMotionPose> Previous =
				Controller->FindPreviousPose(MotionKey))
			{
				ReflowStartOffset = Previous->DisplayedGraphPosition - Target;
				ReflowStartOpacity = Previous->Opacity;
			}
			else
			{
				const bool bParentIsAlsoInserted = !ParentMotionKey.IsEmpty()
					&& !Controller->FindPreviousPose(ParentMotionKey).IsSet();
				if (!bParentIsAlsoInserted)
				{
					const float EntranceDistance = 24.0f
						/ FMath::Max(Controller->GetZoom(), UE_SMALL_NUMBER);
					ReflowStartOffset = Entrance == EVerseGraphMotionEntrance::FromRight
						? FVector2D(EntranceDistance, 0.0f)
						: FVector2D(0.0f, -EntranceDistance);
					ReflowStartOpacity = 0.0f;
				}
			}
			if (!ParentMotionKey.IsEmpty())
			{
				if (const TOptional<FVerseGraphMotionPose> Parent =
					Controller->FindCurrentPose(ParentMotionKey))
				{
					ReflowStartOffset -= Parent->DisplayedGraphPosition
						- Parent->TargetGraphPosition;
				}
			}
			bAnimatingReflow = !ReflowStartOffset.IsNearlyZero()
				|| ReflowStartOpacity < 1.0f;
			ReflowStartTime = InCurrentTime;
		}
	}

	float Opacity = 1.0f;
	if (bAnimatingReflow)
	{
		const float Alpha = Duration > 0.0f
			? static_cast<float>((InCurrentTime - ReflowStartTime) / Duration)
			: 1.0f;
		const float Eased = EvaluateVerseGraphEaseOut(Alpha);
		ReflowOffset = FMath::Lerp(ReflowStartOffset, FVector2D::ZeroVector, Eased);
		Opacity = FMath::Lerp(ReflowStartOpacity, 1.0f, Eased);
		if (Alpha >= 1.0f)
		{
			bAnimatingReflow = false;
			ReflowOffset = FVector2D::ZeroVector;
			Opacity = 1.0f;
		}
	}
	if (bAnimatingReturn)
	{
		const float Alpha = Duration > 0.0f
			? static_cast<float>((InCurrentTime - ReturnStartTime) / Duration)
			: 1.0f;
		DragOffset = FMath::Lerp(
			ReturnStartOffset,
			FVector2D::ZeroVector,
			EvaluateVerseGraphEaseOut(Alpha));
		if (Alpha >= 1.0f)
		{
			bAnimatingReturn = false;
			DragOffset = FVector2D::ZeroVector;
		}
	}

	const FVector2D TotalOffset = ReflowOffset + DragOffset;
	SetRenderTransform(FSlateRenderTransform(TotalOffset));
	CurrentOpacity = Opacity;
	SetRenderOpacity(Opacity);
	Controller->PublishPose(MotionKey, {
		Target,
		Target + TotalOffset,
		Opacity});
	if (bAnimatingReflow || bAnimatingReturn || bDragging)
	{
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}
