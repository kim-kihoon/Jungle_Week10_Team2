#include "SkinnedMeshComponent.h"

#include <algorithm>
#include <cfloat>

#include "Core/ResourceManager.h"
#include "Render/Resource/Material.h"

DEFINE_CLASS(USkinnedMeshComponent, UMeshComponent)

namespace
{
	UMaterialInterface* DuplicateComponentMaterialReference(UMaterialInterface* SourceMaterial)
	{
		if (UMaterialInstance* OrigMatInst = Cast<UMaterialInstance>(SourceMaterial))
		{
			UMaterialInstance* MatInst = UMaterialInstance::CreateTransient(OrigMatInst->Parent);
			MatInst->OverridedParams = OrigMatInst->OverridedParams;
			if (OrigMatInst->HasLightingModelOverride())
			{
				MatInst->SetLightingModelOverride(OrigMatInst->GetLightingModelOverride());
			}
			else
			{
				MatInst->ClearLightingModelOverride();
			}
			return MatInst;
		}

		return SourceMaterial;
	}

	FNormalVertex MakeNormalVertex(const FSkinnedVertex& Vertex)
	{
		FNormalVertex Result = {};
		Result.Position = Vertex.Position;
		Result.Color = Vertex.Color;
		Result.Normal = Vertex.Normal;
		Result.UVs = Vertex.UVs[0];
		Result.Tangent = Vertex.Tangent;
		Result.Bitangent = Vertex.Bitangent;
		return Result;
	}

	FVector NormalizeOrFallback(const FVector& Value, const FVector& Fallback)
	{
		const FVector Normalized = Value.GetSafeNormal();
		return Normalized.IsNearlyZero() ? Fallback : Normalized;
	}

	BoneIndex ResolveBoneIndex(BoneIndex RawBoneIndex, const FSkinnedMeshSection* Section)
	{
		if (RawBoneIndex == InvalidBoneIndex)
		{
			return InvalidBoneIndex;
		}

		if (Section != nullptr && !Section->BoneMap.empty())
		{
			if (RawBoneIndex >= Section->BoneMap.size())
			{
				return InvalidBoneIndex;
			}
			return Section->BoneMap[RawBoneIndex];
		}

		return RawBoneIndex;
	}

	TArray<FMatrix> BuildReferencePoseSkinningMatrices(const FRefSkeleton& RefSkeleton)
	{
		TArray<FMatrix> ComponentSpaceMatrices;
		TArray<FMatrix> SkinningMatrices;

		const int32 BoneCount = static_cast<int32>(RefSkeleton.Bones.size());
		ComponentSpaceMatrices.resize(BoneCount, FMatrix::Identity);
		SkinningMatrices.resize(BoneCount, FMatrix::Identity);

		for (int32 BoneIndexValue = 0; BoneIndexValue < BoneCount; ++BoneIndexValue)
		{
			const FRefBone& Bone = RefSkeleton.Bones[BoneIndexValue];
			const FMatrix LocalBindMatrix = Bone.LocalBindTransform.ToMatrixWithScale();

			FMatrix ComponentSpaceMatrix = LocalBindMatrix;
			if (Bone.ParentIndex >= 0 && Bone.ParentIndex < BoneCount)
			{
				ComponentSpaceMatrix = LocalBindMatrix * ComponentSpaceMatrices[Bone.ParentIndex];
			}

			ComponentSpaceMatrices[BoneIndexValue] = ComponentSpaceMatrix;
			SkinningMatrices[BoneIndexValue] = Bone.InverseBindMatrix * ComponentSpaceMatrix;
		}

		return SkinningMatrices;
	}

