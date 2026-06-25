// SpatialConfigEditor.cpp — éditeur 2D de boundary (parité SpatialConfigEditor2D Unity).

#include "SpatialConfigEditor.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Guid.h"

#include "Framework/Application/SlateApplication.h"
#include "Modules/ModuleManager.h"
#include "ToolMenus.h"

#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Fonts/SlateFontInfo.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Colors/SColorPicker.h"

#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"

#define LOCTEXT_NAMESPACE "VaroniaSpatialEditor"

// ============================================================================
// Helpers JSON (convention Unity : objets {x,y,z} / {x,y,z,w})
// ============================================================================

static TSharedRef<FJsonObject> MakeVec3(double X, double Y, double Z)
{
    TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
    O->SetNumberField(TEXT("x"), X);
    O->SetNumberField(TEXT("y"), Y);
    O->SetNumberField(TEXT("z"), Z);
    return O;
}

static TSharedRef<FJsonObject> MakeVec4(double X, double Y, double Z, double W)
{
    TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
    O->SetNumberField(TEXT("x"), X);
    O->SetNumberField(TEXT("y"), Y);
    O->SetNumberField(TEXT("z"), Z);
    O->SetNumberField(TEXT("w"), W);
    return O;
}

static void ReadVec3(const TSharedPtr<FJsonObject>& Root, const TCHAR* Key, FVector& Out)
{
    const TSharedPtr<FJsonObject>* S = nullptr;
    if (Root->TryGetObjectField(Key, S) && S)
    {
        double v;
        if ((*S)->TryGetNumberField(TEXT("x"), v)) Out.X = v;
        if ((*S)->TryGetNumberField(TEXT("y"), v)) Out.Y = v;
        if ((*S)->TryGetNumberField(TEXT("z"), v)) Out.Z = v;
    }
}

// ============================================================================
// SSpatialCanvas — conversions
// ============================================================================

void SSpatialCanvas::Construct(const FArguments& InArgs)
{
    Owner = InArgs._Owner;
}

FVector2D SSpatialCanvas::WorldToScreen(const FVector2D& W) const
{
    const FVector2D C = LastViewportSize * 0.5f;
    return FVector2D(C.X + (W.X - Pan.X) * Zoom, C.Y - (W.Y - Pan.Y) * Zoom);
}

FVector2D SSpatialCanvas::ScreenToWorld(const FVector2D& S) const
{
    const FVector2D C = LastViewportSize * 0.5f;
    return FVector2D(Pan.X + (S.X - C.X) / Zoom, Pan.Y - (S.Y - C.Y) / Zoom);
}

static FVector2D L2W(const FVector2D& L, const FVector& Sync, float YawDeg)
{
    const float r = FMath::DegreesToRadians(YawDeg);
    const float c = FMath::Cos(r), s = FMath::Sin(r);
    return FVector2D(Sync.X + (L.X * c - L.Y * s), Sync.Z + (L.X * s + L.Y * c));
}

FVector2D SSpatialCanvas::LocalToWorld2D(const FVector2D& L) const
{
    if (!Owner) return L;
    return L2W(L, Owner->Doc().SyncPos, Owner->Doc().SyncYawDeg);
}

FVector2D SSpatialCanvas::WorldToLocal2D(const FVector2D& W) const
{
    if (!Owner) return W;
    const FVector& Sync = Owner->Doc().SyncPos;
    const float r = FMath::DegreesToRadians(Owner->Doc().SyncYawDeg);
    const float c = FMath::Cos(r), s = FMath::Sin(r);
    const float dx = W.X - Sync.X, dy = W.Y - Sync.Z;
    return FVector2D(dx * c + dy * s, -dx * s + dy * c);
}

void SSpatialCanvas::CenterOnWorld(const FVector2D& WorldXZ)
{
    Pan = WorldXZ;
    Invalidate(EInvalidateWidgetReason::Paint);
}

void SSpatialCanvas::FrameAll()
{
    if (!Owner) return;
    const FSpatialEdit& D = Owner->Doc();

    FVector2D Min(FLT_MAX, FLT_MAX), Max(-FLT_MAX, -FLT_MAX);
    bool bAny = false;
    for (const FBoundaryEdit& B : D.Boundaries)
    {
        for (const FVector& P : B.Points)
        {
            const FVector2D W = L2W(FVector2D(P.X, P.Z), D.SyncPos, D.SyncYawDeg);
            Min.X = FMath::Min(Min.X, W.X); Min.Y = FMath::Min(Min.Y, W.Y);
            Max.X = FMath::Max(Max.X, W.X); Max.Y = FMath::Max(Max.Y, W.Y);
            bAny = true;
        }
    }

    if (!bAny)
    {
        Pan = FVector2D(D.SyncPos.X, D.SyncPos.Z);
        Zoom = 30.f;
        Invalidate(EInvalidateWidgetReason::Paint);
        return;
    }

    Pan = (Min + Max) * 0.5f;
    const FVector2D Span = (Max - Min).ComponentMax(FVector2D(1.f, 1.f));
    const FVector2D View = LastViewportSize.ComponentMax(FVector2D(64.f, 64.f));
    const float Zx = View.X / Span.X, Zy = View.Y / Span.Y;
    Zoom = FMath::Clamp(FMath::Min(Zx, Zy) * 0.8f, 2.f, 400.f);
    Invalidate(EInvalidateWidgetReason::Paint);
}

// ============================================================================
// SSpatialCanvas — OnPaint
// ============================================================================

