// Éditeur Slate du GlobalConfig (équivalent du GlobalConfigReflectionEditor Unity).

#include "GlobalConfigEditor.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Modules/ModuleManager.h"
#include "ToolMenus.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Docking/SDockTab.h"
#include "Styling/CoreStyle.h"

#define LOCTEXT_NAMESPACE "VaroniaGlobalConfig"

// ============================================================================
// Controller catalog (valeurs Unity ; stockées en int32 car hors plage uint8)
// ============================================================================

static const TArray<TPair<FString, int32>>& ControllerCatalog()
{
    static const TArray<TPair<FString, int32>> C = {
        { TEXT("Unknown"),                -1 },
        { TEXT("FOCUS3_VBS_VaroniaGun"),   3 },
        { TEXT("PICO_VSVR_CTRL"),          6 },
        { TEXT("FOCUS3_VBS_Striker"),     50 },
        { TEXT("PICO_VSVR_VaroniaGun"),   70 },
        { TEXT("PICO_VSVR_Striker"),      80 },
        { TEXT("FOCUS3_VBS_HK416"),      101 },
        { TEXT("PICO_VSVR_HK416"),       416 },
        { TEXT("PICO_VSVR_Glock"),       417 },
        { TEXT("VORTEX_WEAPON_FOCUS"),   501 },
        { TEXT("HMD"),                   777 },
    };
    return C;
}

static FString ControllerLabelForValue(int32 V)
{
    for (const TPair<FString, int32>& P : ControllerCatalog())
    {
        if (P.Value == V) return FString::Printf(TEXT("%s (%d)"), *P.Key, V);
    }
    return FString::Printf(TEXT("(%d)"), V);
}

static int32 ControllerCatalogIndexForValue(int32 V)
{
    const TArray<TPair<FString, int32>>& C = ControllerCatalog();
    for (int32 i = 0; i < C.Num(); ++i)
    {
        if (C[i].Value == V) return i;
    }
    return 0; // Unknown
}

static void BuildEnumOptions(const UEnum* E, TArray<TSharedPtr<FString>>& Out)
{
    Out.Reset();
    if (!E) return;
    for (int32 i = 0; i < E->NumEnums() - 1; ++i) // -1 : exclut _MAX
    {
        const FString Name = E->GetNameStringByIndex(i);
        const int64 Val = E->GetValueByIndex(i);
        Out.Add(MakeShared<FString>(FString::Printf(TEXT("%s (%lld)"), *Name, Val)));
    }
}

// ============================================================================
// Path / IO
// ============================================================================

FString SGlobalConfigEditor::GetConfigPath()
{
    const FString UserProfile = FPlatformMisc::GetEnvironmentVariable(TEXT("USERPROFILE"));
    FString FullPath = FPaths::Combine(UserProfile, TEXT("AppData"), TEXT("LocalLow"), TEXT("Varonia"), TEXT("GlobalConfig.json"));
    FPaths::NormalizeFilename(FullPath);
    return FullPath;
}

int32 SGlobalConfigEditor::DeviceModeToIndex(EDeviceMode Mode)
{
    const UEnum* E = StaticEnum<EDeviceMode>();
    return E ? FMath::Max(0, (int32)E->GetIndexByValue((int64)Mode)) : 0;
}

int32 SGlobalConfigEditor::MainHandToIndex(EMainHand Hand)
{
    const UEnum* E = StaticEnum<EMainHand>();
    return E ? FMath::Max(0, (int32)E->GetIndexByValue((int64)Hand)) : 0;
}

