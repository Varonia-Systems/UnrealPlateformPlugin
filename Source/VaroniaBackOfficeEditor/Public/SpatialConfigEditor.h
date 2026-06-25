// SpatialConfigEditor.h — éditeur 2D de boundary (équivalent SpatialConfigEditor2D Unity).
// Vue de dessus du plan Unity (X horizontal, Z vers le haut, mètres). Édite NewSpatial.json
// EN CONVENTION UNITY (pas de conversion Unreal : le runtime C++ re-convertit à la lecture).
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Styling/SlateBrush.h"
#include "UObject/StrongObjectPtr.h"

class FJsonObject;
class SDockTab;
class FSpawnTabArgs;
class SVerticalBox;
class SBox;
class UTexture2D;
class SSpatialConfigEditor;

// ----------------------------------------------------------------------------
// Modèle d'édition (convention Unity : mètres, axes X=right / Y=up / Z=forward)
// ----------------------------------------------------------------------------

struct FObstacleEdit
{
    FVector Position = FVector::ZeroVector;  // local (x,y,z) mètres ; plan = (x,z)
    FVector Rotation = FVector::ZeroVector;  // euler degrés (x,y,z) local
    int32   Size      = 1;                   // 0=Small 1=Medium 2=Large
    float   Scale     = 1.f;
    int32   SpecialId = -1;
};

struct FBoundaryEdit
{
    FString               ID;
    TArray<FVector>       Points;            // local (x,y,z) mètres ; plan = (x,z)
    FLinearColor          Color = FLinearColor(0.30f, 0.85f, 0.65f, 1.f);
    TArray<FObstacleEdit> Obstacles;
    bool  bAlertLimit          = false;
    bool  bBoundaryMoreVisible = false;
    bool  bMainBoundary        = false;
    bool  bReverse             = false;
    bool  bHideLineFar         = false;      // champ Unity
    bool  bVisible             = true;       // champ lu par le runtime C++
    float DisplayDistance      = 3.f;
};

struct FSpatialEdit
{
    FVector  SyncPos    = FVector::ZeroVector;       // monde (x,y,z) mètres
    FVector4 SyncQuat   = FVector4(0.f, 0.f, 0.f, 1.f); // brut (x,y,z,w) Unity
    float    SyncYawDeg = 0.f;                        // dérivé de SyncQuat (édition)
    double   Multiplier = 0.0;
    TArray<FBoundaryEdit> Boundaries;
};

enum class ESpatialSel : uint8 { None, Boundary, Vertex, Obstacle, SyncPos };

// ----------------------------------------------------------------------------
// Canvas 2D (dessin + interactions souris/clavier)
// ----------------------------------------------------------------------------

class SSpatialCanvas : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SSpatialCanvas) {}
        SLATE_ARGUMENT(SSpatialConfigEditor*, Owner)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

    virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
        FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(640.f, 480.f); }

    virtual FReply OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& E) override;
    virtual FReply OnMouseMove(const FGeometry& Geo, const FPointerEvent& E) override;
    virtual FReply OnMouseButtonUp(const FGeometry& Geo, const FPointerEvent& E) override;
    virtual FReply OnMouseWheel(const FGeometry& Geo, const FPointerEvent& E) override;
    virtual FReply OnKeyDown(const FGeometry& Geo, const FKeyEvent& E) override;
    virtual bool SupportsKeyboardFocus() const override { return true; }
    virtual FCursorReply OnCursorQuery(const FGeometry& Geo, const FPointerEvent& E) const override;

    /** Cadre toutes les boundaries dans la vue. */
    void FrameAll();
    void CenterOnWorld(const FVector2D& WorldXZ);

    float    GetZoom() const { return Zoom; }
    FVector2D GetPan() const { return Pan; }

private:
    SSpatialConfigEditor* Owner = nullptr;

    // Vue
    float     Zoom = 30.f;                 // px / mètre
    FVector2D Pan  = FVector2D::ZeroVector; // point monde (X,Z) au centre
    mutable FVector2D LastViewportSize = FVector2D(640.f, 480.f);

    // Drag
    enum class EDrag : uint8 { None, Pan, Vertex, Obstacle, Boundary, SyncPos, Rot };
    EDrag      Drag = EDrag::None;
    FVector2D  DragStartWorld = FVector2D::ZeroVector;  // (X,Z) monde au mousedown
    FVector2D  DragStartScreen = FVector2D::ZeroVector;
    FVector2D  PanStart = FVector2D::ZeroVector;
    // Snapshots locaux (au mousedown) pour un drag stable
    FVector2D  SnapVertexLocal = FVector2D::ZeroVector;
    FVector2D  SnapObstacleLocal = FVector2D::ZeroVector;
    TArray<FVector2D> SnapBoundaryLocal;
    FVector2D  SnapSyncPos = FVector2D::ZeroVector;
    FVector2D  MouseWorld = FVector2D::ZeroVector; // pour le HUD

    // Conversions
    FVector2D WorldToScreen(const FVector2D& W) const;
    FVector2D ScreenToWorld(const FVector2D& S) const;
    FVector2D LocalToWorld2D(const FVector2D& L) const; // point boundary local → plan monde
    FVector2D WorldToLocal2D(const FVector2D& W) const;
    static FVector2D PointXZ(const FVector& P) { return FVector2D(P.X, P.Z); }
};

