#pragma once

#include "Component/SkinnedMeshComponent.h"
#include "Render/Resource/VertexTypes.h"

class USkeletalMeshComponent : public USkinnedMeshComponent
{
public:
	DECLARE_CLASS(USkeletalMeshComponent, USkinnedMeshComponent)

	USkeletalMeshComponent() = default;
	void PostDuplicate(UObject* Original) override;

	void SetSkeletalMesh(USkeletalMesh* InSkeletalMesh);

	void SkinVerticesCPU();
	const TArray<FNormalVertex>& GetSkinnedVertices() const;
	const TArray<uint32>& GetSkinnedIndices() const;
	const TArray<FSkeletalMeshSection>& GetSections() const;

	EPrimitiveType GetPrimitiveType() const override { return EPrimitiveType::EPT_SkeletalMesh; }
	void UpdateWorldAABB() const override;
	bool RaycastMesh(const FRay& Ray, FHitResult& OutHitResult) override;
	const FAABB& GetWorldAABB() const override;

protected:
	void OnSkeletalMeshChanged() override;

private:
	void EnsureSkinnedVerticesUpdated() const;
	void MarkBoundsDirty();
	void EnsureBoundsUpdated() const;

private:
	TArray<FNormalVertex> SkinnedVertices;
	FAABB SkinnedLocalBounds;
	mutable bool bSkinningDirty = true;
	mutable bool bBoundsDirty = true;
};