void SGlobalConfigEditor::LoadFromDisk()
{
    SavePath = GetConfigPath();
    LoadedRoot.Reset();
    Config = FLBEConfig();

    FString Json;
    bFileExists = FFileHelper::LoadFileToString(Json, *SavePath);
    if (bFileExists)
    {
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
        TSharedPtr<FJsonObject> Root;
        if (FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid())
        {
            LoadedRoot = Root;

            FString S;
            int32 I;
            bool B;

            if (Root->TryGetStringField(TEXT("ServerIP"), S))             Config.ServerIP = S;
            if (Root->TryGetStringField(TEXT("MQTT_ServerIP"), S))        Config.MQTT_ServerIP = S;
            if (Root->TryGetStringField(TEXT("Language"), S))             Config.Language = S;
            if (Root->TryGetStringField(TEXT("PlayerName"), S))           Config.PlayerName = S;
            if (Root->TryGetStringField(TEXT("WeaponMAC"), S))            Config.WeaponMAC = S;
            if (Root->TryGetStringField(TEXT("HeadsetName"), S))          Config.HeadsetName = S;

            if (Root->TryGetNumberField(TEXT("MQTT_IDClient"), I))        Config.MQTT_IDClient = I;
            if (Root->TryGetNumberField(TEXT("DeviceMode"), I))           Config.DeviceMode = (EDeviceMode)I;
            if (Root->TryGetNumberField(TEXT("MainHand"), I))             Config.MainHand = (EMainHand)I;
            if (Root->TryGetNumberField(TEXT("Controller"), I))           Config.Controller = I;
            if (Root->TryGetNumberField(TEXT("HideMode"), I))             Config.HideMode = I;

            if (Root->TryGetBoolField(TEXT("ForceLegacyController"), B))  Config.ForceLegacyController = B;
            if (Root->TryGetBoolField(TEXT("Direct"), B))                 Config.Direct = B;

            // Devices
            const TArray<TSharedPtr<FJsonValue>>* DevArr = nullptr;
            if (Root->TryGetArrayField(TEXT("Devices"), DevArr) && DevArr)
            {
                for (const TSharedPtr<FJsonValue>& V : *DevArr)
                {
                    const TSharedPtr<FJsonObject>* Obj = nullptr;
                    if (V.IsValid() && V->TryGetObject(Obj) && Obj && Obj->IsValid())
                    {
                        FWeaponBinding Bind;
                        FString Bs; int32 Bi;
                        if ((*Obj)->TryGetNumberField(TEXT("Controller"), Bi))   Bind.Controller = Bi;
                        if ((*Obj)->TryGetStringField(TEXT("SerialNumber"), Bs)) Bind.SerialNumber = Bs;
                        if ((*Obj)->TryGetStringField(TEXT("TrackingId"), Bs))   Bind.TrackingId = Bs;
                        if ((*Obj)->TryGetNumberField(TEXT("ForceSteamId"), Bi)) Bind.ForceSteamId = Bi;
                        Config.Devices.Add(Bind);
                    }
                }
            }
        }
    }

    bDirty = false;
}

void SGlobalConfigEditor::SaveToDisk()
{
    TSharedRef<FJsonObject> Root = LoadedRoot.IsValid()
        ? LoadedRoot.ToSharedRef()
        : MakeShared<FJsonObject>();

    Root->SetStringField(TEXT("ServerIP"), Config.ServerIP);
    Root->SetStringField(TEXT("MQTT_ServerIP"), Config.MQTT_ServerIP);
    Root->SetNumberField(TEXT("MQTT_IDClient"), Config.MQTT_IDClient);
    Root->SetNumberField(TEXT("DeviceMode"), (int32)Config.DeviceMode);
    Root->SetStringField(TEXT("Language"), Config.Language);
    Root->SetNumberField(TEXT("MainHand"), (int32)Config.MainHand);
    Root->SetStringField(TEXT("PlayerName"), Config.PlayerName);

    Root->SetBoolField(TEXT("ForceLegacyController"), Config.ForceLegacyController);
    Root->SetNumberField(TEXT("Controller"), Config.Controller);
    Root->SetStringField(TEXT("WeaponMAC"), Config.WeaponMAC);
    Root->SetNumberField(TEXT("HideMode"), Config.HideMode);
    Root->SetBoolField(TEXT("Direct"), Config.Direct);
    Root->SetStringField(TEXT("HeadsetName"), Config.HeadsetName);

    TArray<TSharedPtr<FJsonValue>> DevArr;
    for (const FWeaponBinding& B : Config.Devices)
    {
        TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
        O->SetNumberField(TEXT("Controller"), B.Controller);
        O->SetStringField(TEXT("SerialNumber"), B.SerialNumber);
        O->SetStringField(TEXT("TrackingId"), B.TrackingId);
        O->SetNumberField(TEXT("ForceSteamId"), B.ForceSteamId);
        DevArr.Add(MakeShared<FJsonValueObject>(O));
    }
    Root->SetArrayField(TEXT("Devices"), DevArr);

    IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
    const FString Dir = FPaths::GetPath(SavePath);
    if (!PF.DirectoryExists(*Dir))
    {
        PF.CreateDirectoryTree(*Dir);
    }

    FString Out;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
    if (FJsonSerializer::Serialize(Root, Writer))
    {
        FFileHelper::SaveStringToFile(Out, *SavePath);
        LoadedRoot = Root;
        bFileExists = true;
        bDirty = false;
    }
}

