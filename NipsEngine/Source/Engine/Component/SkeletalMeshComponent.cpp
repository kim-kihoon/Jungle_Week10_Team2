#include "SkeletalMeshComponent.h"

#include <algorithm>
#include <cfloat>
#include <cstring>

#include "Asset/SkeletalMesh.h"
#include "Core/Logger.h"
#include "Core/ResourceManager.h"
#include "Object/Object.h"
#include "Runtime/Engine.h"

DEFINE_CLASS(USkeletalMeshComponent, USkinnedMeshComponent)
REGISTER_FACTORY(USkeletalMeshComponent)

namespace
{
/*
	constexpr int32 DebugRotateBoneIndex = 19;
	constexpr float DebugRotateDegrees = 45.0f;
*/
}

void USkeletalMeshComponent::SetSkeletalMesh(USkeletalMesh* InSkeletalMesh)
{
	LocalBoneTransforms.clear();
	GlobalBoneMatrices.clear();
	SkinningMatrices.clear();

    USkinnedMeshComponent::SetSkeletalMesh(InSkeletalMesh);
	InitializeBonePoses();

	if (!HasValidMesh())
	{
		return;
	}

/*
	const TArray<FSkeletalBone>& Bones = SkeletalMeshAsset->GetBones();
	if (DebugRotateBoneIndex < 0 || DebugRotateBoneIndex >= static_cast<int32>(Bones.size()))
	{
		UE_LOG("Debug bone index is invalid. BoneIndex: %d, BoneCount: %zu", DebugRotateBoneIndex, Bones.size());
		return;
	}

	UE_LOG("Debug rotate Bone[%d] Name: %s Parent: %d",
		DebugRotateBoneIndex,
		Bones[DebugRotateBoneIndex].Name.c_str(),
		Bones[DebugRotateBoneIndex].ParentIndex);

	FTransform TestPose = Bones[DebugRotateBoneIndex].RefLocalTransform;
	TestPose.SetRotation(TestPose.GetRotation() * FQuat::MakeFromEuler(FVector(0.0f, 0.0f, DebugRotateDegrees)));
	SetBoneLocalTransform(DebugRotateBoneIndex, TestPose);

	UpdateBoneMatrices();
	PerformCPUSkinning();
	MarkBoundsDirty();
	bPoseDirty = false;
*/
}

void USkeletalMeshComponent::SetBoneLocalTransform(int32 BoneIndex, const FTransform& NewTransform)
{
	if (BoneIndex >= 0 && BoneIndex < LocalBoneTransforms.size())
	{
		LocalBoneTransforms[BoneIndex] = NewTransform;

		bPoseDirty = true;
	}
}

void USkeletalMeshComponent::TestReferencePoseSkinning()
{
	if (!SkeletalMeshAsset || !SkeletalMeshAsset->GetMeshData()) return;

	const FSkeletalMesh* MeshData = SkeletalMeshAsset->GetMeshData();
	const TArray<FSkeletalMeshVertex>& SourceVertices = SkeletalMeshAsset->GetVertices();
	const TArray<FSkeletalBone>& Bones = SkeletalMeshAsset->GetBones();

	float MaxPosError = 0.0f;

	for (int32 i = 0; i < SourceVertices.size(); i++)
	{
		const FSkeletalMeshVertex& V = SourceVertices[i];
		FVector SkinnedPos = FVector::Zero();

		for (int32 w = 0; w < 4; w++)
		{
			float Weight = V.BoneWeights[w];
			if (Weight > 0.0f)
			{
				int32 BoneIndex = V.BoneIndices[w];
				const FSkeletalBone& Bone = Bones[BoneIndex];

				FMatrix SkinningMatrix = Bone.InverseBindPose * Bone.RefGlobalMatrix;
				
				FVector TransformPos = SkinningMatrix.TransformPosition(V.Position);

				SkinnedPos += TransformPos * Weight;
			}
		}

		float Error = (SkinnedPos - V.Position).Size();
		if (Error > MaxPosError)
		{
			MaxPosError = Error;
		}
	}
	UE_LOG("=== [Reference Pose Skinning Test] ===");
	UE_LOG("- Max Position Error: %f", MaxPosError);

	if (MaxPosError < 0.01f)
	{
		UE_LOG("[SUCCESS] 완벽합니다! 스키닝 연산 후에도 A-Pose가 정확히 유지됩니다.");
	}
	else
	{
		UE_LOG("[ERROR] 위치가 틀어집니다. 행렬 곱셈 순서나 TransformPosition 로직을 확인하세요.");
	}
}

void USkeletalMeshComponent::TickComponent(float DeltaTime)
{
    USkinnedMeshComponent::TickComponent(DeltaTime);

	if (bPoseDirty)
	{
		UpdateBoneMatrices();
		PerformCPUSkinning();

		bPoseDirty = false;         
		MarkBoundsDirty();
	}
}


void USkeletalMeshComponent::InitializeBonePoses()
{
	if (!HasValidMesh()) return;

	const TArray<FSkeletalBone>& Bones = SkeletalMeshAsset->GetBones();
	const int32 BoneCount = static_cast<int32>(Bones.size());

	LocalBoneTransforms.resize(BoneCount);
	GlobalBoneMatrices.resize(BoneCount);
	SkinningMatrices.resize(BoneCount);

	for (int32 i = 0; i < BoneCount; ++i)
	{
		LocalBoneTransforms[i] = Bones[i].RefLocalTransform;
	}

	UpdateBoneMatrices();
}

void USkeletalMeshComponent::UpdateBoneMatrices()
{
	if (!HasValidMesh()) return;

	const TArray<FSkeletalBone>& Bones = SkeletalMeshAsset->GetBones();

	for (int32 i = 0; i < Bones.size(); ++i)
	{
		const FSkeletalBone& Bone = Bones[i];

		FMatrix LocalMatrix = LocalBoneTransforms[i].ToMatrix();

		if (Bone.ParentIndex == -1)
		{
			GlobalBoneMatrices[i] = LocalMatrix;
		}
		else
		{
			GlobalBoneMatrices[i] = LocalMatrix * GlobalBoneMatrices[Bone.ParentIndex];
		}

		SkinningMatrices[i] = Bone.InverseBindPose * GlobalBoneMatrices[i];
	}
}
