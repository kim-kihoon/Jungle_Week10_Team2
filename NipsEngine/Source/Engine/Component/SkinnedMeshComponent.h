#pragma once

#include "Asset/MeshTypes.h"
#include "MeshComponent.h"
#include "Render/Resource/Buffer.h"

struct ID3D11Device;

class USkinnedMeshComponent : public UMeshComponent
{
public:
	DECLARE_CLASS(USkinnedMeshComponent, UMeshComponent)
	~USkinnedMeshComponent() override;

	void PostDuplicate(UObject* Original) override;
	void Serialize(FArchive& Ar) override;

	void SetSkinnedMesh(FSkeletalMesh* InSkinnedMesh);
	FSkeletalMesh* GetSkinnedMesh() const { return SkinnedMeshAsset; }
	bool HasValidSkinnedMesh() const;
	const FString& GetSkinnedMeshAssetPath() const { return SkinnedMeshAssetPath; }

	void UpdateWorldAABB() const override;
	bool RaycastMesh(const FRay& Ray, FHitResult& OutHitResult) override;
	const FAABB& GetWorldAABB() const override;

	bool ConsumeRenderStateDirty();
	FMeshBuffer* GetSkinnedRenderMeshBuffer() const;
	const TArray<FSkinnedMeshSection>& GetSkinnedRenderSections() const;
	const TArray<FNormalVertex>& GetSkinnedRenderVertices() const;

protected:
	void InitializeMaterialsFromLOD0();
	void MarkBoundsDirty();
	void MarkRenderStateDirty();
	void MarkRenderCacheDirty();
	const FSkinnedMeshLOD* GetLOD0() const;
	void EnsureBoundsUpdated() const;
	void EnsureSkinnedRenderCache() const;
	void EnsureSkinnedRenderBuffer() const;
	void ReleaseSkinnedRenderBuffer() const;

protected:
	FSkeletalMesh* SkinnedMeshAsset = nullptr;
	FString SkinnedMeshAssetPath;

	mutable bool bBoundsDirty = true;
	bool bRenderStateDirty = true;
	mutable bool bSkinnedRenderCacheDirty = true;
	mutable bool bSkinnedRenderBufferDirty = true;
	mutable TArray<FNormalVertex> SkinnedRenderVertices;
	mutable TArray<FSkinnedMeshSection> SkinnedRenderSections;
	mutable FMeshBuffer SkinnedRenderMeshBuffer;
	mutable ID3D11Device* SkinnedRenderBufferDevice = nullptr;
};
