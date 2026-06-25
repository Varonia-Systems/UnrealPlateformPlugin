// VaroniaBackOfficeEditor.cpp
#include "VaroniaBackOfficeEditor.h"
#include "OrthoCapture.h"
#include "GlobalConfigEditor.h"
#include "SpatialConfigEditor.h"

void FVaroniaBackOfficeEditorModule::StartupModule()
{
	FOrthoCaptureModeModule::Register();    // crée le sous-menu "Varonia" + entrée Ortho View
	FGlobalConfigEditorModule::Register();  // ajoute l'entrée "Global Config" au sous-menu
	FSpatialConfigEditorModule::Register(); // ajoute l'entrée "Boundary Editor" au sous-menu
}

void FVaroniaBackOfficeEditorModule::ShutdownModule()
{
	FOrthoCaptureModeModule::Unregister();
	FGlobalConfigEditorModule::Unregister();
	FSpatialConfigEditorModule::Unregister();
}

IMPLEMENT_MODULE(FVaroniaBackOfficeEditorModule, VaroniaBackOfficeEditor)