// ============================================================================
// Lifecycle
// ============================================================================

void SGlobalConfigEditor::Construct(const FArguments& InArgs)
{
    BuildEnumOptions(StaticEnum<EDeviceMode>(), DeviceModeOptions);
    BuildEnumOptions(StaticEnum<EMainHand>(), MainHandOptions);

    ControllerOptions.Reset();
    for (const TPair<FString, int32>& P : ControllerCatalog())
    {
        ControllerOptions.Add(MakeShared<FString>(FString::Printf(TEXT("%s (%d)"), *P.Key, P.Value)));
    }

    LoadFromDisk();
    RebuildUI();
}

void SGlobalConfigEditor::MarkDirty()
{
    bDirty = true; // la pastille de statut est bindée → pas besoin de reconstruire
}

FText SGlobalConfigEditor::GetStatusText() const
{
    if (bDirty)      return LOCTEXT("Unsaved", "● NON SAUVEGARDÉ");
    if (bFileExists) return LOCTEXT("Synced", "SYNCHRONISÉ");
    return LOCTEXT("NoFile", "AUCUN FICHIER");
}

FSlateColor SGlobalConfigEditor::GetStatusColor() const
{
    if (bDirty)      return FSlateColor(FLinearColor(1.f, 0.75f, 0.30f));
    if (bFileExists) return FSlateColor(FLinearColor(0.30f, 0.85f, 0.65f));
    return FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f));
}

// ============================================================================
// Row helpers
// ============================================================================

TSharedRef<SWidget> SGlobalConfigEditor::MakeSectionLabel(const FString& Text, FLinearColor Color)
{
    return SNew(STextBlock)
        .Text(FText::FromString(Text))
        .Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
        .ColorAndOpacity(FSlateColor(Color));
}

TSharedRef<SWidget> SGlobalConfigEditor::MakeTextRow(const FString& Label, TFunction<FString()> Getter, TFunction<void(const FString&)> Setter)
{
    return SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
        [ SNew(SBox).WidthOverride(150.f) [ SNew(STextBlock).Text(FText::FromString(Label)) ] ]
        + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
        [
            SNew(SEditableTextBox)
            .Text_Lambda([Getter]() { return FText::FromString(Getter()); })
            .OnTextCommitted_Lambda([this, Setter](const FText& T, ETextCommit::Type) { Setter(T.ToString()); MarkDirty(); })
        ];
}

TSharedRef<SWidget> SGlobalConfigEditor::MakeIntRow(const FString& Label, TFunction<int32()> Getter, TFunction<void(int32)> Setter, int32 MinValue)
{
    return SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
        [ SNew(SBox).WidthOverride(150.f) [ SNew(STextBlock).Text(FText::FromString(Label)) ] ]
        + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
        [
            SNew(SSpinBox<int32>)
            .MinValue(MinValue)
            .Value_Lambda([Getter]() { return Getter(); })
            .OnValueChanged_Lambda([Setter](int32 V) { Setter(V); })
            .OnValueCommitted_Lambda([this, Setter](int32 V, ETextCommit::Type) { Setter(V); MarkDirty(); })
        ];
}

