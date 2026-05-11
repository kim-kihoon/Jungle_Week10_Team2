#pragma once

#include "Asset/SkeletalMeshTypes.h"
#include "Object/Object.h"
#include "Render/Resource/Buffer.h"

class USkeletalMesh : public UObject
{
public:
	DECLARE_CLASS(USkeletalMesh, UObject)

	USkeletalMesh() = default;
	~USkeletalMesh() override;

	bool LoadFromFbx(const FString& FilePath);
	void SetMeshData(FSkeletalMesh* InMeshData);
	FSkeletalMesh* GetMeshData();
	const FSkeletalMesh* GetMeshData() const;

	const FString& GetAssetPathFileName() const;
	const TArray<FSkeletalMeshVertex>& GetVertices() const;
	const TArray<uint32>& GetIndices() const;
	const TArray<FSkeletalMeshSection>& GetSections() const;
	const TArray<FSkeletalBone>& GetBones() const;
	const TArray<FMatrix>& GetReferencePoseMatrices() const;
	const FAABB& GetLocalBounds() const;

	bool HasValidMeshData() const;

private:

private:
	FSkeletalMesh* MeshData = nullptr;
};
