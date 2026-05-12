#pragma once

#include "Editor/UI/EditorWidget.h"
#include "Editor/SkeletalMesh/SkeletalMeshPreviewScene.h"
#include "Engine/Asset/SkeletalMesh.h"

class FEditorSkeletalMeshViewerWidget : public FEditorWidget
{
public:
	void Initialize(UEditorEngine* InEditorEngine) override;
	void Render(float DeltaTime) override;

	void SetOpen(bool bInOpen) { bIsOpen = bInOpen; }
	bool IsOpen() const { return bIsOpen; }
	bool IsViewportInputActive() const { return bIsOpen && PreviewScene.IsInputActive(); }

	FSkeletalMeshPreviewScene* GetPreviewScene() { return &PreviewScene; }
	const FSkeletalMeshPreviewScene* GetPreviewScene() const { return &PreviewScene; }

private:
	void RenderToolbar();

	void RenderBoneHierarchyPanel();
	void RenderBoneTree(int32 BoneIndex, const TArray<FSkeletalBone>& Bones);
	void RenderBoneDetailsPanel();

	void RebuildBoneCache(USkeletalMesh* Mesh);
	void SyncCurrentMeshFromPreview();
	void RefreshSkeletalMeshPathCache();

	bool bIsOpen = false;
	int32 SelectedMeshPathIndex = -1;
	TArray<FString> CachedSkeletalMeshPaths;

	FSkeletalMeshPreviewScene PreviewScene;

	USkeletalMesh* CurrentSkeletalMesh = nullptr;
	int32 SelectedBoneIndex = -1;

	TArray<TArray<int32>> CachedBoneChildren;

	TArray<int32> RootBones;
	uint32 LastViewportWidth = 0;
	uint32 LastViewportHeight = 0;
};
