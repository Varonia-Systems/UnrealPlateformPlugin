// OrthoCapture.h — outil Ortho View (capture en mémoire + filigrane + annotation paint).
#pragma once

#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Styling/SlateBrush.h"

class ASceneCapture2D;
class UTextureRenderTarget2D;
class UTexture2D;
class SDockTab;
class FSpawnTabArgs;
class SOrthoCapture;

enum class EOrthoMode : uint8 { Start, Setup, Paint };
enum class EOrthoTool : uint8 { Brush, Line, Rect, Eraser };

/**
 * Surface de dessin Slate : affiche la texture capturée (letterbox) et transmet
 * les events souris/clavier (peinture, undo) à l'éditeur Ortho.
 */
class SOrthoPaintSurface : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SOrthoPaintSurface) {}
        SLATE_ARGUMENT(SOrthoCapture*, Owner)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

    virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
        FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(480.f, 270.f); }

    virtual FReply OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& E) override;
    virtual FReply OnMouseMove(const FGeometry& Geo, const FPointerEvent& E) override;
    virtual FReply OnMouseButtonUp(const FGeometry& Geo, const FPointerEvent& E) override;
    virtual FReply OnKeyDown(const FGeometry& Geo, const FKeyEvent& E) override;
    virtual bool SupportsKeyboardFocus() const override { return true; }
    virtual FCursorReply OnCursorQuery(const FGeometry& Geo, const FPointerEvent& E) const override;

private:
    SOrthoCapture* Owner = nullptr;

    /** Rect d'affichage de l'image (letterbox) dans la géométrie locale. */
    void GetFitRect(const FGeometry& Geo, FVector2D& OutOffset, FVector2D& OutSize) const;
    bool LocalToPixel(const FGeometry& Geo, const FVector2D& LocalPos, FIntPoint& OutPx) const;
};

/**
 * Éditeur Ortho : Start → Setup (zoom/altitude, preview live) → Paint (annotation) → Save.
 */
class SOrthoCapture : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SOrthoCapture) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    virtual ~SOrthoCapture();

    // --- API appelée par la surface de dessin ---
    void PointerDown(FIntPoint Px);
    void PointerMove(FIntPoint Px);
    void PointerUp(FIntPoint Px);
    void PerformUndo();

    UTexture2D* GetWorkTexture() const { return WorkTexture.Get(); }
    const FSlateBrush* GetImageBrush() const { return &ImageBrush; }
    int32 GetImgW() const { return ImgW; }
    int32 GetImgH() const { return ImgH; }

private:
    // --- State ---
    EOrthoMode Mode = EOrthoMode::Start;
    float OrthoSize = 10.f;
    float CameraHeight = 50.f;

    TWeakObjectPtr<ASceneCapture2D> CaptureActor;
    TStrongObjectPtr<UTextureRenderTarget2D> RenderTarget;
    TStrongObjectPtr<UTexture2D> WorkTexture;
    FSlateBrush ImageBrush;

    int32 ImgW = 1920;
    int32 ImgH = 1080;
    TArray<FColor> Pixels;          // image courante (BGRA)
    TArray<FColor> OriginalPixels;  // pour la gomme

    // Paint
    EOrthoTool Tool = EOrthoTool::Brush;
    FLinearColor BrushColor = FLinearColor::Red;
    int32 BrushSize = 8;

    bool bStroking = false;
    FIntPoint LastPx = FIntPoint::ZeroValue;
    bool bShapeDragging = false;
    FIntPoint ShapeStart = FIntPoint::ZeroValue;
    TArray<FColor> ShapeBase;
    TArray<TArray<FColor>> UndoStack;
    static constexpr int32 MaxUndo = 20;

    TSharedPtr<SOrthoPaintSurface> PaintSurface;

    // --- UI ---
    void RebuildUI();
    TSharedRef<SWidget> BuildStartUI();
    TSharedRef<SWidget> BuildSetupUI();
    TSharedRef<SWidget> BuildPaintUI();

    // --- Logic ---
    UWorld* GetEditorWorld() const;
    void EnsureCapture();
    void UpdateCapture();          // (re)rend la scène dans le RT (preview)
    void CaptureToBuffer();        // RT + filigrane → Pixels → WorkTexture → mode Paint
    void RebuildWorkTextureFromPixels();
    void UpdateWorkTexture();      // push Pixels → WorkTexture (incrémental)
    void Cleanup();

    void PushUndo();
    void StampCircle(TArray<FColor>& Buf, FIntPoint C, FColor Col, int32 R);
    void StampCircleRestore(TArray<FColor>& Buf, FIntPoint C, int32 R);
    void StampLine(TArray<FColor>& Buf, FIntPoint A, FIntPoint B, bool bRestore);
    void StampRectOutline(TArray<FColor>& Buf, FIntPoint A, FIntPoint B);

    void Save(bool bSaveAs);
    void Retake();

    FString BuildWatermarkText() const;
    FString CurrentSceneName() const;

    // tool button helper
    TSharedRef<SWidget> MakeToolButton(EOrthoTool InTool, const FString& Label, const FString& Tip);
};

/** Enregistrement du menu "Varonia > Ortho View" + ouverture de la fenêtre. */
class FOrthoCaptureModeModule
{
public:
    static void Register();
    static void Unregister();
    static void OpenWindow();

    static FName TabName;

private:
    static TSharedRef<SDockTab> SpawnTab(const FSpawnTabArgs& Args);
};
