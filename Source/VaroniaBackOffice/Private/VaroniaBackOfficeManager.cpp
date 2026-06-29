#include "VaroniaBackOfficeManager.h"
#include "VaroniaMqttLibrary.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "JsonObjectConverter.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "HAL/PlatformFileManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "TimerManager.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SplineComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/PlayerController.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/IInputProcessor.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"

// Capte le clavier globalement (sans composant ni focus particulier) pour l'overlay debug.
class FVaroniaDebugInputProcessor : public IInputProcessor
{
public:
    explicit FVaroniaDebugInputProcessor(UVaroniaBackOfficeManager* InOwner) : Owner(InOwner) {}

    virtual void Tick(const float, FSlateApplication&, TSharedRef<ICursor>) override {}
    virtual bool HandleKeyDownEvent(FSlateApplication&, const FKeyEvent& InKeyEvent) override
    {
        if (Owner.IsValid() && InKeyEvent.GetKey() == Owner->DebugMenuKey)
        {
            Owner->ToggleDebugMenu();
            return true; // consomme la touche
        }
        return false;
    }
    virtual const TCHAR* GetDebugName() const override { return TEXT("VaroniaDebugMenu"); }

private:
    TWeakObjectPtr<UVaroniaBackOfficeManager> Owner;
};

namespace
{
    const TCHAR* MSG_SKIPTUTO = TEXT("GET_SOFTPARTYSKIPTUTOANDSTART_RESULT");
    const TCHAR* MSG_START    = TEXT("GET_SOFTPARTYSTART_RESULT");

    // Crée un composant scène sur un acteur cible à l'exécution (équiv. node AddComponent du BP).
    template <typename T>
    T* AddRuntimeSceneComponent(AActor* Owner)
    {
        if (!Owner)
        {
            return nullptr;
        }
        T* Comp = NewObject<T>(Owner);
        Comp->SetMobility(EComponentMobility::Movable);
        if (USceneComponent* Root = Owner->GetRootComponent())
        {
            Comp->SetupAttachment(Root);
        }
        Comp->RegisterComponent();
        return Comp;
    }
}

// Define the log category
DEFINE_LOG_CATEGORY(LogVaronia);

// ============================================================================
// Coordinate conversion: Unity ? Unreal
// ============================================================================

FVector UVaroniaBackOfficeManager::UnityToUnreal(float X, float Y, float Z)
{
    return FVector(
        Z * 100.f,   // Unity Z (forward) ? Unreal X (forward)
        X * 100.f,   // Unity X (right)   ? Unreal Y (right)
        Y * 100.f    // Unity Y (up)      ? Unreal Z (up)
    );
}

FRotator UVaroniaBackOfficeManager::UnityQuatToUnrealRotator(float X, float Y, float Z, float W)
{
    FQuat UnrealQuat(Z, X, Y, W);
    return UnrealQuat.Rotator();
}

// ============================================================================
// Initialize
// ============================================================================

void UVaroniaBackOfficeManager::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    LoadLBEConfig();
    LoadSpatialConfig();

    FWorldDelegates::OnPostWorldInitialization.AddUObject(this, &UVaroniaBackOfficeManager::OnWorldCreated);

    // Overlay debug global (F12) : capté via un input pre-processor Slate — aucun composant à ajouter.
    if (FSlateApplication::IsInitialized())
    {
        DebugInputProcessor = MakeShared<FVaroniaDebugInputProcessor>(this);
        FSlateApplication::Get().RegisterInputPreProcessor(DebugInputProcessor);
    }
}

// ============================================================================
// World Created ? Spawn MQTT Blueprint
// ============================================================================

void UVaroniaBackOfficeManager::OnWorldCreated(UWorld* World, const UWorld::InitializationValues IValues)
{


    if (!World || !World->IsGameWorld()) return;

    if (!MqttHandler)
    {
        const int32 RandomId = FMath::RandRange(0, 999999999);
        MqttHandler = NewObject<UVaroniaMqttClient>(this);
        MqttHandler->Connect(CurrentConfig.MQTT_ServerIP, 1883, RandomId);
    }

    // Cache le client et (re)démarre le bootstrap runtime pour ce monde (ex BeginPlay de BP_Varonia).
    GetClient();
    if (UGameInstance* GI = GetGameInstance())
    {
        GI->GetTimerManager().SetTimer(ConnectTimerHandle, this, &UVaroniaBackOfficeManager::AfterConnectDelay, ConnectDelay, false);
    }
}

// ============================================================================
// Runtime bootstrap (ex EventGraph BP_Varonia)
// ============================================================================

