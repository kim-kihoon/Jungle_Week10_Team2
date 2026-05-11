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
	if (ViewportType == EVT_Perspective)
	{
		UpdateCameraTransform();
	}
}

void FSkeletalMeshPreviewViewportClient::BuildSceneView(FSceneView& OutView) const
{
	const FViewportRect Rect(0, 0, static_cast<int32>(WindowWidth), static_cast<int32>(WindowHeight));
	Camera.BuildSceneView(OutView, Rect, State ? State->ViewMode : EViewMode::Lit);
}

void FSkeletalMeshPreviewViewportClient::SetViewportSize(float InWidth, float InHeight)
{
	FViewportClient::SetViewportSize(InWidth, InHeight);
	Camera.OnResize(static_cast<uint32>(WindowWidth), static_cast<uint32>(WindowHeight));
}

void FSkeletalMeshPreviewViewportClient::AddOrbitInput(float DeltaYaw, float DeltaPitch)
{
	if (ViewportType != EVT_Perspective)
	{
		return;
	}

	OrbitYaw += DeltaYaw;
	OrbitPitch = MathUtil::Clamp(OrbitPitch + DeltaPitch, -85.0f, 85.0f); // 위아래로 완전히 넘어가지 않게 제한
	UpdateCameraTransform();
}

void FSkeletalMeshPreviewViewportClient::AddZoomInput(float DeltaZoom)
{
	if (ViewportType == EVT_Perspective)
	{
		OrbitDistance = MathUtil::Clamp(OrbitDistance - DeltaZoom, 0.5f, 20.0f);
		UpdateCameraTransform();
	}
	else
	{
		Camera.SetOrthoHeight(MathUtil::Clamp(Camera.GetOrthoHeight() - DeltaZoom, 0.1f, 100.0f));
	}
}

void FSkeletalMeshPreviewViewportClient::ResetCamera()
{
	OrbitPitch = 15.0f;
	OrbitYaw = -45.0f;
	OrbitDistance = 3.0f;
	ViewTarget = FVector(0.0f, 0.0f, 1.0f); // 1미터 정도 높이를 타겟으로
	UpdateCameraTransform();
}

void FSkeletalMeshPreviewViewportClient::ApplyCameraMode()
{
	Camera.SetRotation(FRotator(0.f, 0.f, 0.f));

	switch (ViewportType)
	{
	case EVT_Perspective:
		Camera.SetProjectionType(EViewportProjectionType::Perspective);
		Camera.ClearCustomLookDir();
		UpdateCameraTransform();
		break;

	case EVT_OrthoTop:
		Camera.SetProjectionType(EViewportProjectionType::Orthographic);
		Camera.SetLocation(ViewTarget + FVector(0.f, 0.f, OrbitDistance));
		Camera.SetCustomLookDir(FVector(0.f, 0.f, -1.f), FVector(1.f, 0.f, 0.f));
		break;

	case EVT_OrthoBottom:
		Camera.SetProjectionType(EViewportProjectionType::Orthographic);
		Camera.SetLocation(ViewTarget + FVector(0.f, 0.f, -OrbitDistance));
		Camera.SetCustomLookDir(FVector(0.f, 0.f, 1.f), FVector(1.f, 0.f, 0.f));
		break;

	case EVT_OrthoFront:
		Camera.SetProjectionType(EViewportProjectionType::Orthographic);
		Camera.SetLocation(ViewTarget + FVector(OrbitDistance, 0.f, 0.f));
		Camera.SetCustomLookDir(FVector(-1.f, 0.f, 0.f), FVector(0.f, 0.f, 1.f));
		break;

	case EVT_OrthoBack:
		Camera.SetProjectionType(EViewportProjectionType::Orthographic);
		Camera.SetLocation(ViewTarget + FVector(-OrbitDistance, 0.f, 0.f));
		Camera.SetCustomLookDir(FVector(1.f, 0.f, 0.f), FVector(0.f, 0.f, 1.f));
		break;

	case EVT_OrthoLeft:
		Camera.SetProjectionType(EViewportProjectionType::Orthographic);
		Camera.SetLocation(ViewTarget + FVector(0.f, -OrbitDistance, 0.f));
		Camera.SetCustomLookDir(FVector(0.f, 1.f, 0.f), FVector(0.f, 0.f, 1.f));
		break;

	case EVT_OrthoRight:
		Camera.SetProjectionType(EViewportProjectionType::Orthographic);
		Camera.SetLocation(ViewTarget + FVector(0.f, OrbitDistance, 0.f));
		Camera.SetCustomLookDir(FVector(0.f, -1.f, 0.f), FVector(0.f, 0.f, 1.f));
		break;

	default:
		break;
	}
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
