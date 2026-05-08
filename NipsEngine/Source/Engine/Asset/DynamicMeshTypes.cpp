#include "DynamicMeshTypes.h"

void FDynamicMesh::CacheBounds()
{
	LocalBounds.Reset();

	for (const FDynamicMeshVertex& Vertex : Vertices)
	{
		LocalBounds.Expand(Vertex.Position);
	}
}

void FDynamicMesh::NormalizeVertexWeights()
{
	for (FDynamicMeshVertex& Vertex : Vertices)
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

void FDynamicMesh::EnsureReferencePoseMatrices()
{
	ReferencePoseMatrices.clear();
	ReferencePoseMatrices.reserve(Bones.size());

	for (const FSkeletalBone& Bone : Bones)
	{
		ReferencePoseMatrices.push_back(Bone.RefGlobalMatrix);
	}
}

bool FDynamicMesh::HasValidRenderData() const
{
	return !Vertices.empty() && !Indices.empty();
}
