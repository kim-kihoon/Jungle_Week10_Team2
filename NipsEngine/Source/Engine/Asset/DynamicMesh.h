#pragma once

#include "Asset/DynamicMeshTypes.h"
#include "Object/Object.h"
#include "Render/Resource/Buffer.h"

class UDynamicMesh : public UObject
{
public:
	DECLARE_CLASS(UDynamicMesh, UObject)

	UDynamicMesh() = default;
	~UDynamicMesh() override;

	bool LoadFromFbx(const FString& FilePath);
	void SetMeshData(FDynamicMesh* InMeshData);
	FDynamicMesh* GetMeshData();
	const FDynamicMesh* GetMeshData() const;

	const FString& GetAssetPathFileName() const;
	const TArray<FDynamicMeshVertex>& GetVertices() const;
	const TArray<FNormalVertex>& GetRenderVertices() const;
	const TArray<uint32>& GetIndices() const;
	const TArray<FDynamicMeshSection>& GetSections() const;
	const TArray<FSkeletalBone>& GetBones() const;
	const TArray<FMatrix>& GetReferencePoseMatrices() const;
	const FAABB& GetLocalBounds() const;
	FDynamicMeshBuffer* GetDynamicMeshBuffer();
	void UpdateRenderVertices(ID3D11DeviceContext* InContext, const TArray<FNormalVertex>& InVertices);

	bool HasValidMeshData() const;

private:
	void RebuildRenderVertices();
	void RebuildMeshBuffer();
	void MarkRenderBufferDirty();

private:
	FDynamicMesh* MeshData = nullptr;
	TArray<FNormalVertex> RenderVertices;
	FDynamicMeshBuffer MeshBuffer;
	bool bRenderBufferDirty = true;
};
