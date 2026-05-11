#pragma once

#include "Viewport/ViewportClient.h"
#include "Engine/Viewport/ViewportCamera.h"
#include "Engine/Runtime/SceneView.h"
#include "Math/Utils.h"

class FSkeletalMeshPreviewScene;

class FSkeletalMeshPreviewViewportClient : public FViewportClient
{
public:
	FSkeletalMeshPreviewViewportClient();
	virtual ~FSkeletalMeshPreviewViewportClient() = default;

	virtual void Tick(float DeltaTime) override;
	virtual void BuildSceneView(FSceneView& OutView) const override;

	void SetPreviewScene(FSkeletalMeshPreviewScene* InScene) { PreviewScene = InScene; }
	void SetViewportSize(float InWidth, float InHeight) override;
	FViewportCamera* GetCamera() { return &Camera; }

	// 마우스 입력을 통한 Orbit 카메라 제어용 함수
	void AddOrbitInput(float DeltaYaw, float DeltaPitch);
	void AddZoomInput(float DeltaZoom);
	void ResetCamera();

private:
	void UpdateCameraTransform();

private:
	FSkeletalMeshPreviewScene* PreviewScene = nullptr;
	FViewportCamera Camera;

	// Orbit 카메라 상태
	FVector ViewTarget = FVector(0.0f, 0.0f, 100.0f); // 메시의 대략적인 중심점 (필요시 바운딩 박스 기준으로 갱신)
	float OrbitPitch = 15.0f;
	float OrbitYaw = 45.0f;
	float OrbitDistance = 300.0f;
};