#include "DynamicMesh.h"

#include "Core/ResourceManager.h"
#include "Render/Resource/FbxParser.h"

DEFINE_CLASS(UDynamicMesh, UObject)

UDynamicMesh::~UDynamicMesh()
{
	MeshBuffer.Release();
	delete MeshData;
	MeshData = nullptr;
}

bool UDynamicMesh::LoadFromFbx(const FString& FilePath)
{
	FDynamicMesh* LoadedMeshData = FbxParser::ParseFbx(FilePath);
	SetMeshData(LoadedMeshData);
	return HasValidMeshData();
}

void UDynamicMesh::SetMeshData(FDynamicMesh* InMeshData)
{
	if (MeshData == InMeshData)
	{
		return;
	}

	delete MeshData;
	MeshData = InMeshData;

	if (MeshData != nullptr)
	{
		MeshData->NormalizeVertexWeights();
		MeshData->EnsureReferencePoseMatrices();
		MeshData->CacheBounds();
	}

	RebuildRenderVertices();
	MarkRenderBufferDirty();
	RebuildMeshBuffer();
}

FDynamicMesh* UDynamicMesh::GetMeshData()
{
	return MeshData;
}

const FDynamicMesh* UDynamicMesh::GetMeshData() const
{
	return MeshData;
}

const FString& UDynamicMesh::GetAssetPathFileName() const
{
	static const FString Empty;
	return MeshData ? MeshData->PathFileName : Empty;
}

const TArray<FDynamicMeshVertex>& UDynamicMesh::GetVertices() const
{
	static const TArray<FDynamicMeshVertex> Empty;
	return MeshData ? MeshData->Vertices : Empty;
}

const TArray<FNormalVertex>& UDynamicMesh::GetRenderVertices() const
{
	return RenderVertices;
}

const TArray<uint32>& UDynamicMesh::GetIndices() const
{
	static const TArray<uint32> Empty;
	return MeshData ? MeshData->Indices : Empty;
}

const TArray<FDynamicMeshSection>& UDynamicMesh::GetSections() const
{
	static const TArray<FDynamicMeshSection> Empty;
	return MeshData ? MeshData->Sections : Empty;
}

const TArray<FSkeletalBone>& UDynamicMesh::GetBones() const
{
	static const TArray<FSkeletalBone> Empty;
	return MeshData ? MeshData->Bones : Empty;
}

const TArray<FMatrix>& UDynamicMesh::GetReferencePoseMatrices() const
{
	static const TArray<FMatrix> Empty;
	return MeshData ? MeshData->ReferencePoseMatrices : Empty;
}

const FAABB& UDynamicMesh::GetLocalBounds() const
{
	static const FAABB Empty;
	return MeshData ? MeshData->LocalBounds : Empty;
}

FDynamicMeshBuffer* UDynamicMesh::GetDynamicMeshBuffer()
{
	if (bRenderBufferDirty)
	{
		RebuildMeshBuffer();
	}

	return MeshBuffer.IsValid() ? &MeshBuffer : nullptr;
}

void UDynamicMesh::UpdateRenderVertices(ID3D11DeviceContext* InContext, const TArray<FNormalVertex>& InVertices)
{
	RenderVertices = InVertices;

	if (bRenderBufferDirty || !MeshBuffer.IsValid())
	{
		RebuildMeshBuffer();
		return;
	}

	MeshBuffer.Update(InContext, RenderVertices);
}

bool UDynamicMesh::HasValidMeshData() const
{
	return MeshData != nullptr && MeshData->HasValidRenderData();
}

void UDynamicMesh::RebuildRenderVertices()
{
	RenderVertices.clear();

	if (!HasValidMeshData())
	{
		return;
	}

	RenderVertices.reserve(MeshData->Vertices.size());

	for (const FDynamicMeshVertex& SourceVertex : MeshData->Vertices)
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

void UDynamicMesh::RebuildMeshBuffer()
{
	MeshBuffer.Release();

	if (!HasValidMeshData() || RenderVertices.empty() || MeshData->Indices.empty())
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

	MeshBuffer.Create(Device, RenderVertices, MeshData->Indices);
	bRenderBufferDirty = false;
}

void UDynamicMesh::MarkRenderBufferDirty()
{
	bRenderBufferDirty = true;
}
