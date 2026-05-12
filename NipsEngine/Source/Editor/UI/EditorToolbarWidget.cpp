#include "Editor/UI/EditorToolbarWidget.h"

#include "Editor/UI/EditorContentDrawerWidget.h"
#include "Editor/UI/EditorSceneWidget.h"
#include "Editor/UI/EditorViewportOverlayWidget.h"
#include "Editor/UI/EditorPlayStreamWidget.h"
#include "Editor/EditorEngine.h"
#include "Editor/Selection/SelectionManager.h"
#include "Editor/Viewport/ViewportLayout.h"
#include "Component/GizmoComponent.h"
#include "Core/ResourceManager.h"
#include "Engine/Viewport/ViewportCamera.h"
#include "GameFramework/PrimitiveActors.h"
#include "GameFramework/World.h"
#include "Serialization/SceneSaveManager.h"
#include "ImGui/imgui.h"

#include <algorithm>
#include <Windows.h>
#include <commdlg.h>
#include <filesystem>
#include <shellapi.h>

namespace
{
	template <typename T>
	AActor* SpawnEditorActor(UWorld* World, const FVector& Location)
	{
		if (!World)
		{
			return nullptr;
		}

		T* Actor = World->SpawnActor<T>();
		if (!Actor)
		{
			return nullptr;
		}

		Actor->InitDefaultComponents();
		Actor->SetActorLocation(Location);
		return Actor;
	}

	struct FAddActorEntry
	{
		const char* Label;
		AActor* (*Spawn)(UWorld*, const FVector&);
	};

	static const FAddActorEntry AddActorTypes[] = {
		{ "Pawn", SpawnEditorActor<APawnActor> },
		{ "Scene", SpawnEditorActor<ASceneActor> },
		{ "StaticMesh", SpawnEditorActor<AStaticMeshActor> },
		{ "SkeletalMesh", SpawnEditorActor<ASkeletalMeshActor> },
		{ "TextRender", SpawnEditorActor<ATextRenderActor> },
		{ "SubUV", SpawnEditorActor<ASubUVActor> },
		{ "Billboard", SpawnEditorActor<ABillboardActor> },
		{ "Decal", SpawnEditorActor<ADecalActor> },
		{ "Directional Light", SpawnEditorActor<ADirectionalLightActor> },
		{ "Ambient Light", SpawnEditorActor<AAmbientLightActor> },
		{ "Point Light", SpawnEditorActor<APointLightActor> },
		{ "Spot Light", SpawnEditorActor<ASpotLightActor> },
		{ "Sky Atmosphere", SpawnEditorActor<ASkyAtmosphereActor> },
		{ "Height Fog", SpawnEditorActor<AHeightFogActor> },
		{ "Audio Zone", SpawnEditorActor<AAudioZoneActor> },
		{ "Player Start", SpawnEditorActor<APlayerStartActor> },
	};

	std::wstring GetSceneDialogInitialDir()
	{
		std::filesystem::path SceneDir(FSceneSaveManager::GetSceneDirectory());
		SceneDir = SceneDir.lexically_normal();
		if (!SceneDir.is_absolute())
		{
			SceneDir = std::filesystem::path(FPaths::ToAbsolute(SceneDir.wstring()));
		}
		SceneDir.make_preferred();
		std::error_code Ec;
		std::filesystem::create_directories(SceneDir, Ec);
		return SceneDir.wstring();
	}
}

bool FEditorToolbarWidget::OpenSceneFileDialog(FString& OutFilePath) const
{
	OutFilePath.clear();

	WCHAR FileBuffer[MAX_PATH] = { 0 };
	const std::wstring InitialDir = GetSceneDialogInitialDir();
	const std::filesystem::path PrevCwd = std::filesystem::current_path();
	std::error_code ChdirEc;
	std::filesystem::current_path(std::filesystem::path(InitialDir), ChdirEc);

	const std::wstring OpenPattern = std::filesystem::path(InitialDir).append(L"*.Scene").wstring();
	wcsncpy_s(FileBuffer, MAX_PATH, OpenPattern.c_str(), _TRUNCATE);

	OPENFILENAMEW DialogDesc = {};
	DialogDesc.lStructSize = sizeof(DialogDesc);
	DialogDesc.hwndOwner = static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw);
	DialogDesc.lpstrFilter = L"Scene Files (*.Scene)\0*.Scene\0All Files (*.*)\0*.*\0";
	DialogDesc.lpstrFile = FileBuffer;
	DialogDesc.nMaxFile = MAX_PATH;
	DialogDesc.lpstrInitialDir = InitialDir.c_str();
	DialogDesc.lpstrDefExt = L"Scene";
	DialogDesc.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

	const BOOL bPicked = GetOpenFileNameW(&DialogDesc);
	std::error_code RestoreEc;
	std::filesystem::current_path(PrevCwd, RestoreEc);
	if (!bPicked)
	{
		return false;
	}

	OutFilePath = FPaths::ToUtf8(FileBuffer);
	return true;
}