TSharedRef<SWidget> SGlobalConfigEditor::MakeBoolRow(const FString& Label, TFunction<bool()> Getter, TFunction<void(bool)> Setter)
{
    return SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
        [ SNew(SBox).WidthOverride(150.f) [ SNew(STextBlock).Text(FText::FromString(Label)) ] ]
        + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
        [
            SNew(SCheckBox)
            .IsChecked_Lambda([Getter]() { return Getter() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda([this, Setter](ECheckBoxState S) { Setter(S == ECheckBoxState::Checked); MarkDirty(); })
        ];
}

TSharedRef<SWidget> SGlobalConfigEditor::MakeEnumRow(const FString& Label, const TArray<TSharedPtr<FString>>& Options, TFunction<int32()> GetIndex, TFunction<void(int32)> OnPicked)
{
    const int32 Cur = GetIndex();
    TSharedPtr<FString> Initial = Options.IsValidIndex(Cur) ? Options[Cur] : (Options.Num() ? Options[0] : nullptr);

    return SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
        [ SNew(SBox).WidthOverride(150.f) [ SNew(STextBlock).Text(FText::FromString(Label)) ] ]
        + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
        [
            SNew(SComboBox<TSharedPtr<FString>>)
            .OptionsSource(&Options)
            .InitiallySelectedItem(Initial)
            .OnGenerateWidget_Lambda([](TSharedPtr<FString> In) { return SNew(STextBlock).Text(FText::FromString(In.IsValid() ? *In : FString())); })
            .OnSelectionChanged_Lambda([this, &Options, OnPicked](TSharedPtr<FString> Sel, ESelectInfo::Type)
            {
                if (!Sel.IsValid()) return;
                const int32 Idx = Options.IndexOfByKey(Sel);
                if (Idx != INDEX_NONE) { OnPicked(Idx); MarkDirty(); }
            })
            [
                SNew(STextBlock).Text_Lambda([&Options, GetIndex]()
                {
                    const int32 i = GetIndex();
                    return FText::FromString(Options.IsValidIndex(i) ? *Options[i] : FString());
                })
            ]
        ];
}

TSharedRef<SWidget> SGlobalConfigEditor::MakeControllerCombo(TFunction<int32()> GetValue, TFunction<void(int32)> SetValue)
{
    const int32 CurIdx = ControllerCatalogIndexForValue(GetValue());
    TSharedPtr<FString> Initial = ControllerOptions.IsValidIndex(CurIdx) ? ControllerOptions[CurIdx] : nullptr;

    return SNew(SComboBox<TSharedPtr<FString>>)
        .OptionsSource(&ControllerOptions)
        .InitiallySelectedItem(Initial)
        .OnGenerateWidget_Lambda([](TSharedPtr<FString> In) { return SNew(STextBlock).Text(FText::FromString(In.IsValid() ? *In : FString())); })
        .OnSelectionChanged_Lambda([this, SetValue](TSharedPtr<FString> Sel, ESelectInfo::Type)
        {
            if (!Sel.IsValid()) return;
            const int32 Idx = ControllerOptions.IndexOfByKey(Sel);
            if (ControllerCatalog().IsValidIndex(Idx)) { SetValue(ControllerCatalog()[Idx].Value); MarkDirty(); }
        })
        [
            SNew(STextBlock).Text_Lambda([GetValue]() { return FText::FromString(ControllerLabelForValue(GetValue())); })
        ];
}

// ============================================================================
// Sections
// ============================================================================

TSharedRef<SWidget> SGlobalConfigEditor::BuildDevicesSection()
{
    TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);

    Box->AddSlot().AutoHeight().Padding(0, 8, 0, 2)
    [ MakeSectionLabel(FString::Printf(TEXT("DEVICES (multi-arme) — %d"), Config.Devices.Num()), FLinearColor(0.35f, 0.80f, 0.55f)) ];

    for (int32 i = 0; i < Config.Devices.Num(); ++i)
    {
        Box->AddSlot().AutoHeight().Padding(0, 2)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
            [ SNew(SBox).WidthOverride(28.f) [ SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("#%d"), i))) ] ]

            + SHorizontalBox::Slot().FillWidth(1.4f).VAlign(VAlign_Center).Padding(2, 0)
            [
                MakeControllerCombo(
                    [this, i]() { return Config.Devices.IsValidIndex(i) ? Config.Devices[i].Controller : -1; },
                    [this, i](int32 V) { if (Config.Devices.IsValidIndex(i)) Config.Devices[i].Controller = V; })
            ]

            + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(2, 0)
            [
                SNew(SEditableTextBox)
                .HintText(LOCTEXT("Serial", "Serial"))
                .Text_Lambda([this, i]() { return FText::FromString(Config.Devices.IsValidIndex(i) ? Config.Devices[i].SerialNumber : FString()); })
                .OnTextCommitted_Lambda([this, i](const FText& T, ETextCommit::Type) { if (Config.Devices.IsValidIndex(i)) { Config.Devices[i].SerialNumber = T.ToString(); MarkDirty(); } })
            ]

            + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(2, 0)
            [
                SNew(SEditableTextBox)
                .HintText(LOCTEXT("TrackingId", "Tracking ID"))
                .Text_Lambda([this, i]() { return FText::FromString(Config.Devices.IsValidIndex(i) ? Config.Devices[i].TrackingId : FString()); })
                .OnTextCommitted_Lambda([this, i](const FText& T, ETextCommit::Type) { if (Config.Devices.IsValidIndex(i)) { Config.Devices[i].TrackingId = T.ToString(); MarkDirty(); } })
            ]

            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2, 0)
            [
                SNew(SBox).WidthOverride(70.f)
                [
                    SNew(SSpinBox<int32>)
                    .MinValue(-1)
                    .ToolTipText(LOCTEXT("ForceSteam", "Force Steam device index (-1 = auto)"))
                    .Value_Lambda([this, i]() { return Config.Devices.IsValidIndex(i) ? Config.Devices[i].ForceSteamId : -1; })
                    .OnValueChanged_Lambda([this, i](int32 V) { if (Config.Devices.IsValidIndex(i)) Config.Devices[i].ForceSteamId = V; })
                    .OnValueCommitted_Lambda([this, i](int32 V, ETextCommit::Type) { if (Config.Devices.IsValidIndex(i)) { Config.Devices[i].ForceSteamId = V; MarkDirty(); } })
                ]
            ]

            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0, 0, 0)
            [
                SNew(SButton)
                .Text(LOCTEXT("RemoveX", "X"))
                .ToolTipText(LOCTEXT("RemoveTip", "Supprimer cette arme"))
                .OnClicked_Lambda([this, i]()
                {
                    if (Config.Devices.IsValidIndex(i)) { Config.Devices.RemoveAt(i); MarkDirty(); RebuildUI(); }
                    return FReply::Handled();
                })
            ]
        ];
    }

    Box->AddSlot().AutoHeight().Padding(0, 4)
    [
        SNew(SButton)
        .HAlign(HAlign_Center)
        .Text(LOCTEXT("AddWeapon", "+ Ajouter une arme"))
        .OnClicked_Lambda([this]()
        {
            Config.Devices.Add(FWeaponBinding());
            MarkDirty();
            RebuildUI();
            return FReply::Handled();
        })
    ];

    return Box;
}

