#pragma once

#include "Core/CoreTypes.h"
#include "Engine/Geometry/AABB.h"
#include "Engine/Geometry/Transform.h"
#include "Math/Color.h"
#include "Math/Matrix.h"
#include "Math/Vector2.h"
#include "Render/Resource/Material.h"

struct FBoneInfo
{
    FString Name;
    int32 ParentIndex = -1;
};

struct FReferenceSkeleton
{
    TArray<FBoneInfo> BoneInfo;
    TArray<FTransform> LocalRefPoseTransforms;

    int32 FindBoneIndex(const FString& BoneName) const;
    void Reset();
    int32 Add(const FBoneInfo& InBoneInfo, const FTransform& InRefPose);
    int32 GetNum() const { return static_cast<int32>(BoneInfo.size()); }
    bool IsValidIndex(int32 BoneIndex) const { return BoneIndex >= 0 && BoneIndex < GetNum(); }
};

struct FSkeletalMeshVertex
{
    FVector Position = FVector::ZeroVector;
    FColor Color = FColor::White();
    FVector Normal = FVector(0.0f, 0.0f, 1.0f);
    FVector2 UVs = FVector2(0.0f, 0.0f);
    FVector Tangent = FVector(1.0f, 0.0f, 0.0f);
    FVector Bitangent = FVector(0.0f, 1.0f, 0.0f);
    uint16 BoneIndices[4] = { 0, 0, 0, 0 };
    float BoneWeights[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
};

struct FSkeletalMeshSection
{
    uint32 StartIndex = 0;
    uint32 IndexCount = 0;
    int32 MaterialSlotIndex = -1;
};

struct FSkeletalMaterial
{
    FString MaterialSlotName;
    UMaterialInterface* Material = nullptr;
};

struct FSkeletalMeshLODRenderData
{
    TArray<FSkeletalMeshVertex> Vertices;
    TArray<uint32> Indices;
    TArray<FSkeletalMeshSection> Sections;
    FAABB Bounds;
};

struct FSkeletalMeshRenderData
{
    TArray<FSkeletalMeshLODRenderData> LODRenderData;
};

struct FSkeletalMesh
{
    FString PathFileName;
    FReferenceSkeleton RefSkeleton;
    TArray<FMatrix> InverseRefPoseMatrices;
    TArray<FSkeletalMaterial> Materials;
    FSkeletalMeshRenderData RenderData;
};

struct FSkeletalMeshImportOptions
{
    bool bNormalizeWeights = true;
};
