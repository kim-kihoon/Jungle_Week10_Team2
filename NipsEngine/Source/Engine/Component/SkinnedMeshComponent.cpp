#include "SkinnedMeshComponent.h"

#include <algorithm>
#include <cfloat>
#include <cstring>
#include <cmath>

#include "Asset/SkeletalMesh.h"
#include "Core/ResourceManager.h"

DEFINE_CLASS(USkinnedMeshComponent, UMeshComponent)

void USkinnedMeshComponent::SetSkeletalMesh(USkeletalMesh* InSkeletalMesh)
{
    if (SkeletalMesh == InSkeletalMesh)
    {
        return;
    }

    SkeletalMesh = InSkeletalMesh;

    ReleaseOwnedMaterialInstances();
    Materials.clear();

    if (SkeletalMesh != nullptr)
    {
        SkeletalMeshAssetPath = SkeletalMesh->GetAssetPathFileName();

        const TArray<FSkeletalMeshSection>& Sections = SkeletalMesh->GetSections();
        const TArray<FSkeletalMeshMaterialSlot>& Slots = SkeletalMesh->GetMaterialSlots();

        Materials.reserve(Sections.size());

        for (const FSkeletalMeshSection& Section : Sections)
        {
            UMaterialInterface* Material = nullptr;

            if (Section.MaterialSlotIndex >= 0 &&
                Section.MaterialSlotIndex < static_cast<int32>(Slots.size()))
            {
                Material = Slots[Section.MaterialSlotIndex].Material;
            }

            Materials.push_back(Material);
        }
    }
    else
    {
        SkeletalMeshAssetPath.clear();
    }

    ComponentSpaceBoneTransforms.clear();
    SkinningMatrices.clear();
    SkinnedVertices.clear();

    MarkBoneTransformsDirty();
    MarkSkinningDirty();
    MarkBoundsDirty();
    MarkRenderStateDirty();
}

const TArray<FNormalVertex>& USkinnedMeshComponent::GetSkinnedVertices() const
{
    return SkinnedVertices;
}

const TArray<FMatrix>& USkinnedMeshComponent::GetSkinningMatrices() const
{
    return SkinningMatrices;
}

const TArray<FTransform>& USkinnedMeshComponent::GetComponentSpaceBoneTransforms() const
{
    return ComponentSpaceBoneTransforms;
}

bool USkinnedMeshComponent::HasValidMesh() const
{
    return SkeletalMesh != nullptr && SkeletalMesh->HasValidMeshData();
}

void USkinnedMeshComponent::RefreshBoneTransforms()
{
    if (!HasValidMesh())
    {
        ComponentSpaceBoneTransforms.clear();
        bBoneTransformsDirty = false;
        MarkSkinningDirty();
        return;
    }

	//Bone들은 결국 FName,Index,FTransform 으로 구성.
    const TArray<FSkeletalBone>& Bones = SkeletalMesh->GetBones();
    const int32 BoneCount = static_cast<int32>(Bones.size());

    ComponentSpaceBoneTransforms.clear();
    ComponentSpaceBoneTransforms.resize(BoneCount, FTransform::Identity);

    for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
    {
        const FSkeletalBone& Bone = Bones[BoneIndex];
        const int32 ParentIndex = Bone.ParentIndex;

        if (ParentIndex >= 0 && ParentIndex < BoneIndex)
        {
			//현재 bone에 부모 bone의 transform을 반영.
            ComponentSpaceBoneTransforms[BoneIndex] = Bone.ReferenceLocalTransform * ComponentSpaceBoneTransforms[ParentIndex];
        }
        else
        {
			//루트 이거나 데이터가 잘못 들어와서 현재 본보다 부모의 인덱스가 뒤에 있을 때
            ComponentSpaceBoneTransforms[BoneIndex] = Bone.ReferenceLocalTransform;
        }
    }

    bBoneTransformsDirty = false;
    MarkSkinningDirty();
}

void USkinnedMeshComponent::ComputeSkinningMatrices()
{
    if (!HasValidMesh())
    {
        SkinningMatrices.clear();
        return;
    }

    if (bBoneTransformsDirty)
    {
        RefreshBoneTransforms();
    }

    const TArray<FMatrix>& InverseBindPoseMatrices = SkeletalMesh->GetInverseBindPoseMatrices();
    const int32 BoneCount = static_cast<int32>(ComponentSpaceBoneTransforms.size());

    SkinningMatrices.clear();
    SkinningMatrices.resize(BoneCount, FMatrix::Identity);

    for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
    {
        const FMatrix CurrentBoneMatrix =
            ComponentSpaceBoneTransforms[BoneIndex].ToMatrixWithScale();

        if (BoneIndex < static_cast<int32>(InverseBindPoseMatrices.size()))
        {
			// 보통 vertex들이 bind pose 기준으로 저장되니 현재 pose로 변환해줌.
            // Vertex * InverseBindPose * CurrentBone
            SkinningMatrices[BoneIndex] = InverseBindPoseMatrices[BoneIndex] * CurrentBoneMatrix;
        }
        else
        {
            SkinningMatrices[BoneIndex] = CurrentBoneMatrix;
        }
    }
}

