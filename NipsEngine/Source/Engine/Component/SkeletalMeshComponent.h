#pragma once

#include "SkinnedMeshComponent.h"

class USkeletalMeshComponent : public USkinnedMeshComponent
{
public:
	DECLARE_CLASS(USkeletalMeshComponent, USkinnedMeshComponent)

	void SetSkeletalMesh(FSkeletalMesh* InSkeletalMesh) { SetSkinnedMesh(InSkeletalMesh); }
	FSkeletalMesh* GetSkeletalMesh() const { return GetSkinnedMesh(); }
	bool HasValidSkeletalMesh() const { return HasValidSkinnedMesh(); }
	const FString& GetSkeletalMeshAssetPath() const { return GetSkinnedMeshAssetPath(); }

	EPrimitiveType GetPrimitiveType() const override { return EPrimitiveType::EPT_SkeletalMesh; }
};