void UVaroniaBackOfficeManager::AfterConnectDelay()
{
    if (MqttHandler && MqttHandler->IsConnected())
    {
        if (UGameInstance* GI = GetGameInstance())
        {
            FTimerManager& TM = GI->GetTimerManager();
            if (!TM.IsTimerActive(PingTimerHandle))   // un seul heartbeat, même sur changement de niveau
            {
                TM.SetTimer(PingTimerHandle, this, &UVaroniaBackOfficeManager::PingTick, PingInterval, true, PingInterval);
            }
        }
        SubscribeTopic();
        StartGameLogic();
        BackOfficeIsReady.Broadcast();
    }
    else
    {
        UE_LOG(LogVaronia, Warning, TEXT("MQTT No Connected"));
    }
}

void UVaroniaBackOfficeManager::PingTick()
{
    SetSoftState(CurrentSoftState);
}

void UVaroniaBackOfficeManager::StartGameLogic()
{
    if (IMqttClientInterface* Client = GetClient())
    {
        FOnMessageDelegate OnMessage;
        OnMessage.BindDynamic(this, &UVaroniaBackOfficeManager::OnMqttMessage);
        Client->SetOnMessageHandler(OnMessage);
    }
}

void UVaroniaBackOfficeManager::OnMqttMessage(FMqttMessage Message)
{
    const FString& Body = Message.Message;

    // Inputs d'arme (gâchette/tir) : topic DeviceToUnity/<MAC>/<key>.
    if (Message.Topic.StartsWith(TEXT("DeviceToUnity")))
    {
        HandleDeviceInput(Message.Topic, Body);
        return;
    }

    // BP Sequence then_0 : GET_SOFTPARTYSKIPTUTOANDSTART_RESULT -> OnStartWithoutTuto
    if (Body.Contains(MSG_SKIPTUTO))
    {
        if (GameStarted)
        {
            UE_LOG(LogVaronia, Warning, TEXT("Game Already Started"));
        }
        else
        {
            UE_LOG(LogVaronia, Log, TEXT("Start Without Tuto"));
            OnStartWithoutTuto.Broadcast();
            GameStarted = true;
        }
    }

    // BP Sequence then_1 : GET_SOFTPARTYSTART_RESULT -> OnStartWithTuto
    if (Body.Contains(MSG_START))
    {
        if (GameStarted)
        {
            UE_LOG(LogVaronia, Warning, TEXT("Game Already Started"));
        }
        else
        {
            UE_LOG(LogVaronia, Log, TEXT("Start WithTuto"));
            OnStartWithTuto.Broadcast();
            GameStarted = true;
        }
    }
}

// ============================================================================
// MQTT helpers / API (ex fonctions BP_Varonia)
// ============================================================================

IMqttClientInterface* UVaroniaBackOfficeManager::GetClient()
{
    if (MqttClientInterface.GetObject() == nullptr && MqttHandler)
    {
        MqttClientInterface = MqttHandler->GetMqttClient();
    }
    return MqttClientInterface.GetObject() ? MqttClientInterface.GetInterface() : nullptr;
}

void UVaroniaBackOfficeManager::PublishOnTopic(const FString& BaseTopic, const FString& Json)
{
    if (IMqttClientInterface* Client = GetClient())
    {
        FMqttMessage Msg;
        Msg.Message = Json;
        Msg.Topic = SetTopic(BaseTopic);
        Msg.Retain = false;
        Msg.Qos = 0;
        Client->Publish(Msg);
    }
}

FString UVaroniaBackOfficeManager::SetTopic(const FString& Topic) const
{
    const int32 Id = CurrentConfig.MQTT_IDClient;   // ID logique configuré (≠ ClientId random de connexion)
    return Topic + TEXT("/") + FString::FromInt(Id);
}

FString UVaroniaBackOfficeManager::SetSoftState(ESoftState NewState)
{
    CurrentSoftState = NewState;

    const int32 Id = CurrentConfig.MQTT_IDClient;   // ID logique configuré (≠ ClientId random de connexion)
    const FString Json = UVaroniaMqttLibrary::FormatMqttMessage(Id, TEXT("SET_SOFTSTATE"), static_cast<int32>(NewState));

    if (MqttHandler && MqttHandler->IsConnected())
    {
        PublishOnTopic(TEXT("UnityToServer"), Json);
    }
    return Json;
}

FString UVaroniaBackOfficeManager::SetSoftPartyStarted()
{
    const int32 Id = CurrentConfig.MQTT_IDClient;   // ID logique configuré (≠ ClientId random de connexion)
    const FString Json = UVaroniaMqttLibrary::FormatMqttMessage(Id, TEXT("SET_SOFTPARTYSTARTED"), -1);

    if (MqttHandler && MqttHandler->IsConnected())
    {
        PublishOnTopic(TEXT("UnityToServer"), Json);
    }
    return Json;
}

