// OrthoCapture.cpp — outil Ortho View : capture en mémoire + filigrane + annotation paint.

#include "OrthoCapture.h"

#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "Engine/SceneCapture2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "Engine/Canvas.h"
#include "TextureResource.h"
#include "RHI.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"
#include "HAL/FileManager.h"
#include "Modules/ModuleManager.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "ToolMenus.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Colors/SColorPicker.h"
#include "Widgets/Docking/SDockTab.h"
#include "Styling/CoreStyle.h"

#define LOCTEXT_NAMESPACE "OrthoCapture"

// =============================================================================
// SOrthoPaintSurface
// =============================================================================

void SOrthoPaintSurface::Construct(const FArguments& InArgs)
{
    Owner = InArgs._Owner;
}

void SOrthoPaintSurface::GetFitRect(const FGeometry& Geo, FVector2D& OutOffset, FVector2D& OutSize) const
{
    const FVector2D Local = Geo.GetLocalSize();
    const float ImgAspect = Owner ? (float)Owner->GetImgW() / FMath::Max(1, Owner->GetImgH()) : (16.f / 9.f);
    const float A = Local.Y > 0.f ? Local.X / Local.Y : 1.f;

    FVector2D Sz;
    if (ImgAspect >= A) { Sz.X = Local.X; Sz.Y = Local.X / ImgAspect; }
    else                { Sz.Y = Local.Y; Sz.X = Local.Y * ImgAspect; }

    OutSize = Sz;
    OutOffset = FVector2D((Local.X - Sz.X) * 0.5f, (Local.Y - Sz.Y) * 0.5f);
}

bool SOrthoPaintSurface::LocalToPixel(const FGeometry& Geo, const FVector2D& LocalPos, FIntPoint& OutPx) const
{
    if (!Owner) return false;
    FVector2D Off, Sz; GetFitRect(Geo, Off, Sz);
    if (Sz.X <= 0.f || Sz.Y <= 0.f) return false;
    const float nx = (LocalPos.X - Off.X) / Sz.X;
    const float ny = (LocalPos.Y - Off.Y) / Sz.Y;
    if (nx < 0.f || nx > 1.f || ny < 0.f || ny > 1.f) return false;
    OutPx.X = FMath::Clamp((int32)(nx * (Owner->GetImgW() - 1)), 0, Owner->GetImgW() - 1);
    OutPx.Y = FMath::Clamp((int32)(ny * (Owner->GetImgH() - 1)), 0, Owner->GetImgH() - 1);
    return true;
}

int32 SOrthoPaintSurface::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
    FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
    const FSlateBrush* Brush = Owner ? Owner->GetImageBrush() : nullptr;
    if (Brush && Brush->GetResourceObject())
    {
        FVector2D Off, Sz; GetFitRect(AllottedGeometry, Off, Sz);
        FSlateDrawElement::MakeBox(
            OutDrawElements, LayerId,
            AllottedGeometry.ToPaintGeometry(FVector2f((float)Sz.X, (float)Sz.Y), FSlateLayoutTransform(1.0f, FVector2f((float)Off.X, (float)Off.Y))),
            Brush, ESlateDrawEffect::None, FLinearColor::White);
    }
    return LayerId + 1;
}

FReply SOrthoPaintSurface::OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& E)
{
    if (E.GetEffectingButton() != EKeys::LeftMouseButton || !Owner) return FReply::Unhandled();
    FIntPoint Px;
    if (LocalToPixel(Geo, Geo.AbsoluteToLocal(E.GetScreenSpacePosition()), Px))
    {
        Owner->PointerDown(Px);
        return FReply::Handled().CaptureMouse(AsShared()).SetUserFocus(AsShared(), EFocusCause::Mouse);
    }
    return FReply::Unhandled();
}

FReply SOrthoPaintSurface::OnMouseMove(const FGeometry& Geo, const FPointerEvent& E)
{
    if (HasMouseCapture() && Owner)
    {
        FIntPoint Px;
        if (LocalToPixel(Geo, Geo.AbsoluteToLocal(E.GetScreenSpacePosition()), Px))
        {
            Owner->PointerMove(Px);
        }
        return FReply::Handled();
    }
    return FReply::Unhandled();
}