int32 SSpatialCanvas::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
    FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
    LastViewportSize = AllottedGeometry.GetLocalSize();
    const FVector2D Size = LastViewportSize;
    const FPaintGeometry PG = AllottedGeometry.ToPaintGeometry();
    const FSlateBrush* White = FCoreStyle::Get().GetBrush("WhiteBrush");

    auto Seg = [&](const FVector2D& A, const FVector2D& B, const FLinearColor& Col, float Thick, int32 L)
    {
        TArray<FVector2D> P; P.Add(A); P.Add(B);
        FSlateDrawElement::MakeLines(OutDrawElements, L, PG, P, ESlateDrawEffect::None, Col, true, Thick);
    };
    auto Poly = [&](const TArray<FVector2D>& Pts, const FLinearColor& Col, float Thick, int32 L)
    {
        FSlateDrawElement::MakeLines(OutDrawElements, L, PG, Pts, ESlateDrawEffect::None, Col, true, Thick);
    };
    auto Fill = [&](const FVector2D& Center, float Half, const FLinearColor& Col, int32 L)
    {
        FSlateDrawElement::MakeBox(OutDrawElements, L,
            AllottedGeometry.ToPaintGeometry(FVector2f(Half * 2.f, Half * 2.f),
                FSlateLayoutTransform(1.f, FVector2f((float)(Center.X - Half), (float)(Center.Y - Half)))),
            White, ESlateDrawEffect::None, Col);
    };
    auto Ring = [&](const FVector2D& Center, float R, const FLinearColor& Col, float Thick, int32 L)
    {
        const int32 N = 24; TArray<FVector2D> P; P.Reserve(N + 1);
        for (int32 i = 0; i <= N; ++i) { const float a = 2.f * PI * i / N; P.Add(FVector2D(Center.X + R * FMath::Cos(a), Center.Y + R * FMath::Sin(a))); }
        Poly(P, Col, Thick, L);
    };
    auto Txt = [&](const FVector2D& Pos, const FString& S, const FLinearColor& Col, int32 L, int32 FontSize)
    {
        FSlateDrawElement::MakeText(OutDrawElements, L,
            AllottedGeometry.ToPaintGeometry(FVector2f(600.f, 16.f), FSlateLayoutTransform(1.f, FVector2f((float)Pos.X, (float)Pos.Y))),
            S, FCoreStyle::GetDefaultFontStyle("Regular", FontSize), ESlateDrawEffect::None, Col);
    };

    int32 L = LayerId;

    // --- Fond ---
    FSlateDrawElement::MakeBox(OutDrawElements, L, PG, White, ESlateDrawEffect::None, FLinearColor(0.075f, 0.078f, 0.09f, 1.f));
    ++L;

    if (!Owner) return L;
    const FSpatialEdit& D = Owner->Doc();

    // --- Underlay ortho ---
    if (Owner->bShowOrtho && Owner->bHasOrtho && Owner->OrthoBrush.GetResourceObject())
    {
        const FVector2D TL = WorldToScreen(FVector2D(Owner->OrthoCenterXZ.X - Owner->OrthoHalfX, Owner->OrthoCenterXZ.Y + Owner->OrthoHalfZ));
        const FVector2D Sz(2.f * Owner->OrthoHalfX * Zoom, 2.f * Owner->OrthoHalfZ * Zoom);
        if (Sz.X > 1.f && Sz.Y > 1.f)
        {
            FSlateDrawElement::MakeBox(OutDrawElements, L,
                AllottedGeometry.ToPaintGeometry(FVector2f((float)Sz.X, (float)Sz.Y), FSlateLayoutTransform(1.f, FVector2f((float)TL.X, (float)TL.Y))),
                &Owner->OrthoBrush, ESlateDrawEffect::None, FLinearColor(1.f, 1.f, 1.f, Owner->OrthoOpacity));
        }
    }
    ++L;

    // --- Grille ---
    if (Owner->bShowGrid && Zoom > 0.f)
    {
        float Step = 1.f;
        while (Step * Zoom < 24.f) Step *= 2.f;
        while (Step * Zoom > 96.f) Step *= 0.5f;

        const FVector2D TL = ScreenToWorld(FVector2D(0.f, 0.f));
        const FVector2D BR = ScreenToWorld(Size);
        const float MinX = FMath::Min(TL.X, BR.X), MaxX = FMath::Max(TL.X, BR.X);
        const float MinZ = FMath::Min(TL.Y, BR.Y), MaxZ = FMath::Max(TL.Y, BR.Y);

        const int32 Kx0 = FMath::FloorToInt(MinX / Step), Kx1 = FMath::CeilToInt(MaxX / Step);
        for (int32 k = Kx0; k <= Kx1 && (Kx1 - Kx0) < 500; ++k)
        {
            const float sx = WorldToScreen(FVector2D(k * Step, 0.f)).X;
            const float a = (k % 5 == 0) ? 0.12f : 0.05f;
            Seg(FVector2D(sx, 0.f), FVector2D(sx, Size.Y), FLinearColor(1.f, 1.f, 1.f, a), 1.f, L);
        }
        const int32 Kz0 = FMath::FloorToInt(MinZ / Step), Kz1 = FMath::CeilToInt(MaxZ / Step);
        for (int32 k = Kz0; k <= Kz1 && (Kz1 - Kz0) < 500; ++k)
        {
            const float sy = WorldToScreen(FVector2D(0.f, k * Step)).Y;
            const float a = (k % 5 == 0) ? 0.12f : 0.05f;
            Seg(FVector2D(0.f, sy), FVector2D(Size.X, sy), FLinearColor(1.f, 1.f, 1.f, a), 1.f, L);
        }
    }
    ++L;

    // --- Axes + rectangle de référence ---
    {
        const FVector2D O = WorldToScreen(FVector2D(0.f, 0.f));
        Seg(FVector2D(0.f, O.Y), FVector2D(Size.X, O.Y), FLinearColor(0.85f, 0.27f, 0.27f, 0.55f), 1.3f, L); // X (rouge)
        Seg(FVector2D(O.X, 0.f), FVector2D(O.X, Size.Y), FLinearColor(0.30f, 0.55f, 0.95f, 0.55f), 1.3f, L); // Z (bleu)
    }
    if (Owner->bShowRect)
    {
        const float HW = 3.5f, HH = 2.75f;
        TArray<FVector2D> R;
        R.Add(WorldToScreen(FVector2D(-HW, -HH)));
        R.Add(WorldToScreen(FVector2D(HW, -HH)));
        R.Add(WorldToScreen(FVector2D(HW, HH)));
        R.Add(WorldToScreen(FVector2D(-HW, HH)));
        { const FVector2D Closed = R[0]; R.Add(Closed); }
        Poly(R, FLinearColor(0.55f, 0.55f, 0.6f, 0.5f), 1.f, L);
    }
    ++L;

    // --- Ghosts (dernier sauvegardé) ---
    if (Owner->bShowGhost && Owner->bHasGhost && Owner->IsDirty())
    {
        const FSpatialEdit& G = Owner->Ghost;
        for (const FBoundaryEdit& B : G.Boundaries)
        {
            if (B.Points.Num() < 2) continue;
            TArray<FVector2D> Pts; Pts.Reserve(B.Points.Num() + 1);
            for (const FVector& P : B.Points) Pts.Add(WorldToScreen(L2W(FVector2D(P.X, P.Z), G.SyncPos, G.SyncYawDeg)));
            { const FVector2D Closed = Pts[0]; Pts.Add(Closed); }
            Poly(Pts, FLinearColor(0.7f, 0.7f, 0.75f, 0.18f), 1.4f, L);
        }
    }
    ++L;

    // --- Boundaries (live) ---
    const int32 LBound = L;
    const int32 LVerts = L + 1;
    for (int32 bi = 0; bi < D.Boundaries.Num(); ++bi)
    {
        const FBoundaryEdit& B = D.Boundaries[bi];
        const bool bSel = (Owner->SelB == bi && Owner->SelType != ESpatialSel::None && Owner->SelType != ESpatialSel::SyncPos);
        const float Thick = B.bMainBoundary ? 4.5f : 2.8f;

        if (B.Points.Num() >= 2)
        {
            TArray<FVector2D> Pts; Pts.Reserve(B.Points.Num() + 1);
            for (const FVector& P : B.Points) Pts.Add(WorldToScreen(LocalToWorld2D(FVector2D(P.X, P.Z))));
            { const FVector2D Closed = Pts[0]; Pts.Add(Closed); }
            if (bSel) Poly(Pts, FLinearColor(1.f, 1.f, 1.f, 0.18f), Thick + 5.f, LBound);
            Poly(Pts, B.Color, Thick, LBound);
        }

        // vertices
        for (int32 vi = 0; vi < B.Points.Num(); ++vi)
        {
            const FVector2D S = WorldToScreen(LocalToWorld2D(FVector2D(B.Points[vi].X, B.Points[vi].Z)));
            const bool bVS = (Owner->SelType == ESpatialSel::Vertex && Owner->SelB == bi && Owner->SelV == vi);
            Fill(S, bVS ? 5.f : 3.5f, bVS ? FLinearColor::White : FLinearColor(B.Color.R, B.Color.G, B.Color.B, 1.f), LVerts);
            if (bVS) Ring(S, 9.f, FLinearColor::White, 1.5f, LVerts);
        }

        // obstacles
        for (int32 oi = 0; oi < B.Obstacles.Num(); ++oi)
        {
            const FObstacleEdit& Ob = B.Obstacles[oi];
            const FVector2D S = WorldToScreen(LocalToWorld2D(FVector2D(Ob.Position.X, Ob.Position.Z)));
            const float Rpx = FMath::Max(5.f, Ob.Scale * Zoom * 0.3f);
            const bool bOS = (Owner->SelType == ESpatialSel::Obstacle && Owner->SelB == bi && Owner->SelO == oi);
            Ring(S, Rpx, bOS ? FLinearColor::White : FLinearColor(0.95f, 0.6f, 0.2f, 0.95f), bOS ? 2.2f : 1.6f, LVerts);
            Fill(S, 2.f, FLinearColor(0.95f, 0.6f, 0.2f, 1.f), LVerts);
        }
    }
    L = LVerts + 1;

    // --- Gizmo SyncPos / rotation ---
    {
        const FVector2D SyncW(D.SyncPos.X, D.SyncPos.Z);
        const FVector2D S = WorldToScreen(SyncW);
        const bool bSyncSel = (Owner->SelType == ESpatialSel::SyncPos);
        const FLinearColor GizCol = bSyncSel ? FLinearColor(1.f, 0.9f, 0.2f, 1.f) : FLinearColor(0.95f, 0.85f, 0.25f, 0.9f);
        Seg(FVector2D(S.X - 11.f, S.Y), FVector2D(S.X + 11.f, S.Y), GizCol, 1.6f, L);
        Seg(FVector2D(S.X, S.Y - 11.f), FVector2D(S.X, S.Y + 11.f), GizCol, 1.6f, L);
        Ring(S, 9.f, GizCol, 1.6f, L);

        const FVector2D HandleW = LocalToWorld2D(FVector2D(0.f, 70.f / FMath::Max(1.f, Zoom)));
        const FVector2D H = WorldToScreen(HandleW);
        Seg(S, H, GizCol, 1.4f, L);
        Fill(H, 4.f, GizCol, L);
    }
    ++L;

    // --- HUD ---
    Txt(FVector2D(6.f, Size.Y - 18.f),
        FString::Printf(TEXT("X %.2f   Z %.2f      %.0f px/m"), MouseWorld.X, MouseWorld.Y, Zoom),
        FLinearColor(0.65f, 0.65f, 0.7f, 1.f), L, 8);

    return L;
}

// ============================================================================
// SSpatialCanvas — souris / clavier
// ============================================================================

static float DistPointSeg(const FVector2D& P, const FVector2D& A, const FVector2D& B, FVector2D& OutClosest, float& OutT)
{
    const FVector2D AB = B - A;
    const float L2 = AB.SizeSquared();
    OutT = (L2 > KINDA_SMALL_NUMBER) ? FMath::Clamp((float)FVector2D::DotProduct(P - A, AB) / L2, 0.f, 1.f) : 0.f;
    OutClosest = A + AB * OutT;
    return (float)FVector2D::Distance(P, OutClosest);
}

static bool PointInPoly(const TArray<FVector2D>& Poly, const FVector2D& P)
{
    bool bIn = false;
    const int32 N = Poly.Num();
    for (int32 i = 0, j = N - 1; i < N; j = i++)
    {
        if (((Poly[i].Y > P.Y) != (Poly[j].Y > P.Y)) &&
            (P.X < (Poly[j].X - Poly[i].X) * (P.Y - Poly[i].Y) / (Poly[j].Y - Poly[i].Y) + Poly[i].X))
        {
            bIn = !bIn;
        }
    }
    return bIn;
}

