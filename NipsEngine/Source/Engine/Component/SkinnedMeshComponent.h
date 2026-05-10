#pragma once

#include "MeshComponent.h"
#include "Asset/SkeletalMesh.h"
#include "Render/Resource/Material.h"

class USkinnedMeshComponent : public UMeshComponent
{
public:
	DECLARE_CLASS(USkinnedMeshComponent, UMeshComponent)
	USkinnedMeshComponent() = default;
	virtual ~USkinnedMeshComponent() override = default;

	virtual void PostDuplicate(UObject* Original) override;
	virtual void Serialize(FArchive& Ar) override;

	virtual void SetSkeletalMesh(USkeletalMesh* InSkeletalMesh);
	USkeletalMesh* GetSkeletalMesh() const;
	bool HasValidMesh() const;
	FDynamicMeshBuffer* GetRenderBuffer();

	const TArray<FNormalVertex>& GetRenderVertices() const;
	const TArray<uint32>& GetRenderIndices() const;
	const TArray<FSkeletalMeshSection>& GetSections() const;

	virtual void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	virtual void PostEditProperty(const char* PropertyName) override;

	virtual void UpdateWorldAABB() const override;
	virtual bool RaycastMesh(const FRay& Ray, FHitResult& OutHitResult) override;
	virtual EPrimitiveType GetPrimitiveType() const override { return EPrimitiveType::EPT_SkeletalMesh; }

	virtual const FAABB& GetWorldAABB() const override;

	void UpdateRenderVertices(ID3D11DeviceContext* InContext, const TArray<FNormalVertex>& InVertices);

	virtual void PerformCPUSkinning();

protected:
	virtual void RebuildRenderVertices();
	virtual void RebuildMeshBuffer();
	void MarkBoundsDirty();
	void MarkRenderBufferDirty();
	void EnsureBoundsUpdated() const;

protected:
	TArray<FMatrix> SkinningMatrices;

	USkeletalMesh* SkeletalMeshAsset = nullptr;
	FString SkeletalMeshAssetPath;

	TArray<FNormalVertex> RenderVertices;
	FDynamicMeshBuffer MeshBuffer;

	mutable bool bBoundsDirty = true;
	bool bRenderBufferDirty = true;
};