void USkinnedMeshComponent::ComputeSkinnedVertices()
{
    if (!HasValidMesh())
    {
        SkinnedVertices.clear();
        bSkinningDirty = false;
        return;
    }

    if (!bSkinningDirty && SkinnedVertices.size() == SkeletalMesh->GetVertices().size())
    {
        return;
    }

    ComputeSkinningMatrices();

    const TArray<FSkeletalMeshVertex>& SourceVertices = SkeletalMesh->GetVertices();
    SkinnedVertices.clear();
    SkinnedVertices.resize(SourceVertices.size());

    for (int32 VertexIndex = 0; VertexIndex < static_cast<int32>(SourceVertices.size()); ++VertexIndex)
    {
        const FSkeletalMeshVertex& SourceVertex = SourceVertices[VertexIndex];

        FNormalVertex OutVertex = SourceVertex.ToNormalVertex();

        FVector SkinnedPosition = FVector::ZeroVector;
        FVector SkinnedNormal = FVector::ZeroVector;
        FVector SkinnedTangent = FVector::ZeroVector;
        FVector SkinnedBitangent = FVector::ZeroVector;

        float TotalWeight = 0.0f;

        for (int32 InfluenceIndex = 0; InfluenceIndex < MAX_SKELETAL_BONE_INFLUENCES; ++InfluenceIndex)
        {
            const int32 BoneIndex = SourceVertex.BoneIndices[InfluenceIndex];
            const float Weight = SourceVertex.BoneWeights[InfluenceIndex];

            if (Weight <= 0.0f)
            {
                continue;
            }

            if (BoneIndex < 0 || BoneIndex >= static_cast<int32>(SkinningMatrices.size()))
            {
                continue;
            }

            const FMatrix& SkinningMatrix = SkinningMatrices[BoneIndex];

            SkinnedPosition += SkinningMatrix.TransformPosition(SourceVertex.Position) * Weight;
            SkinnedNormal += SkinningMatrix.TransformVector(SourceVertex.Normal) * Weight;
            SkinnedTangent += SkinningMatrix.TransformVector(SourceVertex.Tangent) * Weight;
            SkinnedBitangent += SkinningMatrix.TransformVector(SourceVertex.Bitangent) * Weight;

            TotalWeight += Weight;
        }

        if (TotalWeight > 1.0e-6f)
        {
            const float InvTotalWeight = 1.0f / TotalWeight;

            OutVertex.Position = SkinnedPosition * InvTotalWeight;
            OutVertex.Normal = (SkinnedNormal * InvTotalWeight).GetSafeNormal();
            OutVertex.Tangent = (SkinnedTangent * InvTotalWeight).GetSafeNormal();
            OutVertex.Bitangent = (SkinnedBitangent * InvTotalWeight).GetSafeNormal();
        }
        else
        {
            // bone weight가 없는 vertex는 원본 그대로 둔다.
            OutVertex = SourceVertex.ToNormalVertex();
        }

        SkinnedVertices[VertexIndex] = OutVertex;
    }

    bSkinningDirty = false;
    MarkBoundsDirty();
    MarkRenderStateDirty();
}

void USkinnedMeshComponent::UpdateWorldAABB() const
{
    WorldAABB.Reset();

    if (!HasValidMesh())
    {
        bBoundsDirty = false;
        return;
    }

    const_cast<USkinnedMeshComponent*>(this)->ComputeSkinnedVertices();

    FAABB LocalSkinnedBounds;
    LocalSkinnedBounds.Reset();

    for (const FNormalVertex& Vertex : SkinnedVertices)
    {
        LocalSkinnedBounds.Expand(Vertex.Position);
    }

    if (!LocalSkinnedBounds.IsValid())
    {
        bBoundsDirty = false;
        return;
    }

    const FVector LocalCorners[8] = {
        FVector(LocalSkinnedBounds.Min.X, LocalSkinnedBounds.Min.Y, LocalSkinnedBounds.Min.Z),
        FVector(LocalSkinnedBounds.Max.X, LocalSkinnedBounds.Min.Y, LocalSkinnedBounds.Min.Z),
        FVector(LocalSkinnedBounds.Min.X, LocalSkinnedBounds.Max.Y, LocalSkinnedBounds.Min.Z),
        FVector(LocalSkinnedBounds.Max.X, LocalSkinnedBounds.Max.Y, LocalSkinnedBounds.Min.Z),
        FVector(LocalSkinnedBounds.Min.X, LocalSkinnedBounds.Min.Y, LocalSkinnedBounds.Max.Z),
        FVector(LocalSkinnedBounds.Max.X, LocalSkinnedBounds.Min.Y, LocalSkinnedBounds.Max.Z),
        FVector(LocalSkinnedBounds.Min.X, LocalSkinnedBounds.Max.Y, LocalSkinnedBounds.Max.Z),
        FVector(LocalSkinnedBounds.Max.X, LocalSkinnedBounds.Max.Y, LocalSkinnedBounds.Max.Z)
    };

    const FMatrix& WorldMatrix = GetWorldMatrix();

    for (const FVector& Corner : LocalCorners)
    {
        WorldAABB.Expand(WorldMatrix.TransformPosition(Corner));
    }

    bBoundsDirty = false;
}

