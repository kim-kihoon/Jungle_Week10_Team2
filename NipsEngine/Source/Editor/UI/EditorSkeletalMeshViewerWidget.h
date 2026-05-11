#pragma once

#include "Editor/UI/EditorWidget.h"
#include "Editor/SkeletalMesh/SkeletalMeshPreviewScene.h"

class FEditorSkeletalMeshViewerWidget : public FEditorWidget
{
public:
	void Initialize(UEditorEngine* InEditorEngine) override;
	void Render(float DeltaTime) override;

	void SetOpen(bool bInOpen) { bIsOpen = bInOpen; }
	bool IsOpen() const { return bIsOpen; }
	bool IsViewportInputActive() const { return PreviewScene.IsInputCaptured(); }

	FSkeletalMeshPreviewScene* GetPreviewScene() { return &PreviewScene; }
	const FSkeletalMeshPreviewScene* GetPreviewScene() const { return &PreviewScene; }

private:
	void RenderToolbar();

	bool bIsOpen = false;
	int32 SelectedMeshPathIndex = -1;
	TArray<FString> CachedSkeletalMeshPaths;

	FSkeletalMeshPreviewScene PreviewScene;
};
