#include "Editor/UI/EditorControlWidget.h"

#include "Editor/EditorEngine.h"
#include "Engine/Viewport/ViewportCamera.h"
#include "Core/Logging/Timer.h"

#include "ImGui/imgui.h"
#include "Component/GizmoComponent.h"
#include "Component/SubUVComponent.h"
#include "Component/TextRenderComponent.h"
#include "Component/StaticMeshComponent.h"
#include "Core/ResourceManager.h"

#include "GameFramework/PrimitiveActors.h"

#define SEPARATOR()     \
	;                   \
	ImGui::Spacing();   \
	ImGui::Spacing();   \
	ImGui::Separator(); \
	ImGui::Spacing();   \
	ImGui::Spacing();

namespace
{
	// 월드에 특정 타입의 액터를 생성하고 초기화하는 템플릿 함수입니다.
	template <typename T>
	void SpawnActor(UWorld* World, const FVector& Location)
	{
		T* Actor = World->SpawnActor<T>();
		Actor->InitDefaultComponents();
		Actor->SetActorLocation(Location);
	}

	struct FSpawnEntry
	{
		const char* Label;
		void (*Spawn)(UWorld*, const FVector&);
	};

	static const FSpawnEntry PrimitiveTypes[] = {
		{ "Pawn", SpawnActor<APawnActor> },
		{ "Scene", SpawnActor<ASceneActor> },
		{ "StaticMesh", SpawnActor<AStaticMeshActor> },
		{ "SkeletalMesh", SpawnActor<ASkeletalMeshActor> },
		{ "TextRender", SpawnActor<ATextRenderActor> },
		{ "SubUV", SpawnActor<ASubUVActor> },
		{ "Billboard", SpawnActor<ABillboardActor> },
		{ "Decal", SpawnActor<ADecalActor> },
		{ "Directional Light", SpawnActor<ADirectionalLightActor> },
		{ "Ambient Light", SpawnActor<AAmbientLightActor> },
		{ "Point Light", SpawnActor<APointLightActor> },
		{ "Spot Light", SpawnActor<ASpotLightActor> },
		{ "Sky Atmosphere", SpawnActor<ASkyAtmosphereActor> },
		{ "Height Fog", SpawnActor<AHeightFogActor> },
		{ "Audio Zone", SpawnActor<AAudioZoneActor> },
		{ "Player Start", SpawnActor<APlayerStartActor> },
	};
}

// 에디터 컨트롤 위젯을 초기화하고 기본 상태를 설정합니다.
void FEditorControlWidget::Initialize(UEditorEngine* InEditorEngine)
{
	FEditorWidget::Initialize(InEditorEngine);
	SelectedPrimitiveType = 0;
}

// 컨트롤 패널 UI를 렌더링하고 액터 생성 및 카메라 제어 기능을 처리합니다.
void FEditorControlWidget::Render(float DeltaTime)
{
	(void)DeltaTime;
	if (!EditorEngine)
	{
		return;
	}

	ImGui::SetNextWindowCollapsed(false, ImGuiCond_Once);
	ImGui::SetNextWindowSize(ImVec2(500.0f, 480.0f), ImGuiCond_Once);

	ImGui::Begin("Jungle Control Panel");

	ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);

	if (ImGui::BeginCombo("Actor", PrimitiveTypes[SelectedPrimitiveType].Label))
	{
		for (int i = 0; i < IM_ARRAYSIZE(PrimitiveTypes); i++)
		{
			const bool bIsSelected = (SelectedPrimitiveType == i);
			if (ImGui::Selectable(PrimitiveTypes[i].Label, bIsSelected))
			{
				SelectedPrimitiveType = i;
			}

			if (bIsSelected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}

	if (ImGui::Button("Spawn"))
	{
		UWorld* World = EditorEngine->GetFocusedWorld();
		if (!World)
		{
			ImGui::End();
			return;
		}

		for (int32 i = 0; i < NumberOfSpawnedActors; i++)
		{
			PrimitiveTypes[SelectedPrimitiveType].Spawn(World, CurSpawnPoint);
		}
		NumberOfSpawnedActors = 1;
	}
	ImGui::InputInt("Number of Spawn", &NumberOfSpawnedActors, 1, 10);

	ImGui::PopItemWidth();

	SEPARATOR();

	// Gizmo Space / Mode
	int32 SelectedSpace = EditorEngine->GetGizmo()->IsWorldSpace() ? 0 : 1;
	if (ImGui::RadioButton("World", &SelectedSpace, 0))
	{
		EditorEngine->GetGizmo()->SetWorldSpace(true);
	}
	ImGui::SameLine();
	if (ImGui::RadioButton("Local", &SelectedSpace, 1))
	{
		EditorEngine->GetGizmo()->SetWorldSpace(false);
	}

	SEPARATOR();

	if (ImGui::Button("Translate"))
		EditorEngine->GetGizmo()->SetTranslateMode();

	ImGui::SameLine();

	if (ImGui::Button("Rotate"))
		EditorEngine->GetGizmo()->SetRotateMode();

	ImGui::SameLine();

	if (ImGui::Button("Scale"))
		EditorEngine->GetGizmo()->SetScaleMode();

	ImGui::End();
}
