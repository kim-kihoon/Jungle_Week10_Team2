#include "SkeletalMeshPreviewController.h"
#include "Viewport/SkeletalMeshPreviewViewportClient.h"
#include "Engine/Input/InputRouter.h"

constexpr int VK_W = 'W';
constexpr int VK_S = 'S';
constexpr int VK_A = 'A';
constexpr int VK_D = 'D';
constexpr int VK_Q = 'Q';
constexpr int VK_E = 'E';

FSkeletalMeshPreviewController::FSkeletalMeshPreviewController()
{
}

void FSkeletalMeshPreviewController::SetViewportClient(FSkeletalMeshPreviewViewportClient* InViewportClient)
{
	ViewportClient = InViewportClient;
}

void FSkeletalMeshPreviewController::Tick(float InDeltaTime)
{
	IInputController::Tick(InDeltaTime);

	if (!FInputRouter::GetKey(VK_RBUTTON))
	{
		bIsRMBDown = false;
	}

	if (!ViewportClient || !bInputEnabled)
		return;

	//우클릭 유지 중일 때만 WASD/QE 카메라 이동 허용
	// 이동 처리를 ViewportClient에 위임.
	if (bIsRMBDown)
	{
		float Forward = 0.0f;
		float Right = 0.0f;
		float Up = 0.0f;

		if (bMoveForward)  Forward += 1.0f;
		if (bMoveBackward) Forward -= 1.0f;
		if (bMoveRight)    Right += 1.0f;
		if (bMoveLeft)     Right -= 1.0f;
		if (bMoveUp)       Up += 1.0f;
		if (bMoveDown)     Up -= 1.0f;

		if (Forward != 0.0f || Right != 0.0f || Up != 0.0f)
		{
			ViewportClient->AddMoveInput(Forward, Right, Up, DeltaTime);
		}
	}
}

void FSkeletalMeshPreviewController::OnRightMouseClick(float DeltaX, float DeltaY)
{
	(void)DeltaX;
	(void)DeltaY;

	bIsRMBDown = true;

	if (ViewportClient)
	{
		ViewportClient->BeginCameraControl();
	}
}

void FSkeletalMeshPreviewController::OnRightMouseButtonUp()
{
	bIsRMBDown = false;
}

void FSkeletalMeshPreviewController::OnRightMouseDrag(float DeltaX, float DeltaY)
{
	if (!ViewportClient)
		return;

	// 수식 키(Ctrl, Alt, Shift)가 눌려 있으면 뷰포트 이동/회전을 차단 (EditorWorldController와 동일)
	if (bCtrlDown || bAltDown || bShiftDown)
		return;

	// ViewportClient가 내부적으로 Perspective/Ortho 분기를 처리하도록 위임
	ViewportClient->AddLookInput(DeltaX, DeltaY);
}

void FSkeletalMeshPreviewController::OnMiddleMouseDrag(float DeltaX, float DeltaY)
{
	if (!ViewportClient)
		return;

	if (bCtrlDown || bAltDown || bShiftDown)
		return;

	ViewportClient->AddPanInput(DeltaX, DeltaY, DeltaTime);
}

void FSkeletalMeshPreviewController::OnWheelScrolled(float Notch)
{
	if (!ViewportClient || Notch == 0.0f)
		return;

	if (bIsRMBDown)
	{
		// 우클릭 누른 상태로 휠: 카메라 이동 속도(MoveSpeedScale) 조절
		ViewportClient->AdjustMoveSpeedScale(Notch);
	}
	else
	{
		// 평상시 휠: 줌 인/아웃 (또는 Ortho 카메라 배율 조정)
		ViewportClient->AddZoomInput(Notch, DeltaTime);
	}
}

void FSkeletalMeshPreviewController::OnKeyDown(int VK)
{
	// 수식 키가 눌려있으면 단축키로 인식하고 카메라 이동 처리를 막음
	if (bCtrlDown || bAltDown || bShiftDown)
		return;

	switch (VK)
	{
	case VK_W: bMoveForward = true; break;
	case VK_S: bMoveBackward = true; break;
	case VK_A: bMoveLeft = true; break;
	case VK_D: bMoveRight = true; break;
	case VK_E: bMoveUp = true; break;
	case VK_Q: bMoveDown = true; break;
	}
}

void FSkeletalMeshPreviewController::OnKeyReleased(int VK)
{
	switch (VK)
	{
	case VK_W: bMoveForward = false; break;
	case VK_S: bMoveBackward = false; break;
	case VK_A: bMoveLeft = false; break;
	case VK_D: bMoveRight = false; break;
	case VK_E: bMoveUp = false; break;
	case VK_Q: bMoveDown = false; break;
	}
}

// ---------------------------------------------------------
// 사용하지 않는 입력 인터페이스 구현 (추후 Raycast, Gizmo 대응용)
// ---------------------------------------------------------
void FSkeletalMeshPreviewController::OnMouseMove(float DeltaX, float DeltaY) {}
void FSkeletalMeshPreviewController::OnMouseMoveAbsolute(float X, float Y) {}
void FSkeletalMeshPreviewController::OnLeftMouseClick(float X, float Y) {}
void FSkeletalMeshPreviewController::OnLeftMouseDragEnd(float X, float Y) {}
void FSkeletalMeshPreviewController::OnLeftMouseButtonUp(float X, float Y) {}
void FSkeletalMeshPreviewController::OnLeftMouseDrag(float X, float Y) {}
void FSkeletalMeshPreviewController::OnKeyPressed(int VK) {}
