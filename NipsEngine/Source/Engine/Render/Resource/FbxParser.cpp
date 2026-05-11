#include "FbxParser.h"
#include "fbxsdk.h"
#include "Asset/SkeletalMeshTypes.h"
#include "Core/Paths.h"
#include "Core/ResourceManager.h"
#include "Core/Logger.h"
#include <vector>
#include <algorithm>
#include <filesystem>
#include <functional>
#include <map>
#include <cmath>

namespace
{
	constexpr int32 TriangleVertexCount = 3;
	constexpr int32 MaxBoneInfluences = 4;
	constexpr int32 Vector2ComponentCount = 2;
	constexpr int32 Vector3ComponentCount = 3;
	constexpr int32 Vector4ComponentCount = 4;
	constexpr float VertexKeyQuantizeScale = 100000.0f;

	FMatrix ToMatrix(const FbxAMatrix& InMatrix)
	{
		FMatrix OutMatrix;
		for (int32 Row = 0; Row < 4; ++Row)
		{
			for (int32 Col = 0; Col < 4; ++Col)
			{
				OutMatrix.M[Row][Col] = static_cast<float>(InMatrix.Get(Row, Col));
			}
		}
		return OutMatrix;
	}

	int32 GetLayerElementIndex(FbxLayerElement::EMappingMode MappingMode, FbxLayerElement::EReferenceMode ReferenceMode,
		const FbxLayerElementArrayTemplate<int>& IndexArray, int32 ControlPointIndex, int32 PolygonVertexIndex)
	{
		int32 MappingIndex = 0;
		if (MappingMode == FbxLayerElement::eByControlPoint)
		{
			MappingIndex = ControlPointIndex;
		}
		else if (MappingMode == FbxLayerElement::eByPolygonVertex)
		{
			MappingIndex = PolygonVertexIndex;
		}
		else
		{
			return -1;
		}

		if (ReferenceMode == FbxLayerElement::eDirect)
		{
			return MappingIndex;
		}

		if (MappingIndex >= 0 && MappingIndex < IndexArray.GetCount())
		{
			return IndexArray.GetAt(MappingIndex);
		}

		return -1;
	}

	int32 GetMaterialIndex(FbxGeometryElementMaterial* MaterialElement, int32 PolygonIndex)
	{
		if (!MaterialElement)
		{
			return 0;
		}

		int32 MappingIndex = 0;
		if (MaterialElement->GetMappingMode() == FbxGeometryElement::eByPolygon)
		{
			MappingIndex = PolygonIndex;
		}
		else if (MaterialElement->GetMappingMode() != FbxGeometryElement::eAllSame)
		{
			return 0;
		}

		if (MaterialElement->GetReferenceMode() == FbxGeometryElement::eDirect)
		{
			return MappingIndex;
		}

		if (MappingIndex >= 0 && MappingIndex < MaterialElement->GetIndexArray().GetCount())
		{
			return MaterialElement->GetIndexArray().GetAt(MappingIndex);
		}

		return 0;
	}

	void HashCombine(size_t& Seed, size_t Value)
	{
		Seed ^= Value + 0x9e3779b97f4a7c15ull + (Seed << 6) + (Seed >> 2);
	}

	int32 QuantizeVertexFloat(float Value)
	{
		return static_cast<int32>(std::round(Value * VertexKeyQuantizeScale));
	}

	bool IsValidDirectIndex(int32 DirectIndex, int32 DirectArrayCount)
	{
		return DirectIndex >= 0 && DirectIndex < DirectArrayCount;
	}

	FVector ReadFbxPosition(FbxVector4* ControlPoints, int32 ControlPointIndex)
	{
		FbxVector4 Position = ControlPoints[ControlPointIndex];
		return FVector(
			static_cast<float>(Position[0]),
			static_cast<float>(Position[1]),
			static_cast<float>(Position[2]));
	}

	FVector ReadFbxNormal(FbxGeometryElementNormal* NormalElement, int32 ControlPointIndex, int32 PolygonVertexIndex)
	{
		if (NormalElement == nullptr)
		{
			return FVector::ZeroVector;
		}

		const int32 DirectIndex = GetLayerElementIndex(
			NormalElement->GetMappingMode(),
			NormalElement->GetReferenceMode(),
			NormalElement->GetIndexArray(),
			ControlPointIndex,
			PolygonVertexIndex);

		if (!IsValidDirectIndex(DirectIndex, NormalElement->GetDirectArray().GetCount()))
		{
			return FVector::ZeroVector;
		}

		FbxVector4 Normal = NormalElement->GetDirectArray().GetAt(DirectIndex);
		Normal.Normalize();
		return FVector(
			static_cast<float>(Normal[0]),
			static_cast<float>(Normal[1]),
			static_cast<float>(Normal[2]));
	}

