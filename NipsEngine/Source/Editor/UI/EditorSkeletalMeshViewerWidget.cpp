#include "EditorSkeletalMeshViewerWidget.h"
#include "d3d11.h"
#include "ImGui/imgui.h"

void FEditorSkeletalMeshViewerWidget::Initialize(UEditorEngine* InEditorEngine)
{
	// 1. 부모 초기화 (EditorEngine 포인터 저장)
	FEditorWidget::Initialize(InEditorEngine);

	// 2. 뷰어 전용 씬 초기화 (내부에서 카메라, 월드, 렌더타겟 세팅됨)
	PreviewScene.Initialize(InEditorEngine);
}

void FEditorSkeletalMeshViewerWidget::Render(float DeltaTime)
{
	// 열려있지 않으면 아무것도 그리지 않음 (Tick 연산도 멈춤)
	if (!bIsOpen)
	{
		PreviewScene.SetVisible(false);
		return;
	}

	PreviewScene.SetVisible(true);

	// 뷰어 창 띄우기
	if (ImGui::Begin("SkeletalMesh Viewer", &bIsOpen))
	{
		// 마우스 입력 등으로 카메라 조작이 필요하므로 매 프레임 Tick 갱신
		PreviewScene.Tick(DeltaTime);

		// 1. ImGui 창 내부의 가용 사이즈 얻기
		ImVec2 ViewportSize = ImGui::GetContentRegionAvail();
		if (ViewportSize.x > 0 && ViewportSize.y > 0)
		{
			// 2. PreviewScene 에 현재 창 크기 알려주기
			PreviewScene.SetViewportSize(static_cast<uint32>(ViewportSize.x), static_cast<uint32>(ViewportSize.y));

			// 3. RenderTarget에 그려진 최종 SRV 가져와서 출력!
			if (ID3D11ShaderResourceView* SRV = PreviewScene.GetSceneViewport().GetOutSRV())
			{
				ImGui::Image(reinterpret_cast<ImTextureID>(SRV), ViewportSize);
			}
			else
			{
				ImGui::Dummy(ViewportSize); // 텍스처가 아직 없으면 빈 공간
			}
		}
	}
	ImGui::End();
}