bool USkinnedMeshComponent::RaycastMesh(const FRay& Ray, FHitResult& OutHitResult)
{
    if (!HasValidMesh())
    {
        return false;
    }

    EnsureBoundsUpdated();

    float BoxT = 0.0f;
    if (!WorldAABB.IntersectRay(Ray, BoxT))
    {
        return false;
    }

    ComputeSkinnedVertices();

    const TArray<uint32>& Indices = SkeletalMesh->GetIndices();

    if (SkinnedVertices.empty() || Indices.empty())
    {
        return false;
    }

    const FMatrix InvWorld = GetWorldMatrix().GetInverse();

    FRay LocalRay = Ray;
    LocalRay.Origin = InvWorld.TransformPosition(LocalRay.Origin);
    LocalRay.Direction = InvWorld.TransformVector(LocalRay.Direction);
    LocalRay.Direction.NormalizeSafe();

    bool bHit = false;
    float ClosestT = FLT_MAX;
    int32 BestFaceIndex = -1;
    FVector BestLocalNormal = FVector::ZeroVector;

    for (uint32 i = 0; i + 2 < static_cast<uint32>(Indices.size()); i += 3)
    {
        const uint32 I0 = Indices[i];
        const uint32 I1 = Indices[i + 1];
        const uint32 I2 = Indices[i + 2];

        if (I0 >= SkinnedVertices.size() ||
            I1 >= SkinnedVertices.size() ||
            I2 >= SkinnedVertices.size())
        {
            continue;
        }

        const FVector& V0 = SkinnedVertices[I0].Position;
        const FVector& V1 = SkinnedVertices[I1].Position;
        const FVector& V2 = SkinnedVertices[I2].Position;

        float HitT = 0.0f;
        if (IntersectTriangle(LocalRay.Origin, LocalRay.Direction, V0, V1, V2, HitT))
        {
            if (HitT < ClosestT)
            {
                ClosestT = HitT;
                bHit = true;
                BestFaceIndex = static_cast<int32>(i / 3);

                const FVector Edge1 = V1 - V0;
                const FVector Edge2 = V2 - V0;
                BestLocalNormal = FVector::CrossProduct(Edge1, Edge2).GetSafeNormal();
            }
        }
    }

    if (!bHit)
    {
        return false;
    }

    const FVector LocalHitLocation = LocalRay.Origin + LocalRay.Direction * ClosestT;
    const FVector WorldHitLocation = GetWorldMatrix().TransformPosition(LocalHitLocation);

    FVector WorldNormal = GetWorldMatrix().TransformVector(BestLocalNormal);
    WorldNormal.NormalizeSafe();

    OutHitResult.bHit = true;
    OutHitResult.HitComponent = this;
    OutHitResult.Distance = (WorldHitLocation - Ray.Origin).Size();
    OutHitResult.Location = WorldHitLocation;
    OutHitResult.Normal = WorldNormal;
    OutHitResult.FaceIndex = BestFaceIndex;

    return true;
}

bool USkinnedMeshComponent::ConsumeRenderStateDirty()
{
    const bool bWasDirty = bRenderStateDirty;
    bRenderStateDirty = false;
    return bWasDirty;
}

void USkinnedMeshComponent::MarkBoneTransformsDirty()
{
    bBoneTransformsDirty = true;
    MarkSkinningDirty();
}

void USkinnedMeshComponent::MarkSkinningDirty()
{
    bSkinningDirty = true;
    MarkBoundsDirty();
    MarkRenderStateDirty();
}

void USkinnedMeshComponent::MarkBoundsDirty()
{
    bBoundsDirty = true;
}

void USkinnedMeshComponent::MarkRenderStateDirty()
{
    bRenderStateDirty = true;
}

void USkinnedMeshComponent::EnsureBoundsUpdated() const
{
    if (!bBoundsDirty && !bTransformDirty)
    {
        return;
    }

    if (bTransformDirty)
    {
        (void)GetWorldMatrix();
    }

    const_cast<USkinnedMeshComponent*>(this)->UpdateWorldAABB();
}