	FVector4 ReadFbxTangent(FbxGeometryElementTangent* TangentElement, int32 ControlPointIndex, int32 PolygonVertexIndex)
	{
		if (TangentElement == nullptr)
		{
			return FVector4(1.0f, 0.0f, 0.0f, 1.0f);
		}

		const int32 DirectIndex = GetLayerElementIndex(
			TangentElement->GetMappingMode(),
			TangentElement->GetReferenceMode(),
			TangentElement->GetIndexArray(),
			ControlPointIndex,
			PolygonVertexIndex);

		if (!IsValidDirectIndex(DirectIndex, TangentElement->GetDirectArray().GetCount()))
		{
			return FVector4(1.0f, 0.0f, 0.0f, 1.0f);
		}

		FbxVector4 Tangent = TangentElement->GetDirectArray().GetAt(DirectIndex);
		Tangent.Normalize();
		return FVector4(
			static_cast<float>(Tangent[0]),
			static_cast<float>(Tangent[1]),
			static_cast<float>(Tangent[2]),
			static_cast<float>(Tangent[3]));
	}

	FVector2 ReadFbxUV(FbxGeometryElementUV* UVElement, int32 ControlPointIndex, int32 PolygonVertexIndex)
	{
		if (UVElement == nullptr)
		{
			return FVector2::ZeroVector;
		}

		const int32 DirectIndex = GetLayerElementIndex(
			UVElement->GetMappingMode(),
			UVElement->GetReferenceMode(),
			UVElement->GetIndexArray(),
			ControlPointIndex,
			PolygonVertexIndex);

		if (!IsValidDirectIndex(DirectIndex, UVElement->GetDirectArray().GetCount()))
		{
			return FVector2::ZeroVector;
		}

		FbxVector2 UV = UVElement->GetDirectArray().GetAt(DirectIndex);
		return FVector2(static_cast<float>(UV[0]), 1.0f - static_cast<float>(UV[1]));
	}

	struct FControlPointInfluences
	{
		TArray<std::pair<int32, float>> Weights;

		void Add(int32 BoneIndex, float Weight)
		{
			Weights.push_back({ BoneIndex, Weight });
		}

		void GetTop4(uint32 OutBoneIndices[MaxBoneInfluences], float OutBoneWeights[MaxBoneInfluences])
		{
			std::sort(Weights.begin(), Weights.end(), [](const auto& A, const auto& B) {
				return A.second > B.second;
				});

			const int32 Count = std::min(MaxBoneInfluences, static_cast<int32>(Weights.size()));

			float TotalWeight = 0.0f;
			for (int32 Index = 0; Index < Count; ++Index)
			{
				TotalWeight += Weights[Index].second;
			}

			for (int32 Index = 0; Index < MaxBoneInfluences; ++Index)
			{
				if (Index < Count && TotalWeight > 0.0001f)
				{
					OutBoneIndices[Index] = static_cast<uint32>(Weights[Index].first);
					OutBoneWeights[Index] = Weights[Index].second / TotalWeight;
				}
				else
				{
					OutBoneIndices[Index] = 0;
					OutBoneWeights[Index] = 0.0f;
				}
			}
		}
	};

	struct FVertexDedupKey
	{
		int32 Position[Vector3ComponentCount] = {};
		int32 Normal[Vector3ComponentCount] = {};
		int32 UV[Vector2ComponentCount] = {};
		int32 Tangent[Vector4ComponentCount] = {};
		int32 Color[Vector4ComponentCount] = {};
		uint32 BoneIndices[MaxBoneInfluences] = {};
		int32 BoneWeights[MaxBoneInfluences] = {};