FReply SOrthoPaintSurface::OnMouseButtonUp(const FGeometry& Geo, const FPointerEvent& E)
{
    if (Owner) Owner->PointerUp(FIntPoint::ZeroValue);
    return FReply::Handled().ReleaseMouseCapture();
}

FReply SOrthoPaintSurface::OnKeyDown(const FGeometry& Geo, const FKeyEvent& E)
{
    if (Owner && E.GetKey() == EKeys::Z && E.IsControlDown())
    {
        Owner->PerformUndo();
        return FReply::Handled();
    }
    return FReply::Unhandled();
}

FCursorReply SOrthoPaintSurface::OnCursorQuery(const FGeometry& Geo, const FPointerEvent& E) const
{
    return FCursorReply::Cursor(EMouseCursor::Crosshairs);
}

// =============================================================================
// SOrthoCapture — lifecycle
// =============================================================================

void SOrthoCapture::Construct(const FArguments& InArgs)
{
    ImageBrush.DrawAs = ESlateBrushDrawType::Image;
    ImageBrush.ImageSize = FVector2D(ImgW, ImgH);
    RebuildUI();
}

SOrthoCapture::~SOrthoCapture()
{
    Cleanup();
    WorkTexture.Reset();
    RenderTarget.Reset();
}

UWorld* SOrthoCapture::GetEditorWorld() const
{
    return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
}

void SOrthoCapture::Cleanup()
{
    if (CaptureActor.IsValid())
    {
        CaptureActor->Destroy();
        CaptureActor.Reset();
    }
}

// =============================================================================
// Capture
// =============================================================================

void SOrthoCapture::EnsureCapture()
{
    UWorld* World = GetEditorWorld();
    if (!World) return;

    if (!RenderTarget.IsValid())
    {
        UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>(GetTransientPackage());
        RT->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
        RT->ClearColor = FLinearColor(0.f, 0.f, 0.f, 1.f);
        RT->bAutoGenerateMips = false;
        RT->InitAutoFormat(ImgW, ImgH);
        RT->UpdateResourceImmediate(true);
        RenderTarget.Reset(RT);
    }

    if (!CaptureActor.IsValid())
    {
        FActorSpawnParameters P;
        P.ObjectFlags |= RF_Transient;
        ASceneCapture2D* Cap = World->SpawnActor<ASceneCapture2D>(FVector(0.f, 0.f, CameraHeight), FRotator(-90.f, 90.f, 0.f), P);
        if (Cap)
        {
            if (USceneCaptureComponent2D* C = Cap->GetCaptureComponent2D())
            {
                C->ProjectionType = ECameraProjectionMode::Orthographic;
                C->OrthoWidth = OrthoSize * 355.f;
                C->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
                C->TextureTarget = RenderTarget.Get();
                C->bCaptureEveryFrame = false;
                C->bCaptureOnMovement = false;
            }
#if WITH_EDITOR
            Cap->SetActorLabel(TEXT("TempOrthoCapture"));
#endif
            CaptureActor = Cap;
        }
    }

    ImageBrush.SetResourceObject(RenderTarget.Get());
    ImageBrush.DrawAs = ESlateBrushDrawType::Image;
    ImageBrush.ImageSize = FVector2D(ImgW, ImgH);
}

void SOrthoCapture::UpdateCapture()
{
    EnsureCapture();
    if (!CaptureActor.IsValid()) return;

    CaptureActor->SetActorLocation(FVector(0.f, 0.f, CameraHeight));
    CaptureActor->SetActorRotation(FRotator(-90.f, 90.f, 0.f));
    if (USceneCaptureComponent2D* C = CaptureActor->GetCaptureComponent2D())
    {
        C->OrthoWidth = OrthoSize * 355.f;
        C->TextureTarget = RenderTarget.Get();
        C->CaptureScene();
    }
}

