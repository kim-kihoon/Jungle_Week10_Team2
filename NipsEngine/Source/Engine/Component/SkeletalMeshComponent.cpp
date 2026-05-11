#include "SkeletalMeshComponent.h"

#include <algorithm>
#include <cfloat>
#include <cstring>

#include "Core/ResourceManager.h"
#include "Object/ObjectFactory.h"

DEFINE_CLASS(USkeletalMeshComponent, USkinnedMeshComponent)
REGISTER_FACTORY(USkeletalMeshComponent)

namespace
{
	FVector SafeTransformPosition(const TArray<FMatrix>& SkinningMatrices, uint16 BoneIndex, const FVector& Position)
	{
		if (BoneIndex >= SkinningMatrices.size())
		{
			return Position;
		}
		return SkinningMatrices[BoneIndex].TransformPosition(Position);
	}

	FVector SafeTransformVector(const TArray<FMatrix>& SkinningMatrices, uint16 BoneIndex, const FVector& Vector)
	{
		if (BoneIndex >= SkinningMatrices.size())
		{
			return Vector;
		}
		return SkinningMatrices[BoneIndex].TransformVector(Vector);
	}
}

void USkeletalMeshComponent::PostDuplicate(UObject* Original)
{
	USkinnedMeshComponent::PostDuplicate(Original);

	const USkeletalMeshComponent* Origin = Cast<USkeletalMeshComponent>(Original);
	SkeletalMesh = Origin->SkeletalMesh;
	SkeletalMeshAssetPath = Origin->SkeletalMeshAssetPath;
	LocalTransforms = Origin->LocalTransforms;
	CurrentGlobalMatrices = Origin->CurrentGlobalMatrices;
	SkinningMatrices = Origin->SkinningMatrices;
	SkinnedVertices = Origin->SkinnedVertices;
	SkinnedLocalBounds = Origin->SkinnedLocalBounds;
	bBoneTransformsDirty = true;
	bSkinningDirty = true;
	bBoundsDirty = true;
	bRenderStateDirty = true;
}

void USkeletalMeshComponent::Serialize(FArchive& Ar)
{
	UMeshComponent::Serialize(Ar);
	Ar << "FbxSkeletalMeshAsset" << SkeletalMeshAssetPath;

	if (Ar.IsLoading())
	{
		TArray<UMaterialInterface*> SavedMaterials = Materials;

		if (!SkeletalMeshAssetPath.empty())
		{
			SetSkeletalMesh(FResourceManager::Get().LoadSkeletalMesh(SkeletalMeshAssetPath));
		}
		else
		{
			SetSkeletalMesh(nullptr);
		}

		const int32 RestoreCount = static_cast<int32>(std::min(SavedMaterials.size(), Materials.size()));
		for (int32 i = 0; i < RestoreCount; ++i)
		{
			if (SavedMaterials[i] != nullptr)
			{
				SetMaterial(i, SavedMaterials[i]);
			}
		}
	}
}

void USkeletalMeshComponent::SetSkeletalMesh(USkeletalMesh* InSkeletalMesh)
{
	USkinnedMeshComponent::SetSkeletalMesh(InSkeletalMesh);
}

void USkeletalMeshComponent::UpdateCPUSkinnedVertices()
{
	RefreshBoneTransforms();
	SkinnedVertices.clear();
	SkinnedLocalBounds.Reset();

	const FSkeletalMeshLODRenderData* LODData = SkeletalMesh ? SkeletalMesh->GetLODRenderData(0) : nullptr;
	if (LODData == nullptr || LODData->Vertices.empty())
	{
		bSkinningDirty = false;
		bBoundsDirty = true;
		return;
	}

	SkinnedVertices.resize(LODData->Vertices.size());
	for (int32 VertexIndex = 0; VertexIndex < static_cast<int32>(LODData->Vertices.size()); ++VertexIndex)
	{
		const FSkeletalMeshVertex& Source = LODData->Vertices[VertexIndex];

		FVector SkinnedPosition = FVector::ZeroVector;
		FVector SkinnedNormal = FVector::ZeroVector;
		FVector SkinnedTangent = FVector::ZeroVector;
		FVector SkinnedBitangent = FVector::ZeroVector;
		float WeightSum = 0.0f;

		for (int32 InfluenceIndex = 0; InfluenceIndex < 4; ++InfluenceIndex)
		{
			const float Weight = Source.BoneWeights[InfluenceIndex];
			if (Weight <= 0.0f)
			{
				continue;
			}

			const uint16 BoneIndex = Source.BoneIndices[InfluenceIndex];
			SkinnedPosition += SafeTransformPosition(SkinningMatrices, BoneIndex, Source.Position) * Weight;
			SkinnedNormal += SafeTransformVector(SkinningMatrices, BoneIndex, Source.Normal) * Weight;
			SkinnedTangent += SafeTransformVector(SkinningMatrices, BoneIndex, Source.Tangent) * Weight;
			SkinnedBitangent += SafeTransformVector(SkinningMatrices, BoneIndex, Source.Bitangent) * Weight;
			WeightSum += Weight;
		}

		if (WeightSum <= 0.0f)
		{
			SkinnedPosition = Source.Position;
			SkinnedNormal = Source.Normal;
			SkinnedTangent = Source.Tangent;
			SkinnedBitangent = Source.Bitangent;
		}

		FNormalVertex& Dest = SkinnedVertices[VertexIndex];
		Dest.Position = SkinnedPosition;
		Dest.Color = Source.Color;
		Dest.Normal = SkinnedNormal.GetSafeNormal();
		Dest.UVs = Source.UVs;
		Dest.Tangent = SkinnedTangent.GetSafeNormal();
		Dest.Bitangent = SkinnedBitangent.GetSafeNormal();

		if (Dest.Normal.IsNearlyZero())
		{
			Dest.Normal = FVector(0.0f, 0.0f, 1.0f);
		}
		if (Dest.Tangent.IsNearlyZero())
		{
			Dest.Tangent = FVector(1.0f, 0.0f, 0.0f);
		}
		if (Dest.Bitangent.IsNearlyZero())
		{
			Dest.Bitangent = FVector(0.0f, 1.0f, 0.0f);
		}

		SkinnedLocalBounds.Expand(Dest.Position);
	}

	bSkinningDirty = false;
	bBoundsDirty = true;
	bRenderStateDirty = true;
}