TSharedRef<SWidget> SGlobalConfigEditor::BuildLegacySection()
{
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 2)
        [ MakeSectionLabel(TEXT("LEGACY (mono-arme)"), FLinearColor(1.0f, 0.65f, 0.25f)) ]

        + SVerticalBox::Slot().AutoHeight().Padding(0, 3)
        [ MakeBoolRow(TEXT("Force Legacy Controller"),
            [this]() { return Config.ForceLegacyController; },
            [this](bool V) { Config.ForceLegacyController = V; }) ]

        + SVerticalBox::Slot().AutoHeight().Padding(0, 3)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
            [ SNew(SBox).WidthOverride(150.f) [ SNew(STextBlock).Text(LOCTEXT("LegacyController", "Controller")) ] ]
            + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
            [
                MakeControllerCombo(
                    [this]() { return Config.Controller; },
                    [this](int32 V) { Config.Controller = V; })
            ]
        ]

        + SVerticalBox::Slot().AutoHeight().Padding(0, 3)
        [ MakeTextRow(TEXT("Weapon MAC"),
            [this]() { return Config.WeaponMAC; },
            [this](const FString& V) { Config.WeaponMAC = V; }) ];
}

void SGlobalConfigEditor::RebuildUI()
{
    ChildSlot
    [
        SNew(SBorder).Padding(14.f) [ BuildContent() ]
    ];
}

