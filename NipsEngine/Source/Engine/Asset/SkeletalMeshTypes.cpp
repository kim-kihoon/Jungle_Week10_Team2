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

	if (Bones.empty())
	{
		UE_LOG("SkeletalMesh Log - 뼈대 데이터 없음");
		bIsValid = false;
	}

	int32 ZeroWeightCount = 0;
	for (const FSkeletalMeshVertex& Vertex : Vertices)
	{
		float WeightSum = 0.0f;
		for (int32 i = 0; i < 4; i++)
		{
			WeightSum += Vertex.BoneWeights[i];
		}

		if (WeightSum <= 0.0001f)
		{
			ZeroWeightCount++;
		}
	}

	if (ZeroWeightCount > 0)
	{
		UE_LOG("SkeletalMesh Log - 가중치가 없는 버텍스 존재");
		bIsValid = false;
	}

	int32 InvalidMatrixCount = 0;
	const float Tolerance = 0.001f;

	for (const FSkeletalBone& Bone : Bones)
	{
		FMatrix TestMatrix = Bone.InverseBindPose * Bone.RefGlobalMatrix;

		bool bIsIdentity = true;
		for (int Row = 0; Row < 4; Row++)
		{
		    for (int Col = 0; Col < 4; Col++)
		    {
				float ExpectedValue = (Row == Col) ? 1.0f : 0.0f;
				float ActualValue = TestMatrix.M[Row][Col];

				if (std::abs(ActualValue - ExpectedValue) > Tolerance)
				{
					bIsIdentity = false;
					break;
				}
		    }
			if (!bIsIdentity) break;
		}

		if (!bIsIdentity)
		{
			InvalidMatrixCount++;
			UE_LOG("SkeletalMesh Log - 유효하지 않은 InverseBindPose 행렬 발견: Bone[%s]", Bone.Name.c_str());
		}
	}

	return bIsValid;
}