const TArray<FNormalVertex>& USkeletalMeshComponent::GetSkinnedVertices() const
{
	EnsureSkinnedVerticesUpdated();
	return SkinnedVertices;
}

const TArray<uint32>& USkeletalMeshComponent::GetSkinnedIndices() const
{
	static const TArray<uint32> Empty;
	const FSkeletalMeshLODRenderData* LODData = SkeletalMesh ? SkeletalMesh->GetLODRenderData(0) : nullptr;
	return LODData ? LODData->Indices : Empty;
}

const TArray<FSkeletalMeshSection>& USkeletalMeshComponent::GetSections() const
{
	static const TArray<FSkeletalMeshSection> Empty;
	const FSkeletalMeshLODRenderData* LODData = SkeletalMesh ? SkeletalMesh->GetLODRenderData(0) : nullptr;
	return LODData ? LODData->Sections : Empty;
}

void USkeletalMeshComponent::UpdateWorldAABB() const
{
	WorldAABB.Reset();
	EnsureSkinnedVerticesUpdated();

	if (!SkinnedLocalBounds.IsValid())
	{
		bBoundsDirty = false;
		return;
	}

	const FVector LocalCorners[8] = {
		FVector(SkinnedLocalBounds.Min.X, SkinnedLocalBounds.Min.Y, SkinnedLocalBounds.Min.Z),
		FVector(SkinnedLocalBounds.Max.X, SkinnedLocalBounds.Min.Y, SkinnedLocalBounds.Min.Z),
		FVector(SkinnedLocalBounds.Min.X, SkinnedLocalBounds.Max.Y, SkinnedLocalBounds.Min.Z),
		FVector(SkinnedLocalBounds.Max.X, SkinnedLocalBounds.Max.Y, SkinnedLocalBounds.Min.Z),
		FVector(SkinnedLocalBounds.Min.X, SkinnedLocalBounds.Min.Y, SkinnedLocalBounds.Max.Z),
		FVector(SkinnedLocalBounds.Max.X, SkinnedLocalBounds.Min.Y, SkinnedLocalBounds.Max.Z),
		FVector(SkinnedLocalBounds.Min.X, SkinnedLocalBounds.Max.Y, SkinnedLocalBounds.Max.Z),
		FVector(SkinnedLocalBounds.Max.X, SkinnedLocalBounds.Max.Y, SkinnedLocalBounds.Max.Z)
	};

	const FMatrix& WorldMatrix = GetWorldMatrix();
	for (const FVector& Corner : LocalCorners)
	{
		WorldAABB.Expand(WorldMatrix.TransformPosition(Corner));
	}

	bBoundsDirty = false;
}

bool USkeletalMeshComponent::RaycastMesh(const FRay& Ray, FHitResult& OutHitResult)
{
	return false;
}

const FAABB& USkeletalMeshComponent::GetWorldAABB() const
{
	EnsureBoundsUpdated();
	return WorldAABB;
}

void USkeletalMeshComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
	UMeshComponent::GetEditableProperties(OutProps);
	OutProps.push_back({ "SkeletalMesh", EPropertyType::String, &SkeletalMeshAssetPath });
}

void USkeletalMeshComponent::PostEditProperty(const char* PropertyName)
{
	UMeshComponent::PostEditProperty(PropertyName);

	if (std::strcmp(PropertyName, "SkeletalMesh") == 0)
	{
		if (SkeletalMeshAssetPath.empty())
		{
			SetSkeletalMesh(nullptr);
			return;
		}

		SetSkeletalMesh(FResourceManager::Get().LoadSkeletalMesh(SkeletalMeshAssetPath));
	}
	else if (std::strcmp(PropertyName, "Materials") == 0)
	{
		for (int32 i = 0; i < static_cast<int32>(Materials.size()); ++i)
		{
			SetMaterial(i, Materials[i]);
		}
	}
}

void USkeletalMeshComponent::OnSkeletalMeshChanged()
{
	SkinnedVertices.clear();
	SkinnedLocalBounds.Reset();
	bSkinningDirty = true;
	MarkBoundsDirty();
}

void USkeletalMeshComponent::EnsureSkinnedVerticesUpdated() const
{
	if (!bSkinningDirty && !bBoneTransformsDirty)
	{
		return;
	}

	const_cast<USkeletalMeshComponent*>(this)->UpdateCPUSkinnedVertices();
}

void USkeletalMeshComponent::MarkBoundsDirty()
{
	bBoundsDirty = true;
}

void USkeletalMeshComponent::EnsureBoundsUpdated() const
{
	if (!bBoundsDirty && !bTransformDirty)
	{
		return;
	}

	if (bTransformDirty)
	{
		(void)GetWorldMatrix();
	}

	const_cast<USkeletalMeshComponent*>(this)->UpdateWorldAABB();
}