TSharedRef<SWidget> SGlobalConfigEditor::BuildContent()
{
    const UEnum* DevEnum = StaticEnum<EDeviceMode>();
    const UEnum* HandEnum = StaticEnum<EMainHand>();

    TSharedRef<SVerticalBox> Fields = SNew(SVerticalBox);

    // Network
    Fields->AddSlot().AutoHeight().Padding(0, 2) [ MakeSectionLabel(TEXT("NETWORK"), FLinearColor(0.30f, 0.85f, 0.65f)) ];
    Fields->AddSlot().AutoHeight().Padding(0, 3) [ MakeTextRow(TEXT("Server IP"), [this]() { return Config.ServerIP; }, [this](const FString& V) { Config.ServerIP = V; }) ];
    Fields->AddSlot().AutoHeight().Padding(0, 3) [ MakeTextRow(TEXT("MQTT Server IP"), [this]() { return Config.MQTT_ServerIP; }, [this](const FString& V) { Config.MQTT_ServerIP = V; }) ];
    Fields->AddSlot().AutoHeight().Padding(0, 3) [ MakeIntRow(TEXT("MQTT ID Client"), [this]() { return Config.MQTT_IDClient; }, [this](int32 V) { Config.MQTT_IDClient = V; }) ];

    // Preferences
    Fields->AddSlot().AutoHeight().Padding(0, 8, 0, 2) [ MakeSectionLabel(TEXT("PREFERENCES"), FLinearColor(0.40f, 0.70f, 1.0f)) ];
    Fields->AddSlot().AutoHeight().Padding(0, 3) [ MakeEnumRow(TEXT("Device Mode"), DeviceModeOptions,
        [this]() { return DeviceModeToIndex(Config.DeviceMode); },
        [this, DevEnum](int32 Idx) { if (DevEnum) Config.DeviceMode = (EDeviceMode)DevEnum->GetValueByIndex(Idx); }) ];
    Fields->AddSlot().AutoHeight().Padding(0, 3) [ MakeTextRow(TEXT("Language"), [this]() { return Config.Language; }, [this](const FString& V) { Config.Language = V; }) ];
    Fields->AddSlot().AutoHeight().Padding(0, 3) [ MakeEnumRow(TEXT("Main Hand"), MainHandOptions,
        [this]() { return MainHandToIndex(Config.MainHand); },
        [this, HandEnum](int32 Idx) { if (HandEnum) Config.MainHand = (EMainHand)HandEnum->GetValueByIndex(Idx); }) ];
    Fields->AddSlot().AutoHeight().Padding(0, 3) [ MakeTextRow(TEXT("Player Name"), [this]() { return Config.PlayerName; }, [this](const FString& V) { Config.PlayerName = V; }) ];

    // Devices + Legacy
    Fields->AddSlot().AutoHeight() [ BuildDevicesSection() ];
    Fields->AddSlot().AutoHeight() [ BuildLegacySection() ];

    // Divers
    Fields->AddSlot().AutoHeight().Padding(0, 8, 0, 2) [ MakeSectionLabel(TEXT("DIVERS"), FLinearColor(0.70f, 0.55f, 1.0f)) ];
    Fields->AddSlot().AutoHeight().Padding(0, 3) [ MakeIntRow(TEXT("Hide Mode"), [this]() { return Config.HideMode; }, [this](int32 V) { Config.HideMode = V; }) ];
    Fields->AddSlot().AutoHeight().Padding(0, 3) [ MakeBoolRow(TEXT("Direct"), [this]() { return Config.Direct; }, [this](bool V) { Config.Direct = V; }) ];
    Fields->AddSlot().AutoHeight().Padding(0, 3) [ MakeTextRow(TEXT("Headset Name"), [this]() { return Config.HeadsetName; }, [this](const FString& V) { Config.HeadsetName = V; }) ];

    return SNew(SVerticalBox)

    // Titre + statut
    + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
        [ SNew(STextBlock).Text(LOCTEXT("Title", "GLOBAL CONFIG")).Font(FCoreStyle::GetDefaultFontStyle("Bold", 16)) ]
        + SHorizontalBox::Slot().FillWidth(1.f).HAlign(HAlign_Right).VAlign(VAlign_Center)
        [
            SNew(STextBlock)
            .Text(this, &SGlobalConfigEditor::GetStatusText)
            .ColorAndOpacity(this, &SGlobalConfigEditor::GetStatusColor)
            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
        ]
    ]

    + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6) [ SNew(SSeparator) ]

    // Champs (scrollables)
    + SVerticalBox::Slot().FillHeight(1.f)
    [
        SNew(SScrollBox)
        + SScrollBox::Slot() [ Fields ]
    ]

    + SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 4) [ SNew(SSeparator) ]

    // Boutons
    + SVerticalBox::Slot().AutoHeight()
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
        [
            SNew(SButton)
            .Text(LOCTEXT("Reload", "Recharger"))
            .ToolTipText(LOCTEXT("ReloadTip", "Recharge GlobalConfig.json depuis le disque (annule les modifs non sauvegardées)."))
            .OnClicked_Lambda([this]() { LoadFromDisk(); RebuildUI(); return FReply::Handled(); })
        ]
        + SHorizontalBox::Slot().FillWidth(1.f) [ SNew(SSpacer) ]
        + SHorizontalBox::Slot().AutoWidth()
        [
            SNew(SButton)
            .Text(LOCTEXT("Save", "SAVE"))
            .ButtonColorAndOpacity(FLinearColor(0.30f, 0.65f, 0.45f))
            .ToolTipText(LOCTEXT("SaveTip", "Écrit les valeurs dans GlobalConfig.json (les clés inconnues sont préservées)."))
            .OnClicked_Lambda([this]() { SaveToDisk(); RebuildUI(); return FReply::Handled(); })
        ]
    ]

    // Chemin
    + SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
    [
        SNew(STextBlock)
        .Text(FText::FromString(SavePath))
        .AutoWrapText(true)
        .ColorAndOpacity(FSlateColor(FLinearColor(0.45f, 0.45f, 0.5f)))
        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
    ];
}