FString UVaroniaBackOfficeManager::SetSoftPartyClosed()
{
    const int32 Id = CurrentConfig.MQTT_IDClient;   // ID logique configuré (≠ ClientId random de connexion)
    const FString Json = UVaroniaMqttLibrary::FormatMqttMessage(Id, TEXT("SET_SOFTPARTYCLOSED"), -1);

    if (MqttHandler && MqttHandler->IsConnected())
    {
        PublishOnTopic(TEXT("UnityToServer"), Json);
    }
    return Json;
}

void UVaroniaBackOfficeManager::SubscribeTopic()
{
    if (MqttHandler && MqttHandler->IsConnected())
    {
        if (IMqttClientInterface* Client = GetClient())
        {
            Client->Subscribe(SetTopic(TEXT("ServerToUnity")), 0);

            // Inputs d'arme : tous les devices publient sur DeviceToUnity/<MAC>/<key>.
            Client->Subscribe(TEXT("DeviceToUnity/#"), 2);
        }
    }
}

// ============================================================================
// Input arme (MQTT DeviceToUnity) — réplique MQTTInput.cs (Unity)
// ============================================================================

int32 UVaroniaBackOfficeManager::ResolveWeaponIndexByMac(const FString& Mac) const
{
    if (Mac.IsEmpty()) return -1;
    for (int32 i = 0; i < CurrentConfig.Devices.Num(); ++i)
    {
        if (CurrentConfig.Devices[i].SerialNumber == Mac) return i;
    }
    return -1;
}

bool UVaroniaBackOfficeManager::GetWeaponButton(int32 WeaponIndex, EVaroniaButton Button) const
{
    if (!CurrentConfig.Devices.IsValidIndex(WeaponIndex)) return false;
    const uint8* State = WeaponButtonState.Find(CurrentConfig.Devices[WeaponIndex].SerialNumber);
    return State ? ((*State & (1 << (uint8)Button)) != 0) : false;
}

EVaroniaController UVaroniaBackOfficeManager::ControllerIdToType(int32 ControllerId) const
{
    switch (ControllerId)
    {
    case 3:   return EVaroniaController::Focus3_VaroniaGun;
    case 6:   return EVaroniaController::Pico_CTRL;
    case 50:  return EVaroniaController::Focus3_Striker;
    case 70:  return EVaroniaController::Pico_VaroniaGun;
    case 80:  return EVaroniaController::Pico_Striker;
    case 101: return EVaroniaController::Focus3_HK416;
    case 416: return EVaroniaController::Pico_HK416;
    case 417: return EVaroniaController::Pico_Glock;
    case 501: return EVaroniaController::Vortex_Focus;
    case 777: return EVaroniaController::HMD;
    default:  return EVaroniaController::Unknown;
    }
}

int32 UVaroniaBackOfficeManager::ControllerTypeToId(EVaroniaController Type) const
{
    switch (Type)
    {
    case EVaroniaController::Focus3_VaroniaGun: return 3;
    case EVaroniaController::Pico_CTRL:         return 6;
    case EVaroniaController::Focus3_Striker:    return 50;
    case EVaroniaController::Pico_VaroniaGun:   return 70;
    case EVaroniaController::Pico_Striker:      return 80;
    case EVaroniaController::Focus3_HK416:      return 101;
    case EVaroniaController::Pico_HK416:        return 416;
    case EVaroniaController::Pico_Glock:        return 417;
    case EVaroniaController::Vortex_Focus:      return 501;
    case EVaroniaController::HMD:               return 777;
    default:                                    return -1;
    }
}

EVaroniaController UVaroniaBackOfficeManager::GetWeaponController(int32 WeaponIndex) const
{
    if (!CurrentConfig.Devices.IsValidIndex(WeaponIndex)) return EVaroniaController::Unknown;
    return ControllerIdToType(CurrentConfig.Devices[WeaponIndex].Controller);
}

int32 UVaroniaBackOfficeManager::FindWeaponIndexByController(EVaroniaController Type) const
{
    for (int32 i = 0; i < CurrentConfig.Devices.Num(); ++i)
    {
        if (ControllerIdToType(CurrentConfig.Devices[i].Controller) == Type) return i;
    }
    return -1;
}

TArray<int32> UVaroniaBackOfficeManager::FindWeaponIndicesByController(EVaroniaController Type) const
{
    TArray<int32> Out;
    for (int32 i = 0; i < CurrentConfig.Devices.Num(); ++i)
    {
        if (ControllerIdToType(CurrentConfig.Devices[i].Controller) == Type) Out.Add(i);
    }
    return Out;
}

void UVaroniaBackOfficeManager::DebugStartWithTuto()
{
    UE_LOG(LogVaronia, Log, TEXT("[Debug] Start With Tuto"));
    OnStartWithTuto.Broadcast();
    GameStarted = true;
}

