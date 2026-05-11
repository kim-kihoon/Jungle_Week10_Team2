#pragma once

#include "Editor/UI/EditorWidget.h"
#include "Editor/SkeletalMesh/SkeletalMeshPreviewScene.h"

class FEditorSkeletalMeshViewerWidget : public FEditorWidget
{
public:
	virtual void Initialize(UEditorEngine* InEditorEngine) override;
	virtual void Render(float DeltaTime) override;

	void Open() { bIsOpen = true; }
	void Close() { bIsOpen = false; }
	void SetOpen(bool bInOpen) { bIsOpen = bInOpen; }
	bool IsOpen() const { return bIsOpen; }

	FSkeletalMeshPreviewScene* GetPreviewScene() { return &PreviewScene; }

private:
	void RenderToolbar();

	bool bIsOpen = false;
	int32 SelectedMeshPathIndex = -1;
	TArray<FString> CachedSkeletalMeshPaths;

	FSkeletalMeshPreviewScene PreviewScene;
};
