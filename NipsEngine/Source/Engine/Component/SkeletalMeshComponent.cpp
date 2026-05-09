#include "SkeletalMeshComponent.h"

#include <algorithm>
#include <cfloat>
#include <cstring>

#include "Core/ResourceManager.h"
#include "Object/Object.h"

DEFINE_CLASS(USkeletalMeshComponent, UMeshComponent)
REGISTER_FACTORY(USkeletalMeshComponent)

USkeletalMeshComponent::~USkeletalMeshComponent()
{
	ReleaseOwnedDynamicMesh();
}

void USkeletalMeshComponent::PostDuplicate(UObject* Original)
{
	UMeshComponent::PostDuplicate(Original);

	const USkeletalMeshComponent* Orig = Cast<USkeletalMeshComponent>(Original);
	DynamicMeshAsset = Orig->DynamicMeshAsset;
	bOwnsDynamicMesh = false;
	DynamicMeshAssetPath = Orig->DynamicMeshAssetPath;
	RenderVertices = Orig->RenderVertices;
	bBoundsDirty = true;
	bRenderStateDirty = true;

	Materials = TArray<UMaterialInterface*>(Orig->Materials.size());
	for (int32 i = 0; i < static_cast<int32>(Orig->Materials.size()); ++i)
	{
		if (UMaterialInstance* OrigMatInst = Cast<UMaterialInstance>(Orig->Materials[i]))
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
			Materials[i] = MatInst;
		}
		else
		{
			Materials[i] = Orig->Materials[i];
		}
	}
}