void UVaroniaBackOfficeManager::DebugStartWithoutTuto()
{
    UE_LOG(LogVaronia, Log, TEXT("[Debug] Start Without Tuto"));
    OnStartWithoutTuto.Broadcast();
    GameStarted = true;
}

// ============================================================================
// Debug overlay (global, sans composant)
// ============================================================================

void UVaroniaBackOfficeManager::ToggleDebugMenu()
{
    bDebugMenuShown ? HideDebugMenu() : ShowDebugMenu();
}

void UVaroniaBackOfficeManager::ShowDebugMenu()
{
    if (bDebugMenuShown) return;
    UGameInstance* GI = GetGameInstance();
    if (!GI) return;
    UGameViewportClient* VP = GI->GetGameViewportClient();
    if (!VP) return;

    DebugMenuWidget = BuildDebugMenu();
    VP->AddViewportWidgetContent(DebugMenuWidget.ToSharedRef(), 1000);
    bDebugMenuShown = true;

    if (APlayerController* PC = GI->GetFirstLocalPlayerController(GetWorld()))
    {
        PC->bShowMouseCursor = true;
        FInputModeGameAndUI Mode;
        Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        Mode.SetHideCursorDuringCapture(false);
        PC->SetInputMode(Mode);
    }
}

void UVaroniaBackOfficeManager::HideDebugMenu()
{
    if (!bDebugMenuShown) return;

    if (UGameInstance* GI = GetGameInstance())
    {
        if (UGameViewportClient* VP = GI->GetGameViewportClient())
        {
            if (DebugMenuWidget.IsValid())
            {
                VP->RemoveViewportWidgetContent(DebugMenuWidget.ToSharedRef());
            }
        }
        if (APlayerController* PC = GI->GetFirstLocalPlayerController(GetWorld()))
        {
            PC->bShowMouseCursor = false;
            PC->SetInputMode(FInputModeGameOnly());
        }
    }

    DebugMenuWidget.Reset();
    bDebugMenuShown = false;
}

TSharedRef<SWidget> UVaroniaBackOfficeManager::BuildDebugMenu()
{
    auto MakeBtn = [this](const FString& Label, TFunction<void()> OnClick)
    {
        return SNew(SButton)
            .HAlign(HAlign_Center)
            .ContentPadding(FMargin(24.f, 10.f))
            .OnClicked_Lambda([OnClick]() { OnClick(); return FReply::Handled(); })
            [
                SNew(STextBlock).Text(FText::FromString(Label)).Font(FCoreStyle::GetDefaultFontStyle("Bold", 13))
            ];
    };

    return SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
    [
        SNew(SBorder)
        .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
        .BorderBackgroundColor(FSlateColor(FLinearColor(0.05f, 0.05f, 0.06f, 0.94f)))
        .Padding(24.f)
        [
            SNew(SVerticalBox)

            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 16)
            [
                SNew(STextBlock)
                .Text(FText::FromString(TEXT("VARONIA — DEBUG")))
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
                .ColorAndOpacity(FSlateColor(FLinearColor(0.30f, 0.85f, 0.65f)))
            ]

            + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
            [ MakeBtn(TEXT("Start With Tutorial"), [this]() { DebugStartWithTuto(); HideDebugMenu(); }) ]

            + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
            [ MakeBtn(TEXT("Start Without Tutorial"), [this]() { DebugStartWithoutTuto(); HideDebugMenu(); }) ]

            + SVerticalBox::Slot().AutoHeight().Padding(0, 12, 0, 0)
            [ MakeBtn(TEXT("Close"), [this]() { HideDebugMenu(); }) ]
        ]
    ];
}

void UVaroniaBackOfficeManager::HandleDeviceInput(const FString& Topic, const FString& Body)
{
    // Topic = DeviceToUnity/<MAC>/<key>  (key 1..4) ; Body = "1" (appui) / "0" (relâché).
    TArray<FString> Parts;
    Topic.ParseIntoArray(Parts, TEXT("/"), true);
    if (Parts.Num() < 3) return;

    const FString Mac = Parts[1];
    const FString Key = Parts.Last();

    EVaroniaButton Button;
    if      (Key == TEXT("1")) Button = EVaroniaButton::Primary;
    else if (Key == TEXT("2")) Button = EVaroniaButton::Secondary;
    else if (Key == TEXT("3")) Button = EVaroniaButton::Tertiary;
    else if (Key == TEXT("4")) Button = EVaroniaButton::Quaternary;
    else return;

    const bool bPressed = (Body.TrimStartAndEnd() == TEXT("1"));

    // Mémorise l'état (pour GetWeaponButton).
    uint8& State = WeaponButtonState.FindOrAdd(Mac);
    if (bPressed) State |= (1 << (uint8)Button);
    else          State &= ~(1 << (uint8)Button);

    const int32 WeaponIndex = ResolveWeaponIndexByMac(Mac);
    const EVaroniaController Controller = GetWeaponController(WeaponIndex);

    OnWeaponInput.Broadcast(WeaponIndex, Controller, Button, bPressed, Mac);

    // Events tir dédiés : gâchette principale (down / up).
    if (Button == EVaroniaButton::Primary)
    {
        if (bPressed) OnWeaponFire.Broadcast(WeaponIndex, Controller, Mac);
        else          OnWeaponFireReleased.Broadcast(WeaponIndex, Controller, Mac);
    }
}

