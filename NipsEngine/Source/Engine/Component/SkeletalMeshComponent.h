#pragma once

#include "SkinnedMeshComponent.h"

class USkeletalMeshComponent : public USkinnedMeshComponent
{
public:
	DECLARE_CLASS(USkeletalMeshComponent, USkinnedMeshComponent)
	USkeletalMeshComponent() = default;
	virtual ~USkeletalMeshComponent() override = default;

	virtual void SetSkeletalMesh(USkeletalMesh* InSkeletalMesh) override;

	void SetBoneLocalTransform(int32 BoneIndex, const FTransform& NewTransform);

	virtual void TickComponent(float DeltaTime) override;

	void InitializeBonePoses();
	void UpdateBoneMatrices();

	void TestReferencePoseSkinning();

protected:
	TArray<FTransform> LocalBoneTransforms;
	TArray<FMatrix> GlobalBoneMatrices;

	bool bPoseDirty = true;
};