FString SOrthoCapture::CurrentSceneName() const
{
    if (UWorld* World = GetEditorWorld())
    {
        FString Name = World->GetMapName();
        Name.RemoveFromStart(World->StreamingLevelsPrefix);
        if (!Name.IsEmpty()) return Name;
    }
    return TEXT("UntitledMap");
}

FString SOrthoCapture::BuildWatermarkText() const
{
    return FString::Printf(TEXT("%s    -    Zoom %d    -    %s"),
        *CurrentSceneName(),
        FMath::RoundToInt(OrthoSize),
        *FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M")));
}

void SOrthoCapture::CaptureToBuffer()
{
    UpdateCapture();
    UWorld* World = GetEditorWorld();
    if (!RenderTarget.IsValid() || !World) return;

    // --- Filigrane (Canvas → RT) ---
    UCanvas* Canvas = nullptr;
    FVector2D CanvasSize;
    FDrawToRenderTargetContext Ctx;
    UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(World, RenderTarget.Get(), Canvas, CanvasSize, Ctx);
    if (Canvas)
    {
        UFont* Font = GEngine ? GEngine->GetLargeFont() : nullptr;
        const FString WM = BuildWatermarkText();
        const float Scale = 1.6f;
        float TW = 0.f, TH = 0.f;
        if (Font) Canvas->TextSize(Font, WM, TW, TH, Scale, Scale);

        const float PadX = 18.f, PadY = 12.f, Margin = 24.f;
        const float BoxW = TW + PadX * 2.f;
        const float BoxH = TH + PadY * 2.f;
        const float BoxX = Margin;
        const float BoxY = ImgH - Margin - BoxH;

        Canvas->K2_DrawTexture(nullptr, FVector2D(BoxX, BoxY), FVector2D(BoxW, BoxH), FVector2D(0, 0), FVector2D(1, 1),
            FLinearColor(0.06f, 0.06f, 0.08f, 0.72f), EBlendMode::BLEND_Translucent);

        if (Font)
        {
            Canvas->K2_DrawText(Font, WM, FVector2D(BoxX + PadX, BoxY + PadY), FVector2D(Scale, Scale),
                FLinearColor::White, 0.f, FLinearColor(0, 0, 0, 0.6f), FVector2D(1, 1), false, false, false, FLinearColor::Black);
        }
    }
    UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(World, Ctx);

    // --- Lecture pixels ---
    FTextureRenderTargetResource* RTRes = RenderTarget->GameThread_GetRenderTargetResource();
    Pixels.Reset();
    if (RTRes) RTRes->ReadPixels(Pixels); // flags par défaut : octets corrects pour le JPG
    if (Pixels.Num() != ImgW * ImgH) Pixels.Init(FColor(20, 20, 25, 255), ImgW * ImgH);

    // Le RT RTF_RGBA8 stocke en LINÉAIRE ; ReadPixels renvoie ce linéaire brut (sombre si
    // sauvé/affiché tel quel, alors que l'aperçu Slate du RT applique le gamma). On encode
    // donc en sRGB ici → aperçu paint ET JPG alignés sur le viewport.
    for (FColor& C : Pixels)
    {
        C = FLinearColor(C.R / 255.f, C.G / 255.f, C.B / 255.f).ToFColor(true);
        C.A = 255;
    }

    OriginalPixels = Pixels;
    UndoStack.Reset();
    bStroking = bShapeDragging = false;

    RebuildWorkTextureFromPixels();
    Mode = EOrthoMode::Paint;
    RebuildUI();
}

void SOrthoCapture::RebuildWorkTextureFromPixels()
{
    UTexture2D* Tex = UTexture2D::CreateTransient(ImgW, ImgH, PF_B8G8R8A8);
    if (!Tex) return;
    Tex->SRGB = true; // Pixels sont désormais en sRGB (encodés après ReadPixels)
#if WITH_EDITORONLY_DATA
    Tex->MipGenSettings = TMGS_NoMipmaps;
#endif
    FTexture2DMipMap& Mip = Tex->GetPlatformData()->Mips[0];
    void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
    FMemory::Memcpy(Data, Pixels.GetData(), (SIZE_T)ImgW * ImgH * sizeof(FColor));
    Mip.BulkData.Unlock();
    Tex->UpdateResource();

    WorkTexture.Reset(Tex);
    ImageBrush.SetResourceObject(Tex);
    ImageBrush.DrawAs = ESlateBrushDrawType::Image;
    ImageBrush.ImageSize = FVector2D(ImgW, ImgH);
}

void SOrthoCapture::UpdateWorkTexture()
{
    UTexture2D* Tex = WorkTexture.Get();
    if (!Tex || Pixels.Num() != ImgW * ImgH) return;

    FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, ImgW, ImgH);
    Tex->UpdateTextureRegions(0, 1, Region, (uint32)(ImgW * sizeof(FColor)), (uint32)sizeof(FColor),
        (uint8*)Pixels.GetData(),
        [](uint8*, const FUpdateTextureRegion2D* R) { delete R; });

    if (PaintSurface.IsValid()) PaintSurface->Invalidate(EInvalidateWidgetReason::Paint);
}