	FNormalVertex SkinVertexToNormalVertex(
		const FSkinnedVertex& Vertex,
		const TArray<FMatrix>& SkinningMatrices,
		const FSkinnedMeshSection* Section)
	{
		FNormalVertex Result = MakeNormalVertex(Vertex);

		FVector SkinnedPosition = FVector::ZeroVector;
		FVector SkinnedNormal = FVector::ZeroVector;
		FVector SkinnedTangent = FVector::ZeroVector;
		FVector SkinnedBitangent = FVector::ZeroVector;
		float ValidWeightSum = 0.0f;

		for (uint32 InfluenceIndex = 0; InfluenceIndex < MaxBoneInfluences; ++InfluenceIndex)
		{
			const float Weight = Vertex.SkinWeights.Weights[InfluenceIndex];
			if (Weight <= 0.0f)
			{
				continue;
			}

			const BoneIndex SkeletonBoneIndex = ResolveBoneIndex(Vertex.SkinWeights.BoneIndices[InfluenceIndex], Section);
			if (SkeletonBoneIndex == InvalidBoneIndex || SkeletonBoneIndex >= SkinningMatrices.size())
			{
				continue;
			}

			const FMatrix& SkinningMatrix = SkinningMatrices[SkeletonBoneIndex];
			SkinnedPosition += SkinningMatrix.TransformPosition(Vertex.Position) * Weight;
			SkinnedNormal += SkinningMatrix.TransformVector(Vertex.Normal) * Weight;
			SkinnedTangent += SkinningMatrix.TransformVector(Vertex.Tangent) * Weight;
			SkinnedBitangent += SkinningMatrix.TransformVector(Vertex.Bitangent) * Weight;
			ValidWeightSum += Weight;
		}

		if (ValidWeightSum <= 0.0f)
		{
			return Result;
		}

		const float InvWeightSum = 1.0f / ValidWeightSum;
		Result.Position = SkinnedPosition * InvWeightSum;
		Result.Normal = NormalizeOrFallback(SkinnedNormal * InvWeightSum, Vertex.Normal);
		Result.Tangent = NormalizeOrFallback(SkinnedTangent * InvWeightSum, Vertex.Tangent);
		Result.Bitangent = NormalizeOrFallback(SkinnedBitangent * InvWeightSum, Vertex.Bitangent);

		return Result;
	}
}

USkinnedMeshComponent::~USkinnedMeshComponent()
{
	ReleaseSkinnedRenderBuffer();
}

void USkinnedMeshComponent::PostDuplicate(UObject* Original)
{
	UMeshComponent::PostDuplicate(Original);

	const USkinnedMeshComponent* Orig = Cast<USkinnedMeshComponent>(Original);
	if (Orig == nullptr)
	{
		return;
	}

	SkinnedMeshAsset = Orig->SkinnedMeshAsset;
	SkinnedMeshAssetPath = Orig->SkinnedMeshAssetPath;
	bBoundsDirty = true;
	bRenderStateDirty = true;
	MarkRenderCacheDirty();

	ReleaseOwnedMaterialInstances();
	Materials = TArray<UMaterialInterface*>(Orig->Materials.size());
	for (int32 i = 0; i < static_cast<int32>(Orig->Materials.size()); ++i)
	{
		Materials[i] = DuplicateComponentMaterialReference(Orig->Materials[i]);
	}
}

void USkinnedMeshComponent::Serialize(FArchive& Ar)
{
	UMeshComponent::Serialize(Ar);
	Ar << "SkinnedMeshAsset" << SkinnedMeshAssetPath;

	if (Ar.IsLoading())
	{
		SkinnedMeshAsset = nullptr;
		MarkBoundsDirty();
		MarkRenderStateDirty();
	}
}

void USkinnedMeshComponent::SetSkinnedMesh(FSkeletalMesh* InSkinnedMesh)
{
	if (SkinnedMeshAsset == InSkinnedMesh)
	{
		if (InSkinnedMesh == nullptr && (!SkinnedMeshAssetPath.empty() || !Materials.empty()))
		{
			ReleaseOwnedMaterialInstances();
			Materials.clear();
			SkinnedMeshAssetPath.clear();
			MarkBoundsDirty();
			MarkRenderStateDirty();
		}
		return;
	}

	SkinnedMeshAsset = InSkinnedMesh;
	ReleaseOwnedMaterialInstances();
	Materials.clear();

	if (SkinnedMeshAsset != nullptr)
	{
		SkinnedMeshAssetPath = SkinnedMeshAsset->PathFileName;
		InitializeMaterialsFromLOD0();
	}
	else
	{
		SkinnedMeshAssetPath.clear();
	}

	MarkBoundsDirty();
	MarkRenderStateDirty();
}

bool USkinnedMeshComponent::HasValidSkinnedMesh() const
{
	const FSkinnedMeshLOD* LOD0 = GetLOD0();
	return LOD0 != nullptr
		&& !LOD0->Vertices.empty()
		&& !LOD0->Indices.empty();
}

