#include "EditorSkeletalMeshViewerWidget.h"

#include "d3d11.h"
#include "Engine/Core/ResourceManager.h"
#include "Editor/Settings/EditorSettings.h"
#include "ImGui/imgui.h"

namespace
{
	const char* GetPreviewViewportTypeName(EEditorViewportType Type)
	{
		switch (Type)
		{
		case EVT_Perspective:  return "Perspective";
		case EVT_OrthoTop:     return "Top";
		case EVT_OrthoBottom:  return "Bottom";
		case EVT_OrthoFront:   return "Front";
		case EVT_OrthoBack:    return "Back";
		case EVT_OrthoLeft:    return "Left";
		case EVT_OrthoRight:   return "Right";
		default:               return "Viewport";
		}
	}

	const char* GetPreviewViewModeName(EViewMode Mode)
	{
		switch (Mode)
		{
		case EViewMode::Lit:            return "Lit";
		case EViewMode::Unlit:          return "Unlit";
		case EViewMode::Wireframe:      return "Wireframe";
		case EViewMode::SceneDepth:     return "Scene Depth";
		case EViewMode::WorldNormal:    return "World Normal";
		case EViewMode::CascadeShadow:  return "Cascade Shadow";
		case EViewMode::DebugCollision: return "Collision";
		default:                        return "Lit";
		}
	}
}

void FEditorSkeletalMeshViewerWidget::Initialize(UEditorEngine* InEditorEngine)
{
	FEditorWidget::Initialize(InEditorEngine);
	PreviewScene.Initialize(InEditorEngine);
}

void FEditorSkeletalMeshViewerWidget::Render(float DeltaTime)
{
	if (!bIsOpen)
	{
		PreviewScene.SetVisible(false);
		return;
	}

	PreviewScene.SetVisible(true);

	// 뷰어 창 띄우기
	if (ImGui::Begin("SkeletalMesh Viewer", &bIsOpen, ImGuiWindowFlags_MenuBar))
	{
		RenderToolbar();

		// 에디터 세팅값을 뷰포트 클라이언트에 갱신
		FSkeletalMeshPreviewViewportClient& Client = PreviewScene.GetViewportClient();
		const FEditorSettings& Settings = FEditorSettings::Get();
		Client.SetMoveSpeed(Settings.CameraSpeed);
		Client.SetMoveSensitivity(Settings.CameraMoveSensitivity);
		Client.SetRotateSensitivity(Settings.CameraRotateSensitivity);
		Client.SetZoomSpeed(Settings.CameraZoomSpeed);

		// ImGui 창 내부의 가용 사이즈 얻기
		ImVec2 ViewportSize = ImGui::GetContentRegionAvail();
		if (ViewportSize.x > 0 && ViewportSize.y > 0)
		{
			// PreviewScene 에 현재 창 크기 알려주기
			PreviewScene.SetViewportSize(static_cast<uint32>(ViewportSize.x), static_cast<uint32>(ViewportSize.y));

			// RenderTarget에 그려진 최종 SRV 가져와서 출력
			if (ID3D11ShaderResourceView* SRV = PreviewScene.GetSceneViewport().GetOutSRV())
			{
				ImGui::Image(reinterpret_cast<ImTextureID>(SRV), ViewportSize);
			}
			else
			{
				ImGui::Dummy(ViewportSize); // 텍스처가 아직 없으면 빈 공간
			}

			// ImGui 이미지 영역의 화면 좌표와 Hover 상태를 추출하여 Scene에 전달
			ImVec2 Min = ImGui::GetItemRectMin();
			ImVec2 Max = ImGui::GetItemRectMax();
			PreviewScene.SetInputRectFromScreenRect(Min.x, Min.y, Max.x, Max.y);
			PreviewScene.SetViewportHovered(ImGui::IsItemHovered());
		}

		PreviewScene.Tick(DeltaTime);
	}
	ImGui::End();
}

