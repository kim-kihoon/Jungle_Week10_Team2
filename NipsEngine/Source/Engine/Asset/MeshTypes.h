#pragma once

#include "Core/CoreMinimal.h"
#include "Render/Resource/Material.h"
#include "Render/Resource/VertexTypes.h"

struct ID3D11Buffer;

struct FMeshSection
{
	uint32 StartIndex = 0;
	uint32 IndexCount = 0;
	int32 MaterialSlotIndex = -1;
};

struct FMeshMaterialSlot
{
	FString SlotName;
	FString MaterialPath;
	UMaterialInterface* Material = nullptr;
};

struct FMeshRenderData
{
	ID3D11Buffer* VertexBuffer = nullptr;
	ID3D11Buffer* IndexBuffer = nullptr;
};

// Cooked static mesh data.
struct FStaticMesh
{
	FString PathFileName;
	TArray<FNormalVertex> Vertices;
	TArray<uint32> Indices;
	TArray<FMeshSection> Sections;
	TArray<FMeshMaterialSlot> Slots;
	FAABB LocalBounds;

	FMeshRenderData RenderData;
};

constexpr uint32 MaxBoneInfluences = 4;
constexpr uint32 MaxUVChannels = 4;
constexpr uint32 InvalidBoneIndex = 0xffff;
using BoneIndex = uint16;

struct FSkinWeights
{
	TStaticArray<BoneIndex, MaxBoneInfluences> BoneIndices{};
	TStaticArray<float, MaxBoneInfluences> Weights{};
};

struct FSkinnedVertex
{
	FVector Position;
	FColor Color;
	FVector Normal;
	FVector Tangent;
	FVector Bitangent;
	TStaticArray<FVector2, MaxUVChannels> UVs{};
	FSkinWeights SkinWeights;
};

struct FSkinnedMeshSection : public FMeshSection
{
	uint32 BaseVertex = 0;
	uint32 VertexCount = 0;

	TArray<BoneIndex> BoneMap;
};

struct FSkinnedMeshRenderData : public FMeshRenderData
{
	ID3D11Buffer* SkinningMatrixBuffer = nullptr;
};

struct FSkinnedMeshLOD
{
	TArray<FSkinnedVertex> Vertices;
	TArray<uint32> Indices;
	TArray<FSkinnedMeshSection> Sections;
	FAABB LocalBounds;

	FSkinnedMeshRenderData RenderData;
};

struct FRefBone
{
	FString Name;
	int32 ParentIndex = -1;

	FTransform LocalBindTransform;
	FMatrix InverseBindMatrix;
};

struct FRefSkeleton
{
	TArray<FRefBone> Bones;
	TMap<FString, uint32> BoneNameToIndex;
};

struct FSkinnedMeshPose
{
	TArray<FTransform> LocalTransforms;
	TArray<FMatrix> ComponentSpaceMatrices;
	TArray<FMatrix> SkinningMatrices;
};

struct FSkeletalMesh
{
	FString PathFileName;
	TArray<FSkinnedMeshLOD> LODs;
	TArray<FMeshMaterialSlot> Slots;
	FRefSkeleton RefSkeleton;
};