void USkinnedMeshComponent::UpdateWorldAABB() const
{
	WorldAABB.Reset();
	EnsureSkinnedRenderCache();

	const FSkinnedMeshLOD* LOD0 = GetLOD0();
	if (LOD0 == nullptr || !HasValidSkinnedMesh() || SkinnedRenderVertices.empty())
	{
		bBoundsDirty = false;
		return;
	}

	FAABB LocalBounds;
	LocalBounds.Reset();
	for (const FNormalVertex& Vertex : SkinnedRenderVertices)
	{
		LocalBounds.Expand(Vertex.Position);
	}

	if (!LocalBounds.IsValid())
	{
		bBoundsDirty = false;
		return;
	}

	const FVector LocalCorners[8] = {
		FVector(LocalBounds.Min.X, LocalBounds.Min.Y, LocalBounds.Min.Z),
		FVector(LocalBounds.Max.X, LocalBounds.Min.Y, LocalBounds.Min.Z),
		FVector(LocalBounds.Min.X, LocalBounds.Max.Y, LocalBounds.Min.Z),
		FVector(LocalBounds.Max.X, LocalBounds.Max.Y, LocalBounds.Min.Z),
		FVector(LocalBounds.Min.X, LocalBounds.Min.Y, LocalBounds.Max.Z),
		FVector(LocalBounds.Max.X, LocalBounds.Min.Y, LocalBounds.Max.Z),
		FVector(LocalBounds.Min.X, LocalBounds.Max.Y, LocalBounds.Max.Z),
		FVector(LocalBounds.Max.X, LocalBounds.Max.Y, LocalBounds.Max.Z)
	};

	const FMatrix& WorldMatrix = GetWorldMatrix();

	for (const FVector& Corner : LocalCorners)
	{
		const FVector WorldPos = WorldMatrix.TransformPosition(Corner);
		WorldAABB.Expand(WorldPos);
	}

	bBoundsDirty = false;
}

