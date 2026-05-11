#pragma once

#include "Core/CoreMinimal.h"
#include "Object/FName.h"
#include "Editor/Viewport/FSceneViewport.h" 
#include "Editor/Viewport/SkeletalMeshPreviewViewportClient.h"

#include "Editor/Input/SkeletalMeshPreviewController.h"

class UEditorEngine;
class UWorld;
class AActor;
class USkeletalMesh;

class FSkeletalMeshPreviewScene
{
public:
	FSkeletalMeshPreviewScene() = default;
	~FSkeletalMeshPreviewScene();

	void Initialize(UEditorEngine* InEditor);
	void Shutdown();

	void Tick(float DeltaTime);

	// 뷰어 창이 숨겨지거나 포커스를 잃었을 때 연산 낭비를 막기 위함
	void SetVisible(bool bInVisible);

	void SetSkeletalMesh(USkeletalMesh* Mesh);
	void ResetPose();

	FSceneViewport& GetSceneViewport() { return PreviewViewport; }
	int32 GetViewportIndex() const { return 4; }

	void SetViewportSize(uint32 Width, uint32 Height);

	UWorld* GetWorld() const { return PreviewWorld; }
	AActor* GetPreviewActor() const { return PreviewActor; }
	FSkeletalMeshPreviewViewportClient& GetViewportClient() { return ViewportClient; }

	void SetInputRectFromScreenRect(float MinX, float MinY, float MaxX, float MaxY);
	void SetViewportHovered(bool bHovered) { bPreviewHovered = bHovered; }

	bool IsInputCaptured() const { return bPreviewInputCaptured; }

private:
	UEditorEngine* Editor = nullptr;
	FName WorldHandle;

	FSkeletalMeshPreviewViewportClient ViewportClient;
	FSceneViewport PreviewViewport;

	UWorld* PreviewWorld = nullptr;
	AActor* PreviewActor = nullptr;

	FInputRouter PreviewInputRouter;
	FSkeletalMeshPreviewController PreviewInputController;

	FViewportRect PreviewInputRect;
	bool bPreviewHovered = false;
	bool bPreviewInputCaptured = false;
};