// ============================================================================
// Spatial runtime (ex fonctions BP_Varonia)
// ============================================================================

void UVaroniaBackOfficeManager::SyncPosAndRot(AActor* PlayerVR)
{
    if (!PlayerVR)
    {
        return;
    }
    PlayerVR->SetActorLocation(SpatialConfig.SyncPosition);
    PlayerVR->SetActorRotation(SpatialConfig.SyncRotation);
}

void UVaroniaBackOfficeManager::CoefMul(USceneComponent* PosMul, UCameraComponent* Camera)
{
    if (!PosMul || !Camera)
    {
        return;
    }
    const float Mult = SpatialConfig.Multiplier;
    const FVector Cam = Camera->GetRelativeLocation();
    PosMul->SetRelativeLocation(FVector(Cam.X * Mult, Cam.Y * Mult, 0.f));
}

void UVaroniaBackOfficeManager::SetSyncAndBoundaries(AActor* PlayerVR)
{
    SyncPosAndRot(PlayerVR);
    SpawnBoundaries(PlayerVR);
}

FTransform UVaroniaBackOfficeManager::GetSyncTransform() const
{
    return FTransform(SpatialConfig.SyncRotation, SpatialConfig.SyncPosition);
}

void UVaroniaBackOfficeManager::EnsureBoundaryAssets()
{
    if (!SegmentMesh)
    {
        SegmentMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    }
    if (!PointMesh)
    {
        PointMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    }
    if (!LineMaterial)
    {
        LineMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/VaroniaBackOffice/M_LINE.M_LINE"));
    }
}

void UVaroniaBackOfficeManager::SpawnBoundaries(AActor* PlayerVR)
{
    if (!PlayerVR)
    {
        return;
    }
    EnsureBoundaryAssets();

    for (const FSpatialBoundary& Boundary : SpatialConfig.Boundaries)
    {
        USplineComponent* Spline = AddRuntimeSceneComponent<USplineComponent>(PlayerVR);
        if (!Spline)
        {
            continue;
        }

        const FLinearColor CurrentColor = Boundary.BoundaryColor;

        Spline->ClearSplinePoints(false);
        for (int32 i = 0; i < Boundary.Points.Num(); ++i)
        {
            Spline->AddSplinePoint(Boundary.Points[i], ESplineCoordinateSpace::Local, false);
            Spline->SetSplinePointType(i, ESplinePointType::Linear, false);
        }
        Spline->SetClosedLoop(true, true);

        const int32 LastIndex = Boundary.Points.Num() - 1;
        for (int32 i = 0; i <= LastIndex; ++i)
        {
            UMaterialInstanceDynamic* MID = LineMaterial
                ? UMaterialInstanceDynamic::Create(LineMaterial, this)
                : nullptr;
            if (MID)
            {
                MID->SetVectorParameterValue(TEXT("BaseColor"), CurrentColor);
            }

            const FVector StartPos = Spline->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::Local);
            const FVector EndPos   = Spline->GetLocationAtSplinePoint(i + 1, ESplineCoordinateSpace::Local);
            const FVector Tangent  = EndPos - StartPos;

            if (USplineMeshComponent* SplineMesh = AddRuntimeSceneComponent<USplineMeshComponent>(PlayerVR))
            {
                SplineMesh->SetForwardAxis(ESplineMeshAxis::X, false);
                if (SegmentMesh)
                {
                    SplineMesh->SetStaticMesh(SegmentMesh);
                }
                if (MID)
                {
                    SplineMesh->SetMaterial(0, MID);
                }
                SplineMesh->SetStartScale(SegmentScale, false);
                SplineMesh->SetEndScale(SegmentScale, false);
                SplineMesh->SetStartAndEnd(StartPos, Tangent, EndPos, Tangent, true);
                SplineMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            }

            if (UStaticMeshComponent* Marker = AddRuntimeSceneComponent<UStaticMeshComponent>(PlayerVR))
            {
                if (PointMesh)
                {
                    Marker->SetStaticMesh(PointMesh);
                }
                if (MID)
                {
                    Marker->SetMaterial(0, MID);
                }
                Marker->SetRelativeLocation(StartPos);
                Marker->SetWorldScale3D(FVector(PointScale));
                Marker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            }
        }
    }
}


