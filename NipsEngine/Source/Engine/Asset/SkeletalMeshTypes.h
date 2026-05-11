#pragma once

#include "Core/CoreMinimal.h"
#include "Engine/Geometry/AABB.h"
#include "Engine/Geometry/Transform.h"
#include "Render/Resource/Material.h"

struct FSkeletalMeshVertex
{
	FVector Position = FVector::ZeroVector;
	FVector Normal = FVector::UpVector;
	FVector2 UV = FVector2::ZeroVector;
	FVector4 Color = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
	FVector4 Tangent = FVector4(1.0f, 0.0f, 0.0f, 1.0f);
	uint32 BoneIndices[4] = { 0, 0, 0, 0 };
	float BoneWeights[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
};

struct FSkeletalBone
{
	FString Name;
	int32 ParentIndex = -1;
	FTransform RefLocalTransform = FTransform::Identity;
	FMatrix RefGlobalMatrix = FMatrix::Identity;
	FMatrix InverseBindPose = FMatrix::Identity;
};

struct FSkeletalMeshSection
{
	uint32 FirstIndex = 0;
	uint32 NumTriangles = 0;
	int32 MaterialIndex = -1;
	FString MaterialSlotName;

	uint32 GetIndexCount() const { return NumTriangles * 3; }
};

struct FSkeletalMeshMaterialSlot
{
	FString SlotName;
	UMaterialInterface* Material = nullptr;
};

struct FSkeletalMesh
{
	FString PathFileName;
	TArray<FSkeletalMeshVertex> Vertices;
	TArray<uint32> Indices;
	TArray<FSkeletalMeshSection> Sections;
	TArray<FSkeletalMeshMaterialSlot> Slots;
	TArray<FSkeletalBone> Bones;
	TArray<FMatrix> ReferencePoseMatrices;
	FAABB LocalBounds;

	void CacheBounds();
	void NormalizeVertexWeights();
	void EnsureReferencePoseMatrices();
	bool HasValidRenderData() const;
	bool ValidateSkinningData() const;
};