		explicit FVertexDedupKey(const FSkeletalMeshVertex& Vertex)
		{
			Position[0] = QuantizeVertexFloat(Vertex.Position.X);
			Position[1] = QuantizeVertexFloat(Vertex.Position.Y);
			Position[2] = QuantizeVertexFloat(Vertex.Position.Z);

			Normal[0] = QuantizeVertexFloat(Vertex.Normal.X);
			Normal[1] = QuantizeVertexFloat(Vertex.Normal.Y);
			Normal[2] = QuantizeVertexFloat(Vertex.Normal.Z);

			UV[0] = QuantizeVertexFloat(Vertex.UV.X);
			UV[1] = QuantizeVertexFloat(Vertex.UV.Y);

			Tangent[0] = QuantizeVertexFloat(Vertex.Tangent.X);
			Tangent[1] = QuantizeVertexFloat(Vertex.Tangent.Y);
			Tangent[2] = QuantizeVertexFloat(Vertex.Tangent.Z);
			Tangent[3] = QuantizeVertexFloat(Vertex.Tangent.W);

			Color[0] = QuantizeVertexFloat(Vertex.Color.X);
			Color[1] = QuantizeVertexFloat(Vertex.Color.Y);
			Color[2] = QuantizeVertexFloat(Vertex.Color.Z);
			Color[3] = QuantizeVertexFloat(Vertex.Color.W);

			for (int32 Index = 0; Index < MaxBoneInfluences; ++Index)
			{
				BoneIndices[Index] = Vertex.BoneIndices[Index];
				BoneWeights[Index] = QuantizeVertexFloat(Vertex.BoneWeights[Index]);
			}
		}

		bool operator==(const FVertexDedupKey& Other) const
		{
			for (int32 Index = 0; Index < Vector3ComponentCount; ++Index)
			{
				if (Position[Index] != Other.Position[Index] || Normal[Index] != Other.Normal[Index])
				{
					return false;
				}
			}

			for (int32 Index = 0; Index < Vector2ComponentCount; ++Index)
			{
				if (UV[Index] != Other.UV[Index])
				{
					return false;
				}
			}

			for (int32 Index = 0; Index < Vector4ComponentCount; ++Index)
			{
				if (Tangent[Index] != Other.Tangent[Index] || Color[Index] != Other.Color[Index])
				{
					return false;
				}
			}

			for (int32 Index = 0; Index < MaxBoneInfluences; ++Index)
			{
				if (BoneIndices[Index] != Other.BoneIndices[Index] ||
					BoneWeights[Index] != Other.BoneWeights[Index])
				{
					return false;
				}
			}

			return true;
		}
	};

	struct FVertexDedupKeyHash
	{
		size_t operator()(const FVertexDedupKey& Key) const
		{
			size_t Seed = 0;
			for (int32 Index = 0; Index < Vector3ComponentCount; ++Index)
			{
				HashCombine(Seed, std::hash<int32>{}(Key.Position[Index]));
				HashCombine(Seed, std::hash<int32>{}(Key.Normal[Index]));
			}

			for (int32 Index = 0; Index < Vector2ComponentCount; ++Index)
			{
				HashCombine(Seed, std::hash<int32>{}(Key.UV[Index]));
			}

			for (int32 Index = 0; Index < Vector4ComponentCount; ++Index)
			{
				HashCombine(Seed, std::hash<int32>{}(Key.Tangent[Index]));
				HashCombine(Seed, std::hash<int32>{}(Key.Color[Index]));
			}

			for (int32 Index = 0; Index < MaxBoneInfluences; ++Index)
			{
				HashCombine(Seed, std::hash<uint32>{}(Key.BoneIndices[Index]));
				HashCombine(Seed, std::hash<int32>{}(Key.BoneWeights[Index]));
			}

			return Seed;
		}
	};

	struct FUniqueVertexMap
	{
		int32 FindOrAdd(const FVertexDedupKey& Key, const FSkeletalMeshVertex& Vertex, FSkeletalMesh* OutMesh)
		{
			auto FoundVertex = VertexToIndex.find(Key);
			if (FoundVertex != VertexToIndex.end())
			{
				return FoundVertex->second;
			}

			const int32 VertexIndex = static_cast<int32>(OutMesh->Vertices.size());
			OutMesh->Vertices.push_back(Vertex);
			VertexToIndex.emplace(Key, VertexIndex);
			return VertexIndex;
		}

	private:
		TMap<FVertexDedupKey, int32, FVertexDedupKeyHash> VertexToIndex;
	};
}

bool FbxParser::TextureFileExists(const FString& TexturePath)
{
	if (TexturePath.empty())
	{
		return false;
	}

	return std::filesystem::exists(std::filesystem::path(FPaths::ToAbsolute(FPaths::ToWide(TexturePath))));
}