void UVaroniaBackOfficeManager::Deinitialize()
{
    HideDebugMenu();
    if (DebugInputProcessor.IsValid() && FSlateApplication::IsInitialized())
    {
        FSlateApplication::Get().UnregisterInputPreProcessor(DebugInputProcessor);
    }
    DebugInputProcessor.Reset();

    if (UGameInstance* GI = GetGameInstance())
    {
        GI->GetTimerManager().ClearTimer(ConnectTimerHandle);
        GI->GetTimerManager().ClearTimer(PingTimerHandle);
    }

    FWorldDelegates::OnPostWorldInitialization.RemoveAll(this);

    if (MqttHandler)
    {
        MqttHandler->Disconnect();
    }
    Super::Deinitialize();
}




// ============================================================================
// Paths
// ============================================================================

FString UVaroniaBackOfficeManager::GetConfigPath()
{
    FString UserProfile = FPlatformMisc::GetEnvironmentVariable(TEXT("USERPROFILE"));
    FString FullPath = FPaths::Combine(UserProfile, TEXT("AppData"), TEXT("LocalLow"), TEXT("Varonia"), TEXT("GlobalConfig.json"));
    FPaths::NormalizeFilename(FullPath);

    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    FString Directory = FPaths::GetPath(FullPath);
    if (!PlatformFile.DirectoryExists(*Directory)) { PlatformFile.CreateDirectoryTree(*Directory); }

    return FullPath;
}

FString UVaroniaBackOfficeManager::GetSpatialPath()
{
    FString UserProfile = FPlatformMisc::GetEnvironmentVariable(TEXT("USERPROFILE"));
    FString FullPath = FPaths::Combine(UserProfile, TEXT("AppData"), TEXT("LocalLow"), TEXT("Varonia"), TEXT("NewSpatial.json"));
    FPaths::NormalizeFilename(FullPath);
    return FullPath;
}

// ============================================================================
// LoadLBEConfig
// ============================================================================

bool UVaroniaBackOfficeManager::LoadLBEConfig()
{
    FString FilePath = GetConfigPath();
    FString JsonString;

    UE_LOG(LogVaronia, Verbose, TEXT("Config path: %s"), *FilePath);

    if (FFileHelper::LoadFileToString(JsonString, *FilePath)) {
        if (FJsonObjectConverter::JsonObjectStringToUStruct(JsonString, &CurrentConfig, 0, 0)) {

            const UEnum* ModeEnum = StaticEnum<EDeviceMode>();
            const UEnum* HandEnum = StaticEnum<EMainHand>();

            UE_LOG(LogVaronia, Log, TEXT("Config loaded successfully"));
            UE_LOG(LogVaronia, Log, TEXT("  PlayerName: %s"), *CurrentConfig.PlayerName);
            UE_LOG(LogVaronia, Log, TEXT("  ServerIP: %s"), *CurrentConfig.ServerIP);
            UE_LOG(LogVaronia, Log, TEXT("  MQTT_ServerIP: %s"), *CurrentConfig.MQTT_ServerIP);
            UE_LOG(LogVaronia, Log, TEXT("  MQTT_IDClient: %d"), CurrentConfig.MQTT_IDClient);
            UE_LOG(LogVaronia, Log, TEXT("  DeviceMode: %s"), *ModeEnum->GetNameStringByValue((int64)CurrentConfig.DeviceMode));
            UE_LOG(LogVaronia, Log, TEXT("  MainHand: %s"), *HandEnum->GetNameStringByValue((int64)CurrentConfig.MainHand));
            UE_LOG(LogVaronia, Log, TEXT("  Language: %s"), *CurrentConfig.Language);

            return true;
        }
        else
        {
            UE_LOG(LogVaronia, Error, TEXT("Failed to parse GlobalConfig.json"));
        }
    }
    else
    {
        UE_LOG(LogVaronia, Warning, TEXT("GlobalConfig.json not found, creating default"));
    }

    // Default config creation
    CurrentConfig = FLBEConfig();
    TSharedRef<FJsonObject> JsonObject = MakeShareable(new FJsonObject);
    JsonObject->SetStringField(TEXT("ServerIP"), CurrentConfig.ServerIP);
    JsonObject->SetStringField(TEXT("MQTT_ServerIP"), CurrentConfig.MQTT_ServerIP);
    JsonObject->SetNumberField(TEXT("MQTT_IDClient"), (double)CurrentConfig.MQTT_IDClient);

    
    JsonObject->SetNumberField(TEXT("DeviceMode"), (int32)CurrentConfig.DeviceMode);
    JsonObject->SetStringField(TEXT("Language"), CurrentConfig.Language);

 
    JsonObject->SetNumberField(TEXT("MainHand"), (int32)CurrentConfig.MainHand);
    JsonObject->SetStringField(TEXT("PlayerName"), CurrentConfig.PlayerName);

    JsonObject->SetBoolField(TEXT("ForceLegacyController"), CurrentConfig.ForceLegacyController);
    JsonObject->SetNumberField(TEXT("Controller"), CurrentConfig.Controller);
    JsonObject->SetStringField(TEXT("WeaponMAC"), CurrentConfig.WeaponMAC);
    JsonObject->SetArrayField(TEXT("Devices"), TArray<TSharedPtr<FJsonValue>>());
    JsonObject->SetNumberField(TEXT("HideMode"), CurrentConfig.HideMode);
    JsonObject->SetBoolField(TEXT("Direct"), CurrentConfig.Direct);
    JsonObject->SetStringField(TEXT("HeadsetName"), CurrentConfig.HeadsetName);

    FString OutputString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
    if (FJsonSerializer::Serialize(JsonObject, Writer)) { FFileHelper::SaveStringToFile(OutputString, *FilePath); }

    return false;
}

