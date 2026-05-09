#pragma once

#include "MeshComponent.h"
#include "Asset/SkeletalMesh.h"
#include "Render/Resource/Material.h"

class USkeletalMeshComponent : public UMeshComponent
{
public:
	DECLARE_CLASS(USkeletalMeshComponent, UMeshComponent)
	USkeletalMeshComponent() = default;
	~USkeletalMeshComponent() override;

	void PostDuplicate(UObject* Original) override;
	void Serialize(FArchive& Ar) override;

	void SetSkeletalMesh(USkeletalMesh* InSkeletalMesh);
	USkeletalMesh* GetSkeletalMesh() const;
	bool HasValidMesh() const;
	FDynamicMeshBuffer* GetRenderBuffer();
	void TestReferencePoseSkinning();

	const TArray<FNormalVertex>& GetRenderVertices() const;
	const TArray<uint32>& GetRenderIndices() const;
	const TArray<FSkeletalMeshSection>& GetSections() const;

	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	void PostEditProperty(const char* PropertyName) override;

	void UpdateWorldAABB() const override;
	bool RaycastMesh(const FRay& Ray, FHitResult& OutHitResult) override;
	EPrimitiveType GetPrimitiveType() const override { return EPrimitiveType::EPT_SkeletalMesh; }

	const FAABB& GetWorldAABB() const override;

	void UpdateRenderVertices(ID3D11DeviceContext* InContext, const TArray<FNormalVertex>& InVertices);

	void InitializeBonePoses();
	void UpdateBoneMatrices();
	void PerformCPUSkinning();

private:
	void RebuildRenderVertices();
	void RebuildMeshBuffer();
	void MarkBoundsDirty();
	void MarkRenderBufferDirty();
	void EnsureBoundsUpdated() const;

protected:
	TArray<FTransform> LocalBoneTransforms;
	TArray<FMatrix> GlobalBoneMatrices;
	TArray<FMatrix> SkinningMatrices;

private:
	USkeletalMesh* SkeletalMeshAsset = nullptr;
	FString SkeletalMeshAssetPath;

	TArray<FNormalVertex> RenderVertices;
	FDynamicMeshBuffer MeshBuffer;
	mutable bool bBoundsDirty = true;
	bool bRenderBufferDirty = true;
};
