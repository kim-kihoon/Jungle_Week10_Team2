#include "SkeletalMeshTypes.h"

#include "Core/Logger.h"

void FSkeletalMesh::CacheBounds()
{
	LocalBounds.Reset();

	for (const FSkeletalMeshVertex& Vertex : Vertices)
	{
		LocalBounds.Expand(Vertex.Position);
	}
}

void FSkeletalMesh::NormalizeVertexWeights()
{
	for (FSkeletalMeshVertex& Vertex : Vertices)
	{
		float WeightSum = 0.0f;
		for (float Weight : Vertex.BoneWeights)
		{
			WeightSum += Weight;
		}

		if (WeightSum > 0.0001f)
		{
			for (float& Weight : Vertex.BoneWeights)
			{
				Weight /= WeightSum;
			}
		}
		else
		{
			Vertex.BoneIndices[0] = 0;
			Vertex.BoneWeights[0] = 1.0f;
			for (int32 Index = 1; Index < 4; ++Index)
			{
				Vertex.BoneIndices[Index] = 0;
				Vertex.BoneWeights[Index] = 0.0f;
			}
		}
	}
}

void FSkeletalMesh::EnsureReferencePoseMatrices()
{
	ReferencePoseMatrices.clear();
	ReferencePoseMatrices.reserve(Bones.size());

	for (const FSkeletalBone& Bone : Bones)
	{
		ReferencePoseMatrices.push_back(Bone.RefGlobalMatrix);
	}
}

bool FSkeletalMesh::HasValidRenderData() const
{
	return !Vertices.empty() && !Indices.empty();
}

bool FSkeletalMesh::ValidateSkinningData() const
{
	bool bIsValid = true;

	UE_LOG("=== [SkeletalMesh Debug Report] ===");
	UE_LOG("- Vertices Count : %zu", Vertices.size());
	UE_LOG("- Indices Count  : %zu", Indices.size());
	UE_LOG("- Bones Count    : %zu", Bones.size());

	if (Bones.empty() || Vertices.empty())
	{
		UE_LOG("[ERROR] 필수 데이터(Bone 또는 Vertex)가 없습니다!");
		return false;
	}

	int32 InvalidParentCount = 0;
	int32 RootBoneCount = 0;
	float MaxMatrixError = 0.0f;

	for (int32 i = 0; i < Bones.size(); ++i)
	{
		const FSkeletalBone& Bone = Bones[i];

		if (Bone.ParentIndex == -1)
		{
			RootBoneCount++;
		}
		else if (Bone.ParentIndex < 0 || Bone.ParentIndex >= Bones.size())
		{
			InvalidParentCount++;
			UE_LOG("[ERROR] 잘못된 부모 인덱스! Bone[%s], ParentIndex: %d", Bone.Name.c_str(), Bone.ParentIndex);
		}

		FMatrix TestMatrix = Bone.InverseBindPose * Bone.RefGlobalMatrix;
		for (int Row = 0; Row < 4; Row++)
		{
			for (int Col = 0; Col < 4; Col++)
			{
				float Expected = (Row == Col) ? 1.0f : 0.0f;
				float Error = std::abs(TestMatrix.M[Row][Col] - Expected);
				if (Error > MaxMatrixError)
				{
					MaxMatrixError = Error;
				}
			}
		}
	}

	UE_LOG("- Root Bone Count: %d", RootBoneCount);
	if (RootBoneCount == 0) UE_LOG("[WARNING] 루트 본(ParentIndex == -1)이 존재하지 않습니다.");
	if (RootBoneCount > 1)  UE_LOG("[WARNING] 루트 본이 여러 개입니다."); 

	if (InvalidParentCount > 0) bIsValid = false;

	UE_LOG("- Max Matrix Error (InvBind * RefGlobal) : %f", MaxMatrixError);
	if (MaxMatrixError > 0.01f)
	{
		UE_LOG("[ERROR] 행렬 짝이 맞지 않습니다. (FBX Export 문제이거나 파싱 행렬 곱셈 순서 오류)");
		bIsValid = false;
	}

	int32 ZeroWeightCount = 0;
	int32 InvalidBoneIndexCount = 0;
	float MinWeightSum = 9999.0f;
	float MaxWeightSum = -9999.0f;

	for (const FSkeletalMeshVertex& Vertex : Vertices)
	{
		float WeightSum = 0.0f;
		for (int32 j = 0; j < 4; j++)
		{
			WeightSum += Vertex.BoneWeights[j];

			if (Vertex.BoneIndices[j] >= Bones.size())
			{
				InvalidBoneIndexCount++;
			}
		}

		if (WeightSum < MinWeightSum) MinWeightSum = WeightSum;
		if (WeightSum > MaxWeightSum) MaxWeightSum = WeightSum;

		if (WeightSum <= 0.0001f)
		{
			ZeroWeightCount++;
		}
	}

	UE_LOG("- Weight Sum [Min: %f, Max: %f]", MinWeightSum, MaxWeightSum);

	if (ZeroWeightCount > 0)
	{
		UE_LOG("[ERROR] 가중치가 0인 버텍스가 %d개 존재합니다.", ZeroWeightCount);
		bIsValid = false;
	}
	if (InvalidBoneIndexCount > 0)
	{
		UE_LOG("[CRITICAL] 존재하지 않는 Bone 배열 공간을 참조하는 버텍스가 %d개 존재합니다! (Out of Bounds)", InvalidBoneIndexCount);
		bIsValid = false;
	}

	UE_LOG("===================================");
	return bIsValid;
}