FReply SSpatialCanvas::OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& E)
{
    if (!Owner) return FReply::Unhandled();
    LastViewportSize = Geo.GetLocalSize();
    const FVector2D Local = Geo.AbsoluteToLocal(E.GetScreenSpacePosition());
    const FVector2D W = ScreenToWorld(Local);

    // Pan (clic milieu ou droit)
    if (E.GetEffectingButton() == EKeys::MiddleMouseButton || E.GetEffectingButton() == EKeys::RightMouseButton)
    {
        Drag = EDrag::Pan;
        DragStartScreen = Local;
        PanStart = Pan;
        return FReply::Handled().CaptureMouse(AsShared());
    }

    if (E.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();

    DragStartWorld = W;
    FSpatialEdit& D = Owner->Doc();

    auto Begin = [&]() { return FReply::Handled().CaptureMouse(AsShared()).SetUserFocus(AsShared(), EFocusCause::Mouse); };

    // 1) Rotation handle (10px)
    {
        const FVector2D HandleW = LocalToWorld2D(FVector2D(0.f, 70.f / FMath::Max(1.f, Zoom)));
        const FVector2D H = WorldToScreen(HandleW);
        if (FVector2D::Distance(Local, H) <= 10.f)
        {
            Owner->PushUndo();
            Owner->Select(ESpatialSel::SyncPos, -1);
            Drag = EDrag::Rot;
            return Begin();
        }
    }
    // 2) SyncPos (9px)
    {
        const FVector2D S = WorldToScreen(FVector2D(D.SyncPos.X, D.SyncPos.Z));
        if (FVector2D::Distance(Local, S) <= 9.f)
        {
            Owner->PushUndo();
            Owner->Select(ESpatialSel::SyncPos, -1);
            Drag = EDrag::SyncPos;
            SnapSyncPos = FVector2D(D.SyncPos.X, D.SyncPos.Z);
            return Begin();
        }
    }
    // 3) Vertex (8px)
    {
        int32 BestB = -1, BestV = -1; float BestD = 8.f;
        for (int32 bi = 0; bi < D.Boundaries.Num(); ++bi)
            for (int32 vi = 0; vi < D.Boundaries[bi].Points.Num(); ++vi)
            {
                const FVector2D S = WorldToScreen(LocalToWorld2D(FVector2D(D.Boundaries[bi].Points[vi].X, D.Boundaries[bi].Points[vi].Z)));
                const float d = FVector2D::Distance(Local, S);
                if (d <= BestD) { BestD = d; BestB = bi; BestV = vi; }
            }
        if (BestB != INDEX_NONE && BestV != INDEX_NONE)
        {
            FBoundaryEdit& B = D.Boundaries[BestB];
            if (E.IsAltDown())
            {
                if (B.Points.Num() > 3)
                {
                    Owner->PushUndo();
                    B.Points.RemoveAt(BestV);
                    Owner->Select(ESpatialSel::Boundary, BestB);
                    Owner->StructuralChanged();
                }
                return FReply::Handled();
            }
            Owner->PushUndo();
            Owner->Select(ESpatialSel::Vertex, BestB, BestV);
            Drag = EDrag::Vertex;
            SnapVertexLocal = FVector2D(B.Points[BestV].X, B.Points[BestV].Z);
            return Begin();
        }
    }
    // 4) Obstacle (12px)
    {
        int32 BestB = -1, BestO = -1; float BestD = 12.f;
        for (int32 bi = 0; bi < D.Boundaries.Num(); ++bi)
            for (int32 oi = 0; oi < D.Boundaries[bi].Obstacles.Num(); ++oi)
            {
                const FObstacleEdit& Ob = D.Boundaries[bi].Obstacles[oi];
                const FVector2D S = WorldToScreen(LocalToWorld2D(FVector2D(Ob.Position.X, Ob.Position.Z)));
                const float d = FVector2D::Distance(Local, S);
                if (d <= BestD) { BestD = d; BestB = bi; BestO = oi; }
            }
        if (BestB != INDEX_NONE && BestO != INDEX_NONE)
        {
            Owner->PushUndo();
            Owner->Select(ESpatialSel::Obstacle, BestB, -1, BestO);
            Drag = EDrag::Obstacle;
            const FObstacleEdit& Ob = D.Boundaries[BestB].Obstacles[BestO];
            SnapObstacleLocal = FVector2D(Ob.Position.X, Ob.Position.Z);
            return Begin();
        }
    }
    // 5) Arête → insertion d'un vertex (8px)
    {
        for (int32 bi = 0; bi < D.Boundaries.Num(); ++bi)
        {
            FBoundaryEdit& B = D.Boundaries[bi];
            const int32 N = B.Points.Num();
            if (N < 2) continue;
            for (int32 vi = 0; vi < N; ++vi)
            {
                const int32 vj = (vi + 1) % N;
                const FVector2D A = WorldToScreen(LocalToWorld2D(FVector2D(B.Points[vi].X, B.Points[vi].Z)));
                const FVector2D Bs = WorldToScreen(LocalToWorld2D(FVector2D(B.Points[vj].X, B.Points[vj].Z)));
                FVector2D Closest; float T;
                if (DistPointSeg(Local, A, Bs, Closest, T) <= 8.f)
                {
                    Owner->PushUndo();
                    const FVector2D NewLocal = WorldToLocal2D(ScreenToWorld(Closest));
                    const float NewY = FMath::Lerp((float)B.Points[vi].Y, (float)B.Points[vj].Y, T);
                    B.Points.Insert(FVector(NewLocal.X, NewY, NewLocal.Y), vi + 1);
                    Owner->Select(ESpatialSel::Vertex, bi, vi + 1);
                    Owner->StructuralChanged();
                    Drag = EDrag::Vertex;
                    SnapVertexLocal = NewLocal;
                    return Begin();
                }
            }
        }
    }
    // 6) Intérieur d'une boundary
    {
        int32 Hit = -1;
        for (int32 bi = 0; bi < D.Boundaries.Num(); ++bi)
        {
            const FBoundaryEdit& B = D.Boundaries[bi];
            if (B.Points.Num() < 3) continue;
            TArray<FVector2D> Poly; Poly.Reserve(B.Points.Num());
            for (const FVector& P : B.Points) Poly.Add(LocalToWorld2D(FVector2D(P.X, P.Z)));
            if (PointInPoly(Poly, W)) Hit = bi;
        }
        if (Hit != INDEX_NONE)
        {
            Owner->Select(ESpatialSel::Boundary, Hit);
            if (E.IsShiftDown())
            {
                Owner->PushUndo();
                Drag = EDrag::Boundary;
                SnapBoundaryLocal.Reset();
                for (const FVector& P : D.Boundaries[Hit].Points) SnapBoundaryLocal.Add(FVector2D(P.X, P.Z));
            }
            return Begin();
        }
    }

    // 7) Vide → désélection
    Owner->Select(ESpatialSel::None, -1);
    return Begin();
}

FReply SSpatialCanvas::OnMouseMove(const FGeometry& Geo, const FPointerEvent& E)
{
    if (!Owner) return FReply::Unhandled();
    LastViewportSize = Geo.GetLocalSize();
    const FVector2D Local = Geo.AbsoluteToLocal(E.GetScreenSpacePosition());
    MouseWorld = ScreenToWorld(Local);

    if (Drag == EDrag::None)
    {
        Invalidate(EInvalidateWidgetReason::Paint); // HUD
        return FReply::Unhandled();
    }

    FSpatialEdit& D = Owner->Doc();

    switch (Drag)
    {
    case EDrag::Pan:
        Pan.X = PanStart.X - (Local.X - DragStartScreen.X) / Zoom;
        Pan.Y = PanStart.Y + (Local.Y - DragStartScreen.Y) / Zoom;
        Invalidate(EInvalidateWidgetReason::Paint);
        break;

    case EDrag::Vertex:
        if (D.Boundaries.IsValidIndex(Owner->SelB) && D.Boundaries[Owner->SelB].Points.IsValidIndex(Owner->SelV))
        {
            const FVector2D Delta = WorldToLocal2D(MouseWorld) - WorldToLocal2D(DragStartWorld);
            const FVector2D NL = SnapVertexLocal + Delta;
            FVector& P = D.Boundaries[Owner->SelB].Points[Owner->SelV];
            P.X = NL.X; P.Z = NL.Y;
            Owner->MarkDirtyRepaint();
        }
        break;

    case EDrag::Obstacle:
        if (D.Boundaries.IsValidIndex(Owner->SelB) && D.Boundaries[Owner->SelB].Obstacles.IsValidIndex(Owner->SelO))
        {
            const FVector2D Delta = WorldToLocal2D(MouseWorld) - WorldToLocal2D(DragStartWorld);
            const FVector2D NL = SnapObstacleLocal + Delta;
            FVector& P = D.Boundaries[Owner->SelB].Obstacles[Owner->SelO].Position;
            P.X = NL.X; P.Z = NL.Y;
            Owner->MarkDirtyRepaint();
        }
        break;

    case EDrag::Boundary:
        if (D.Boundaries.IsValidIndex(Owner->SelB))
        {
            const FVector2D Delta = WorldToLocal2D(MouseWorld) - WorldToLocal2D(DragStartWorld);
            FBoundaryEdit& B = D.Boundaries[Owner->SelB];
            for (int32 i = 0; i < B.Points.Num() && i < SnapBoundaryLocal.Num(); ++i)
            {
                B.Points[i].X = SnapBoundaryLocal[i].X + Delta.X;
                B.Points[i].Z = SnapBoundaryLocal[i].Y + Delta.Y;
            }
            Owner->MarkDirtyRepaint();
        }
        break;

    case EDrag::SyncPos:
    {
        const FVector2D Delta = MouseWorld - DragStartWorld;
        const FVector2D NW = SnapSyncPos + Delta;
        D.SyncPos.X = NW.X; D.SyncPos.Z = NW.Y;
        Owner->MarkDirtyRepaint();
        break;
    }

    case EDrag::Rot:
    {
        const FVector2D Dir = MouseWorld - FVector2D(D.SyncPos.X, D.SyncPos.Z);
        if (Dir.SizeSquared() > KINDA_SMALL_NUMBER)
        {
            const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(-Dir.X, Dir.Y));
            D.SyncYawDeg = Yaw;
            const float h = FMath::DegreesToRadians(Yaw) * 0.5f;
            D.SyncQuat = FVector4(0.f, FMath::Sin(h), 0.f, FMath::Cos(h));
            Owner->MarkDirtyRepaint();
        }
        break;
    }
    default: break;
    }

    return FReply::Handled();
}

