#include "SkeletalMeshComponent.h"

#include <algorithm>
#include <cfloat>
#include <cstring>

#include "Asset/SkeletalMesh.h"
#include "Core/ResourceManager.h"
#include "Object/Object.h"

DEFINE_CLASS(USkeletalMeshComponent, UMeshComponent)
REGISTER_FACTORY(USkeletalMeshComponent)

USkeletalMeshComponent::~USkeletalMeshComponent()
{
	ReleaseOwnedSkeletalMesh();
}

void USkeletalMeshComponent::PostDuplicate(UObject* Original)
{
	UMeshComponent::PostDuplicate(Original);

	const USkeletalMeshComponent* Orig = Cast<USkeletalMeshComponent>(Original);
	SkeletalMeshAsset = Orig->SkeletalMeshAsset;
	SkeletalMeshAssetPath = Orig->SkeletalMeshAssetPath;
	RenderVertices = Orig->RenderVertices;
	bBoundsDirty = true;
	bRenderBufferDirty = true;

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
	Ar << "SkeletalMeshAsset" << SkeletalMeshAssetPath;

	if (Ar.IsLoading())
	{
		TArray<UMaterialInterface*> SavedMaterials = Materials;
		if (!SkeletalMeshAssetPath.empty())
		{
			LoadSkeletalMesh(SkeletalMeshAssetPath);
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

bool USkeletalMeshComponent::InitializeSkeletalMesh(const FString& FilePath)
{
	return LoadSkeletalMesh(FilePath);
}

bool USkeletalMeshComponent::LoadSkeletalMesh(const FString& FilePath)
{
	USkeletalMesh* LoadedMesh = UObjectManager::Get().CreateObject<USkeletalMesh>();
	if (!LoadedMesh->LoadFromFbx(FilePath))
	{
		UObjectManager::Get().DestroyObject(LoadedMesh);
		SetSkeletalMesh(nullptr);
		SkeletalMeshAssetPath = FilePath;
		return false;
	}

	SetSkeletalMesh(LoadedMesh, true);
	SkeletalMeshAssetPath = FilePath;
	return true;
}

void USkeletalMeshComponent::SetSkeletalMesh(USkeletalMesh* InSkeletalMesh, bool bTakeOwnership)
{
	if (SkeletalMeshAsset == InSkeletalMesh)
	{
		bOwnsSkeletalMesh = bTakeOwnership;
		return;
	}

	ReleaseOwnedSkeletalMesh();
	SkeletalMeshAsset = InSkeletalMesh;
	bOwnsSkeletalMesh = bTakeOwnership;
	ReleaseOwnedMaterialInstances();
	Materials.clear();

	if (SkeletalMeshAsset != nullptr)
	{
		SkeletalMeshAssetPath = SkeletalMeshAsset->GetAssetPathFileName();
		const FSkeletalMesh* MeshData = SkeletalMeshAsset->GetMeshData();
		if (MeshData != nullptr)
		{
			Materials.reserve(MeshData->Sections.size());
			for (const FSkeletalMeshSection& Section : MeshData->Sections)
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
		SkeletalMeshAssetPath.clear();
	}

	MarkBoundsDirty();
	MarkRenderBufferDirty();
}

USkeletalMesh* USkeletalMeshComponent::GetSkeletalMesh() const
{
	return SkeletalMeshAsset;
}

bool USkeletalMeshComponent::HasValidMesh() const
{
	return SkeletalMeshAsset != nullptr && SkeletalMeshAsset->HasValidMeshData();
}

FDynamicMeshBuffer* USkeletalMeshComponent::GetRenderBuffer()
{
	if (bRenderBufferDirty)
	{
		RebuildRenderVertices();
		RebuildMeshBuffer();
	}

	return MeshBuffer.IsValid() ? &MeshBuffer : nullptr;
}

const TArray<FNormalVertex>& USkeletalMeshComponent::GetRenderVertices() const
{
	return RenderVertices;
}

const TArray<uint32>& USkeletalMeshComponent::GetRenderIndices() const
{
	static const TArray<uint32> Empty;
	return SkeletalMeshAsset ? SkeletalMeshAsset->GetIndices() : Empty;
}

const TArray<FSkeletalMeshSection>& USkeletalMeshComponent::GetSections() const
{
	static const TArray<FSkeletalMeshSection> Empty;
	return SkeletalMeshAsset ? SkeletalMeshAsset->GetSections() : Empty;
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

		LoadSkeletalMesh(SkeletalMeshAssetPath);
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

	const FAABB& LocalBounds = SkeletalMeshAsset->GetLocalBounds();
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

void USkeletalMeshComponent::ReleaseOwnedSkeletalMesh()
{
	if (bOwnsSkeletalMesh && SkeletalMeshAsset != nullptr)
	{
		UObjectManager::Get().DestroyObject(SkeletalMeshAsset);
	}
	SkeletalMeshAsset = nullptr;
	bOwnsSkeletalMesh = false;
}

void USkeletalMeshComponent::RebuildRenderVertices()
{
	RenderVertices.clear();

	if (!HasValidMesh())
	{
		return;
	}

	const TArray<FSkeletalMeshVertex>& SourceVertices = SkeletalMeshAsset->GetVertices();
	RenderVertices.reserve(SourceVertices.size());

	for (const FSkeletalMeshVertex& SourceVertex : SourceVertices)
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

void USkeletalMeshComponent::UpdateRenderVertices(ID3D11DeviceContext* InContext, const TArray<FNormalVertex>& InVertices)
{
	RenderVertices = InVertices;

	if (bRenderBufferDirty || !MeshBuffer.IsValid())
	{
		RebuildMeshBuffer();
		return;
	}

	MeshBuffer.Update(InContext, RenderVertices);
}

void USkeletalMeshComponent::RebuildMeshBuffer()
{
	MeshBuffer.Release();

	if (!HasValidMesh())
	{
		bRenderBufferDirty = false;
		return;
	}

	ID3D11Device* Device = FResourceManager::Get().GetCachedDevice();
	if (Device == nullptr)
	{
		bRenderBufferDirty = true;
		return;
	}

	MeshBuffer.Create(Device, RenderVertices, SkeletalMeshAsset->GetMeshData()->Indices);
	bRenderBufferDirty = false;
}

void USkeletalMeshComponent::MarkRenderBufferDirty()
{
	bRenderBufferDirty = true;
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
		return;
	}

	const_cast<USkeletalMeshComponent*>(this)->UpdateWorldAABB();
}