bool FEditorToolbarWidget::SaveSceneFileDialog(FString& OutFilePath) const
{
	OutFilePath.clear();

	WCHAR FileBuffer[MAX_PATH] = { 0 };
	const std::wstring InitialDir = GetSceneDialogInitialDir();
	const std::filesystem::path PrevCwd = std::filesystem::current_path();
	std::error_code ChdirEc;
	std::filesystem::current_path(std::filesystem::path(InitialDir), ChdirEc);

	const std::wstring DefaultFile = std::filesystem::path(InitialDir).append(L"NewScene.Scene").wstring();
	wcsncpy_s(FileBuffer, MAX_PATH, DefaultFile.c_str(), _TRUNCATE);

	OPENFILENAMEW DialogDesc = {};
	DialogDesc.lStructSize = sizeof(DialogDesc);
	DialogDesc.hwndOwner = static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw);
	DialogDesc.lpstrFilter = L"Scene Files (*.Scene)\0*.Scene\0All Files (*.*)\0*.*\0";
	DialogDesc.lpstrFile = FileBuffer;
	DialogDesc.nMaxFile = MAX_PATH;
	DialogDesc.lpstrInitialDir = InitialDir.c_str();
	DialogDesc.lpstrDefExt = L"Scene";
	DialogDesc.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;

	const BOOL bPicked = GetSaveFileNameW(&DialogDesc);
	std::error_code RestoreEc;
	std::filesystem::current_path(PrevCwd, RestoreEc);
	if (!bPicked)
	{
		return false;
	}

	OutFilePath = FPaths::ToUtf8(FileBuffer);
	return true;
}

void FEditorToolbarWidget::SetViewportOverlayWidget(FEditorViewportOverlayWidget* InViewportOverlayWidget)
{
	ViewportOverlayWidget = InViewportOverlayWidget;
}

void FEditorToolbarWidget::SetSceneWidget(FEditorSceneWidget* InSceneWidget)
{
	SceneWidget = InSceneWidget;
}

void FEditorToolbarWidget::SetPlayStreamWidget(FEditorPlayStreamWidget* InPlayStreamWidget)
{
	PlayStreamWidget = InPlayStreamWidget;
}

void FEditorToolbarWidget::SetContentDrawerWidget(FEditorContentDrawerWidget* InContentDrawerWidget)
{
	ContentDrawerWidget = InContentDrawerWidget;
}

void FEditorToolbarWidget::SetPanelVisibilityRefs(
	bool* InShowConsole,
	bool* InShowControl,
	bool* InShowProperty,
	bool* InShowSceneManager,
	bool* InShowMaterialEditor,
	bool* InShowStatProfiler,
	bool* InShowSkeletalMeshViewer)
{
	bShowConsole = InShowConsole;
	bShowControl = InShowControl;
	bShowProperty = InShowProperty;
	bShowSceneManager = InShowSceneManager;
	bShowMaterialEditor = InShowMaterialEditor;
	bShowStatProfiler = InShowStatProfiler;
	bShowSkeletalMeshViewer = InShowSkeletalMeshViewer;
}

void FEditorToolbarWidget::Render(float DeltaTime)
{
	(void)DeltaTime;

	const ImGuiIO& IO = ImGui::GetIO();
	if (!IO.WantTextInput && IO.KeyCtrl)
	{
		if (ContentDrawerWidget && ImGui::IsKeyPressed(ImGuiKey_Space, false))
		{
			ContentDrawerWidget->ToggleOpen();
		}

		if (SceneWidget && ImGui::IsKeyPressed(ImGuiKey_N, false))
		{
			SceneWidget->NewScene();
		}
		if (SceneWidget && ImGui::IsKeyPressed(ImGuiKey_O, false))
		{
			FString PickedPath;
			if (OpenSceneFileDialog(PickedPath))
			{
				SceneWidget->LoadSceneFromFilePath(PickedPath);
			}
		}
		if (SceneWidget && ImGui::IsKeyPressed(ImGuiKey_S, false))
		{
			FString PickedPath;
			if (SaveSceneFileDialog(PickedPath))
			{
				SceneWidget->SaveSceneToFilePath(PickedPath);
			}
		}
	}

	ImVec2 OriginalPadding = ImGui::GetStyle().FramePadding;
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(OriginalPadding.x, 5.0f));

	bool bMenuBarOpened = ImGui::BeginMainMenuBar();

	ImGui::PopStyleVar();

	if (!bMenuBarOpened)
	{
		return;
	}

	RenderFilesMenu();
	RenderViewMenu();
	RenderEditMenu();
	RenderHelpMenu();

	if (PlayStreamWidget)
	{
		PlayStreamWidget->Render(DeltaTime);
	}

	ImGui::EndMainMenuBar();
}

