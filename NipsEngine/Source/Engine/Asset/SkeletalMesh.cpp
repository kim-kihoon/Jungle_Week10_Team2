#include "SkeletalMesh.h"

DEFINE_CLASS(USkeletalMesh, UObject)

int32 FReferenceSkeleton::FindBoneIndex(const FString& BoneName) const
{
    for (int32 Index = 0; Index < static_cast<int32>(BoneInfo.size()); ++Index)
    {
        if (BoneInfo[Index].Name == BoneName)
        {
            return Index;
        }
    }
    return -1;
}

void FReferenceSkeleton::Reset()
{
    BoneInfo.clear();
    LocalBindPoseTransforms.clear();
}

int32 FReferenceSkeleton::Add(const FBoneInfo& InBoneInfo, const FTransform& InRefPose)
{
    const int32 ExistingIndex = FindBoneIndex(InBoneInfo.Name);
    if (ExistingIndex >= 0)
    {
        return ExistingIndex;
    }

    BoneInfo.push_back(InBoneInfo);
    LocalBindPoseTransforms.push_back(InRefPose);
    return static_cast<int32>(BoneInfo.size() - 1);
}

USkeletalMesh::~USkeletalMesh()
{
    delete MeshData;
    MeshData = nullptr;
}

void USkeletalMesh::SetSkeletalMeshData(FSkeletalMesh* InMeshData)
{
    if (MeshData == InMeshData)
    {
        return;
    }

    delete MeshData;
    MeshData = InMeshData;
}

FSkeletalMesh* USkeletalMesh::GetSkeletalMeshData()
{
    return MeshData;
}

const FSkeletalMesh* USkeletalMesh::GetSkeletalMeshData() const
{
    return MeshData;
}

const FString& USkeletalMesh::GetAssetPathFileName() const
{
    static const FString Empty;
    return MeshData ? MeshData->PathFileName : Empty;
}

const FReferenceSkeleton& USkeletalMesh::GetRefSkeleton() const
{
    static const FReferenceSkeleton Empty;
    return MeshData ? MeshData->RefSkeleton : Empty;
}

const TArray<FMatrix>& USkeletalMesh::GetInverseBindPoseMatrices() const
{
    static const TArray<FMatrix> Empty;
    return MeshData ? MeshData->InverseBindPoseMatrices : Empty;
}

const FSkeletalMeshLODRenderData* USkeletalMesh::GetLODRenderData(int32 LODIndex) const
{
    if (MeshData == nullptr || LODIndex < 0 || LODIndex >= static_cast<int32>(MeshData->RenderData.LODRenderData.size()))
    {
        return nullptr;
    }
    return &MeshData->RenderData.LODRenderData[LODIndex];
}

const TArray<FSkeletalMaterial>& USkeletalMesh::GetMaterials() const
{
    static const TArray<FSkeletalMaterial> Empty;
    return MeshData ? MeshData->Materials : Empty;
}

bool USkeletalMesh::HasValidMeshData() const
{
    const FSkeletalMeshLODRenderData* LODData = GetLODRenderData(0);
    return LODData != nullptr && !LODData->Vertices.empty() && !LODData->Indices.empty();
}