FString FbxParser::FindExistingTexturePath(const TArray<FString>& Candidates)
{
	for (const FString& Candidate : Candidates)
	{
		if (TextureFileExists(Candidate))
		{
			return Candidate;
		}
	}

	return {};
}

FString FbxParser::ResolveFbxTexturePath(const FString& FbxFilePath, const char* TextureFilePath)
{
	if (TextureFilePath == nullptr || TextureFilePath[0] == '\0')
	{
		return {};
	}

	std::filesystem::path TexturePath(FPaths::ToWide(TextureFilePath));
	if (TexturePath.is_absolute())
	{
		return FPaths::ToUtf8(TexturePath.lexically_normal().generic_wstring());
	}

	const std::filesystem::path FbxDir = std::filesystem::path(FPaths::ToWide(FbxFilePath)).parent_path();
	const FString FbxRelativePath = FPaths::ToUtf8((FbxDir / TexturePath).lexically_normal().generic_wstring());
	if (TextureFileExists(FbxRelativePath))
	{
		return FbxRelativePath;
	}

	const FString FileName = FPaths::ToUtf8(TexturePath.filename().generic_wstring());
	const FString AssetTexturePath = FindExistingTexturePath({
		"Asset/Texture/" + FileName,
		"Asset/Fbx/Textures/" + FileName
		});

	return AssetTexturePath.empty() ? FbxRelativePath : AssetTexturePath;
}

FString FbxParser::GetTexturePathFromProperty(const FString& FbxFilePath, FbxSurfaceMaterial* Material, const char* PropertyName)
{
	if (Material == nullptr)
	{
		return {};
	}

	FbxProperty TextureProperty = Material->FindProperty(PropertyName);
	if (!TextureProperty.IsValid())
	{
		return {};
	}

	const int32 TextureCount = TextureProperty.GetSrcObjectCount<FbxFileTexture>();
	for (int32 TextureIndex = 0; TextureIndex < TextureCount; ++TextureIndex)
	{
		FbxFileTexture* Texture = TextureProperty.GetSrcObject<FbxFileTexture>(TextureIndex);
		if (Texture == nullptr)
		{
			continue;
		}

		const char* RelativeFileName = Texture->GetRelativeFileName();
		if (RelativeFileName != nullptr && RelativeFileName[0] != '\0')
		{
			return ResolveFbxTexturePath(FbxFilePath, RelativeFileName);
		}

		const char* FileName = Texture->GetFileName();
		if (FileName != nullptr && FileName[0] != '\0')
		{
			return ResolveFbxTexturePath(FbxFilePath, FileName);
		}
	}

	return {};
}

FString FbxParser::BuildTextureStemFromMaterialName(const FString& MaterialName, const char* TextureSuffix)
{
	FString BaseName = MaterialName;
	if (BaseName.starts_with("MI_"))
	{
		BaseName = BaseName.substr(3);
	}
	else if (BaseName.starts_with("M_"))
	{
		BaseName = BaseName.substr(2);
	}

	if (!BaseName.starts_with("T_"))
	{
		BaseName = "T_" + BaseName;
	}

	return BaseName + "_" + TextureSuffix;
}

FString FbxParser::FindAssetTextureByMaterialName(const FString& MaterialName, const char* TextureSuffix)
{
	const FString Stem = BuildTextureStemFromMaterialName(MaterialName, TextureSuffix);
	const TArray<FString> Extensions = { ".PNG", ".png", ".DDS", ".dds", ".JPG", ".jpg", ".JPEG", ".jpeg" };

	TArray<FString> Candidates;
	for (const FString& Extension : Extensions)
	{
		Candidates.push_back("Asset/Texture/" + Stem + Extension);
		Candidates.push_back("Asset/Fbx/Textures/" + Stem + Extension);
	}

	return FindExistingTexturePath(Candidates);
}


FString FbxParser::GetDiffuseTexturePath(const FString& FbxFilePath, FbxSurfaceMaterial* Material)
{
	FString TexturePath = GetTexturePathFromProperty(FbxFilePath, Material, FbxSurfaceMaterial::sDiffuse);
	if (!TexturePath.empty())
	{
		return TexturePath;
	}

	return Material ? FindAssetTextureByMaterialName(Material->GetName(), "D") : FString();
}