void FEditorToolbarWidget::RenderViewportToolBarItems(int32 ViewportIndex)
{
	RenderAddActorMenu(ViewportIndex);
	ImGui::SameLine();
	RenderGizmoTools();
	ImGui::SameLine();
	ImGui::TextDisabled("|");
	ImGui::SameLine();
}

void FEditorToolbarWidget::RenderAddActorMenu(int32 ViewportIndex)
{
	constexpr ImVec2 ToolButtonSize(68.0f, 22.0f);
	constexpr float CountWidth = 48.0f;

	ImGui::PushID(ViewportIndex);
	if (ImGui::Button("+  Add", ToolButtonSize))
	{
		ImGui::OpenPopup("AddActorMenu");
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth(CountWidth);
	if (ImGui::DragInt("##SpawnCount", &SpawnCount, 0.1f, 1, 100))
	{
		SpawnCount = std::clamp(SpawnCount, 1, 100);
	}
	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("Spawn count");
	}

	if (!ImGui::BeginPopup("AddActorMenu"))
	{
		ImGui::PopID();
		return;
	}

	if (!EditorEngine)
	{
		ImGui::TextDisabled("No editor world.");
		ImGui::EndPopup();
		ImGui::PopID();
		return;
	}

	UWorld* World = EditorEngine->GetFocusedWorld();
	if (!World)
	{
		ImGui::TextDisabled("No focused world.");
		ImGui::EndPopup();
		ImGui::PopID();
		return;
	}

	for (const FAddActorEntry& Entry : AddActorTypes)
	{
		if (ImGui::Selectable(Entry.Label))
		{
			AActor* LastSpawnedActor = nullptr;
			const FVector BaseLocation = GetActorPlacementLocation(ViewportIndex);
			const int32 ClampedCount = std::clamp(SpawnCount, 1, 100);
			for (int32 Index = 0; Index < ClampedCount; ++Index)
			{
				const FVector Offset(static_cast<float>(Index) * 1.5f, 0.0f, 0.0f);
				LastSpawnedActor = Entry.Spawn(World, BaseLocation + Offset);
			}

			if (LastSpawnedActor)
			{
				EditorEngine->GetSelectionManager().Select(LastSpawnedActor);
			}
			SpawnCount = 1;
		}
	}

	ImGui::EndPopup();
	ImGui::PopID();
}

void FEditorToolbarWidget::RenderGizmoTools()
{
	if (!EditorEngine || !EditorEngine->GetGizmo())
	{
		ImGui::TextDisabled("Gizmo unavailable");
		return;
	}

	constexpr ImVec2 GizmoModeButtonSize(58.0f, 22.0f);

	UGizmoComponent* Gizmo = EditorEngine->GetGizmo();
	if (ImGui::Selectable("Move", Gizmo->IsTranslateMode(), 0, GizmoModeButtonSize))
	{
		Gizmo->SetTranslateMode();
	}
	ImGui::SameLine();
	if (ImGui::Selectable("Rotate", Gizmo->IsRotateMode(), 0, GizmoModeButtonSize))
	{
		Gizmo->SetRotateMode();
	}
	ImGui::SameLine();
	if (ImGui::Selectable("Scale", Gizmo->IsScaleMode(), 0, GizmoModeButtonSize))
	{
		Gizmo->SetScaleMode();
	}
	ImGui::SameLine();

	const char* SpaceLabel = Gizmo->IsWorldSpace() ? "World" : "Local";
	ImGui::SetNextItemWidth(76.0f);
	if (ImGui::BeginCombo("##GizmoSpace", SpaceLabel))
	{
		const bool bWorldSelected = Gizmo->IsWorldSpace();
		if (ImGui::Selectable("World", bWorldSelected))
		{
			Gizmo->SetWorldSpace(true);
		}
		if (ImGui::Selectable("Local", !bWorldSelected))
		{
			Gizmo->SetWorldSpace(false);
		}
		ImGui::EndCombo();
	}
}