// =============================================================================
// Painting
// =============================================================================

void SOrthoCapture::PushUndo()
{
    UndoStack.Add(Pixels);
    if (UndoStack.Num() > MaxUndo) UndoStack.RemoveAt(0);
}

void SOrthoCapture::PerformUndo()
{
    if (UndoStack.Num() == 0) return;
    Pixels = UndoStack.Last();
    UndoStack.Pop();
    bShapeDragging = bStroking = false;
    UpdateWorkTexture();
}

void SOrthoCapture::StampCircle(TArray<FColor>& Buf, FIntPoint C, FColor Col, int32 R)
{
    R = FMath::Max(1, R);
    for (int32 dy = -R; dy <= R; ++dy)
    for (int32 dx = -R; dx <= R; ++dx)
    {
        if (dx * dx + dy * dy > R * R) continue;
        const int32 x = FMath::Clamp(C.X + dx, 0, ImgW - 1);
        const int32 y = FMath::Clamp(C.Y + dy, 0, ImgH - 1);
        Buf[y * ImgW + x] = Col;
    }
}

void SOrthoCapture::StampCircleRestore(TArray<FColor>& Buf, FIntPoint C, int32 R)
{
    if (OriginalPixels.Num() != Buf.Num()) return;
    R = FMath::Max(1, R);
    for (int32 dy = -R; dy <= R; ++dy)
    for (int32 dx = -R; dx <= R; ++dx)
    {
        if (dx * dx + dy * dy > R * R) continue;
        const int32 x = FMath::Clamp(C.X + dx, 0, ImgW - 1);
        const int32 y = FMath::Clamp(C.Y + dy, 0, ImgH - 1);
        Buf[y * ImgW + x] = OriginalPixels[y * ImgW + x];
    }
}