FString FbxParser::GetNormalTexturePath(const FString& FbxFilePath, FbxSurfaceMaterial* Material)
{
	FString TexturePath = GetTexturePathFromProperty(FbxFilePath, Material, FbxSurfaceMaterial::sNormalMap);
	if (!TexturePath.empty())
	{
		return TexturePath;
	}

	TexturePath = GetTexturePathFromProperty(FbxFilePath, Material, FbxSurfaceMaterial::sBump);
	if (!TexturePath.empty())
	{
		return TexturePath;
	}

	return Material ? FindAssetTextureByMaterialName(Material->GetName(), "N") : FString();
}

UMaterialInterface* FbxParser::GetOrCreateFbxMaterial(const FString& FbxFilePath, FbxSurfaceMaterial* FbxMaterial)
{
	FResourceManager& ResourceManager = FResourceManager::Get();
	if (FbxMaterial == nullptr)
	{
		return ResourceManager.GetMaterial("DefaultWhite");
	}

	const FString MaterialName = FbxMaterial->GetName();
	if (UMaterial* ExistingMaterial = ResourceManager.GetMaterial(MaterialName))
	{
		return ExistingMaterial;
	}

	const FString DiffuseTexturePath = GetDiffuseTexturePath(FbxFilePath, FbxMaterial);
	const FString NormalTexturePath = GetNormalTexturePath(FbxFilePath, FbxMaterial);
	if (DiffuseTexturePath.empty() && NormalTexturePath.empty())
	{
		return ResourceManager.GetMaterial("DefaultWhite");
	}

	UTexture* DiffuseTexture = DiffuseTexturePath.empty() ? ResourceManager.GetTexture("DefaultWhite") : ResourceManager.LoadTexture(DiffuseTexturePath);
	if (DiffuseTexture == nullptr && !DiffuseTexturePath.empty())
	{
		UE_LOG("Fbx Parser diffuse texture 로드 실패: %s", DiffuseTexturePath.c_str());
	}

	UTexture* NormalTexture = NormalTexturePath.empty() ? ResourceManager.GetTexture("DefaultNormal") : ResourceManager.LoadTexture(NormalTexturePath);
	if (NormalTexture == nullptr && !NormalTexturePath.empty())
	{
		UE_LOG("Fbx Parser normal texture 로드 실패: %s", NormalTexturePath.c_str());
	}

	if (DiffuseTexture == nullptr && NormalTexture == nullptr)
	{
		return ResourceManager.GetMaterial("DefaultWhite");
	}

	UMaterial* Material = ResourceManager.GetOrCreateMaterial(MaterialName, "Shaders/UberLit.hlsl");
	Material->MaterialData.Name = MaterialName;
	Material->MaterialData.DiffuseTexPath = DiffuseTexturePath;
	Material->MaterialData.bHasDiffuseTexture = DiffuseTexture != nullptr && !DiffuseTexturePath.empty();
	Material->MaterialData.NormalTexPath = NormalTexturePath;
	Material->MaterialData.bHasNormalTexture = NormalTexture != nullptr && !NormalTexturePath.empty();

	Material->MaterialParams["BaseColor"] = FMaterialParamValue(Material->MaterialData.BaseColor);
	Material->MaterialParams["SpecularColor"] = FMaterialParamValue(Material->MaterialData.SpecularColor);
	Material->MaterialParams["EmissiveColor"] = FMaterialParamValue(Material->MaterialData.EmissiveColor);
	Material->MaterialParams["Shininess"] = FMaterialParamValue(Material->MaterialData.Shininess);
	Material->MaterialParams["Opacity"] = FMaterialParamValue(Material->MaterialData.Opacity);
	Material->MaterialParams["DiffuseMap"] = FMaterialParamValue(DiffuseTexture ? DiffuseTexture : ResourceManager.GetTexture("DefaultWhite"));
	Material->MaterialParams["bHasDiffuseMap"] = FMaterialParamValue(Material->MaterialData.bHasDiffuseTexture);

	if (UTexture* DefaultWhite = ResourceManager.GetTexture("DefaultWhite"))
	{
		Material->MaterialParams["SpecularMap"] = FMaterialParamValue(DefaultWhite);
		Material->MaterialParams["BumpMap"] = FMaterialParamValue(DefaultWhite);
	}

	Material->MaterialParams["NormalMap"] = FMaterialParamValue(NormalTexture ? NormalTexture : ResourceManager.GetTexture("DefaultNormal"));

	Material->MaterialParams["bHasSpecularMap"] = FMaterialParamValue(false);
	Material->MaterialParams["bHasNormalMap"] = FMaterialParamValue(Material->MaterialData.bHasNormalTexture);
	Material->MaterialParams["bHasBumpMap"] = FMaterialParamValue(false);
	Material->MaterialParams["ScrollUV"] = FMaterialParamValue(FVector2(0.0f, 0.0f));

	return Material;
}