bool USkinnedMeshComponent::RaycastMesh(const FRay& Ray, FHitResult& OutHitResult)
{
	if (!HasValidSkinnedMesh())
	{
		return false;
	}

	EnsureBoundsUpdated();

	float BoxT = 0.0f;
	if (!WorldAABB.IntersectRay(Ray, BoxT))
	{
		return false;
	}

	const FSkinnedMeshLOD* LOD0 = GetLOD0();
	const TArray<FNormalVertex>& Vertices = GetSkinnedRenderVertices();
	const TArray<uint32>& Indices = LOD0->Indices;

	if (Vertices.empty() || Indices.empty())
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

		if (I0 >= Vertices.size() || I1 >= Vertices.size() || I2 >= Vertices.size())
		{
			continue;
		}

		const FVector& V0 = Vertices[I0].Position;
		const FVector& V1 = Vertices[I1].Position;
		const FVector& V2 = Vertices[I2].Position;

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

const FAABB& USkinnedMeshComponent::GetWorldAABB() const
{
	EnsureBoundsUpdated();
	return WorldAABB;
}

bool USkinnedMeshComponent::ConsumeRenderStateDirty()
{
	const bool bWasDirty = bRenderStateDirty;
	bRenderStateDirty = false;
	return bWasDirty;
}

FMeshBuffer* USkinnedMeshComponent::GetSkinnedRenderMeshBuffer() const
{
	EnsureSkinnedRenderBuffer();
	return SkinnedRenderMeshBuffer.IsValid() ? &SkinnedRenderMeshBuffer : nullptr;
}

const TArray<FSkinnedMeshSection>& USkinnedMeshComponent::GetSkinnedRenderSections() const
{
	EnsureSkinnedRenderCache();
	return SkinnedRenderSections;
}

const TArray<FNormalVertex>& USkinnedMeshComponent::GetSkinnedRenderVertices() const
{
	EnsureSkinnedRenderCache();
	return SkinnedRenderVertices;
}

void USkinnedMeshComponent::InitializeMaterialsFromLOD0()
{
	const FSkinnedMeshLOD* LOD0 = GetLOD0();
	if (SkinnedMeshAsset == nullptr || LOD0 == nullptr)
	{
		return;
	}

	Materials.reserve(LOD0->Sections.size());
	for (const FSkinnedMeshSection& Section : LOD0->Sections)
	{
		UMaterialInterface* Material = nullptr;
		if (Section.MaterialSlotIndex >= 0 && Section.MaterialSlotIndex < static_cast<int32>(SkinnedMeshAsset->Slots.size()))
		{
			Material = SkinnedMeshAsset->Slots[Section.MaterialSlotIndex].Material;
		}
		Materials.push_back(Material);
	}
}

void USkinnedMeshComponent::MarkBoundsDirty()
{
	bBoundsDirty = true;
	NotifySpatialIndexDirty();
}

void USkinnedMeshComponent::MarkRenderStateDirty()
{
	bRenderStateDirty = true;
	MarkRenderCacheDirty();
}

void USkinnedMeshComponent::MarkRenderCacheDirty()
{
	bSkinnedRenderCacheDirty = true;
	bSkinnedRenderBufferDirty = true;
}

const FSkinnedMeshLOD* USkinnedMeshComponent::GetLOD0() const
{
	if (SkinnedMeshAsset == nullptr || SkinnedMeshAsset->LODs.empty())
	{
		return nullptr;
	}

	return &SkinnedMeshAsset->LODs[0];
}

void USkinnedMeshComponent::EnsureSkinnedRenderCache() const
{
	if (!bSkinnedRenderCacheDirty)
	{
		return;
	}

	SkinnedRenderVertices.clear();
	SkinnedRenderSections.clear();
	bSkinnedRenderCacheDirty = false;
	bSkinnedRenderBufferDirty = true;

	const FSkinnedMeshLOD* LOD0 = GetLOD0();
	if (LOD0 == nullptr || LOD0->Vertices.empty() || LOD0->Indices.empty())
	{
		ReleaseSkinnedRenderBuffer();
		return;
	}

	SkinnedRenderSections = LOD0->Sections;
	if (SkinnedRenderSections.empty())
	{
		FSkinnedMeshSection DefaultSection;
		DefaultSection.StartIndex = 0;
		DefaultSection.IndexCount = static_cast<uint32>(LOD0->Indices.size());
		DefaultSection.MaterialSlotIndex = 0;
		DefaultSection.BaseVertex = 0;
		DefaultSection.VertexCount = static_cast<uint32>(LOD0->Vertices.size());
		SkinnedRenderSections.push_back(DefaultSection);
	}

	const TArray<FMatrix> SkinningMatrices = SkinnedMeshAsset != nullptr
		? BuildReferencePoseSkinningMatrices(SkinnedMeshAsset->RefSkeleton)
		: TArray<FMatrix>();

	SkinnedRenderVertices.reserve(LOD0->Vertices.size());
	for (const FSkinnedVertex& Vertex : LOD0->Vertices)
	{
		SkinnedRenderVertices.push_back(SkinVertexToNormalVertex(Vertex, SkinningMatrices, nullptr));
	}

	for (const FSkinnedMeshSection& Section : SkinnedRenderSections)
	{
		if (Section.BoneMap.empty() || Section.VertexCount == 0)
		{
			continue;
		}

		const uint32 StartVertex = Section.BaseVertex;
		const uint32 EndVertex = std::min<uint32>(
			static_cast<uint32>(LOD0->Vertices.size()),
			StartVertex + Section.VertexCount);

		for (uint32 VertexIndex = StartVertex; VertexIndex < EndVertex; ++VertexIndex)
		{
			SkinnedRenderVertices[VertexIndex] = SkinVertexToNormalVertex(
				LOD0->Vertices[VertexIndex],
				SkinningMatrices,
				&Section);
		}
	}
}

void USkinnedMeshComponent::EnsureSkinnedRenderBuffer() const
{
	EnsureSkinnedRenderCache();

	if (SkinnedRenderVertices.empty())
	{
		ReleaseSkinnedRenderBuffer();
		return;
	}

	ID3D11Device* Device = FResourceManager::Get().GetCachedDevice();
	if (Device == nullptr)
	{
		return;
	}

	if (!bSkinnedRenderBufferDirty && SkinnedRenderMeshBuffer.IsValid() && SkinnedRenderBufferDevice == Device)
	{
		return;
	}

	const FSkinnedMeshLOD* LOD0 = GetLOD0();
	if (LOD0 == nullptr || LOD0->Indices.empty())
	{
		ReleaseSkinnedRenderBuffer();
		return;
	}

	SkinnedRenderMeshBuffer.Release();
	SkinnedRenderMeshBuffer.Create(Device, SkinnedRenderVertices, LOD0->Indices);
	SkinnedRenderBufferDevice = Device;
	bSkinnedRenderBufferDirty = false;
}

void USkinnedMeshComponent::ReleaseSkinnedRenderBuffer() const
{
	SkinnedRenderMeshBuffer.Release();
	SkinnedRenderBufferDevice = nullptr;
	bSkinnedRenderBufferDirty = true;
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
		return;
	}

	UpdateWorldAABB();
}