FReply SSpatialCanvas::OnMouseButtonUp(const FGeometry& Geo, const FPointerEvent& E)
{
    Drag = EDrag::None;
    return FReply::Handled().ReleaseMouseCapture();
}

FReply SSpatialCanvas::OnMouseWheel(const FGeometry& Geo, const FPointerEvent& E)
{
    LastViewportSize = Geo.GetLocalSize();
    const FVector2D Local = Geo.AbsoluteToLocal(E.GetScreenSpacePosition());
    const FVector2D Before = ScreenToWorld(Local);
    const float f = (E.GetWheelDelta() > 0.f) ? (1.f / 0.85f) : 0.85f;
    Zoom = FMath::Clamp(Zoom * f, 2.f, 400.f);
    const FVector2D After = ScreenToWorld(Local);
    Pan += Before - After;
    Invalidate(EInvalidateWidgetReason::Paint);
    return FReply::Handled();
}

FReply SSpatialCanvas::OnKeyDown(const FGeometry& Geo, const FKeyEvent& E)
{
    if (!Owner) return FReply::Unhandled();

    if (E.GetKey() == EKeys::Z && E.IsControlDown()) { Owner->Undo(); return FReply::Handled(); }
    if (E.GetKey() == EKeys::F) { FrameAll(); return FReply::Handled(); }

    if (E.GetKey() == EKeys::Delete || E.GetKey() == EKeys::BackSpace)
    {
        FSpatialEdit& D = Owner->Doc();
        if (Owner->SelType == ESpatialSel::Vertex && D.Boundaries.IsValidIndex(Owner->SelB) && D.Boundaries[Owner->SelB].Points.IsValidIndex(Owner->SelV))
        {
            if (D.Boundaries[Owner->SelB].Points.Num() > 3)
            {
                Owner->PushUndo();
                D.Boundaries[Owner->SelB].Points.RemoveAt(Owner->SelV);
                Owner->Select(ESpatialSel::Boundary, Owner->SelB);
                Owner->StructuralChanged();
            }
            return FReply::Handled();
        }
        if (Owner->SelType == ESpatialSel::Obstacle && D.Boundaries.IsValidIndex(Owner->SelB) && D.Boundaries[Owner->SelB].Obstacles.IsValidIndex(Owner->SelO))
        {
            Owner->PushUndo();
            D.Boundaries[Owner->SelB].Obstacles.RemoveAt(Owner->SelO);
            Owner->Select(ESpatialSel::Boundary, Owner->SelB);
            Owner->StructuralChanged();
            return FReply::Handled();
        }
        if (Owner->SelType == ESpatialSel::Boundary && D.Boundaries.IsValidIndex(Owner->SelB))
        {
            Owner->PushUndo();
            D.Boundaries.RemoveAt(Owner->SelB);
            Owner->Select(ESpatialSel::None, -1);
            Owner->StructuralChanged();
            return FReply::Handled();
        }
    }
    return FReply::Unhandled();
}

FCursorReply SSpatialCanvas::OnCursorQuery(const FGeometry& Geo, const FPointerEvent& E) const
{
    return FCursorReply::Cursor(EMouseCursor::Crosshairs);
}

// ============================================================================
// SSpatialConfigEditor — IO
// ============================================================================

FString SSpatialConfigEditor::GetSpatialPath()
{
    const FString UserProfile = FPlatformMisc::GetEnvironmentVariable(TEXT("USERPROFILE"));
    FString FullPath = FPaths::Combine(UserProfile, TEXT("AppData"), TEXT("LocalLow"), TEXT("Varonia"), TEXT("NewSpatial.json"));
    FPaths::NormalizeFilename(FullPath);
    return FullPath;
}

void SSpatialConfigEditor::LoadFromDisk()
{
    SavePath = GetSpatialPath();
    LoadedRoot.Reset();
    Document = FSpatialEdit();
    UndoStack.Reset();

    FString Json;
    bFileExists = FFileHelper::LoadFileToString(Json, *SavePath);
    if (bFileExists)
    {
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
        TSharedPtr<FJsonObject> Root;
        if (FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid())
        {
            LoadedRoot = Root;

            ReadVec3(Root, TEXT("SyncPos"), Document.SyncPos);

            const TSharedPtr<FJsonObject>* QObj = nullptr;
            if (Root->TryGetObjectField(TEXT("SyncQuaterion"), QObj) && QObj)
            {
                double v;
                FVector4 Q(0, 0, 0, 1);
                if ((*QObj)->TryGetNumberField(TEXT("x"), v)) Q.X = v;
                if ((*QObj)->TryGetNumberField(TEXT("y"), v)) Q.Y = v;
                if ((*QObj)->TryGetNumberField(TEXT("z"), v)) Q.Z = v;
                if ((*QObj)->TryGetNumberField(TEXT("w"), v)) Q.W = v;
                Document.SyncQuat = Q;
                Document.SyncYawDeg = FMath::RadiansToDegrees(2.f * FMath::Atan2((float)Q.Y, (float)Q.W));
            }

            double Mult;
            if (Root->TryGetNumberField(TEXT("Multiplier"), Mult)) Document.Multiplier = Mult;

            const TArray<TSharedPtr<FJsonValue>>* BArr = nullptr;
            if (Root->TryGetArrayField(TEXT("Boundaries"), BArr) && BArr)
            {
                for (const TSharedPtr<FJsonValue>& BV : *BArr)
                {
                    const TSharedPtr<FJsonObject> BObj = BV->AsObject();
                    if (!BObj.IsValid()) continue;

                    FBoundaryEdit B;
                    BObj->TryGetStringField(TEXT("ID"), B.ID);

                    const TSharedPtr<FJsonObject>* CObj = nullptr;
                    if (BObj->TryGetObjectField(TEXT("BoundaryColor"), CObj) && CObj)
                    {
                        double v;
                        if ((*CObj)->TryGetNumberField(TEXT("x"), v)) B.Color.R = v;
                        if ((*CObj)->TryGetNumberField(TEXT("y"), v)) B.Color.G = v;
                        if ((*CObj)->TryGetNumberField(TEXT("z"), v)) B.Color.B = v;
                        B.Color.A = 1.f;
                    }

                    const TArray<TSharedPtr<FJsonValue>>* PArr = nullptr;
                    if (BObj->TryGetArrayField(TEXT("Points"), PArr) && PArr)
                        for (const TSharedPtr<FJsonValue>& PV : *PArr)
                        {
                            const TSharedPtr<FJsonObject> PObj = PV->AsObject();
                            if (!PObj.IsValid()) continue;
                            FVector P(0, 0, 0); double v;
                            if (PObj->TryGetNumberField(TEXT("x"), v)) P.X = v;
                            if (PObj->TryGetNumberField(TEXT("y"), v)) P.Y = v;
                            if (PObj->TryGetNumberField(TEXT("z"), v)) P.Z = v;
                            B.Points.Add(P);
                        }

                    const TArray<TSharedPtr<FJsonValue>>* OArr = nullptr;
                    if (BObj->TryGetArrayField(TEXT("Obstacles"), OArr) && OArr)
                        for (const TSharedPtr<FJsonValue>& OV : *OArr)
                        {
                            const TSharedPtr<FJsonObject> OObj = OV->AsObject();
                            if (!OObj.IsValid()) continue;
                            FObstacleEdit Ob; double v;
                            ReadVec3(OObj, TEXT("Position"), Ob.Position);
                            ReadVec3(OObj, TEXT("Rotation"), Ob.Rotation);
                            if (OObj->TryGetNumberField(TEXT("Size"), v)) Ob.Size = (int32)v;
                            if (OObj->TryGetNumberField(TEXT("Scale"), v)) Ob.Scale = (float)v;
                            if (OObj->TryGetNumberField(TEXT("SpecialId"), v)) Ob.SpecialId = (int32)v;
                            B.Obstacles.Add(Ob);
                        }

                    bool b; double d;
                    if (BObj->TryGetBoolField(TEXT("AlertLimit"), b)) B.bAlertLimit = b;
                    if (BObj->TryGetBoolField(TEXT("BoundaryMoreVisible"), b)) B.bBoundaryMoreVisible = b;
                    if (BObj->TryGetBoolField(TEXT("MainBoundary"), b)) B.bMainBoundary = b;
                    if (BObj->TryGetBoolField(TEXT("Reverse"), b)) B.bReverse = b;
                    if (BObj->TryGetBoolField(TEXT("HideLineFar"), b)) B.bHideLineFar = b;
                    if (BObj->TryGetBoolField(TEXT("Visible"), b)) B.bVisible = b;
                    if (BObj->TryGetNumberField(TEXT("DisplayDistance"), d)) B.DisplayDistance = (float)d;

                    Document.Boundaries.Add(B);
                }
            }
        }
    }

    LastMultiplier = Document.Multiplier;
    bDirty = false;
    CaptureGhost();
}