FSkeletalMesh* FbxParser::ParseFbx(const std::string& FilePath)
{
	FSkeletalMesh* OutMesh = new FSkeletalMesh();
	OutMesh->PathFileName = FilePath.c_str();

	UE_LOG("Fbx Parser 파싱 시작: %s", FilePath.c_str());

	FbxManager* SdkManager = FbxManager::Create();
	FbxIOSettings* ios = FbxIOSettings::Create(SdkManager, IOSROOT);
	SdkManager->SetIOSettings(ios);

	FbxImporter* Importer = FbxImporter::Create(SdkManager, "");
	if (!Importer->Initialize(FilePath.c_str(), -1, SdkManager->GetIOSettings()))
	{
		UE_LOG("Fbx Parser 로드 실패 사유: %s", Importer->GetStatus().GetErrorString());
		SdkManager->Destroy();
		return OutMesh;
	}

	FbxScene* Scene = FbxScene::Create(SdkManager, "myScene");
	Importer->Import(Scene);
	Importer->Destroy();

	// 다각형 삼각형화
	FbxGeometryConverter GeometryConverter(SdkManager);
	GeometryConverter.Triangulate(Scene, true);

	TMap<std::string, int32> BoneMap;

	ProcessNode(Scene->GetRootNode(), OutMesh, BoneMap);
	BuildBoneHierarchy(Scene, OutMesh, BoneMap);

	SdkManager->Destroy();

	OutMesh->NormalizeVertexWeights();
	OutMesh->EnsureReferencePoseMatrices();
	OutMesh->CacheBounds();

	UE_LOG("Fbx Parser ========= [Fbx SDK 파싱 결과] =========");
	UE_LOG("Fbx Parser 정점 총합: %zu", OutMesh->Vertices.size());
	UE_LOG("Fbx Parser 인덱스 총합: %zu", OutMesh->Indices.size());
	UE_LOG("Fbx Parser 섹션 개수: %zu", OutMesh->Sections.size());
	UE_LOG("Fbx Parser 고유 뼈대 개수: %zu", OutMesh->Bones.size());
	UE_LOG("Fbx Parser =======================================");

	return OutMesh;
}

void FbxParser::ProcessNode(FbxNode* Node, FSkeletalMesh* OutMesh, TMap<std::string, int32>& BoneMap)
{
	if (!Node) return;

	FbxNodeAttribute* Attribute = Node->GetNodeAttribute();
	if (Attribute && Attribute->GetAttributeType() == FbxNodeAttribute::eMesh)
	{
		FbxMesh* Mesh = Node->GetMesh();
		if (Mesh)
		{
			ProcessMesh(Node, Mesh, OutMesh, BoneMap);
		}
	}

	for (int32 i = 0; i < Node->GetChildCount(); i++)
	{
		ProcessNode(Node->GetChild(i), OutMesh, BoneMap);
	}
}

