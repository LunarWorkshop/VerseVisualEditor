#pragma once

#include "Templates/SharedPointer.h"
#include "Widgets/SCompoundWidget.h"

class SVerseTile;
class FVerseDocument;
struct FVerseVisualTile;

enum class EVerseGraphMotionEntrance : uint8
{
	FromRight,
	FromTop,
};

/** Screen-space rubber-band displacement used by tile dragging. */
FVector2D ApplyVerseGraphDragResistance(FVector2D ScreenDisplacement);

/** Fixed cubic ease-out used by every graph motion channel. */
float EvaluateVerseGraphEaseOut(float Alpha);

/** Stable presentation fingerprint used to reconcile one tile across source revisions. */
FString BuildVerseGraphMotionKeyBase(
	const FVerseVisualTile& Tile,
	const FVerseDocument& Document);

/** Converts surface-local positions into the graph space anchored at the function entry. */
FVector2D ComputeVerseAnchorRelativeGraphPosition(
	FVector2D SurfaceLocalPosition,
	FVector2D AnchorSurfaceLocalPosition);

struct FVerseGraphMotionPose
{
	FVector2D TargetGraphPosition = FVector2D::ZeroVector;
	FVector2D DisplayedGraphPosition = FVector2D::ZeroVector;
	float Opacity = 1.0f;
};

class SVerseGraphMotionWidget;

/** Persistent per-canvas history used to reconcile rebuilt tile presentations. */
class FVerseGraphMotionController final
	: public TSharedFromThis<FVerseGraphMotionController>
{
public:
	void BeginBuild(bool bAnimateChanges);
	FString AllocateKey(const FString& BaseKey);
	void SetSurfaceGeometry(
		const FGeometry& InGeometry,
		float InZoom,
		bool bInRequiresCurrentAnchor = false);
	/** Establishes graph-space zero from the anchor's current-frame allotted geometry. */
	void EstablishCurrentAnchor(FVector2D AnchorDesktopPosition);
	void InvalidateSurfaceGeometry();
	bool CanResolveGraphPositions() const
	{
		return bHasSurfaceGeometry && bHasCurrentGraphOrigin;
	}
	TOptional<FVerseGraphMotionPose> FindPreviousPose(const FString& Key) const;
	TOptional<FVerseGraphMotionPose> FindCurrentPose(const FString& Key) const;
	void PublishPose(const FString& Key, const FVerseGraphMotionPose& Pose);
	FVector2D DesktopToGraph(FVector2D DesktopPosition) const;
	float GetZoom() const { return Zoom; }
	bool ShouldAnimateBuild() const { return bAnimateCurrentBuild; }

private:
	TMap<FString, int32> KeyOccurrences;
	TMap<FString, FVerseGraphMotionPose> Poses;
	TMap<FString, FVerseGraphMotionPose> PreviousPoses;
	FGeometry SurfaceGeometry;
	FVector2D GraphOrigin = FVector2D::ZeroVector;
	float Zoom = 1.0f;
	bool bHasSurfaceGeometry = false;
	bool bRequiresCurrentAnchor = false;
	bool bHasCurrentGraphOrigin = false;
	bool bAnimateCurrentBuild = false;
};

/** Render-only motion wrapper. Layout always remains at its final position. */
class SVerseGraphMotionWidget final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SVerseGraphMotionWidget) {}
		SLATE_ARGUMENT(TSharedPtr<FVerseGraphMotionController>, Controller)
		SLATE_ARGUMENT(FString, MotionKey)
		SLATE_ARGUMENT(FString, ParentMotionKey)
		SLATE_ARGUMENT(EVerseGraphMotionEntrance, Entrance)
		SLATE_ARGUMENT(bool, IsGraphAnchor)
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void BeginElasticDrag(FVector2D DesktopPosition);
	void UpdateElasticDrag(FVector2D DesktopPosition);
	void EndElasticDrag();
	bool IsElasticDragging() const { return bDragging; }
	float GetCurrentOpacity() const { return CurrentOpacity; }

	virtual void Tick(
		const FGeometry& AllottedGeometry,
		double InCurrentTime,
		float InDeltaTime) override;

private:
	void StartReturnAnimation(double CurrentTime);
	float GetDuration() const;

	TSharedPtr<FVerseGraphMotionController> Controller;
	FString MotionKey;
	FString ParentMotionKey;
	EVerseGraphMotionEntrance Entrance = EVerseGraphMotionEntrance::FromRight;
	bool bIsGraphAnchor = false;
	FVector2D ReflowStartOffset = FVector2D::ZeroVector;
	FVector2D ReflowOffset = FVector2D::ZeroVector;
	FVector2D DragStartDesktop = FVector2D::ZeroVector;
	FVector2D DragOffset = FVector2D::ZeroVector;
	FVector2D ReturnStartOffset = FVector2D::ZeroVector;
	FVector2D LastTargetGraphPosition = FVector2D::ZeroVector;
	double ReflowStartTime = 0.0;
	double ReturnStartTime = 0.0;
	float ReflowStartOpacity = 1.0f;
	float CurrentOpacity = 1.0f;
	bool bInitialized = false;
	bool bAnimatingReflow = false;
	bool bAnimatingReturn = false;
	bool bDragging = false;
};