void SSpatialConfigEditor::SaveToDisk()
{
    TSharedRef<FJsonObject> Root = LoadedRoot.IsValid() ? LoadedRoot.ToSharedRef() : MakeShared<FJsonObject>();

    Root->SetObjectField(TEXT("SyncPos"), MakeVec3(Document.SyncPos.X, Document.SyncPos.Y, Document.SyncPos.Z));
    Root->SetObjectField(TEXT("SyncQuaterion"), MakeVec4(Document.SyncQuat.X, Document.SyncQuat.Y, Document.SyncQuat.Z, Document.SyncQuat.W));
    Root->SetNumberField(TEXT("Multiplier"), Document.Multiplier);

    TArray<TSharedPtr<FJsonValue>> BArr;
    for (const FBoundaryEdit& B : Document.Boundaries)
    {
        TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
        O->SetStringField(TEXT("ID"), B.ID.IsEmpty() ? FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens) : B.ID);
        O->SetObjectField(TEXT("BoundaryColor"), MakeVec3(B.Color.R, B.Color.G, B.Color.B));

        TArray<TSharedPtr<FJsonValue>> Pts;
        for (const FVector& P : B.Points) Pts.Add(MakeShared<FJsonValueObject>(MakeVec3(P.X, P.Y, P.Z)));
        O->SetArrayField(TEXT("Points"), Pts);

        TArray<TSharedPtr<FJsonValue>> Obs;
        for (const FObstacleEdit& Ob : B.Obstacles)
        {
            TSharedRef<FJsonObject> OO = MakeShared<FJsonObject>();
            OO->SetObjectField(TEXT("Position"), MakeVec3(Ob.Position.X, Ob.Position.Y, Ob.Position.Z));
            OO->SetObjectField(TEXT("Rotation"), MakeVec3(Ob.Rotation.X, Ob.Rotation.Y, Ob.Rotation.Z));
            OO->SetNumberField(TEXT("Size"), Ob.Size);
            OO->SetNumberField(TEXT("Scale"), Ob.Scale);
            OO->SetNumberField(TEXT("SpecialId"), Ob.SpecialId);
            Obs.Add(MakeShared<FJsonValueObject>(OO));
        }
        O->SetArrayField(TEXT("Obstacles"), Obs);

        O->SetBoolField(TEXT("AlertLimit"), B.bAlertLimit);
        O->SetBoolField(TEXT("BoundaryMoreVisible"), B.bBoundaryMoreVisible);
        O->SetBoolField(TEXT("MainBoundary"), B.bMainBoundary);
        O->SetBoolField(TEXT("Reverse"), B.bReverse);
        O->SetBoolField(TEXT("HideLineFar"), B.bHideLineFar);
        O->SetBoolField(TEXT("Visible"), B.bVisible);
        O->SetNumberField(TEXT("DisplayDistance"), B.DisplayDistance);

        BArr.Add(MakeShared<FJsonValueObject>(O));
    }
    Root->SetArrayField(TEXT("Boundaries"), BArr);

    IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
    const FString Dir = FPaths::GetPath(SavePath);
    if (!PF.DirectoryExists(*Dir)) PF.CreateDirectoryTree(*Dir);

    FString Out;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
    if (FJsonSerializer::Serialize(Root, Writer))
    {
        FFileHelper::SaveStringToFile(Out, *SavePath);
        LoadedRoot = Root;
        bFileExists = true;
        bDirty = false;
        LastMultiplier = Document.Multiplier;
        CaptureGhost();
    }
}

void SSpatialConfigEditor::CaptureGhost()
{
    Ghost = Document;
    bHasGhost = true;
}

// ============================================================================
// SSpatialConfigEditor — undo / état
// ============================================================================

void SSpatialConfigEditor::PushUndo()
{
    UndoStack.Add(Document);
    if (UndoStack.Num() > MaxUndo) UndoStack.RemoveAt(0);
}

void SSpatialConfigEditor::Undo()
{
    if (UndoStack.Num() == 0) return;
    Document = UndoStack.Pop();
    LastMultiplier = Document.Multiplier;
    if (!Document.Boundaries.IsValidIndex(SelB)) { SelType = ESpatialSel::None; SelB = SelV = SelO = -1; }
    bDirty = true;
    RebuildSidebar();
    if (Canvas.IsValid()) Canvas->Invalidate(EInvalidateWidgetReason::Paint);
}

void SSpatialConfigEditor::MarkDirtyRepaint()
{
    bDirty = true;
    if (Canvas.IsValid()) Canvas->Invalidate(EInvalidateWidgetReason::Paint);
}

void SSpatialConfigEditor::StructuralChanged()
{
    bDirty = true;
    if (!Document.Boundaries.IsValidIndex(SelB)) { SelType = ESpatialSel::None; SelB = SelV = SelO = -1; }
    RebuildSidebar();
    if (Canvas.IsValid()) Canvas->Invalidate(EInvalidateWidgetReason::Paint);
}

void SSpatialConfigEditor::Select(ESpatialSel Type, int32 B, int32 V, int32 O)
{
    SelType = Type; SelB = B; SelV = V; SelO = O;
    RebuildSidebar();
    if (Canvas.IsValid()) Canvas->Invalidate(EInvalidateWidgetReason::Paint);
}

FText SSpatialConfigEditor::GetStatusText() const
{
    if (bDirty)      return LOCTEXT("Unsaved", "● NON SAUVEGARDÉ");
    if (bFileExists) return LOCTEXT("Synced", "SYNCHRONISÉ");
    return LOCTEXT("NoFile", "AUCUN FICHIER");
}

FSlateColor SSpatialConfigEditor::GetStatusColor() const
{
    if (bDirty)      return FSlateColor(FLinearColor(1.f, 0.75f, 0.30f));
    if (bFileExists) return FSlateColor(FLinearColor(0.30f, 0.85f, 0.65f));
    return FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f));
}

// ============================================================================
// SSpatialConfigEditor — actions
// ============================================================================

void SSpatialConfigEditor::AddBoundary()
{
    PushUndo();
    FBoundaryEdit B;
    B.ID = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
    B.bMainBoundary = (Document.Boundaries.Num() == 0);
    B.DisplayDistance = 3.f;
    B.Color = FLinearColor(0.30f, 0.85f, 0.65f, 1.f);

    // Carré 2x2 m centré sur la vue (en LOCAL).
    const FVector2D CenterWorld = Canvas.IsValid() ? Canvas->GetPan() : FVector2D::ZeroVector;
    const float r = FMath::DegreesToRadians(Document.SyncYawDeg);
    const float c = FMath::Cos(r), s = FMath::Sin(r);
    const float dx = CenterWorld.X - Document.SyncPos.X, dy = CenterWorld.Y - Document.SyncPos.Z;
    const FVector2D CenterLocal(dx * c + dy * s, -dx * s + dy * c);
    const float H = 1.f;
    B.Points.Add(FVector(CenterLocal.X - H, 0.f, CenterLocal.Y - H));
    B.Points.Add(FVector(CenterLocal.X + H, 0.f, CenterLocal.Y - H));
    B.Points.Add(FVector(CenterLocal.X + H, 0.f, CenterLocal.Y + H));
    B.Points.Add(FVector(CenterLocal.X - H, 0.f, CenterLocal.Y + H));

    Document.Boundaries.Add(B);
    Select(ESpatialSel::Boundary, Document.Boundaries.Num() - 1);
    StructuralChanged();
}

void SSpatialConfigEditor::RemoveBoundary(int32 Index)
{
    if (!Document.Boundaries.IsValidIndex(Index)) return;
    PushUndo();
    Document.Boundaries.RemoveAt(Index);
    Select(ESpatialSel::None, -1);
    StructuralChanged();
}

void SSpatialConfigEditor::AddObstacle()
{
    if (!Document.Boundaries.IsValidIndex(SelB)) return;
    PushUndo();
    FObstacleEdit Ob;
    const FVector2D CenterWorld = Canvas.IsValid() ? Canvas->GetPan() : FVector2D::ZeroVector;
    const float r = FMath::DegreesToRadians(Document.SyncYawDeg);
    const float c = FMath::Cos(r), s = FMath::Sin(r);
    const float dx = CenterWorld.X - Document.SyncPos.X, dy = CenterWorld.Y - Document.SyncPos.Z;
    Ob.Position = FVector(dx * c + dy * s, 0.f, -dx * s + dy * c);
    Document.Boundaries[SelB].Obstacles.Add(Ob);
    Select(ESpatialSel::Obstacle, SelB, -1, Document.Boundaries[SelB].Obstacles.Num() - 1);
    StructuralChanged();
}

void SSpatialConfigEditor::ApplyMultiplier(double NewValue)
{
    const double Old = Document.Multiplier;
    const double Scale = (FMath::Abs(1.0 + Old) > KINDA_SMALL_NUMBER) ? (1.0 + NewValue) / (1.0 + Old) : 1.0;
    if (!FMath::IsNearlyEqual(Scale, 1.0) && FMath::IsFinite(Scale))
    {
        PushUndo();
        for (FBoundaryEdit& B : Document.Boundaries)
        {
            for (FVector& P : B.Points) { P.X *= Scale; P.Z *= Scale; }
            for (FObstacleEdit& Ob : B.Obstacles) { Ob.Position.X *= Scale; Ob.Position.Z *= Scale; }
        }
    }
    Document.Multiplier = NewValue;
    LastMultiplier = NewValue;
    MarkDirtyRepaint();
}

void SSpatialConfigEditor::BrowseOrtho()
{
    IDesktopPlatform* DP = FDesktopPlatformModule::Get();
    if (!DP) return;
    const void* Wnd = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
    TArray<FString> Out;
    const bool bOk = DP->OpenFileDialog(const_cast<void*>(Wnd), TEXT("Choisir une image ortho"), TEXT(""), TEXT(""),
        TEXT("Images|*.jpg;*.jpeg;*.png"), EFileDialogFlags::None, Out);
    if (!bOk || Out.Num() == 0) return;

    OrthoPath = Out[0];

    // Taille du nom : suffixe _<m> (demi-largeur en mètres).
    const FString Base = FPaths::GetBaseFilename(OrthoPath);
    int32 Us; if (Base.FindLastChar('_', Us))
    {
        const FString Suffix = Base.RightChop(Us + 1);
        const float M = FCString::Atof(*Suffix);
        if (M > 0.f) OrthoHalfX = M;
    }

    RebuildOrthoBrush();
    RebuildSidebar();
    if (Canvas.IsValid()) Canvas->Invalidate(EInvalidateWidgetReason::Paint);
}

