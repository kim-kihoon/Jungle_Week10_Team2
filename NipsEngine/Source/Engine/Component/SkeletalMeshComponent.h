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

	bool InitializeSkeletalMesh(const FString& FilePath);
	bool LoadSkeletalMesh(const FString& FilePath);
	void SetSkeletalMesh(USkeletalMesh* InSkeletalMesh, bool bTakeOwnership = false);
	USkeletalMesh* GetSkeletalMesh() const;
	bool HasValidMesh() const;
	FDynamicMeshBuffer* GetRenderBuffer();

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

private:
	void ReleaseOwnedSkeletalMesh();
	void RebuildRenderVertices();
	void RebuildMeshBuffer();
	void MarkBoundsDirty();
	void MarkRenderBufferDirty();
	void EnsureBoundsUpdated() const;

private:
	USkeletalMesh* SkeletalMeshAsset = nullptr;
	FString SkeletalMeshAssetPath;
	bool bOwnsSkeletalMesh = false;

	TArray<FNormalVertex> RenderVertices;
	FDynamicMeshBuffer MeshBuffer;
	mutable bool bBoundsDirty = true;
	bool bRenderBufferDirty = true;
};
