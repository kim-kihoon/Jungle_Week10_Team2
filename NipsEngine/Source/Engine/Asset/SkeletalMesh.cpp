#include "SkeletalMesh.h"

#include "Core/Logger.h"
#include "Core/ResourceManager.h"
#include "Render/Resource/FbxParser.h"

DEFINE_CLASS(USkeletalMesh, UObject)

USkeletalMesh::~USkeletalMesh()
{
	delete MeshData;
	MeshData = nullptr;
}

bool USkeletalMesh::LoadFromFbx(const FString& FilePath)
{
	FSkeletalMesh* LoadedMeshData = FbxParser::ParseFbx(FilePath);

	if (LoadedMeshData != nullptr)
	{
		if (!LoadedMeshData->ValidateSkinningData())
		{
			UE_LOG("FBX 로드 실패: 스키닝 데이터가 유효하지 않음 (%s)", FilePath);
			delete LoadedMeshData;
			return false;
		}
	}

	SetMeshData(LoadedMeshData);
	return HasValidMeshData();
}

void USkeletalMesh::SetMeshData(FSkeletalMesh* InMeshData)
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

FSkeletalMesh* USkeletalMesh::GetMeshData()
{
	return MeshData;
}

const FSkeletalMesh* USkeletalMesh::GetMeshData() const
{
	return MeshData;
}

const FString& USkeletalMesh::GetAssetPathFileName() const
{
	static const FString Empty;
	return MeshData ? MeshData->PathFileName : Empty;
}

const TArray<FSkeletalMeshVertex>& USkeletalMesh::GetVertices() const
{
	static const TArray<FSkeletalMeshVertex> Empty;
	return MeshData ? MeshData->Vertices : Empty;
}

const TArray<uint32>& USkeletalMesh::GetIndices() const
{
	static const TArray<uint32> Empty;
	return MeshData ? MeshData->Indices : Empty;
}

const TArray<FSkeletalMeshSection>& USkeletalMesh::GetSections() const
{
	static const TArray<FSkeletalMeshSection> Empty;
	return MeshData ? MeshData->Sections : Empty;
}

const TArray<FSkeletalBone>& USkeletalMesh::GetBones() const
{
	static const TArray<FSkeletalBone> Empty;
	return MeshData ? MeshData->Bones : Empty;
}

const TArray<FMatrix>& USkeletalMesh::GetReferencePoseMatrices() const
{
	static const TArray<FMatrix> Empty;
	return MeshData ? MeshData->ReferencePoseMatrices : Empty;
}

const FAABB& USkeletalMesh::GetLocalBounds() const
{
	static const FAABB Empty;
	return MeshData ? MeshData->LocalBounds : Empty;
}

bool USkeletalMesh::HasValidMeshData() const
{
	return MeshData != nullptr && MeshData->HasValidRenderData();
}
