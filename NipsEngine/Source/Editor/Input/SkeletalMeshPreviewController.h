#pragma once

#include "Input/IInputController.h"

class FSkeletalMeshPreviewViewportClient;

class FSkeletalMeshPreviewController : public IInputController
{
public:
	FSkeletalMeshPreviewController();
	virtual ~FSkeletalMeshPreviewController() = default;

	void SetViewportClient(FSkeletalMeshPreviewViewportClient* InViewportClient);

	virtual void Tick(float InDeltaTime) override;
	virtual void OnMouseMove(float DeltaX, float DeltaY) override;
	virtual void OnMouseMoveAbsolute(float X, float Y) override;
	virtual void OnLeftMouseClick(float X, float Y) override;
	virtual void OnLeftMouseDragEnd(float X, float Y) override;
	virtual void OnLeftMouseButtonUp(float X, float Y) override;
	virtual void OnRightMouseClick(float DeltaX, float DeltaY) override;
	virtual void OnLeftMouseDrag(float X, float Y) override;
	virtual void OnRightMouseDrag(float DeltaX, float DeltaY) override;
	virtual void OnMiddleMouseDrag(float DeltaX, float DeltaY) override;
	virtual void OnKeyPressed(int VK) override;
	virtual void OnKeyDown(int VK) override;
	virtual void OnKeyReleased(int VK) override;
	virtual void OnWheelScrolled(float Notch) override;

	void OnRightMouseButtonUp();

private:
	FSkeletalMeshPreviewViewportClient* ViewportClient = nullptr;

	bool bIsRMBDown = false;

	bool bMoveForward = false;
	bool bMoveBackward = false;
	bool bMoveRight = false;
	bool bMoveLeft = false;
	bool bMoveUp = false;
	bool bMoveDown = false;
};