void FEditorSkeletalMeshViewerWidget::RenderToolbar()
{
	FSkeletalMeshPreviewViewportClient& Client = PreviewScene.GetViewportClient();
	FEditorViewportState& State = PreviewScene.GetSceneViewport().GetState();

	if (!ImGui::BeginMenuBar())
	{
		return;
	}

	ImGui::TextDisabled("SkeletalMesh Viewer | %s | %s",
						GetPreviewViewportTypeName(Client.GetViewportType()),
						GetPreviewViewModeName(State.ViewMode));
	ImGui::SameLine();

	if (ImGui::BeginMenu("Type"))
	{
		static constexpr EEditorViewportType ViewTypes[] = {
			EVT_Perspective,
			EVT_OrthoTop,
			EVT_OrthoBottom,
			EVT_OrthoFront,
			EVT_OrthoBack,
			EVT_OrthoLeft,
			EVT_OrthoRight,
		};

		for (EEditorViewportType Type : ViewTypes)
		{
			const bool bSelected = (Client.GetViewportType() == Type);
			if (ImGui::MenuItem(GetPreviewViewportTypeName(Type), nullptr, bSelected))
			{
				Client.SetViewportType(Type);
				Client.ApplyCameraMode();
			}
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("View"))
	{
		static constexpr EViewMode MainModes[] = {
			EViewMode::Lit,
			EViewMode::Unlit,
			EViewMode::Wireframe,
			EViewMode::SceneDepth,
			EViewMode::WorldNormal,
		};

		for (EViewMode Mode : MainModes)
		{
			const bool bSelected = (State.ViewMode == Mode);
			if (ImGui::MenuItem(GetPreviewViewModeName(Mode), nullptr, bSelected))
			{
				State.ViewMode = Mode;
			}
		}

		ImGui::Separator();

		static constexpr EViewMode DebugModes[] = {
			EViewMode::CascadeShadow,
			EViewMode::DebugCollision,
		};

		for (EViewMode Mode : DebugModes)
		{
			const bool bSelected = (State.ViewMode == Mode);
			if (ImGui::MenuItem(GetPreviewViewModeName(Mode), nullptr, bSelected))
			{
				State.ViewMode = Mode;
			}
		}
		ImGui::EndMenu();
	}

	CachedSkeletalMeshPaths = FResourceManager::Get().GetSkeletalMeshPaths();
	if (SelectedMeshPathIndex >= static_cast<int32>(CachedSkeletalMeshPaths.size()))
	{
		SelectedMeshPathIndex = CachedSkeletalMeshPaths.empty() ? -1 : 0;
	}

	const char* MeshPreviewLabel = "None";
	if (SelectedMeshPathIndex >= 0 && SelectedMeshPathIndex < static_cast<int32>(CachedSkeletalMeshPaths.size()))
	{
		MeshPreviewLabel = CachedSkeletalMeshPaths[SelectedMeshPathIndex].c_str();
	}

	if (ImGui::BeginMenu("Mesh"))
	{
		for (int32 i = 0; i < static_cast<int32>(CachedSkeletalMeshPaths.size()); ++i)
		{
			const bool bSelected = (SelectedMeshPathIndex == i);
			if (ImGui::MenuItem(CachedSkeletalMeshPaths[i].c_str(), nullptr, bSelected))
			{
				SelectedMeshPathIndex = i;
				PreviewScene.SetSkeletalMesh(FResourceManager::Get().LoadSkeletalMesh(CachedSkeletalMeshPaths[i]));
			}
		}
		if (CachedSkeletalMeshPaths.empty())
		{
			ImGui::MenuItem("No SkeletalMesh assets", nullptr, false, false);
		}
		ImGui::EndMenu();
	}

	if (ImGui::MenuItem("Reset Camera"))
	{
		Client.ResetCamera();
		Client.ApplyCameraMode();
	}

	ImGui::EndMenuBar();
}