void SOrthoCapture::StampLine(TArray<FColor>& Buf, FIntPoint A, FIntPoint B, bool bRestore)
{
    const FColor Col = BrushColor.ToFColor(true);
    int32 x0 = A.X, y0 = A.Y, x1 = B.X, y1 = B.Y;
    const int32 dx = FMath::Abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    const int32 dy = -FMath::Abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int32 err = dx + dy;
    while (true)
    {
        if (bRestore) StampCircleRestore(Buf, FIntPoint(x0, y0), BrushSize);
        else          StampCircle(Buf, FIntPoint(x0, y0), Col, BrushSize);
        if (x0 == x1 && y0 == y1) break;
        const int32 e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void SOrthoCapture::StampRectOutline(TArray<FColor>& Buf, FIntPoint A, FIntPoint B)
{
    const FColor Col = BrushColor.ToFColor(true);
    const int32 x0 = FMath::Min(A.X, B.X), y0 = FMath::Min(A.Y, B.Y);
    const int32 x1 = FMath::Max(A.X, B.X), y1 = FMath::Max(A.Y, B.Y);
    for (int32 x = x0; x <= x1; ++x) { StampCircle(Buf, FIntPoint(x, y0), Col, BrushSize); StampCircle(Buf, FIntPoint(x, y1), Col, BrushSize); }
    for (int32 y = y0; y <= y1; ++y) { StampCircle(Buf, FIntPoint(x0, y), Col, BrushSize); StampCircle(Buf, FIntPoint(x1, y), Col, BrushSize); }
}

void SOrthoCapture::PointerDown(FIntPoint Px)
{
    if (Pixels.Num() != ImgW * ImgH) return;
    PushUndo();
    const FColor Col = BrushColor.ToFColor(true);
    switch (Tool)
    {
    case EOrthoTool::Brush:  bStroking = true; LastPx = Px; StampCircle(Pixels, Px, Col, BrushSize); UpdateWorkTexture(); break;
    case EOrthoTool::Eraser: bStroking = true; LastPx = Px; StampCircleRestore(Pixels, Px, BrushSize); UpdateWorkTexture(); break;
    case EOrthoTool::Line:
    case EOrthoTool::Rect:   bShapeDragging = true; ShapeStart = Px; ShapeBase = Pixels; break;
    }
}

void SOrthoCapture::PointerMove(FIntPoint Px)
{
    if (Pixels.Num() != ImgW * ImgH) return;
    if (bStroking && Tool == EOrthoTool::Brush)  { StampLine(Pixels, LastPx, Px, false); LastPx = Px; UpdateWorkTexture(); }
    else if (bStroking && Tool == EOrthoTool::Eraser) { StampLine(Pixels, LastPx, Px, true); LastPx = Px; UpdateWorkTexture(); }
    else if (bShapeDragging)
    {
        Pixels = ShapeBase;
        if (Tool == EOrthoTool::Line) StampLine(Pixels, ShapeStart, Px, false);
        else                          StampRectOutline(Pixels, ShapeStart, Px);
        UpdateWorkTexture();
    }
}

void SOrthoCapture::PointerUp(FIntPoint Px)
{
    bStroking = false;
    bShapeDragging = false;
}

// =============================================================================
// Save / Retake
// =============================================================================

void SOrthoCapture::Save(bool bSaveAs)
{
    if (Pixels.Num() != ImgW * ImgH) return;

    FString Dir = FPaths::ProjectDir();
    const FString File = FString::Printf(TEXT("%s_%d.jpg"), *CurrentSceneName(), FMath::RoundToInt(OrthoSize));

    // Sous-dossier GameID (parité Unity), si présent.
    FString Gid;
    const FString GidPath = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("StreamingAssets"), TEXT("GameID.txt"));
    if (FFileHelper::LoadFileToString(Gid, *GidPath))
    {
        Gid = Gid.TrimStartAndEnd();
        if (!Gid.IsEmpty())
        {
            Dir = FPaths::Combine(Dir, Gid);
            IFileManager::Get().MakeDirectory(*Dir, true);
        }
    }

    FString FullPath = FPaths::Combine(Dir, File);

    if (bSaveAs)
    {
        IDesktopPlatform* DP = FDesktopPlatformModule::Get();
        if (!DP) return;
        void* WindowHandle = nullptr;
        TSharedPtr<SWindow> Win = FSlateApplication::Get().FindWidgetWindow(AsShared());
        if (Win.IsValid() && Win->GetNativeWindow().IsValid()) WindowHandle = Win->GetNativeWindow()->GetOSWindowHandle();

        TArray<FString> OutFiles;
        const bool bOk = DP->SaveFileDialog(WindowHandle, TEXT("Save Ortho Capture As"), Dir, File,
            TEXT("JPEG Image|*.jpg"), EFileDialogFlags::None, OutFiles);
        if (!bOk || OutFiles.Num() == 0) return; // annulé → reste en paint
        FullPath = OutFiles[0];
    }

    IImageWrapperModule& Mod = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
    TSharedPtr<IImageWrapper> Wrapper = Mod.CreateImageWrapper(EImageFormat::JPEG);
    if (Wrapper.IsValid() &&
        Wrapper->SetRaw(Pixels.GetData(), (int64)Pixels.Num() * sizeof(FColor), ImgW, ImgH, ERGBFormat::BGRA, 8))
    {
        const TArray64<uint8>& Compressed = Wrapper->GetCompressed(95);
        TArray<uint8> Bytes((const uint8*)Compressed.GetData(), (int32)Compressed.Num());
        if (FFileHelper::SaveArrayToFile(Bytes, *FullPath))
        {
            UE_LOG(LogTemp, Log, TEXT("[Ortho] Saved -> %s"), *FullPath);
        }
    }

    Cleanup();
    TSharedPtr<SWindow> ParentWindow = FSlateApplication::Get().FindWidgetWindow(AsShared());
    if (ParentWindow.IsValid()) ParentWindow->RequestDestroyWindow();
}

void SOrthoCapture::Retake()
{
    UndoStack.Reset();
    bStroking = bShapeDragging = false;
    Mode = EOrthoMode::Setup;
    UpdateCapture();
    RebuildUI();
}

// =============================================================================
// UI
// =============================================================================

void SOrthoCapture::RebuildUI()
{
    TSharedRef<SWidget> Content = SNullWidget::NullWidget;
    switch (Mode)
    {
    case EOrthoMode::Start: Content = BuildStartUI(); break;
    case EOrthoMode::Setup: UpdateCapture(); Content = BuildSetupUI(); break;
    case EOrthoMode::Paint: Content = BuildPaintUI(); break;
    }

    ChildSlot
    [
        SNew(SBorder).Padding(10.f) [ Content ]
    ];
}

TSharedRef<SWidget> SOrthoCapture::BuildStartUI()
{
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
        [ SNew(STextBlock).Text(LOCTEXT("Title", "ORTHO CAPTURE")).Font(FCoreStyle::GetDefaultFontStyle("Bold", 16)) ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
        [ SNew(STextBlock).AutoWrapText(true)
            .Text(LOCTEXT("About", "Place une caméra orthographique au-dessus de la scène. Règle zoom et altitude, capture en 1920×1080, puis annote (pinceau/ligne/rect/gomme) avant d'enregistrer en JPEG (filigrane scène · zoom · date).")) ]
        + SVerticalBox::Slot().AutoHeight()
        [
            SNew(SButton).HAlign(HAlign_Center)
            .OnClicked_Lambda([this]() { Mode = EOrthoMode::Setup; RebuildUI(); return FReply::Handled(); })
            [ SNew(STextBlock).Text(LOCTEXT("StartBtn", "Démarrer le setup")).Font(FCoreStyle::GetDefaultFontStyle("Bold", 13)) ]
        ];
}

TSharedRef<SWidget> SOrthoCapture::BuildSetupUI()
{
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
        [ SNew(STextBlock).Text(LOCTEXT("Setup", "ORTHO · SETUP")).Font(FCoreStyle::GetDefaultFontStyle("Bold", 14)) ]

        + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0) [ SNew(SBox).WidthOverride(90.f) [ SNew(STextBlock).Text(LOCTEXT("Zoom", "Zoom (Size)")) ] ]
            + SHorizontalBox::Slot().FillWidth(1.f)
            [
                SNew(SSpinBox<float>).MinValue(1.f).MaxValue(150.f).Delta(1.f).Value(OrthoSize)
                .OnValueChanged_Lambda([this](float V) { OrthoSize = V; UpdateCapture(); })
            ]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0) [ SNew(SBox).WidthOverride(90.f) [ SNew(STextBlock).Text(LOCTEXT("Alt", "Altitude (Z)")) ] ]
            + SHorizontalBox::Slot().FillWidth(1.f)
            [
                SNew(SSpinBox<float>).MinValue(1.f).MaxValue(10000.f).Value(CameraHeight)
                .OnValueChanged_Lambda([this](float V) { CameraHeight = V; UpdateCapture(); })
            ]
        ]

        // Aperçu live
        + SVerticalBox::Slot().FillHeight(1.f).Padding(0, 8)
        [
            SNew(SBorder).Padding(2.f)
            [
                SNew(SScaleBox).Stretch(EStretch::ScaleToFit)
                [ SNew(SImage).Image(&ImageBrush) ]
            ]
        ]

        + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth()
            [ SNew(SButton).Text(LOCTEXT("Cancel", "Annuler"))
                .OnClicked_Lambda([this]() { Cleanup(); TSharedPtr<SWindow> W = FSlateApplication::Get().FindWidgetWindow(AsShared()); if (W.IsValid()) W->RequestDestroyWindow(); return FReply::Handled(); }) ]
            + SHorizontalBox::Slot().FillWidth(1.f) [ SNew(SSpacer) ]
            + SHorizontalBox::Slot().AutoWidth()
            [ SNew(SButton).ButtonColorAndOpacity(FLinearColor(0.30f, 0.65f, 0.45f))
                .Text(LOCTEXT("Capture", "📸  CAPTURE · 1920×1080"))
                .OnClicked_Lambda([this]() { CaptureToBuffer(); return FReply::Handled(); }) ]
        ];
}