// ============================================================================
// LoadSpatialConfig
// ============================================================================

bool UVaroniaBackOfficeManager::LoadSpatialConfig()
{
    FString FilePath = GetSpatialPath();
    FString JsonString;

    UE_LOG(LogVaronia, Verbose, TEXT("Spatial path: %s"), *FilePath);

    if (!FFileHelper::LoadFileToString(JsonString, *FilePath))
    {
        UE_LOG(LogVaronia, Warning, TEXT("NewSpatial.json not found at: %s"), *FilePath);
        bSpatialConfigLoaded = false;
        return false;
    }

    TSharedPtr<FJsonObject> RootObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);

    if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
    {
        UE_LOG(LogVaronia, Error, TEXT("Failed to parse NewSpatial.json"));
        bSpatialConfigLoaded = false;
        return false;
    }

    // --- Root fields ---
    SpatialConfig.ID = RootObject->GetStringField(TEXT("ID"));
    SpatialConfig.Name = RootObject->GetStringField(TEXT("Name"));
    SpatialConfig.AreaValue = RootObject->GetStringField(TEXT("AreaValue"));
    SpatialConfig.MaxRect = RootObject->GetStringField(TEXT("MaxRect"));
    SpatialConfig.GroupName = RootObject->GetStringField(TEXT("GroupName"));
    SpatialConfig.MaxPlayer = (int32)RootObject->GetNumberField(TEXT("MaxPlayer"));
    SpatialConfig.Multiplier = (float)RootObject->GetNumberField(TEXT("Multiplier"));
    SpatialConfig.OrthoKey = RootObject->GetStringField(TEXT("OrthoKey"));

    // --- SyncPos ---
    const TSharedPtr<FJsonObject>* SyncPosObj;
    if (RootObject->TryGetObjectField(TEXT("SyncPos"), SyncPosObj))
    {
        float sx = (float)(*SyncPosObj)->GetNumberField(TEXT("x"));
        float sy = (float)(*SyncPosObj)->GetNumberField(TEXT("y"));
        float sz = (float)(*SyncPosObj)->GetNumberField(TEXT("z"));
        SpatialConfig.SyncPosition = UnityToUnreal(sx, sy, sz);
    }

    // --- SyncQuaternion ---
    const TSharedPtr<FJsonObject>* SyncQuatObj;
    if (RootObject->TryGetObjectField(TEXT("SyncQuaterion"), SyncQuatObj))
    {
        float qx = (float)(*SyncQuatObj)->GetNumberField(TEXT("x"));
        float qy = (float)(*SyncQuatObj)->GetNumberField(TEXT("y"));
        float qz = (float)(*SyncQuatObj)->GetNumberField(TEXT("z"));
        float qw = (float)(*SyncQuatObj)->GetNumberField(TEXT("w"));
        SpatialConfig.SyncRotation = UnityQuatToUnrealRotator(qx, qy, qz, qw);
    }

    // --- Boundaries ---
    const TArray<TSharedPtr<FJsonValue>>* BoundariesArray;
    if (RootObject->TryGetArrayField(TEXT("Boundaries"), BoundariesArray))
    {
        SpatialConfig.Boundaries.Empty();

        for (const TSharedPtr<FJsonValue>& BoundaryValue : *BoundariesArray)
        {
            const TSharedPtr<FJsonObject>& BObj = BoundaryValue->AsObject();
            if (!BObj.IsValid()) continue;

            FSpatialBoundary Boundary;
            Boundary.ID = BObj->GetStringField(TEXT("ID"));
            Boundary.DisplayDistance = (float)BObj->GetNumberField(TEXT("DisplayDistance"));
            Boundary.bReverse = BObj->GetBoolField(TEXT("Reverse"));
            Boundary.bBoundaryMoreVisible = BObj->GetBoolField(TEXT("BoundaryMoreVisible"));
            Boundary.bAlertLimit = BObj->GetBoolField(TEXT("AlertLimit"));
            Boundary.bMainBoundary = BObj->GetBoolField(TEXT("MainBoundary"));
            Boundary.bVisible = BObj->GetBoolField(TEXT("Visible"));

            // Color
            const TSharedPtr<FJsonObject>* ColorObj;
            if (BObj->TryGetObjectField(TEXT("BoundaryColor"), ColorObj))
            {
                Boundary.BoundaryColor = FLinearColor(
                    (float)(*ColorObj)->GetNumberField(TEXT("x")),
                    (float)(*ColorObj)->GetNumberField(TEXT("y")),
                    (float)(*ColorObj)->GetNumberField(TEXT("z")),
                    1.0f
                );
            }

            // Points
            const TArray<TSharedPtr<FJsonValue>>* PointsArray;
            if (BObj->TryGetArrayField(TEXT("Points"), PointsArray))
            {
                for (const TSharedPtr<FJsonValue>& PointValue : *PointsArray)
                {
                    const TSharedPtr<FJsonObject>& PObj = PointValue->AsObject();
                    if (!PObj.IsValid()) continue;

                    float px = (float)PObj->GetNumberField(TEXT("x"));
                    float py = (float)PObj->GetNumberField(TEXT("y"));
                    float pz = (float)PObj->GetNumberField(TEXT("z"));

                    Boundary.Points.Add(UnityToUnreal(px, py, pz));
                }
            }

            SpatialConfig.Boundaries.Add(Boundary);

            UE_LOG(LogVaronia, Verbose, TEXT("  Boundary [%s] � %d points | Main=%d | Visible=%d"),
                *Boundary.ID, Boundary.Points.Num(), Boundary.bMainBoundary, Boundary.bVisible);
        }
    }

    bSpatialConfigLoaded = true;

    UE_LOG(LogVaronia, Log, TEXT("Spatial loaded: %s (%s) � %d boundaries"),
        *SpatialConfig.Name, *SpatialConfig.AreaValue, SpatialConfig.Boundaries.Num());
    UE_LOG(LogVaronia, Verbose, TEXT("  SyncPos: %s"), *SpatialConfig.SyncPosition.ToString());
    UE_LOG(LogVaronia, Verbose, TEXT("  SyncRot: %s"), *SpatialConfig.SyncRotation.ToString());

    return true;
}