void SSpatialConfigEditor::RebuildOrthoBrush()
{
    bHasOrtho = false;
    OrthoTexture.Reset();
    if (OrthoPath.IsEmpty()) return;

    TArray<uint8> Raw;
    if (!FFileHelper::LoadFileToArray(Raw, *OrthoPath)) return;

    const FString Ext = FPaths::GetExtension(OrthoPath).ToLower();
    const EImageFormat Fmt = (Ext == TEXT("png")) ? EImageFormat::PNG : EImageFormat::JPEG;

    IImageWrapperModule& Mod = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
    TSharedPtr<IImageWrapper> Wrapper = Mod.CreateImageWrapper(Fmt);
    if (!Wrapper.IsValid() || !Wrapper->SetCompressed(Raw.GetData(), Raw.Num())) return;

    TArray64<uint8> BGRA;
    if (!Wrapper->GetRaw(ERGBFormat::BGRA, 8, BGRA)) return;

    const int32 W = Wrapper->GetWidth();
    const int32 Hgt = Wrapper->GetHeight();
    if (W <= 0 || Hgt <= 0) return;

    UTexture2D* Tex = UTexture2D::CreateTransient(W, Hgt, PF_B8G8R8A8);
    if (!Tex) return;
    Tex->SRGB = true;
#if WITH_EDITORONLY_DATA
    Tex->MipGenSettings = TMGS_NoMipmaps;
#endif
    FTexture2DMipMap& Mip = Tex->GetPlatformData()->Mips[0];
    void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
    FMemory::Memcpy(Data, BGRA.GetData(), (SIZE_T)W * Hgt * 4);
    Mip.BulkData.Unlock();
    Tex->UpdateResource();

    OrthoTexture.Reset(Tex);
    OrthoBrush.SetResourceObject(Tex);
    OrthoBrush.DrawAs = ESlateBrushDrawType::Image;
    OrthoBrush.ImageSize = FVector2D(W, Hgt);
    OrthoHalfZ = OrthoHalfX * ((float)Hgt / (float)FMath::Max(1, W));
    bHasOrtho = true;
}

// ============================================================================
// SSpatialConfigEditor — UI helpers
// ============================================================================

TSharedRef<SWidget> SSpatialConfigEditor::MakeSectionLabel(const FString& Text, FLinearColor Color)
{
    return SNew(STextBlock)
        .Text(FText::FromString(Text))
        .Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
        .ColorAndOpacity(FSlateColor(Color));
}

TSharedRef<SWidget> SSpatialConfigEditor::MakeFloatRow(const FString& Label, TFunction<float()> Get, TFunction<void(float)> Set, float Min, float Max)
{
    return SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
        [ SNew(SBox).WidthOverride(120.f) [ SNew(STextBlock).Text(FText::FromString(Label)) ] ]
        + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
        [
            SNew(SSpinBox<float>)
            .MinValue(Min).MaxValue(Max)
            .Value_Lambda([Get]() { return Get(); })
            .OnValueChanged_Lambda([Set](float V) { Set(V); })
            .OnValueCommitted_Lambda([Set](float V, ETextCommit::Type) { Set(V); })
        ];
}

TSharedRef<SWidget> SSpatialConfigEditor::MakeBoolRow(const FString& Label, TFunction<bool()> Get, TFunction<void(bool)> Set)
{
    return SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
        [ SNew(SBox).WidthOverride(120.f) [ SNew(STextBlock).Text(FText::FromString(Label)) ] ]
        + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
        [
            SNew(SCheckBox)
            .IsChecked_Lambda([Get]() { return Get() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda([Set](ECheckBoxState S) { Set(S == ECheckBoxState::Checked); })
        ];
}

// ============================================================================
// SSpatialConfigEditor — sidebar
// ============================================================================

TSharedRef<SWidget> SSpatialConfigEditor::BuildBoundaryListSection()
{
    TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);

    Box->AddSlot().AutoHeight().Padding(0, 8, 0, 2)
    [ MakeSectionLabel(FString::Printf(TEXT("BOUNDARIES — %d"), Document.Boundaries.Num()), FLinearColor(0.30f, 0.85f, 0.65f)) ];

    for (int32 i = 0; i < Document.Boundaries.Num(); ++i)
    {
        const FBoundaryEdit& B = Document.Boundaries[i];
        const bool bSel = (SelB == i && SelType != ESpatialSel::None && SelType != ESpatialSel::SyncPos);

        Box->AddSlot().AutoHeight().Padding(0, 1)
        [
            SNew(SHorizontalBox)
            // pastille couleur → color picker
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
            [
                SNew(SBox).WidthOverride(18.f).HeightOverride(18.f)
                [
                    SNew(SButton)
                    .ButtonColorAndOpacity(B.Color)
                    .ToolTipText(LOCTEXT("PickColor", "Couleur de la boundary"))
                    .OnClicked_Lambda([this, i]()
                    {
                        if (!Document.Boundaries.IsValidIndex(i)) return FReply::Handled();
                        FColorPickerArgs Args;
                        Args.InitialColor = Document.Boundaries[i].Color;
                        Args.bUseAlpha = false;
                        Args.OnColorCommitted = FOnLinearColorValueChanged::CreateLambda([this, i](FLinearColor C)
                        {
                            if (Document.Boundaries.IsValidIndex(i)) { Document.Boundaries[i].Color = FLinearColor(C.R, C.G, C.B, 1.f); MarkDirtyRepaint(); }
                        });
                        OpenColorPicker(Args);
                        return FReply::Handled();
                    })
                ]
            ]
            // libellé cliquable (sélection)
            + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
            [
                SNew(SButton)
                .HAlign(HAlign_Left)
                .ButtonColorAndOpacity(bSel ? FLinearColor(0.25f, 0.45f, 0.4f) : FLinearColor(0.12f, 0.12f, 0.13f))
                .OnClicked_Lambda([this, i]() { Select(ESpatialSel::Boundary, i); return FReply::Handled(); })
                [
                    SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("#%d %s  %d pts  %d obs"),
                        i, B.bMainBoundary ? TEXT("★") : TEXT(" "), B.Points.Num(), B.Obstacles.Num())))
                ]
            ]
            // recentrer
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2, 0, 0, 0)
            [
                SNew(SButton).Text(LOCTEXT("Recenter", "⌖"))
                .ToolTipText(LOCTEXT("RecenterTip", "Recentrer la vue"))
                .OnClicked_Lambda([this, i]()
                {
                    if (Canvas.IsValid() && Document.Boundaries.IsValidIndex(i) && Document.Boundaries[i].Points.Num() > 0)
                    {
                        FVector2D Sum(0, 0);
                        for (const FVector& P : Document.Boundaries[i].Points)
                            Sum += L2W(FVector2D(P.X, P.Z), Document.SyncPos, Document.SyncYawDeg);
                        Canvas->CenterOnWorld(Sum / Document.Boundaries[i].Points.Num());
                    }
                    return FReply::Handled();
                })
            ]
            // supprimer
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2, 0, 0, 0)
            [
                SNew(SButton).Text(LOCTEXT("RemoveX", "X"))
                .OnClicked_Lambda([this, i]() { RemoveBoundary(i); return FReply::Handled(); })
            ]
        ];
    }

    Box->AddSlot().AutoHeight().Padding(0, 4)
    [
        SNew(SButton).HAlign(HAlign_Center).Text(LOCTEXT("AddBoundary", "+ Boundary"))
        .OnClicked_Lambda([this]() { AddBoundary(); return FReply::Handled(); })
    ];

    return Box;
}

