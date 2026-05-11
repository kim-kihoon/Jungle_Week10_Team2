#pragma once

#include "SkeletalMeshTypes.h"
#include "Object/Object.h"

class USkeletalMesh : public UObject
{
public:
    DECLARE_CLASS(USkeletalMesh, UObject)

    USkeletalMesh() = default;
    ~USkeletalMesh() override;

    void SetMeshData(FSkeletalMesh* InMeshData);

    FSkeletalMesh* GetMeshData();
    const FSkeletalMesh* GetMeshData() const;

    const FString& GetAssetPathFileName() const;

    const TArray<FSkeletalMeshVertex>& GetVertices() const;
    const TArray<uint32>& GetIndices() const;

    const TArray<FSkeletalMeshSection>& GetSections() const;
    const TArray<FSkeletalMeshMaterialSlot>& GetMaterialSlots() const;

    const TArray<FSkeletalBone>& GetBones() const;
    const TArray<FMatrix>& GetInverseBindPoseMatrices() const;

    const FAABB& GetLocalBounds() const;

    bool HasValidMeshData() const;
    bool HasValidSkeleton() const;

    // FBX import 이후 반드시 한 번 호출.
    void CalculateInvRefMatrices();

private:
    void RebuildLocalBoundsFromMeshData();

private:
    FSkeletalMesh* MeshData = nullptr;
};