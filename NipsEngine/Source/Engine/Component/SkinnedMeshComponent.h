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

	const TArray<FTransform>& GetCurrentLocalTransforms() const { return CurrentLocalTransforms; }
	const TArray<FMatrix>& GetCurrentComponentSpaceMatrices() const { return CurrentComponentSpaceMatrices; }
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

	TArray<FTransform> CurrentLocalTransforms;
	TArray<FMatrix> CurrentComponentSpaceMatrices;
	TArray<FMatrix> SkinningMatrices;

	bool bBoneTransformsDirty = true;
	bool bRenderStateDirty = true;
};
