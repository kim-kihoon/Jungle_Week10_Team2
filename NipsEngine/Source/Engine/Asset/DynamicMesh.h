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
	const TArray<uint32>& GetIndices() const;
	const TArray<FDynamicMeshSection>& GetSections() const;
	const TArray<FSkeletalBone>& GetBones() const;
	const TArray<FMatrix>& GetReferencePoseMatrices() const;
	const FAABB& GetLocalBounds() const;

	bool HasValidMeshData() const;

private:
	FDynamicMesh* MeshData = nullptr;
};

class FDynamicMeshBuffer
{
public:
	void Create(ID3D11Device* InDevice, const FDynamicMesh* InMeshData);
	void Release();

	void UpdateBoneMatrices(ID3D11DeviceContext* InDeviceContext, const TArray<FMatrix>& InBoneMatrices);
	void UpdateReferencePose(ID3D11DeviceContext* InDeviceContext, const FDynamicMesh* InMeshData);

	FMeshBuffer& GetMeshBuffer() { return MeshBuffer; }
	const FMeshBuffer& GetMeshBuffer() const { return MeshBuffer; }
	FStructuredBuffer& GetBoneMatrixBuffer() { return BoneMatrixBuffer; }
	const FStructuredBuffer& GetBoneMatrixBuffer() const { return BoneMatrixBuffer; }

	bool IsValid() const { return MeshBuffer.IsValid(); }

private:
	FMeshBuffer MeshBuffer;
	FStructuredBuffer BoneMatrixBuffer;
	uint32 BoneCapacity = 0;
};
