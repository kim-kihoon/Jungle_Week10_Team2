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

	const TArray<FTransform>& GetBoneSpaceTransforms() const { return BoneSpaceTransforms; }
	const TArray<FMatrix>& GetComponentSpaceTransforms() const { return ComponentSpaceTransforms; }
	const TArray<FMatrix>& GetSkinningMatrices() const { return SkinningMatrices; }

	void RefreshBoneTransforms();
	void MarkSkinningDirty();
	bool ConsumeRenderStateDirty();

protected:
	virtual void OnSkeletalMeshChanged();
	void InitializeReferencePose();
	void UpdateSkinningMatrices();

protected:
	USkeletalMesh* SkeletalMesh = nullptr;
	FString SkeletalMeshAssetPath;

	TArray<FTransform> BoneSpaceTransforms;
	TArray<FMatrix> ComponentSpaceTransforms;
	TArray<FMatrix> SkinningMatrices;

	bool bBoneTransformsDirty = true;
	bool bRenderStateDirty = true;
};