// ============================================================================
// Module / menu
// ============================================================================

FName FGlobalConfigEditorModule::TabName = FName("VaroniaGlobalConfig");

void FGlobalConfigEditorModule::Register()
{
    UToolMenus* ToolMenus = UToolMenus::Get();
    if (!ToolMenus) return;

    UToolMenu* VaroniaSubMenu = ToolMenus->ExtendMenu("LevelEditor.MainMenu.Varonia");
    VaroniaSubMenu->AddMenuEntry(
        NAME_None,
        FToolMenuEntry::InitMenuEntry(
            "GlobalConfig",
            LOCTEXT("MenuGlobalConfig", "Global Config"),
            LOCTEXT("MenuGlobalConfigTip", "Éditer GlobalConfig.json"),
            FSlateIcon(),
            FUIAction(FExecuteAction::CreateStatic(&FGlobalConfigEditorModule::OpenWindow))
        )
    );

    ToolMenus->RefreshAllWidgets();
}

void FGlobalConfigEditorModule::Unregister()
{
}

TSharedRef<SDockTab> FGlobalConfigEditorModule::SpawnTab(const FSpawnTabArgs& Args)
{
    return SNew(SDockTab).TabRole(ETabRole::NomadTab) [ SNew(SGlobalConfigEditor) ];
}

void FGlobalConfigEditorModule::OpenWindow()
{
    TSharedRef<SWindow> Window = SNew(SWindow)
        .Title(LOCTEXT("WindowTitle", "Varonia · Global Config"))
        .ClientSize(FVector2D(560, 720))
        .SupportsMinimize(false)
        .SupportsMaximize(false)
        [
            SNew(SGlobalConfigEditor)
        ];

    FSlateApplication::Get().AddWindow(Window);
}

#undef LOCTEXT_NAMESPACE
