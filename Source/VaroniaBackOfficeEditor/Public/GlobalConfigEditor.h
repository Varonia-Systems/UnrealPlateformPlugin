// Éditeur Slate du GlobalConfig (équivalent du GlobalConfigReflectionEditor Unity).
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "LBE_Types.h"

class FJsonObject;
class SDockTab;
class FSpawnTabArgs;
class SVerticalBox;

/**
 * Fenêtre éditeur pour lire/modifier %USERPROFILE%/AppData/LocalLow/Varonia/GlobalConfig.json.
 * Édite tous les champs de FLBEConfig (incl. Devices multi-armes + legacy) et préserve
 * les clés JSON inconnues à la sauvegarde.
 */
class SGlobalConfigEditor : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SGlobalConfigEditor) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    // --- State ---
    FLBEConfig Config;
    FString SavePath;
    bool bDirty = false;
    bool bFileExists = false;

    /** JSON racine chargé, conservé pour préserver les clés inconnues au save. */
    TSharedPtr<FJsonObject> LoadedRoot;

    // Options des combos (label "Name (val)")
    TArray<TSharedPtr<FString>> DeviceModeOptions;
    TArray<TSharedPtr<FString>> MainHandOptions;
    TArray<TSharedPtr<FString>> ControllerOptions;

    // --- Paths / IO ---
    static FString GetConfigPath();
    void LoadFromDisk();
    void SaveToDisk();

    // --- UI ---
    void RebuildUI();
    TSharedRef<SWidget> BuildContent();
    TSharedRef<SWidget> BuildDevicesSection();
    TSharedRef<SWidget> BuildLegacySection();

    TSharedRef<SWidget> MakeSectionLabel(const FString& Text, FLinearColor Color = FLinearColor(0.5f, 0.5f, 0.55f));
    TSharedRef<SWidget> MakeTextRow(const FString& Label, TFunction<FString()> Getter, TFunction<void(const FString&)> Setter);
    TSharedRef<SWidget> MakeIntRow(const FString& Label, TFunction<int32()> Getter, TFunction<void(int32)> Setter, int32 MinValue = 0);
    TSharedRef<SWidget> MakeBoolRow(const FString& Label, TFunction<bool()> Getter, TFunction<void(bool)> Setter);
    TSharedRef<SWidget> MakeEnumRow(const FString& Label, const TArray<TSharedPtr<FString>>& Options, TFunction<int32()> GetIndex, TFunction<void(int32)> OnPicked);
    TSharedRef<SWidget> MakeControllerCombo(TFunction<int32()> GetValue, TFunction<void(int32)> SetValue);

    FText GetStatusText() const;
    FSlateColor GetStatusColor() const;
    void MarkDirty();

    static int32 DeviceModeToIndex(EDeviceMode Mode);
    static int32 MainHandToIndex(EMainHand Hand);
};

/** Enregistrement du menu "Varonia > Global Config" + ouverture de la fenêtre. */
class FGlobalConfigEditorModule
{
public:
    static void Register();
    static void Unregister();
    static void OpenWindow();

    static FName TabName;

private:
    static TSharedRef<SDockTab> SpawnTab(const FSpawnTabArgs& Args);
};