TSharedRef<SWidget> SSpatialConfigEditor::BuildSelectionSection()
{
    TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);

    if (!Document.Boundaries.IsValidIndex(SelB) || SelType == ESpatialSel::None || SelType == ESpatialSel::SyncPos)
    {
        Box->AddSlot().AutoHeight().Padding(0, 8, 0, 2)
        [ SNew(STextBlock).Text(LOCTEXT("NoSel", "Sélectionne une boundary dans le canvas."))
            .ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.55f, 0.6f))).AutoWrapText(true) ];
        return Box;
    }

    const int32 b = SelB;
    FBoundaryEdit& B = Document.Boundaries[b];

    Box->AddSlot().AutoHeight().Padding(0, 10, 0, 2)
    [ MakeSectionLabel(FString::Printf(TEXT("BOUNDARY #%d"), b), FLinearColor(0.40f, 0.70f, 1.0f)) ];

    Box->AddSlot().AutoHeight().Padding(0, 3) [ MakeBoolRow(TEXT("Main Boundary"), [this, b]() { return Document.Boundaries[b].bMainBoundary; }, [this, b](bool V) { Document.Boundaries[b].bMainBoundary = V; MarkDirtyRepaint(); }) ];
    Box->AddSlot().AutoHeight().Padding(0, 3) [ MakeBoolRow(TEXT("Visible"), [this, b]() { return Document.Boundaries[b].bVisible; }, [this, b](bool V) { Document.Boundaries[b].bVisible = V; MarkDirtyRepaint(); }) ];
    Box->AddSlot().AutoHeight().Padding(0, 3) [ MakeBoolRow(TEXT("Reverse"), [this, b]() { return Document.Boundaries[b].bReverse; }, [this, b](bool V) { Document.Boundaries[b].bReverse = V; MarkDirtyRepaint(); }) ];
    Box->AddSlot().AutoHeight().Padding(0, 3) [ MakeBoolRow(TEXT("Alert Limit"), [this, b]() { return Document.Boundaries[b].bAlertLimit; }, [this, b](bool V) { Document.Boundaries[b].bAlertLimit = V; MarkDirtyRepaint(); }) ];
    Box->AddSlot().AutoHeight().Padding(0, 3) [ MakeBoolRow(TEXT("More Visible"), [this, b]() { return Document.Boundaries[b].bBoundaryMoreVisible; }, [this, b](bool V) { Document.Boundaries[b].bBoundaryMoreVisible = V; MarkDirtyRepaint(); }) ];
    Box->AddSlot().AutoHeight().Padding(0, 3) [ MakeBoolRow(TEXT("Hide Line Far"), [this, b]() { return Document.Boundaries[b].bHideLineFar; }, [this, b](bool V) { Document.Boundaries[b].bHideLineFar = V; MarkDirtyRepaint(); }) ];
    Box->AddSlot().AutoHeight().Padding(0, 3) [ MakeFloatRow(TEXT("Display Dist."), [this, b]() { return Document.Boundaries[b].DisplayDistance; }, [this, b](float V) { Document.Boundaries[b].DisplayDistance = V; MarkDirtyRepaint(); }, 0.f, 1000.f) ];

    // Points
    Box->AddSlot().AutoHeight().Padding(0, 8, 0, 2)
    [ MakeSectionLabel(FString::Printf(TEXT("POINTS (%d)"), B.Points.Num()), FLinearColor(0.35f, 0.80f, 0.55f)) ];

    for (int32 i = 0; i < B.Points.Num(); ++i)
    {
        const bool bVS = (SelType == ESpatialSel::Vertex && SelV == i);
        Box->AddSlot().AutoHeight().Padding(0, 1)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [
                SNew(SBox).WidthOverride(34.f)
                [
                    SNew(SButton).Text(FText::FromString(FString::Printf(TEXT("#%d"), i)))
                    .ButtonColorAndOpacity(bVS ? FLinearColor(0.25f, 0.45f, 0.4f) : FLinearColor(0.12f, 0.12f, 0.13f))
                    .OnClicked_Lambda([this, b, i]() { Select(ESpatialSel::Vertex, b, i); return FReply::Handled(); })
                ]
            ]
            + SHorizontalBox::Slot().FillWidth(1.f).Padding(2, 0)
            [
                SNew(SSpinBox<float>).MinValue(-100000.f).MaxValue(100000.f).ToolTipText(LOCTEXT("PX", "X (m)"))
                .Value_Lambda([this, b, i]() { return Document.Boundaries.IsValidIndex(b) && Document.Boundaries[b].Points.IsValidIndex(i) ? (float)Document.Boundaries[b].Points[i].X : 0.f; })
                .OnValueChanged_Lambda([this, b, i](float V) { if (Document.Boundaries.IsValidIndex(b) && Document.Boundaries[b].Points.IsValidIndex(i)) { Document.Boundaries[b].Points[i].X = V; MarkDirtyRepaint(); } })
            ]
            + SHorizontalBox::Slot().FillWidth(1.f).Padding(2, 0)
            [
                SNew(SSpinBox<float>).MinValue(-100000.f).MaxValue(100000.f).ToolTipText(LOCTEXT("PZ", "Z (m)"))
                .Value_Lambda([this, b, i]() { return Document.Boundaries.IsValidIndex(b) && Document.Boundaries[b].Points.IsValidIndex(i) ? (float)Document.Boundaries[b].Points[i].Z : 0.f; })
                .OnValueChanged_Lambda([this, b, i](float V) { if (Document.Boundaries.IsValidIndex(b) && Document.Boundaries[b].Points.IsValidIndex(i)) { Document.Boundaries[b].Points[i].Z = V; MarkDirtyRepaint(); } })
            ]
            + SHorizontalBox::Slot().AutoWidth().Padding(2, 0, 0, 0)
            [
                SNew(SButton).Text(LOCTEXT("RemoveX", "X"))
                .OnClicked_Lambda([this, b, i]()
                {
                    if (Document.Boundaries.IsValidIndex(b) && Document.Boundaries[b].Points.Num() > 3)
                    { PushUndo(); Document.Boundaries[b].Points.RemoveAt(i); Select(ESpatialSel::Boundary, b); StructuralChanged(); }
                    return FReply::Handled();
                })
            ]
        ];
    }

    // Obstacles
    Box->AddSlot().AutoHeight().Padding(0, 8, 0, 2)
    [ MakeSectionLabel(FString::Printf(TEXT("OBSTACLES (%d)"), B.Obstacles.Num()), FLinearColor(0.95f, 0.6f, 0.2f)) ];

    for (int32 i = 0; i < B.Obstacles.Num(); ++i)
    {
        const bool bOS = (SelType == ESpatialSel::Obstacle && SelO == i);
        Box->AddSlot().AutoHeight().Padding(0, 1)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [
                SNew(SBox).WidthOverride(34.f)
                [
                    SNew(SButton).Text(FText::FromString(FString::Printf(TEXT("#%d"), i)))
                    .ButtonColorAndOpacity(bOS ? FLinearColor(0.45f, 0.3f, 0.1f) : FLinearColor(0.12f, 0.12f, 0.13f))
                    .OnClicked_Lambda([this, b, i]() { Select(ESpatialSel::Obstacle, b, -1, i); return FReply::Handled(); })
                ]
            ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2, 0)
            [
                SNew(SBox).WidthOverride(70.f)
                [
                    SNew(SSpinBox<int32>).MinValue(0).MaxValue(2).ToolTipText(LOCTEXT("Size", "Size 0=S 1=M 2=L"))
                    .Value_Lambda([this, b, i]() { return Document.Boundaries.IsValidIndex(b) && Document.Boundaries[b].Obstacles.IsValidIndex(i) ? Document.Boundaries[b].Obstacles[i].Size : 1; })
                    .OnValueChanged_Lambda([this, b, i](int32 V) { if (Document.Boundaries.IsValidIndex(b) && Document.Boundaries[b].Obstacles.IsValidIndex(i)) { Document.Boundaries[b].Obstacles[i].Size = V; MarkDirtyRepaint(); } })
                ]
            ]
            + SHorizontalBox::Slot().FillWidth(1.f).Padding(2, 0)
            [
                SNew(SSpinBox<float>).MinValue(0.01f).MaxValue(100.f).ToolTipText(LOCTEXT("Scale", "Scale"))
                .Value_Lambda([this, b, i]() { return Document.Boundaries.IsValidIndex(b) && Document.Boundaries[b].Obstacles.IsValidIndex(i) ? Document.Boundaries[b].Obstacles[i].Scale : 1.f; })
                .OnValueChanged_Lambda([this, b, i](float V) { if (Document.Boundaries.IsValidIndex(b) && Document.Boundaries[b].Obstacles.IsValidIndex(i)) { Document.Boundaries[b].Obstacles[i].Scale = V; MarkDirtyRepaint(); } })
            ]
            + SHorizontalBox::Slot().AutoWidth().Padding(2, 0, 0, 0)
            [
                SNew(SBox).WidthOverride(64.f)
                [
                    SNew(SSpinBox<int32>).MinValue(-1).ToolTipText(LOCTEXT("SpecialId", "SpecialId"))
                    .Value_Lambda([this, b, i]() { return Document.Boundaries.IsValidIndex(b) && Document.Boundaries[b].Obstacles.IsValidIndex(i) ? Document.Boundaries[b].Obstacles[i].SpecialId : -1; })
                    .OnValueChanged_Lambda([this, b, i](int32 V) { if (Document.Boundaries.IsValidIndex(b) && Document.Boundaries[b].Obstacles.IsValidIndex(i)) { Document.Boundaries[b].Obstacles[i].SpecialId = V; MarkDirtyRepaint(); } })
                ]
            ]
            + SHorizontalBox::Slot().AutoWidth().Padding(2, 0, 0, 0)
            [
                SNew(SButton).Text(LOCTEXT("RemoveX", "X"))
                .OnClicked_Lambda([this, b, i]()
                {
                    if (Document.Boundaries.IsValidIndex(b) && Document.Boundaries[b].Obstacles.IsValidIndex(i))
                    { PushUndo(); Document.Boundaries[b].Obstacles.RemoveAt(i); Select(ESpatialSel::Boundary, b); StructuralChanged(); }
                    return FReply::Handled();
                })
            ]
        ];
    }

    Box->AddSlot().AutoHeight().Padding(0, 4)
    [
        SNew(SButton).HAlign(HAlign_Center).Text(LOCTEXT("AddObstacle", "+ Obstacle"))
        .OnClicked_Lambda([this]() { AddObstacle(); return FReply::Handled(); })
    ];

    return Box;
}

TSharedRef<SWidget> SSpatialConfigEditor::BuildSyncSection()
{
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 2)
        [ MakeSectionLabel(TEXT("SYNC + MULTIPLIER"), FLinearColor(0.70f, 0.55f, 1.0f)) ]

        + SVerticalBox::Slot().AutoHeight().Padding(0, 3)
        [ MakeFloatRow(TEXT("Sync X (cm)"),
            [this]() { return (float)(Document.SyncPos.X * 100.0); },
            [this](float V) { Document.SyncPos.X = V / 100.0; MarkDirtyRepaint(); }, -1000000.f, 1000000.f) ]

        + SVerticalBox::Slot().AutoHeight().Padding(0, 3)
        [ MakeFloatRow(TEXT("Sync Z (cm)"),
            [this]() { return (float)(Document.SyncPos.Z * 100.0); },
            [this](float V) { Document.SyncPos.Z = V / 100.0; MarkDirtyRepaint(); }, -1000000.f, 1000000.f) ]

        + SVerticalBox::Slot().AutoHeight().Padding(0, 3)
        [ MakeFloatRow(TEXT("Rotation Y (°)"),
            [this]() { float y = FMath::Fmod(Document.SyncYawDeg, 360.f); return y < 0.f ? y + 360.f : y; },
            [this](float V) { Document.SyncYawDeg = V; const float h = FMath::DegreesToRadians(V) * 0.5f; Document.SyncQuat = FVector4(0.f, FMath::Sin(h), 0.f, FMath::Cos(h)); MarkDirtyRepaint(); }, 0.f, 360.f) ]

        + SVerticalBox::Slot().AutoHeight().Padding(0, 3)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
            [ SNew(SBox).WidthOverride(120.f) [ SNew(STextBlock).Text(LOCTEXT("MultPct", "Multiplier (%)")) ] ]
            + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
            [
                SNew(SSpinBox<float>).MinValue(0.f).MaxValue(300.f)
                .ToolTipText(LOCTEXT("MultTip", "Rééchelonne destructivement tous les points (parité Unity). Appliqué à la validation."))
                .Value_Lambda([this]() { return (float)(Document.Multiplier * 100.0); })
                .OnValueCommitted_Lambda([this](float V, ETextCommit::Type) { ApplyMultiplier((double)V / 100.0); })
            ]
        ];
}