// ============================================================================
// Blueprint Helpers
// ============================================================================

bool UVaroniaBackOfficeManager::GetMainBoundary(FSpatialBoundary& OutBoundary) const
{
    for (const FSpatialBoundary& B : SpatialConfig.Boundaries)
    {
        if (B.bMainBoundary)
        {
            OutBoundary = B;
            return true;
        }
    }
    return false;
}

TArray<FSpatialBoundary> UVaroniaBackOfficeManager::GetSubBoundaries() const
{
    TArray<FSpatialBoundary> Result;
    for (const FSpatialBoundary& B : SpatialConfig.Boundaries)
    {
        if (!B.bMainBoundary)
        {
            Result.Add(B);
        }
    }
    return Result;
}

TArray<FSpatialBoundary> UVaroniaBackOfficeManager::GetAllBoundaries() const
{
    return SpatialConfig.Boundaries;
}

TArray<FVector> UVaroniaBackOfficeManager::GetAllPoints() const
{
    TArray<FVector> Out;
    for (const FSpatialBoundary& B : SpatialConfig.Boundaries)
    {
        Out.Append(B.Points);
    }
    return Out;
}

TArray<FVector> UVaroniaBackOfficeManager::GetMainBoundaryPoints() const
{
    for (const FSpatialBoundary& B : SpatialConfig.Boundaries)
    {
        if (B.bMainBoundary)
        {
            return B.Points;
        }
    }
    return TArray<FVector>();
}

bool UVaroniaBackOfficeManager::GetWeaponBinding(int32 WeaponIndex, FWeaponBinding& OutBinding) const
{
    if (!CurrentConfig.ForceLegacyController && CurrentConfig.Devices.IsValidIndex(WeaponIndex))
    {
        OutBinding = CurrentConfig.Devices[WeaponIndex];
        return true;
    }

    if (WeaponIndex == 0)
    {
        OutBinding = FWeaponBinding();
        OutBinding.Controller = CurrentConfig.Controller;
        OutBinding.SerialNumber = CurrentConfig.WeaponMAC;
        return true;
    }

    return false;
}