void FbxParser::ProcessMesh(FbxNode* Node, FbxMesh* Mesh, FSkeletalMesh* OutMesh, TMap<std::string, int32>& BoneMap)
{
	Mesh->GenerateTangentsDataForAllUVSets();

	int32 ControlPointCount = Mesh->GetControlPointsCount();
	TArray<FControlPointInfluences> ControlPointInfluences(ControlPointCount);

	int32 SkinCount = Mesh->GetDeformerCount(FbxDeformer::eSkin);
	if (SkinCount > 0)
	{
		FbxSkin* Skin = static_cast<FbxSkin*>(Mesh->GetDeformer(0, FbxDeformer::eSkin));
		int32 ClusterCount = Skin->GetClusterCount();

		for (int32 c = 0; c < ClusterCount; c++)
		{
			FbxCluster* Cluster = Skin->GetCluster(c);
			FbxNode* BoneNode = Cluster->GetLink();
			if (!BoneNode) continue;

			std::string BoneName = BoneNode->GetName();
			int32 BoneIndex = -1;

			if (BoneMap.find(BoneName) == BoneMap.end())
			{
				BoneIndex = static_cast<int32>(OutMesh->Bones.size());
				BoneMap[BoneName] = BoneIndex;

				FSkeletalBone NewBone;
				NewBone.Name = BoneName;
				NewBone.ParentIndex = -1;

				FbxAMatrix TransformMatrix, TransformLinkMatrix;
				Cluster->GetTransformMatrix(TransformMatrix);
				Cluster->GetTransformLinkMatrix(TransformLinkMatrix);

				FMatrix RefGlobalMatrix = ToMatrix(TransformLinkMatrix * TransformMatrix.Inverse());
				NewBone.RefGlobalMatrix = RefGlobalMatrix;
				NewBone.InverseBindPose = RefGlobalMatrix.GetInverse();

				OutMesh->Bones.push_back(NewBone);
			}
			else
			{
				BoneIndex = BoneMap[BoneName];
			}

			int32 IndexCount = Cluster->GetControlPointIndicesCount();
			int32* Indices = Cluster->GetControlPointIndices();
			double* Weights = Cluster->GetControlPointWeights();

			for (int32 i = 0; i < IndexCount; i++)
			{
				if (Indices[i] >= 0 && Indices[i] < ControlPointCount)
				{
					ControlPointInfluences[Indices[i]].Add(BoneIndex, static_cast<float>(Weights[i]));
				}
			}
		}
	}

	FbxVector4* ControlPoints = Mesh->GetControlPoints();
	FbxGeometryElementNormal* NormalElement = Mesh->GetElementNormal(0);
	FbxGeometryElementTangent* TangentElement = Mesh->GetElementTangent(0);
	FbxGeometryElementUV* UVElement = Mesh->GetElementUV(0);
	FbxGeometryElementMaterial* MaterialElement = Mesh->GetElementMaterial(0);

	int32 PolygonCount = Mesh->GetPolygonCount();
	TMap<int32, TArray<int32>> MaterialToIndices;
	FUniqueVertexMap UniqueVertexMap;
	int32 VertexCounter = 0;

	for (int32 PolygonIndex = 0; PolygonIndex < PolygonCount; PolygonIndex++)
	{
		int32 PolyMaterialIndex = GetMaterialIndex(MaterialElement, PolygonIndex);

		for (int32 CornerIndex = 0; CornerIndex < TriangleVertexCount; CornerIndex++)
		{
			int32 ControlPointIndex = Mesh->GetPolygonVertex(PolygonIndex, CornerIndex);
			if (ControlPointIndex < 0 || ControlPointIndex >= ControlPointCount)
			{
				continue;
			}

			FSkeletalMeshVertex NewVert;
			NewVert.Position = ReadFbxPosition(ControlPoints, ControlPointIndex);
			NewVert.Normal = ReadFbxNormal(NormalElement, ControlPointIndex, VertexCounter);
			NewVert.Tangent = ReadFbxTangent(TangentElement, ControlPointIndex, VertexCounter);
			NewVert.UV = ReadFbxUV(UVElement, ControlPointIndex, VertexCounter);
			NewVert.Color = FVector4(1, 1, 1, 1);

			ControlPointInfluences[ControlPointIndex].GetTop4(NewVert.BoneIndices, NewVert.BoneWeights);

			const FVertexDedupKey VertexKey(NewVert);
			const int32 VertexIndex = UniqueVertexMap.FindOrAdd(VertexKey, NewVert, OutMesh);
			MaterialToIndices[PolyMaterialIndex].push_back(VertexIndex);
			VertexCounter++;
		}
	}

	for (const auto& MatPair : MaterialToIndices)
	{
		int32 MatIndex = MatPair.first;
		const TArray<int32>& Indices = MatPair.second;

		FString MaterialSlotName = "DefaultWhite";
		FbxSurfaceMaterial* Material = nullptr;
		if (Node->GetMaterialCount() > MatIndex)
		{
			Material = Node->GetMaterial(MatIndex);
			if (Material)
			{
				MaterialSlotName = Material->GetName();
			}
		}

		int32 MaterialSlotIndex = -1;
		for (int32 SlotIndex = 0; SlotIndex < static_cast<int32>(OutMesh->Slots.size()); ++SlotIndex)
		{
			if (OutMesh->Slots[SlotIndex].SlotName == MaterialSlotName)
			{
				MaterialSlotIndex = SlotIndex;
				break;
			}
		}

		if (MaterialSlotIndex < 0)
		{
			FSkeletalMeshMaterialSlot NewSlot;
			NewSlot.SlotName = MaterialSlotName;
			NewSlot.Material = GetOrCreateFbxMaterial(OutMesh->PathFileName, Material);
			OutMesh->Slots.push_back(NewSlot);
			MaterialSlotIndex = static_cast<int32>(OutMesh->Slots.size()) - 1;
		}

		FSkeletalMeshSection NewSection;
		NewSection.MaterialIndex = MaterialSlotIndex;
		NewSection.FirstIndex = static_cast<int32>(OutMesh->Indices.size());
		NewSection.NumTriangles = static_cast<int32>(Indices.size()) / 3;
		NewSection.MaterialSlotName = MaterialSlotName;

		OutMesh->Indices.insert(OutMesh->Indices.end(), Indices.begin(), Indices.end());
		OutMesh->Sections.push_back(NewSection);
	}
}

