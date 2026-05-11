#include "SkeletalMeshPreviewViewportClient.h"

FSkeletalMeshPreviewViewportClient::FSkeletalMeshPreviewViewportClient()
{
	Camera.SetProjectionType(EViewportProjectionType::Perspective);
	Camera.SetFOV(MathUtil::DegreesToRadians(90.0f));
	Camera.SetNearPlane(1.0f);
	Camera.SetFarPlane(100.0f);

	ResetCamera();
}

void FSkeletalMeshPreviewViewportClient::Tick(float DeltaTime)
{
	// 필요시 보간(Lerp) 로직을 여기에 추가할 수 있습니다.
	UpdateCameraTransform();
}

void FSkeletalMeshPreviewViewportClient::BuildSceneView(FSceneView& OutView) const
{
	const FViewportRect Rect(0, 0, static_cast<int32>(WindowWidth), static_cast<int32>(WindowHeight));
	Camera.BuildSceneView(OutView, Rect, EViewMode::Lit);
}

void FSkeletalMeshPreviewViewportClient::AddOrbitInput(float DeltaYaw, float DeltaPitch)
{
	OrbitYaw += DeltaYaw;
	OrbitPitch = MathUtil::Clamp(OrbitPitch + DeltaPitch, -85.0f, 85.0f); // 위아래로 완전히 넘어가지 않게 제한
	UpdateCameraTransform();
}

void FSkeletalMeshPreviewViewportClient::AddZoomInput(float DeltaZoom)
{
	OrbitDistance = MathUtil::Clamp(OrbitDistance - DeltaZoom, 0.5f, 20.0f);
	UpdateCameraTransform();
}

void FSkeletalMeshPreviewViewportClient::ResetCamera()
{
	OrbitPitch = 15.0f;
	OrbitYaw = -45.0f;
	OrbitDistance = 3.0f;
	ViewTarget = FVector(0.0f, 0.0f, 1.0f); // 1미터 정도 높이를 타겟으로
	UpdateCameraTransform();
}

void FSkeletalMeshPreviewViewportClient::UpdateCameraTransform()
{
	// 구면 좌표계(Spherical Coordinates)를 이용한 Orbit 회전 계산
	FQuat PitchQuat = FQuat(FVector::RightVector, MathUtil::DegreesToRadians(OrbitPitch));
	FQuat YawQuat = FQuat(FVector::UpVector, MathUtil::DegreesToRadians(OrbitYaw));
	FQuat CameraRot = YawQuat * PitchQuat;

	// 타겟 위치에서 뒤로(Forward의 반대) 거리만큼 이동
	FVector CameraLoc = ViewTarget - (CameraRot.GetForwardVector() * OrbitDistance);

	Camera.SetRotation(CameraRot);
	Camera.SetLocation(CameraLoc);
}