TSharedRef<SWidget> SSpatialConfigEditor::BuildOrthoSection()
{
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 2)
        [ MakeSectionLabel(TEXT("ORTHO (fond)"), FLinearColor(0.95f, 0.6f, 0.2f)) ]

        + SVerticalBox::Slot().AutoHeight().Padding(0, 3)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth()
            [ SNew(SButton).Text(LOCTEXT("Browse", "Parcourir...")).OnClicked_Lambda([this]() { BrowseOrtho(); return FReply::Handled(); }) ]
            + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(6, 0, 0, 0)
            [ SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(OrthoPath.IsEmpty() ? TEXT("(aucune)") : FPaths::GetCleanFilename(OrthoPath)); })
                .ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.65f))) ]
        ]

        + SVerticalBox::Slot().AutoHeight().Padding(0, 3)
        [ MakeFloatRow(TEXT("Opacité"), [this]() { return OrthoOpacity; }, [this](float V) { OrthoOpacity = V; MarkDirtyRepaint(); }, 0.f, 1.f) ]

        + SVerticalBox::Slot().AutoHeight().Padding(0, 3)
        [ MakeFloatRow(TEXT("Largeur (m)"), [this]() { return OrthoHalfX; }, [this](float V) { OrthoHalfX = FMath::Max(0.01f, V); if (bHasOrtho && OrthoBrush.ImageSize.X > 0) OrthoHalfZ = OrthoHalfX * (OrthoBrush.ImageSize.Y / OrthoBrush.ImageSize.X); MarkDirtyRepaint(); }, 0.1f, 100000.f) ]

        + SVerticalBox::Slot().AutoHeight().Padding(0, 3)
        [ MakeFloatRow(TEXT("Centre X (m)"), [this]() { return (float)OrthoCenterXZ.X; }, [this](float V) { OrthoCenterXZ.X = V; MarkDirtyRepaint(); }, -100000.f, 100000.f) ]

        + SVerticalBox::Slot().AutoHeight().Padding(0, 3)
        [ MakeFloatRow(TEXT("Centre Z (m)"), [this]() { return (float)OrthoCenterXZ.Y; }, [this](float V) { OrthoCenterXZ.Y = V; MarkDirtyRepaint(); }, -100000.f, 100000.f) ];
}

TSharedRef<SWidget> SSpatialConfigEditor::BuildSidebar()
{
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight() [ BuildBoundaryListSection() ]
        + SVerticalBox::Slot().AutoHeight() [ BuildSelectionSection() ]
        + SVerticalBox::Slot().AutoHeight() [ BuildSyncSection() ]
        + SVerticalBox::Slot().AutoHeight() [ BuildOrthoSection() ];
}

void SSpatialConfigEditor::RebuildSidebar()
{
    if (SidebarContainer.IsValid())
    {
        SidebarContainer->SetContent(BuildSidebar());
    }
}

TSharedRef<SWidget> SSpatialConfigEditor::BuildToolbar()
{
    auto Toggle = [this](const FString& Label, bool* Flag, const FString& Tip)
    {
        return SNew(SCheckBox)
            .ToolTipText(FText::FromString(Tip))
            .IsChecked_Lambda([Flag]() { return *Flag ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda([this, Flag](ECheckBoxState S) { *Flag = (S == ECheckBoxState::Checked); if (Canvas.IsValid()) Canvas->Invalidate(EInvalidateWidgetReason::Paint); })
            [ SNew(STextBlock).Text(FText::FromString(Label)) ];
    };

    return SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0) [ Toggle(TEXT("Grille"), &bShowGrid, TEXT("Afficher la grille")) ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0) [ Toggle(TEXT("Repère"), &bShowRect, TEXT("Rectangle de référence 7x5.5 m")) ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0) [ Toggle(TEXT("Ortho"), &bShowOrtho, TEXT("Afficher le fond ortho")) ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 16, 0) [ Toggle(TEXT("Ghost"), &bShowGhost, TEXT("Afficher l'état sauvegardé (si non sauvegardé)")) ]

        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
        [ SNew(SButton).Text(LOCTEXT("Fit", "Cadrer")).OnClicked_Lambda([this]() { if (Canvas.IsValid()) Canvas->FrameAll(); return FReply::Handled(); }) ]

        + SHorizontalBox::Slot().FillWidth(1.f) [ SNew(SSpacer) ]

        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
        [
            SNew(STextBlock).Text(this, &SSpatialConfigEditor::GetStatusText)
            .ColorAndOpacity(this, &SSpatialConfigEditor::GetStatusColor)
            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
        ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
        [ SNew(SButton).Text(LOCTEXT("Reload", "Recharger")).OnClicked_Lambda([this]() { LoadFromDisk(); RebuildSidebar(); if (Canvas.IsValid()) { Canvas->FrameAll(); } return FReply::Handled(); }) ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
        [ SNew(SButton).Text(LOCTEXT("Save", "SAVE")).ButtonColorAndOpacity(FLinearColor(0.30f, 0.65f, 0.45f))
            .OnClicked_Lambda([this]() { SaveToDisk(); if (Canvas.IsValid()) Canvas->Invalidate(EInvalidateWidgetReason::Paint); return FReply::Handled(); }) ];
}

// ============================================================================
// SSpatialConfigEditor — Construct
// ============================================================================

void SSpatialConfigEditor::Construct(const FArguments& InArgs)
{
    LoadFromDisk();

    SAssignNew(SidebarContainer, SBox);

    ChildSlot
    [
        SNew(SBorder).Padding(8.f)
        [
            SNew(SVerticalBox)

            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [ SNew(STextBlock).Text(LOCTEXT("Title", "BOUNDARY EDITOR")).Font(FCoreStyle::GetDefaultFontStyle("Bold", 15)) ]
            ]

            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6) [ BuildToolbar() ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6) [ SNew(SSeparator) ]

            + SVerticalBox::Slot().FillHeight(1.f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.f).Padding(0, 0, 6, 0)
                [
                    SNew(SBorder).Padding(0.f)
                    [ SAssignNew(Canvas, SSpatialCanvas).Owner(this) ]
                ]
                + SHorizontalBox::Slot().AutoWidth()
                [
                    SNew(SBox).WidthOverride(330.f)
                    [
                        SNew(SScrollBox)
                        + SScrollBox::Slot() [ SidebarContainer.ToSharedRef() ]
                    ]
                ]
            ]

            + SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
            [
                SNew(STextBlock).Text(FText::FromString(SavePath)).AutoWrapText(true)
                .ColorAndOpacity(FSlateColor(FLinearColor(0.45f, 0.45f, 0.5f)))
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
            ]
        ]
    ];

    RebuildSidebar();
    if (Canvas.IsValid()) Canvas->FrameAll();
    Select(ESpatialSel::None, -1);
}

// ============================================================================
// Module / menu
// ============================================================================

FName FSpatialConfigEditorModule::TabName = FName("VaroniaBoundaryEditor");

void FSpatialConfigEditorModule::Register()
{
    UToolMenus* ToolMenus = UToolMenus::Get();
    if (!ToolMenus) return;

    UToolMenu* VaroniaSubMenu = ToolMenus->ExtendMenu("LevelEditor.MainMenu.Varonia");
    VaroniaSubMenu->AddMenuEntry(
        NAME_None,
        FToolMenuEntry::InitMenuEntry(
            "BoundaryEditor",
            LOCTEXT("MenuBoundary", "Boundary Editor"),
            LOCTEXT("MenuBoundaryTip", "Éditer NewSpatial.json (boundaries 2D)"),
            FSlateIcon(),
            FUIAction(FExecuteAction::CreateStatic(&FSpatialConfigEditorModule::OpenWindow))
        )
    );

    ToolMenus->RefreshAllWidgets();
}

void FSpatialConfigEditorModule::Unregister()
{
}

TSharedRef<SDockTab> FSpatialConfigEditorModule::SpawnTab(const FSpawnTabArgs& Args)
{
    return SNew(SDockTab).TabRole(ETabRole::NomadTab) [ SNew(SSpatialConfigEditor) ];
}

void FSpatialConfigEditorModule::OpenWindow()
{
    TSharedRef<SWindow> Window = SNew(SWindow)
        .Title(LOCTEXT("WindowTitle", "Varonia · Boundary Editor"))
        .ClientSize(FVector2D(1180, 760))
        .SupportsMinimize(true)
        .SupportsMaximize(true)
        [
            SNew(SSpatialConfigEditor)
        ];

    FSlateApplication::Get().AddWindow(Window);
}

#undef LOCTEXT_NAMESPACE
