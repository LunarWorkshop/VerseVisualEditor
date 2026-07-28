#pragma once

#include "Layout/Geometry.h"

/** Explicit coordinate wrappers prevent Slate's desktop and paint "absolute" spaces from being mixed. */
struct FVerseDesktopPoint
{
	explicit FVerseDesktopPoint(FVector2D InValue = FVector2D::ZeroVector) : Value(InValue) {}
	FVector2D Value;
};

struct FVerseCanvasPoint
{
	explicit FVerseCanvasPoint(FVector2D InValue = FVector2D::ZeroVector) : Value(InValue) {}
	FVector2D Value;
};

struct FVerseGraphPoint
{
	explicit FVerseGraphPoint(FVector2D InValue = FVector2D::ZeroVector) : Value(InValue) {}
	FVector2D Value;
};

struct FVersePaintPoint
{
	explicit FVersePaintPoint(FVector2D InValue = FVector2D::ZeroVector) : Value(InValue) {}
	FVector2D Value;
};

inline FVerseCanvasPoint VerseDesktopToCanvas(
	const FGeometry& DesktopGeometry,
	FVerseDesktopPoint DesktopPoint)
{
	return FVerseCanvasPoint(DesktopGeometry.AbsoluteToLocal(DesktopPoint.Value));
}

inline FVersePaintPoint VerseCanvasToPaint(
	const FGeometry& PaintGeometry,
	FVerseCanvasPoint CanvasPoint)
{
	return FVersePaintPoint(PaintGeometry.LocalToAbsolute(CanvasPoint.Value));
}

inline FVerseCanvasPoint VerseGraphToCanvas(
	FVerseGraphPoint GraphPoint,
	FVerseCanvasPoint GraphOrigin,
	float Zoom)
{
	return FVerseCanvasPoint(GraphOrigin.Value + GraphPoint.Value * Zoom);
}

inline FVerseGraphPoint VerseCanvasToGraph(
	FVerseCanvasPoint CanvasPoint,
	FVerseCanvasPoint GraphOrigin,
	float Zoom)
{
	return FVerseGraphPoint((CanvasPoint.Value - GraphOrigin.Value) / Zoom);
}
