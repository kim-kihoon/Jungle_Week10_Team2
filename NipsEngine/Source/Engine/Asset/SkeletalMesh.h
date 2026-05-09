#pragma once

#include "Asset/SkeletalMeshTypes.h"
#include "Object/Object.h"

class USkeletalMesh : public UObject
{
public:
    DECLARE_CLASS(USkeletalMesh, UObject)

    USkeletalMesh() = default;
    ~USkeletalMesh() override;

    void SetSkeletalMeshData(FSkeletalMesh* InMeshData);

    FSkeletalMesh* GetSkeletalMeshData();
    const FSkeletalMesh* GetSkeletalMeshData() const;

    const FString& GetAssetPathFileName() const;
    const FReferenceSkeleton& GetRefSkeleton() const;
    const TArray<FMatrix>& GetInverseRefMatrices() const;
    const FSkeletalMeshLODRenderData* GetLODRenderData(int32 LODIndex = 0) const;
    const TArray<FSkeletalMaterial>& GetMaterials() const;

    bool HasValidMeshData() const;

private:
    FSkeletalMesh* MeshData = nullptr;
};
