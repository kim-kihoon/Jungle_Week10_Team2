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

	ResetToRefPose();
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
		CurrentComponentSpaceMatrices.clear();
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

	if (static_cast<int32>(CurrentLocalTransforms.size()) != BoneCount)
	{
		ResetToRefPose();
	}

	CurrentComponentSpaceMatrices.resize(BoneCount, FMatrix::Identity);
	for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
	{
		const FMatrix CurrentLocalMatrix = CurrentLocalTransforms[BoneIndex].ToMatrixWithScale();
		const int32 ParentIndex = RefSkeleton.BoneInfo[BoneIndex].ParentIndex;

		// The engine uses row-vector transforms. Local * Parent accumulates into component space.
		if (ParentIndex >= 0 && ParentIndex < BoneIndex)
		{
			CurrentComponentSpaceMatrices[BoneIndex] = CurrentLocalMatrix * CurrentComponentSpaceMatrices[ParentIndex];
		}
		else
		{
			CurrentComponentSpaceMatrices[BoneIndex] = CurrentLocalMatrix;
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

void USkinnedMeshComponent::ResetToRefPose()
{
	CurrentLocalTransforms.clear();
	CurrentComponentSpaceMatrices.clear();
	SkinningMatrices.clear();

	if (SkeletalMesh == nullptr)
	{
		return;
	}

	const FReferenceSkeleton& RefSkeleton = SkeletalMesh->GetRefSkeleton();
	CurrentLocalTransforms = RefSkeleton.LocalRefTransforms;
	CurrentComponentSpaceMatrices.resize(RefSkeleton.GetNum(), FMatrix::Identity);
	SkinningMatrices.resize(RefSkeleton.GetNum(), FMatrix::Identity);
}

void USkinnedMeshComponent::UpdateSkinningMatrices()
{
	if (SkeletalMesh == nullptr)
	{
		return;
	}

	const TArray<FMatrix>& InverseRefMatrices = SkeletalMesh->GetInverseRefMatrices();
	const int32 BoneCount = static_cast<int32>(CurrentComponentSpaceMatrices.size());
	SkinningMatrices.resize(BoneCount, FMatrix::Identity);

	for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
	{
		const FMatrix& InvRef = BoneIndex < static_cast<int32>(InverseRefMatrices.size())
			? InverseRefMatrices[BoneIndex]
			: FMatrix::Identity;

		// Row-vector convention: Position * InvRef * CurrentComponentSpace.
		SkinningMatrices[BoneIndex] = InvRef * CurrentComponentSpaceMatrices[BoneIndex];
	}
}