TSharedRef<SWidget> SOrthoCapture::MakeToolButton(EOrthoTool InTool, const FString& Label, const FString& Tip)
{
    return SNew(SButton)
        .ToolTipText(FText::FromString(Tip))
        .ButtonColorAndOpacity_Lambda([this, InTool]() { return Tool == InTool ? FLinearColor(0.30f, 0.85f, 0.65f) : FLinearColor(0.22f, 0.22f, 0.28f); })
        .OnClicked_Lambda([this, InTool]() { Tool = InTool; return FReply::Handled(); })
        [ SNew(SBox).MinDesiredWidth(34.f).HAlign(HAlign_Center) [ SNew(STextBlock).Text(FText::FromString(Label)) ] ];
}

TSharedRef<SWidget> SOrthoCapture::BuildPaintUI()
{
    return SNew(SVerticalBox)

        // Toolbar
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
        [
            SNew(SHorizontalBox)
            // Color
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
            [
                SNew(SButton)
                .ToolTipText(LOCTEXT("ColorTip", "Couleur du pinceau"))
                .OnClicked_Lambda([this]()
                {
                    FColorPickerArgs Args;
                    Args.bUseAlpha = false;
                    Args.InitialColor = BrushColor;
                    Args.OnColorCommitted = FOnLinearColorValueChanged::CreateLambda([this](FLinearColor C) { BrushColor = C; });
                    OpenColorPicker(Args);
                    return FReply::Handled();
                })
                [ SNew(SBox).WidthOverride(30.f).HeightOverride(18.f) [ SNew(SColorBlock).Color_Lambda([this]() { return BrushColor; }) ] ]
            ]
            // Size
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0) [ SNew(STextBlock).Text(LOCTEXT("Size", "Taille")) ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0)
            [ SNew(SBox).WidthOverride(70.f) [ SNew(SSpinBox<int32>).MinValue(1).MaxValue(50).Value_Lambda([this]() { return BrushSize; }).OnValueChanged_Lambda([this](int32 V) { BrushSize = V; }) ] ]
            // Tools
            + SHorizontalBox::Slot().AutoWidth().Padding(1, 0) [ MakeToolButton(EOrthoTool::Brush, TEXT("Pinceau"), TEXT("Pinceau")) ]
            + SHorizontalBox::Slot().AutoWidth().Padding(1, 0) [ MakeToolButton(EOrthoTool::Line, TEXT("Ligne"), TEXT("Ligne (drag)")) ]
            + SHorizontalBox::Slot().AutoWidth().Padding(1, 0) [ MakeToolButton(EOrthoTool::Rect, TEXT("Rect"), TEXT("Rectangle (drag)")) ]
            + SHorizontalBox::Slot().AutoWidth().Padding(1, 0) [ MakeToolButton(EOrthoTool::Eraser, TEXT("Gomme"), TEXT("Gomme (restaure l'original)")) ]
            // Undo
            + SHorizontalBox::Slot().AutoWidth().Padding(8, 0, 0, 0)
            [ SNew(SButton).ToolTipText(LOCTEXT("UndoTip", "Annuler (Ctrl+Z)")).Text(LOCTEXT("Undo", "↩"))
                .OnClicked_Lambda([this]() { PerformUndo(); return FReply::Handled(); }) ]

            + SHorizontalBox::Slot().FillWidth(1.f) [ SNew(SSpacer) ]

            + SHorizontalBox::Slot().AutoWidth().Padding(2, 0)
            [ SNew(SButton).ButtonColorAndOpacity(FLinearColor(1.f, 0.65f, 0.25f)).Text(LOCTEXT("Retake", "↺ Retake"))
                .OnClicked_Lambda([this]() { Retake(); return FReply::Handled(); }) ]
            + SHorizontalBox::Slot().AutoWidth().Padding(2, 0)
            [ SNew(SButton).Text(LOCTEXT("SaveAs", "Save As…"))
                .OnClicked_Lambda([this]() { Save(true); return FReply::Handled(); }) ]
            + SHorizontalBox::Slot().AutoWidth().Padding(2, 0)
            [ SNew(SButton).ButtonColorAndOpacity(FLinearColor(0.30f, 0.65f, 0.45f)).Text(LOCTEXT("Save", "💾 Save"))
                .OnClicked_Lambda([this]() { Save(false); return FReply::Handled(); }) ]
        ]

        // Surface
        + SVerticalBox::Slot().FillHeight(1.f)
        [
            SNew(SBorder).Padding(2.f)
            [ SAssignNew(PaintSurface, SOrthoPaintSurface).Owner(this) ]
        ];
}

