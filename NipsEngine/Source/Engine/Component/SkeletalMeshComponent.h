#pragma once

#include "MeshComponent.h"
#include "Asset/DynamicMesh.h"
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
	void SetDynamicMesh(UDynamicMesh* InDynamicMesh, bool bTakeOwnership = false);
	UDynamicMesh* GetDynamicMesh() const;
	bool HasValidMesh() const;
	FDynamicMeshBuffer* GetDynamicMeshBuffer();

	const TArray<FNormalVertex>& GetRenderVertices() const;
	const TArray<uint32>& GetRenderIndices() const;
	const TArray<FDynamicMeshSection>& GetSections() const;

	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	void PostEditProperty(const char* PropertyName) override;

	void UpdateWorldAABB() const override;
	bool RaycastMesh(const FRay& Ray, FHitResult& OutHitResult) override;
	EPrimitiveType GetPrimitiveType() const override { return EPrimitiveType::EPT_DynamicMesh; }

	const FAABB& GetWorldAABB() const override;

private:
	void ReleaseOwnedDynamicMesh();
	void RebuildRenderVertices();
	void MarkBoundsDirty();
	void MarkRenderStateDirty();
	void EnsureBoundsUpdated() const;

private:
	UDynamicMesh* DynamicMeshAsset = nullptr;
	bool bOwnsDynamicMesh = false;
	FString DynamicMeshAssetPath;

	TArray<FNormalVertex> RenderVertices;
	mutable bool bBoundsDirty = true;
	bool bRenderStateDirty = true;
};