FVector FEditorToolbarWidget::GetActorPlacementLocation(int32 ViewportIndex) const
{
	if (!EditorEngine)
	{
		return FVector::ZeroVector;
	}

	const FEditorViewportLayout& Layout = EditorEngine->GetViewportLayout();
	const int32 ClampedViewportIndex =
		std::clamp(ViewportIndex, 0, FEditorViewportLayout::MaxViewports - 1);
	const FEditorViewportClient* Client = Layout.GetViewportClient(ClampedViewportIndex);
	const FViewportCamera* Camera = Client ? Client->GetCamera() : nullptr;
	if (!Camera)
	{
		return FVector::ZeroVector;
	}

	FVector Forward = Camera->GetEffectiveForward();
	if (Forward.IsNearlyZero())
	{
		Forward = Camera->GetForwardVector();
	}
	Forward.NormalizeSafe();
	return Camera->GetLocation() + Forward * 5.0f;
}

void FEditorToolbarWidget::RenderFilesMenu()
{
	if (!ImGui::BeginMenu("Files"))
	{
		return;
	}

	if (SceneWidget)
	{
		if (ImGui::MenuItem("New Scene", "Ctrl+N"))
		{
			SceneWidget->NewScene();
		}
		if (ImGui::MenuItem("Load Scene", "Ctrl+O"))
		{
			FString PickedPath;
			if (OpenSceneFileDialog(PickedPath))
			{
				SceneWidget->LoadSceneFromFilePath(PickedPath);
			}
		}
		if (ImGui::MenuItem("Save Scene", "Ctrl+S"))
		{
			FString PickedPath;
			if (SaveSceneFileDialog(PickedPath))
			{
				SceneWidget->SaveSceneToFilePath(PickedPath);
			}
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Reload Asset From Disk"))
		{
			SceneWidget->RefreshSceneAndAssets();
			if (ContentDrawerWidget)
			{
				ContentDrawerWidget->RefreshAssetTree();
			}
		}
		if (ImGui::MenuItem("Open Asset Folder"))
		{
			const std::wstring AssetDir = FPaths::ToAbsolute(L"Asset");
			ShellExecuteW(nullptr, L"open", AssetDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
		}
	}
	else
	{
		ImGui::MenuItem("New Scene", "Ctrl+N", false, false);
		ImGui::MenuItem("Load Scene", "Ctrl+O", false, false);
		ImGui::MenuItem("Save Scene", "Ctrl+S", false, false);
		ImGui::Separator();
		ImGui::MenuItem("Reload Asset From Disk", nullptr, false, false);
		ImGui::MenuItem("Open Asset Folder", nullptr, false, false);
	}

	ImGui::EndMenu();
}

void FEditorToolbarWidget::RenderViewMenu()
{
	if (!ImGui::BeginMenu("View"))
	{
		return;
	}

	if (bShowConsole) ImGui::MenuItem("Console", nullptr, bShowConsole);
	if (bShowProperty) ImGui::MenuItem("Property", nullptr, bShowProperty);
	if (bShowSceneManager) ImGui::MenuItem("Scene Manager", nullptr, bShowSceneManager);
	if (bShowMaterialEditor) ImGui::MenuItem("Material Editor", nullptr, bShowMaterialEditor);
	if (bShowStatProfiler) ImGui::MenuItem("Stat Profiler", nullptr, bShowStatProfiler);
	if (bShowSkeletalMeshViewer) ImGui::MenuItem("SkeletalMesh Viewer", nullptr, bShowSkeletalMeshViewer);

	if (ContentDrawerWidget)
	{
		bool bContentDrawerOpen = ContentDrawerWidget->IsOpen();
		if (ImGui::MenuItem("Content Drawer", "Ctrl+Space", bContentDrawerOpen))
		{
			ContentDrawerWidget->SetOpen(!bContentDrawerOpen);
		}
	}
	else
	{
		ImGui::MenuItem("Content Drawer", "Ctrl+Space", false, false);
	}

	if (ViewportOverlayWidget)
	{
		bool bShowViewportSettings = ViewportOverlayWidget->IsViewportSettingsVisible();
		if (ImGui::MenuItem("Viewport Settings", nullptr, bShowViewportSettings))
		{
			ViewportOverlayWidget->SetViewportSettingsVisible(!bShowViewportSettings);
		}
	}

	ImGui::EndMenu();
}

void FEditorToolbarWidget::RenderEditMenu()
{
	if (!ImGui::BeginMenu("Edit"))
	{
		return;
	}

	if (ImGui::MenuItem("Remove Cache from Disk"))
	{
		FResourceManager::Get().DeleteAllCacheFiles();
	}

	ImGui::EndMenu();
}

void FEditorToolbarWidget::RenderHelpMenu()
{
	if (!ImGui::BeginMenu("Help"))
	{
		return;
	}

	if (ViewportOverlayWidget)
	{
		if (ImGui::MenuItem("Shortcuts"))
		{
			ViewportOverlayWidget->SetShortcutsWindowVisible(true);
		}
	}
	else
	{
		ImGui::MenuItem("Shortcuts", nullptr, false, false);
	}

	ImGui::EndMenu();
}