// =============================================================================
// FOrthoCaptureModeModule — menu
// =============================================================================

FName FOrthoCaptureModeModule::TabName = FName("OrthoCapture");

void FOrthoCaptureModeModule::Register()
{
    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
        TabName, FOnSpawnTab::CreateStatic(&FOrthoCaptureModeModule::SpawnTab))
        .SetDisplayName(LOCTEXT("TabTitle", "Ortho Capture"))
        .SetMenuType(ETabSpawnerMenuType::Hidden);

    UToolMenus* ToolMenus = UToolMenus::Get();
    if (!ToolMenus) return;

    UToolMenu* MainMenu = ToolMenus->ExtendMenu("LevelEditor.MainMenu");
    MainMenu->AddSubMenu("MainMenu", NAME_None, "Varonia",
        LOCTEXT("VaroniaMenu", "Varonia"), LOCTEXT("VaroniaMenuTip", "Varonia Tools"));

    UToolMenu* VaroniaSubMenu = ToolMenus->ExtendMenu("LevelEditor.MainMenu.Varonia");
    VaroniaSubMenu->AddMenuEntry(NAME_None,
        FToolMenuEntry::InitMenuEntry("OrthoView",
            LOCTEXT("MenuOrtho", "Ortho View"),
            LOCTEXT("MenuOrthoTip", "Ouvre l'outil de capture orthographique"),
            FSlateIcon(),
            FUIAction(FExecuteAction::CreateStatic(&FOrthoCaptureModeModule::OpenWindow))));

    ToolMenus->RefreshAllWidgets();
}

void FOrthoCaptureModeModule::Unregister()
{
    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabName);
}

TSharedRef<SDockTab> FOrthoCaptureModeModule::SpawnTab(const FSpawnTabArgs& Args)
{
    return SNew(SDockTab).TabRole(ETabRole::NomadTab) [ SNew(SOrthoCapture) ];
}

void FOrthoCaptureModeModule::OpenWindow()
{
    TSharedRef<SWindow> Window = SNew(SWindow)
        .Title(LOCTEXT("WindowTitle", "Varonia · Ortho Capture"))
        .ClientSize(FVector2D(1100, 720))
        .SupportsMinimize(true)
        .SupportsMaximize(true)
        [
            SNew(SOrthoCapture)
        ];

    FSlateApplication::Get().AddWindow(Window);
}

#undef LOCTEXT_NAMESPACE
