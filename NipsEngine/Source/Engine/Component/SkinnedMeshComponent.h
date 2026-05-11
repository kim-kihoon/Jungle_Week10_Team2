#pragma once

#include "Component/MeshComponent.h"
#include "Asset/SkeletalMesh.h"

class USkinnedMeshComponent : public UMeshComponent
{
public:
	DECLARE_CLASS(USkinnedMeshComponent, UMeshComponent)

	void SetSkeletalMesh(USkeletalMesh* InSkeletalMesh);
	USkeletalMesh* GetSkeletalMesh() const { return SkeletalMesh; }
	bool HasValidMesh() const;

	const TArray<FTransform>& GetLocalTransforms() const { return LocalTransforms; }
	const TArray<FMatrix>& GetCurrentGlobalMatrices() const { return CurrentGlobalMatrices; }
	const TArray<FMatrix>& GetSkinningMatrices() const { return SkinningMatrices; }

	void RefreshBoneTransforms();
	void MarkSkinningDirty();
	bool ConsumeRenderStateDirty();

protected:
	virtual void OnSkeletalMeshChanged();
	void ResetToRefPose();
	void UpdateSkinningMatrices();

protected:
	USkeletalMesh* SkeletalMesh = nullptr;
	FString SkeletalMeshAssetPath;

	TArray<FTransform> LocalTransforms;
	TArray<FMatrix> CurrentGlobalMatrices;
	TArray<FMatrix> SkinningMatrices;

	bool bBoneTransformsDirty = true;
	bool bRenderStateDirty = true;
};