void FbxParser::BuildBoneHierarchy(FbxScene* Scene, FSkeletalMesh* OutMesh, const TMap<std::string, int32>& BoneMap)
{
	for (int32 i = 0; i < static_cast<int32>(OutMesh->Bones.size()); i++)
	{
		FbxNode* BoneNode = Scene->FindNodeByName(OutMesh->Bones[i].Name.c_str());
		if (BoneNode && BoneNode->GetParent())
		{
			std::string ParentName = BoneNode->GetParent()->GetName();
			auto it = BoneMap.find(ParentName);
			if (it != BoneMap.end())
			{
				OutMesh->Bones[i].ParentIndex = it->second;
			}
		}
	}

	TArray<FSkeletalBone> SortedBones;
	TMap<int32, int32> OldToNewIndexMap;
	TArray<bool> Processed(OutMesh->Bones.size(), false);

	while (SortedBones.size() < OutMesh->Bones.size())
	{
		bool bProgress = false;
		for (size_t i = 0; i < OutMesh->Bones.size(); ++i)
		{
			if (!Processed[i])
			{
				int32 ParentIdx = OutMesh->Bones[i].ParentIndex;
				if (ParentIdx == -1 || ParentIdx < 0 || ParentIdx >= static_cast<int32>(OutMesh->Bones.size()) || Processed[ParentIdx])
				{
					OldToNewIndexMap[static_cast<int32>(i)] = static_cast<int32>(SortedBones.size());
					FSkeletalBone SortedBone = OutMesh->Bones[i];
					if (ParentIdx < -1 || ParentIdx >= static_cast<int32>(OutMesh->Bones.size()))
					{
						UE_LOG("Fbx Parser 잘못된 부모 인덱스를 루트로 처리: %s", SortedBone.Name.c_str());
						SortedBone.ParentIndex = -1;
					}
					SortedBones.push_back(SortedBone);
					Processed[i] = true;
					bProgress = true;
				}
			}
		}

		if (!bProgress)
		{
			UE_LOG("Fbx Parser 본 계층 정렬 실패: 순환 참조 또는 누락된 부모가 있어 남은 본을 루트로 처리합니다.");
			for (size_t i = 0; i < OutMesh->Bones.size(); ++i)
			{
				if (!Processed[i])
				{
					OldToNewIndexMap[static_cast<int32>(i)] = static_cast<int32>(SortedBones.size());
					FSkeletalBone SortedBone = OutMesh->Bones[i];
					SortedBone.ParentIndex = -1;
					SortedBones.push_back(SortedBone);
					Processed[i] = true;
				}
			}
		}
	}

	for (auto& Bone : SortedBones)
	{
		if (Bone.ParentIndex != -1)
		{
			auto ParentIt = OldToNewIndexMap.find(Bone.ParentIndex);
			if (ParentIt == OldToNewIndexMap.end())
			{
				Bone.ParentIndex = -1;
				Bone.RefLocalTransform = FTransform(Bone.RefGlobalMatrix);
				continue;
			}

			Bone.ParentIndex = ParentIt->second;

			FMatrix ParentGlobalInv = SortedBones[Bone.ParentIndex].RefGlobalMatrix.GetInverse();
			Bone.RefLocalTransform = FTransform(Bone.RefGlobalMatrix * ParentGlobalInv);
		}
		else
		{
			Bone.RefLocalTransform = FTransform(Bone.RefGlobalMatrix);
		}
	}

	OutMesh->Bones = SortedBones;

	for (auto& Vert : OutMesh->Vertices)
	{
		for (int32 w = 0; w < 4; w++)
		{
			if (OldToNewIndexMap.find(Vert.BoneIndices[w]) != OldToNewIndexMap.end())
			{
				Vert.BoneIndices[w] = OldToNewIndexMap[Vert.BoneIndices[w]];
			}
		}
	}
}
