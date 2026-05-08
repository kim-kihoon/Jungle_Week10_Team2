#include "DynamicMesh.h"

#include "Render/Resource/FbxParser.h"

DEFINE_CLASS(UDynamicMesh, UObject)

UDynamicMesh::~UDynamicMesh()
{
	delete MeshData;
	MeshData = nullptr;
}

bool UDynamicMesh::LoadFromFbx(const FString& FilePath)
{
	FDynamicMesh* LoadedMeshData = FbxParser::ParseFbx(FilePath);
	SetMeshData(LoadedMeshData);
	return HasValidMeshData();
}

void UDynamicMesh::SetMeshData(FDynamicMesh* InMeshData)
{
	if (MeshData == InMeshData)
	{
		return;
	}

	delete MeshData;
	MeshData = InMeshData;

	if (MeshData != nullptr)
	{
		MeshData->NormalizeVertexWeights();
		MeshData->EnsureReferencePoseMatrices();
		MeshData->CacheBounds();
	}
}

FDynamicMesh* UDynamicMesh::GetMeshData()
{
	return MeshData;
}

const FDynamicMesh* UDynamicMesh::GetMeshData() const
{
	return MeshData;
}

const FString& UDynamicMesh::GetAssetPathFileName() const
{
	static const FString Empty;
	return MeshData ? MeshData->PathFileName : Empty;
}

const TArray<FDynamicMeshVertex>& UDynamicMesh::GetVertices() const
{
	static const TArray<FDynamicMeshVertex> Empty;
	return MeshData ? MeshData->Vertices : Empty;
}

const TArray<uint32>& UDynamicMesh::GetIndices() const
{
	static const TArray<uint32> Empty;
	return MeshData ? MeshData->Indices : Empty;
}

const TArray<FDynamicMeshSection>& UDynamicMesh::GetSections() const
{
	static const TArray<FDynamicMeshSection> Empty;
	return MeshData ? MeshData->Sections : Empty;
}

const TArray<FSkeletalBone>& UDynamicMesh::GetBones() const
{
	static const TArray<FSkeletalBone> Empty;
	return MeshData ? MeshData->Bones : Empty;
}

const TArray<FMatrix>& UDynamicMesh::GetReferencePoseMatrices() const
{
	static const TArray<FMatrix> Empty;
	return MeshData ? MeshData->ReferencePoseMatrices : Empty;
}

const FAABB& UDynamicMesh::GetLocalBounds() const
{
	static const FAABB Empty;
	return MeshData ? MeshData->LocalBounds : Empty;
}

bool UDynamicMesh::HasValidMeshData() const
{
	return MeshData != nullptr && MeshData->HasValidRenderData();
}

void FDynamicMeshBuffer::Create(ID3D11Device* InDevice, const FDynamicMesh* InMeshData)
{
	Release();

	if (InDevice == nullptr || InMeshData == nullptr || !InMeshData->HasValidRenderData())
	{
		return;
	}

	MeshBuffer.Create(InDevice, InMeshData->Vertices, InMeshData->Indices);

	BoneCapacity = static_cast<uint32>(InMeshData->Bones.size());
	if (BoneCapacity > 0)
	{
		BoneMatrixBuffer.Create(InDevice, sizeof(FMatrix), BoneCapacity);
	}
}

void FDynamicMeshBuffer::Release()
{
	MeshBuffer.Release();
	BoneMatrixBuffer.Release();
	BoneCapacity = 0;
}

void FDynamicMeshBuffer::UpdateBoneMatrices(ID3D11DeviceContext* InDeviceContext, const TArray<FMatrix>& InBoneMatrices)
{
	if (BoneCapacity == 0)
	{
		return;
	}

	BoneMatrixBuffer.Update(InDeviceContext, InBoneMatrices.data(), static_cast<uint32>(InBoneMatrices.size()));
}

void FDynamicMeshBuffer::UpdateReferencePose(ID3D11DeviceContext* InDeviceContext, const FDynamicMesh* InMeshData)
{
	if (InMeshData == nullptr)
	{
		return;
	}

	UpdateBoneMatrices(InDeviceContext, InMeshData->ReferencePoseMatrices);
}