// ----------------------------------------------------------------------------
// Éditeur hôte (toolbar + canvas + sidebar)
// ----------------------------------------------------------------------------

class SSpatialConfigEditor : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SSpatialConfigEditor) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

    // --- API utilisée par le canvas ---
    FSpatialEdit&       Doc()       { return Document; }
    const FSpatialEdit& Doc() const { return Document; }

    void PushUndo();
    void Undo();                 // restaure le dernier snapshot
    void MarkDirtyRepaint();     // dirty + repaint canvas (édition de valeur, pas de structure)
    void StructuralChanged();    // dirty + reconstruit la sidebar + repaint
    void Select(ESpatialSel Type, int32 B, int32 V = -1, int32 O = -1);

    // Sélection (lue par le canvas)
    ESpatialSel SelType = ESpatialSel::None;
    int32 SelB = -1, SelV = -1, SelO = -1;

    // Options d'affichage (lues par le canvas)
    bool bShowGrid  = true;
    bool bShowRect  = true;
    bool bShowOrtho = true;
    bool bShowGhost = true;

    // Underlay ortho (lu par le canvas)
    bool          bHasOrtho   = false;
    FSlateBrush   OrthoBrush;
    float         OrthoOpacity = 0.55f;
    FVector2D     OrthoCenterXZ = FVector2D::ZeroVector; // centre monde (X,Z) mètres
    float         OrthoHalfX = 10.f;                     // demi-largeur (X) mètres
    float         OrthoHalfZ = 10.f;                     // demi-hauteur (Z) mètres

    // Ghost (dernier état sauvegardé)
    bool                  bHasGhost = false;
    FSpatialEdit          Ghost;

    bool IsDirty() const { return bDirty; }

private:
    // --- État ---
    FSpatialEdit Document;
    FString      SavePath;
    bool         bDirty = false;
    bool         bFileExists = false;
    TSharedPtr<FJsonObject> LoadedRoot;            // préserve les clés racine inconnues
    TArray<FSpatialEdit> UndoStack;
    static constexpr int32 MaxUndo = 64;

    double LastMultiplier = 0.0;                    // pour le delta-tracking destructif

    // Ortho
    TStrongObjectPtr<UTexture2D> OrthoTexture;
    FString OrthoPath;

    TSharedPtr<SSpatialCanvas> Canvas;
    TSharedPtr<SBox> SidebarContainer;

    // --- IO ---
    static FString GetSpatialPath();
    void LoadFromDisk();
    void SaveToDisk();
    void CaptureGhost();

    // --- UI ---
    void RebuildSidebar();
    TSharedRef<SWidget> BuildToolbar();
    TSharedRef<SWidget> BuildSidebar();
    TSharedRef<SWidget> BuildBoundaryListSection();
    TSharedRef<SWidget> BuildSelectionSection();
    TSharedRef<SWidget> BuildSyncSection();
    TSharedRef<SWidget> BuildOrthoSection();

    // Row helpers (style GlobalConfig)
    TSharedRef<SWidget> MakeSectionLabel(const FString& Text, FLinearColor Color);
    TSharedRef<SWidget> MakeFloatRow(const FString& Label, TFunction<float()> Get, TFunction<void(float)> Set, float Min, float Max);
    TSharedRef<SWidget> MakeBoolRow(const FString& Label, TFunction<bool()> Get, TFunction<void(bool)> Set);

    // Actions
    void AddBoundary();
    void RemoveBoundary(int32 Index);
    void AddObstacle();
    void BrowseOrtho();
    void RebuildOrthoBrush();
    void ApplyMultiplier(double NewValue);

    FText      GetStatusText() const;
    FSlateColor GetStatusColor() const;

    friend class SSpatialCanvas;
};

// ----------------------------------------------------------------------------
// Module / menu "Varonia > Boundary Editor"
// ----------------------------------------------------------------------------

class FSpatialConfigEditorModule
{
public:
    static void Register();
    static void Unregister();
    static void OpenWindow();

    static FName TabName;

private:
    static TSharedRef<SDockTab> SpawnTab(const FSpawnTabArgs& Args);
};
