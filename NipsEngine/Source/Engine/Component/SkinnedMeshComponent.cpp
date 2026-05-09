#include "SkinnedMeshComponent.h"

#include "Render/Resource/Material.h"

DEFINE_CLASS(USkinnedMeshComponent, UMeshComponent)

void USkinnedMeshComponent::SetSkeletalMesh(USkeletalMesh* InSkeletalMesh)
{
	if (SkeletalMesh == InSkeletalMesh)
	{
		return;
	}

	SkeletalMesh = InSkeletalMesh;
	ReleaseOwnedMaterialInstances();
	Materials.clear();

	if (SkeletalMesh != nullptr)
	{
		SkeletalMeshAssetPath = SkeletalMesh->GetAssetPathFileName();

		const TArray<FSkeletalMaterial>& MeshMaterials = SkeletalMesh->GetMaterials();
		Materials.reserve(MeshMaterials.size());
		for (const FSkeletalMaterial& MaterialSlot : MeshMaterials)
		{
			Materials.push_back(MaterialSlot.Material);
		}
	}
	else
	{
		SkeletalMeshAssetPath.clear();
	}

	InitializeReferencePose();
	OnSkeletalMeshChanged();
	MarkSkinningDirty();
}

bool USkinnedMeshComponent::HasValidMesh() const
{
	return SkeletalMesh != nullptr && SkeletalMesh->HasValidMeshData();
}

void USkinnedMeshComponent::RefreshBoneTransforms()
{
	if (!HasValidMesh())
	{
		ComponentSpaceTransforms.clear();
		SkinningMatrices.clear();
		bBoneTransformsDirty = false;
		return;
	}

	if (!bBoneTransformsDirty)
	{
		return;
	}

	const FReferenceSkeleton& RefSkeleton = SkeletalMesh->GetRefSkeleton();
	const int32 BoneCount = RefSkeleton.GetNum();

	if (static_cast<int32>(BoneSpaceTransforms.size()) != BoneCount)
	{
		InitializeReferencePose();
	}

	ComponentSpaceTransforms.resize(BoneCount, FMatrix::Identity);
	for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
	{
		const FMatrix LocalMatrix = BoneSpaceTransforms[BoneIndex].ToMatrixWithScale();
		const int32 ParentIndex = RefSkeleton.BoneInfo[BoneIndex].ParentIndex;

		// The engine uses row-vector transforms. Local * Parent accumulates into component space.
		if (ParentIndex >= 0 && ParentIndex < BoneIndex)
		{
			ComponentSpaceTransforms[BoneIndex] = LocalMatrix * ComponentSpaceTransforms[ParentIndex];
		}
		else
		{
			ComponentSpaceTransforms[BoneIndex] = LocalMatrix;
		}
	}

	UpdateSkinningMatrices();
	bBoneTransformsDirty = false;
	bRenderStateDirty = true;
}

void USkinnedMeshComponent::MarkSkinningDirty()
{
	bBoneTransformsDirty = true;
	bRenderStateDirty = true;
}

bool USkinnedMeshComponent::ConsumeRenderStateDirty()
{
	const bool bWasDirty = bRenderStateDirty;
	bRenderStateDirty = false;
	return bWasDirty;
}

void USkinnedMeshComponent::OnSkeletalMeshChanged()
{
}

void USkinnedMeshComponent::InitializeReferencePose()
{
	BoneSpaceTransforms.clear();
	ComponentSpaceTransforms.clear();
	SkinningMatrices.clear();

	if (SkeletalMesh == nullptr)
	{
		return;
	}

	const FReferenceSkeleton& RefSkeleton = SkeletalMesh->GetRefSkeleton();
	BoneSpaceTransforms = RefSkeleton.RefBonePose;
	ComponentSpaceTransforms.resize(RefSkeleton.GetNum(), FMatrix::Identity);
	SkinningMatrices.resize(RefSkeleton.GetNum(), FMatrix::Identity);
}

void USkinnedMeshComponent::UpdateSkinningMatrices()
{
	if (SkeletalMesh == nullptr)
	{
		return;
	}

	const TArray<FMatrix>& RefBasesInvMatrix = SkeletalMesh->GetRefBasesInvMatrix();
	const int32 BoneCount = static_cast<int32>(ComponentSpaceTransforms.size());
	SkinningMatrices.resize(BoneCount, FMatrix::Identity);

	for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
	{
		const FMatrix& InvBind = BoneIndex < static_cast<int32>(RefBasesInvMatrix.size())
			? RefBasesInvMatrix[BoneIndex]
			: FMatrix::Identity;

		// Row-vector convention: Position * InvBind * CurrentComponentSpace.
		SkinningMatrices[BoneIndex] = InvBind * ComponentSpaceTransforms[BoneIndex];
	}
}