void USkeletalMeshComponent::Serialize(FArchive& Ar)
{
	UMeshComponent::Serialize(Ar);
	Ar << "SkeletalMeshAsset" << DynamicMeshAssetPath;

	if (Ar.IsLoading())
	{
		TArray<UMaterialInterface*> SavedMaterials = Materials;
		if (!DynamicMeshAssetPath.empty())
		{
			LoadSkeletalMesh(DynamicMeshAssetPath);
		}
		else
		{
			SetDynamicMesh(nullptr);
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

bool USkeletalMeshComponent::InitializeSkeletalMesh(const FString& FilePath)
{
	return LoadSkeletalMesh(FilePath);
}

bool USkeletalMeshComponent::LoadSkeletalMesh(const FString& FilePath)
{
	UDynamicMesh* LoadedMesh = UObjectManager::Get().CreateObject<UDynamicMesh>();
	if (!LoadedMesh->LoadFromFbx(FilePath))
	{
		UObjectManager::Get().DestroyObject(LoadedMesh);
		SetDynamicMesh(nullptr);
		DynamicMeshAssetPath = FilePath;
		return false;
	}

	SetDynamicMesh(LoadedMesh, true);
	DynamicMeshAssetPath = FilePath;
	return true;
}

void USkeletalMeshComponent::SetDynamicMesh(UDynamicMesh* InDynamicMesh, bool bTakeOwnership)
{
	if (DynamicMeshAsset == InDynamicMesh)
	{
		bOwnsDynamicMesh = bTakeOwnership;
		return;
	}

	ReleaseOwnedDynamicMesh();
	DynamicMeshAsset = InDynamicMesh;
	bOwnsDynamicMesh = bTakeOwnership;
	ReleaseOwnedMaterialInstances();
	Materials.clear();

	if (DynamicMeshAsset != nullptr)
	{
		DynamicMeshAssetPath = DynamicMeshAsset->GetAssetPathFileName();
		const FDynamicMesh* MeshData = DynamicMeshAsset->GetMeshData();
		if (MeshData != nullptr)
		{
			Materials.reserve(MeshData->Sections.size());
			for (const FDynamicMeshSection& Section : MeshData->Sections)
			{
				UMaterialInterface* Material = nullptr;
				if (Section.MaterialIndex >= 0 && Section.MaterialIndex < static_cast<int32>(MeshData->Slots.size()))
				{
					Material = MeshData->Slots[Section.MaterialIndex].Material;
				}
				if (Material == nullptr && !Section.MaterialSlotName.empty())
				{
					Material = FResourceManager::Get().GetMaterial(Section.MaterialSlotName);
				}
				if (Material == nullptr)
				{
					Material = FResourceManager::Get().GetMaterial("DefaultWhite");
				}
				Materials.push_back(Material);
			}
		}
	}
	else
	{
		DynamicMeshAssetPath.clear();
	}

	RebuildRenderVertices();
	MarkBoundsDirty();
	MarkRenderStateDirty();
}

UDynamicMesh* USkeletalMeshComponent::GetDynamicMesh() const
{
	return DynamicMeshAsset;
}

bool USkeletalMeshComponent::HasValidMesh() const
{
	return DynamicMeshAsset != nullptr && DynamicMeshAsset->HasValidMeshData();
}

FDynamicMeshBuffer* USkeletalMeshComponent::GetDynamicMeshBuffer()
{
	if (DynamicMeshAsset == nullptr)
	{
		return nullptr;
	}

	return DynamicMeshAsset->GetDynamicMeshBuffer();
}

const TArray<FNormalVertex>& USkeletalMeshComponent::GetRenderVertices() const
{
	return RenderVertices;
}

const TArray<uint32>& USkeletalMeshComponent::GetRenderIndices() const
{
	static const TArray<uint32> Empty;
	return DynamicMeshAsset ? DynamicMeshAsset->GetIndices() : Empty;
}

const TArray<FDynamicMeshSection>& USkeletalMeshComponent::GetSections() const
{
	static const TArray<FDynamicMeshSection> Empty;
	return DynamicMeshAsset ? DynamicMeshAsset->GetSections() : Empty;
}

void USkeletalMeshComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
	UMeshComponent::GetEditableProperties(OutProps);
	OutProps.push_back({ "SkeletalMesh", EPropertyType::String, &DynamicMeshAssetPath });
}

void USkeletalMeshComponent::PostEditProperty(const char* PropertyName)
{
	UMeshComponent::PostEditProperty(PropertyName);

	if (std::strcmp(PropertyName, "SkeletalMesh") == 0)
	{
		if (DynamicMeshAssetPath.empty())
		{
			SetDynamicMesh(nullptr);
			return;
		}

		LoadSkeletalMesh(DynamicMeshAssetPath);
	}
	else if (std::strcmp(PropertyName, "Materials") == 0)
	{
		for (int32 i = 0; i < static_cast<int32>(Materials.size()); ++i)
		{
			SetMaterial(i, Materials[i]);
		}
	}
}

void USkeletalMeshComponent::UpdateWorldAABB() const
{
	WorldAABB.Reset();

	if (!HasValidMesh())
	{
		bBoundsDirty = false;
		return;
	}

	const FAABB& LocalBounds = DynamicMeshAsset->GetLocalBounds();
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
		WorldAABB.Expand(WorldMatrix.TransformPosition(Corner));
	}

	bBoundsDirty = false;
}

bool USkeletalMeshComponent::RaycastMesh(const FRay& Ray, FHitResult& OutHitResult)
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

	const TArray<uint32>& Indices = GetRenderIndices();
	if (RenderVertices.empty() || Indices.empty())
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

		if (I0 >= RenderVertices.size() || I1 >= RenderVertices.size() || I2 >= RenderVertices.size())
		{
			continue;
		}

		const FVector& V0 = RenderVertices[I0].Position;
		const FVector& V1 = RenderVertices[I1].Position;
		const FVector& V2 = RenderVertices[I2].Position;

		float HitT = 0.0f;
		if (IntersectTriangle(LocalRay.Origin, LocalRay.Direction, V0, V1, V2, HitT) && HitT < ClosestT)
		{
			ClosestT = HitT;
			bHit = true;
			BestFaceIndex = static_cast<int32>(i / 3);
			BestLocalNormal = FVector::CrossProduct(V1 - V0, V2 - V0).GetSafeNormal();
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

const FAABB& USkeletalMeshComponent::GetWorldAABB() const
{
	EnsureBoundsUpdated();
	return WorldAABB;
}

void USkeletalMeshComponent::ReleaseOwnedDynamicMesh()
{
	if (bOwnsDynamicMesh && DynamicMeshAsset != nullptr)
	{
		UObjectManager::Get().DestroyObject(DynamicMeshAsset);
	}
	DynamicMeshAsset = nullptr;
	bOwnsDynamicMesh = false;
}

void USkeletalMeshComponent::RebuildRenderVertices()
{
	RenderVertices.clear();

	if (!HasValidMesh())
	{
		return;
	}

	const TArray<FDynamicMeshVertex>& SourceVertices = DynamicMeshAsset->GetVertices();
	RenderVertices.reserve(SourceVertices.size());

	for (const FDynamicMeshVertex& SourceVertex : SourceVertices)
	{
		FNormalVertex RenderVertex;
		RenderVertex.Position = SourceVertex.Position;
		RenderVertex.Color = FColor(SourceVertex.Color.X, SourceVertex.Color.Y, SourceVertex.Color.Z, SourceVertex.Color.W);
		RenderVertex.Normal = SourceVertex.Normal;
		RenderVertex.UVs = SourceVertex.UV;
		RenderVertex.Tangent = FVector(SourceVertex.Tangent.X, SourceVertex.Tangent.Y, SourceVertex.Tangent.Z);
		RenderVertex.Bitangent = FVector::CrossProduct(RenderVertex.Normal, RenderVertex.Tangent).GetSafeNormal();
		RenderVertices.push_back(RenderVertex);
	}
}

void USkeletalMeshComponent::MarkBoundsDirty()
{
	bBoundsDirty = true;
}

void USkeletalMeshComponent::MarkRenderStateDirty()
{
	bRenderStateDirty = true;
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
		return;
	}

	const_cast<USkeletalMeshComponent*>(this)->UpdateWorldAABB();
}
