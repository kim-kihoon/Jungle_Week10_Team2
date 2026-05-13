#include "FbxLoader.h"
#include "SkeletalMesh.h"
#include "Core/Logger.h"
#include "Core/Paths.h"
#include "Core/ResourceManager.h"

#include <fbxsdk.h> //FbxManager, FbxScene, FbxIOSettings 등 FBX SDK 관련 클래스 제공.
#include <string>
#include <algorithm>
#include <cmath>
#include <functional>
#include <filesystem>

namespace
{
	// StringUtils에 이런 함수들이 있어야 할 텐데 현재 obj importer 쪽에 네임스페이스로 존재.
	// 따라서 리팩토링하며 해당 유틸들을 나중에 빼놔야 함. 현재는 개인 브랜치이므로 구조에 큰 변경을 주지 않고자 냅둠.
	FString ToLowerAscii(FString Value)
	{
		std::transform(Value.begin(), Value.end(), Value.begin(), [](unsigned char CharValue)
					   { return static_cast<char>(std::tolower(CharValue)); });
		return Value;
	}

	void HashCombine(size_t& Seed, size_t Value)
	{
		Seed ^= Value + 0x9e3779b9u + (Seed << 6) + (Seed >> 2);
	}

	void HashCombineFloat(size_t& Seed, float Value)
	{
		HashCombine(Seed, std::hash<float>{}(Value));
	}

	void HashCombineVector(size_t& Seed, const FVector& Value)
	{
		HashCombineFloat(Seed, Value.X);
		HashCombineFloat(Seed, Value.Y);
		HashCombineFloat(Seed, Value.Z);
	}

	void HashCombineVector2(size_t& Seed, const FVector2& Value)
	{
		HashCombineFloat(Seed, Value.X);
		HashCombineFloat(Seed, Value.Y);
	}

	void HashCombineColor(size_t& Seed, const FColor& Value)
	{
		HashCombineFloat(Seed, Value.R);
		HashCombineFloat(Seed, Value.G);
		HashCombineFloat(Seed, Value.B);
		HashCombineFloat(Seed, Value.A);
	}

	struct FSkeletalMeshVertexHasher
	{
		size_t operator()(const FSkeletalMeshVertex& Vertex) const
		{
			// Tangent/bitangent는 dedupe 이후 index buffer 기준으로 재계산되는 파생 데이터다.
			size_t Seed = 0;
			HashCombineVector(Seed, Vertex.Position);
			HashCombineColor(Seed, Vertex.Color);
			HashCombineVector(Seed, Vertex.Normal);
			HashCombineVector2(Seed, Vertex.UVs);

			for (int32 InfluenceIndex = 0; InfluenceIndex < MAX_SKELETAL_BONE_INFLUENCES; ++InfluenceIndex)
			{
				HashCombine(Seed, std::hash<int32>{}(Vertex.BoneIndices[InfluenceIndex]));
				HashCombineFloat(Seed, Vertex.BoneWeights[InfluenceIndex]);
			}

			return Seed;
		}
	};

	struct FSkeletalMeshVertexEqual
	{
		bool operator()(const FSkeletalMeshVertex& Left, const FSkeletalMeshVertex& Right) const
		{
			// Epsilon 병합은 normal, UV seam, bone weight 차이를 합칠 수 있으므로 사용하지 않는다.
			if (Left.Position != Right.Position ||
				Left.Color.R != Right.Color.R ||
				Left.Color.G != Right.Color.G ||
				Left.Color.B != Right.Color.B ||
				Left.Color.A != Right.Color.A ||
				Left.Normal != Right.Normal ||
				Left.UVs != Right.UVs)
			{
				return false;
			}

			for (int32 InfluenceIndex = 0; InfluenceIndex < MAX_SKELETAL_BONE_INFLUENCES; ++InfluenceIndex)
			{
				if (Left.BoneIndices[InfluenceIndex] != Right.BoneIndices[InfluenceIndex] ||
					Left.BoneWeights[InfluenceIndex] != Right.BoneWeights[InfluenceIndex])
				{
					return false;
				}
			}

			return true;
		}
	};

	struct FFbxLoadContext
	{
		FString SourcePath;

		FbxManager* Manager = nullptr;
		FbxIOSettings* IOSettings = nullptr;
		FbxScene* Scene = nullptr;

		//아래 두 항목은 Collect Skeleton 에서 채워짐
		FSkeletalMesh LoadedMesh;
		TMap<FbxNode*, int32> BoneIndexMap;

		uint32 RawVertexCount = 0;
		TMap<FSkeletalMeshVertex, uint32, FSkeletalMeshVertexHasher, FSkeletalMeshVertexEqual> UniqueVertexIndices;
	};

	uint32 AddUniqueSkeletalMeshVertex(FFbxLoadContext& Context, const FSkeletalMeshVertex& Vertex)
	{
		auto ExistingIt = Context.UniqueVertexIndices.find(Vertex);
		if (ExistingIt != Context.UniqueVertexIndices.end())
		{
			return ExistingIt->second;
		}

		const uint32 NewVertexIndex = static_cast<uint32>(Context.LoadedMesh.Vertices.size());
		Context.LoadedMesh.Vertices.push_back(Vertex);
		Context.UniqueVertexIndices.emplace(Vertex, NewVertexIndex);
		return NewVertexIndex;
	}

	float CalculateReductionPercent(uint32 RawVertexCount, uint32 UniqueVertexCount)
	{
		if (RawVertexCount == 0)
		{
			return 0.0f;
		}

		const uint32 RemovedVertexCount = RawVertexCount > UniqueVertexCount ? RawVertexCount - UniqueVertexCount : 0;
		return (static_cast<float>(RemovedVertexCount) / static_cast<float>(RawVertexCount)) * 100.0f;
	}

	bool CreateFbxSdkContext(FFbxLoadContext& Context)
	{
		/* FbxManager : FBX SDK 전체의 루트 객체. */
		Context.Manager = FbxManager::Create();
		if (Context.Manager == nullptr)
		{
			return false;
		}

		/* FbxIOSettings : FBX import/export 설정을 담는 객체. */
		Context.IOSettings = FbxIOSettings::Create(Context.Manager, IOSROOT);
		if (Context.IOSettings == nullptr)
		{
			Context.Manager->Destroy();
			Context.Manager = nullptr;
			return false;
		}

		/* 만든 IOSettings를 Manager에 등록. 관련해 사용 ex) Manager->GetIOSettings() */
		Context.Manager->SetIOSettings(Context.IOSettings);

		/* FbxScene은 import된 FBX 데이터가 들어갈 컨테이너. 들어가는 애들 ex) FbxNode, FbxMesh, FbxSkeleton, FbxMaterial, FbxSkin, FbxCluster */
		Context.Scene = FbxScene::Create(Context.Manager, "FbxImportScene");

		if (Context.Scene == nullptr)
		{
			Context.Manager->Destroy();
			Context.Manager = nullptr;
			Context.IOSettings = nullptr;
			Context.Scene = nullptr;
			return false;
		}
		return true;
	}

	void DestroyFbxSdkContext(FFbxLoadContext& Context)
	{
		/* FbxManager를 Destroy하면, 해당 Manager를 통해 생성된 FBX SDK 객체들도 같이 정리된다. */
		if (Context.Manager != nullptr)
		{
			Context.Manager->Destroy();
		}

		Context.Manager = nullptr;
		Context.IOSettings = nullptr;
		Context.Scene = nullptr;

		Context.BoneIndexMap.clear();
		Context.UniqueVertexIndices.clear();
		Context.RawVertexCount = 0;
	}

	/**
	 * FBX 파일을 FbxScene 객체에 로드.
	 */
	bool ImportFbxScene(FFbxLoadContext& Context, const FString& Path)
	{
		if (Context.Manager == nullptr || Context.Scene == nullptr)
		{
			return false;
		}

		FbxImporter* importer = FbxImporter::Create(Context.Manager, "TempImporter");
		if (importer == nullptr)
		{
			return false;
		}

		const FString FilePath = Path;
		const bool initializeResult = importer->Initialize(FilePath.c_str(), -1, Context.Manager->GetIOSettings());
		if (!initializeResult)
		{
			const char* ErrorString = importer->GetStatus().GetErrorString();
			UE_LOG("ErrorString is %s", ErrorString ? ErrorString : "Unknown FBX error");
			importer->Destroy();
			return false;
		}

		const bool bImportSuccess = importer->Import(Context.Scene);

		if (!bImportSuccess)
		{
			const char* ErrorString = importer->GetStatus().GetErrorString();
			UE_LOG("FBX import failed for '%s': %s", Path.c_str(), ErrorString ? ErrorString : "Unknown FBX error");

			importer->Destroy();
			return false;
		}
		importer->Destroy();

		return true;
	}

	void ConvertSceneToEngineSpace(FFbxLoadContext& Context)
	{
		if (Context.Scene == nullptr)
		{
			return;
		}

		/*
			Axis System
			엔진 좌표계는 left-handed, Z-up이며 축은 Forward=+X, Right=+Y, Up=+Z.
			FBX SDK 문자열 표현은 right/up/forward 순서이므로 "yzx"가 엔진 축과 일치한다.
		*/
		FbxAxisSystem EngineAxisSystem;
		if (!FbxAxisSystem::ParseAxisSystem("yzx", EngineAxisSystem))
		{
			UE_LOG("FBX engine axis system parse failed. Path: %s", Context.SourcePath.c_str());
			return;
		}

		const FbxAxisSystem CurrentAxisSystem =
			Context.Scene->GetGlobalSettings().GetAxisSystem();

		if (CurrentAxisSystem != EngineAxisSystem)
		{
			// ConvertScene() cannot fully represent handedness changes. DeepConvertScene()
			// converts transforms, vertices and animation curves for RH -> LH assets.
			EngineAxisSystem.DeepConvertScene(Context.Scene);
		}

		/*
			Unit System
		*/
		const FbxSystemUnit EngineUnitSystem = FbxSystemUnit::m; // meter 단위

		const FbxSystemUnit CurrentUnitSystem =
			Context.Scene->GetGlobalSettings().GetSystemUnit();

		if (CurrentUnitSystem != EngineUnitSystem)
		{
			EngineUnitSystem.ConvertScene(Context.Scene);
		}
	}

	bool TriangulateFbxScene(FFbxLoadContext& Context)
	{
		// 여기서까지 오류 검사? Context에 문제가 있었으면 진작에 위에서 걸렸겠지요

		FbxGeometryConverter GeometryConverter(Context.Manager);
		bool bConvertResult = GeometryConverter.Triangulate(Context.Scene, true); //true: 변환한 결과로 기존 geometry를 교체
		if (!bConvertResult)
		{
			UE_LOG("FBX triangulation failed. Path: %s", Context.SourcePath.c_str());
			return false;
		}

		//UE_LOG("FBX triangulation completed. Path: %s", Context.SourcePath.c_str());
		return true;
	}

	struct FBoneInfluence
	{
		int32 BoneIndex = -1;
		float Weight = 0.0f;
	};

	struct FControlPointInfluenceList
	{
		TArray<FBoneInfluence> Influences;
	};

	FString GetFbxObjectName(const FbxObject* Object, const char* FallbackName)
	{
		if (Object == nullptr || Object->GetName() == nullptr || Object->GetName()[0] == '\0')
		{
			return FString(FallbackName ? FallbackName : "Unnamed");
		}

		return FString(Object->GetName());
	}

	bool TextureFileExists(const FString& TexturePath)
	{
		if (TexturePath.empty())
		{
			return false;
		}

		return std::filesystem::exists(std::filesystem::path(FPaths::ToAbsolute(FPaths::ToWide(TexturePath))));
	}

	FString FindExistingTexturePath(const TArray<FString>& Candidates)
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

	FString ResolveFbxTexturePath(const FString& FbxFilePath, const char* TextureFilePath)
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
			"Asset/Fbx/Textures/" + FileName,
		});

		return AssetTexturePath.empty() ? FbxRelativePath : AssetTexturePath;
	}

	FString GetTexturePathFromProperty(const FString& FbxFilePath, FbxSurfaceMaterial* Material, const char* PropertyName)
	{
		if (Material == nullptr || PropertyName == nullptr)
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

	FString BuildTextureStemFromMaterialName(const FString& MaterialName, const char* TextureSuffix)
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

		return BaseName + "_" + FString(TextureSuffix ? TextureSuffix : "");
	}

	FString FindAssetTextureByMaterialName(const FString& MaterialName, const char* TextureSuffix)
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

	FString GetDiffuseTexturePath(const FString& FbxFilePath, FbxSurfaceMaterial* Material)
	{
		FString TexturePath = GetTexturePathFromProperty(FbxFilePath, Material, FbxSurfaceMaterial::sDiffuse);
		if (!TexturePath.empty())
		{
			return TexturePath;
		}

		return Material ? FindAssetTextureByMaterialName(Material->GetName(), "D") : FString();
	}

	FString GetNormalTexturePath(const FString& FbxFilePath, FbxSurfaceMaterial* Material)
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

	FString GetSpecularTexturePath(const FString& FbxFilePath, FbxSurfaceMaterial* Material)
	{
		FString TexturePath = GetTexturePathFromProperty(FbxFilePath, Material, FbxSurfaceMaterial::sSpecular);
		if (!TexturePath.empty())
		{
			return TexturePath;
		}

		return Material ? FindAssetTextureByMaterialName(Material->GetName(), "S") : FString();
	}

	void SetDefaultUberLitParams(UMaterial* Material, UTexture* DiffuseTexture, UTexture* NormalTexture, UTexture* SpecularTexture)
	{
		if (Material == nullptr)
		{
			return;
		}

		FResourceManager& ResourceManager = FResourceManager::Get();
		UTexture* DefaultWhite = ResourceManager.GetTexture("DefaultWhite");
		UTexture* DefaultNormal = ResourceManager.GetTexture("DefaultNormal");

		Material->MaterialParams["BaseColor"] = FMaterialParamValue(Material->MaterialData.BaseColor);
		Material->MaterialParams["SpecularColor"] = FMaterialParamValue(Material->MaterialData.SpecularColor);
		Material->MaterialParams["EmissiveColor"] = FMaterialParamValue(Material->MaterialData.EmissiveColor);
		Material->MaterialParams["Shininess"] = FMaterialParamValue(Material->MaterialData.Shininess);
		Material->MaterialParams["Opacity"] = FMaterialParamValue(Material->MaterialData.Opacity);
		Material->MaterialParams["DiffuseMap"] = FMaterialParamValue(DiffuseTexture ? DiffuseTexture : DefaultWhite);
		Material->MaterialParams["SpecularMap"] = FMaterialParamValue(SpecularTexture ? SpecularTexture : DefaultWhite);
		Material->MaterialParams["NormalMap"] = FMaterialParamValue(NormalTexture ? NormalTexture : DefaultNormal);
		Material->MaterialParams["BumpMap"] = FMaterialParamValue(DefaultWhite);
		Material->MaterialParams["bHasDiffuseMap"] = FMaterialParamValue(Material->MaterialData.bHasDiffuseTexture);
		Material->MaterialParams["bHasSpecularMap"] = FMaterialParamValue(Material->MaterialData.bHasSpecularTexture);
		Material->MaterialParams["bHasNormalMap"] = FMaterialParamValue(Material->MaterialData.bHasNormalTexture);
		Material->MaterialParams["bHasBumpMap"] = FMaterialParamValue(false);
		Material->MaterialParams["ScrollUV"] = FMaterialParamValue(FVector2(0.0f, 0.0f));
	}

	UMaterialInterface* GetOrCreateFbxMaterial(const FString& FbxFilePath, FbxSurfaceMaterial* FbxMaterial)
	{
		FResourceManager& ResourceManager = FResourceManager::Get();
		if (FbxMaterial == nullptr)
		{
			return ResourceManager.GetMaterial("DefaultWhite");
		}

		const FString MaterialName = GetFbxObjectName(FbxMaterial, "Material");
		if (UMaterial* ExistingMaterial = ResourceManager.GetMaterial(MaterialName))
		{
			return ExistingMaterial;
		}

		const FString DiffuseTexturePath = GetDiffuseTexturePath(FbxFilePath, FbxMaterial);
		const FString NormalTexturePath = GetNormalTexturePath(FbxFilePath, FbxMaterial);
		const FString SpecularTexturePath = GetSpecularTexturePath(FbxFilePath, FbxMaterial);
		if (DiffuseTexturePath.empty() && NormalTexturePath.empty() && SpecularTexturePath.empty())
		{
			return ResourceManager.GetMaterial("DefaultWhite");
		}

		UTexture* DiffuseTexture = DiffuseTexturePath.empty() ? nullptr : ResourceManager.LoadTexture(DiffuseTexturePath);
		if (DiffuseTexture == nullptr && !DiffuseTexturePath.empty())
		{
			UE_LOG("FBX diffuse texture load failed: %s", DiffuseTexturePath.c_str());
		}

		UTexture* NormalTexture = NormalTexturePath.empty() ? nullptr : ResourceManager.LoadTexture(NormalTexturePath);
		if (NormalTexture == nullptr && !NormalTexturePath.empty())
		{
			UE_LOG("FBX normal texture load failed: %s", NormalTexturePath.c_str());
		}

		UTexture* SpecularTexture = SpecularTexturePath.empty() ? nullptr : ResourceManager.LoadTexture(SpecularTexturePath);
		if (SpecularTexture == nullptr && !SpecularTexturePath.empty())
		{
			UE_LOG("FBX specular texture load failed: %s", SpecularTexturePath.c_str());
		}

		if (DiffuseTexture == nullptr && NormalTexture == nullptr && SpecularTexture == nullptr)
		{
			return ResourceManager.GetMaterial("DefaultWhite");
		}

		UMaterial* Material = ResourceManager.GetOrCreateMaterial(MaterialName, "Shaders/UberLit.hlsl");
		Material->MaterialData.Name = MaterialName;
		Material->MaterialData.DiffuseTexPath = DiffuseTexturePath;
		Material->MaterialData.bHasDiffuseTexture = DiffuseTexture != nullptr && !DiffuseTexturePath.empty();
		Material->MaterialData.NormalTexPath = NormalTexturePath;
		Material->MaterialData.bHasNormalTexture = NormalTexture != nullptr && !NormalTexturePath.empty();
		Material->MaterialData.SpecularTexPath = SpecularTexturePath;
		Material->MaterialData.bHasSpecularTexture = SpecularTexture != nullptr && !SpecularTexturePath.empty();

		SetDefaultUberLitParams(Material, DiffuseTexture, NormalTexture, SpecularTexture);
		return Material;
	}

	FVector ToEngineVector(const FbxVector4& Vector)
	{
		return FVector(
			static_cast<float>(Vector[0]),
			static_cast<float>(Vector[1]),
			static_cast<float>(Vector[2]));
	}

	FVector2 ToEngineVector2(const FbxVector2& Vector)
	{
		return FVector2(
			static_cast<float>(Vector[0]),
			static_cast<float>(Vector[1]));
	}

	FMatrix ToEngineMatrix(const FbxAMatrix& Matrix)
	{
		return FMatrix(
			static_cast<float>(Matrix.Get(0, 0)), static_cast<float>(Matrix.Get(0, 1)), static_cast<float>(Matrix.Get(0, 2)), static_cast<float>(Matrix.Get(0, 3)),
			static_cast<float>(Matrix.Get(1, 0)), static_cast<float>(Matrix.Get(1, 1)), static_cast<float>(Matrix.Get(1, 2)), static_cast<float>(Matrix.Get(1, 3)),
			static_cast<float>(Matrix.Get(2, 0)), static_cast<float>(Matrix.Get(2, 1)), static_cast<float>(Matrix.Get(2, 2)), static_cast<float>(Matrix.Get(2, 3)),
			static_cast<float>(Matrix.Get(3, 0)), static_cast<float>(Matrix.Get(3, 1)), static_cast<float>(Matrix.Get(3, 2)), static_cast<float>(Matrix.Get(3, 3)));
	}

	bool IsSkeletonNode(const FbxNode* Node)
	{
		if (Node == nullptr)
		{
			return false;
		}

		const FbxNodeAttribute* Attribute = Node->GetNodeAttribute();
		return Attribute != nullptr && Attribute->GetAttributeType() == FbxNodeAttribute::eSkeleton;
	}

	void TraverseFbxNodes(FbxNode* Node, const std::function<void(FbxNode*)>& Visitor)
	{
		if (Node == nullptr)
		{
			return;
		}

		Visitor(Node);

		const int32 ChildCount = static_cast<int32>(Node->GetChildCount());
		for (int32 ChildIndex = 0; ChildIndex < ChildCount; ++ChildIndex)
		{
			TraverseFbxNodes(Node->GetChild(ChildIndex), Visitor);
		}
	}

	int32 AddBoneRecursive(FFbxLoadContext& Context, FbxNode* BoneNode)
	{
		if (BoneNode == nullptr)
		{
			return -1;
		}

		// 이미 등록된 bone이면, 해당 bone의 index 반환.
		auto ExistingIt = Context.BoneIndexMap.find(BoneNode);
		if (ExistingIt != Context.BoneIndexMap.end())
		{
			return ExistingIt->second;
		}

		// bone의 부모 노드가 skeleton 노드라면, 부모 노드도 bone으로 등록.
		// 재귀적으로 올라가면서 등록하다가, skeleton이 아닌 노드 만나거나 부모 노드가 없으면 종료.
		// 부모 bone이 항상 자식 bone보다 먼저 들어가게 만드는 구조
		int32 ParentIndex = -1;
		FbxNode* ParentNode = BoneNode->GetParent();
		if (IsSkeletonNode(ParentNode))
		{
			ParentIndex = AddBoneRecursive(Context, ParentNode);
		}

		FSkeletalBone NewBone;
		NewBone.Name = GetFbxObjectName(BoneNode, "Bone");
		NewBone.ParentIndex = ParentIndex;

		const FMatrix BoneGlobalMatrix = ToEngineMatrix(BoneNode->EvaluateGlobalTransform());
		FMatrix ReferenceLocalMatrix = BoneGlobalMatrix;
		if (ParentIndex >= 0 && ParentNode != nullptr)
		{
			const FMatrix ParentGlobalMatrix = ToEngineMatrix(ParentNode->EvaluateGlobalTransform());
			ReferenceLocalMatrix = BoneGlobalMatrix * ParentGlobalMatrix.GetInverse();
		}

		NewBone.ReferenceLocalTransform = FTransform(ReferenceLocalMatrix);

		const int32 NewBoneIndex = static_cast<int32>(Context.LoadedMesh.Bones.size());
		Context.LoadedMesh.Bones.push_back(NewBone);
		Context.BoneIndexMap[BoneNode] = NewBoneIndex;

		return NewBoneIndex;
	}

	void AppendClusterBonesFromMesh(FFbxLoadContext& Context, FbxMesh* Mesh)
	{
		if (Mesh == nullptr)
		{
			return;
		}

		//mesh가 가진 skin deformer, 즉 skin의 개수
		const int32 SkinCount = static_cast<int32>(Mesh->GetDeformerCount(FbxDeformer::eSkin));
		for (int32 SkinIndex = 0; SkinIndex < SkinCount; ++SkinIndex)
		{
			FbxSkin* Skin = static_cast<FbxSkin*>(Mesh->GetDeformer(SkinIndex, FbxDeformer::eSkin));
			if (Skin == nullptr)
			{
				continue;
			}

			//skin이 가진 cluster, 즉 skinning에 관여하는 bone의 개수
			const int32 ClusterCount = static_cast<int32>(Skin->GetClusterCount());
			for (int32 ClusterIndex = 0; ClusterIndex < ClusterCount; ++ClusterIndex)
			{
				FbxCluster* Cluster = Skin->GetCluster(ClusterIndex);
				if (Cluster == nullptr || Cluster->GetLink() == nullptr)
				{
					continue;
				}

				AddBoneRecursive(Context, Cluster->GetLink());
			}
		}
	}

	/**
	 * skin cluster link node 기반으로 bone 목록 생성.
	 */
	void CollectSkeletons(FFbxLoadContext& Context)
	{
		Context.LoadedMesh.Bones.clear();
		Context.BoneIndexMap.clear();

		if (Context.Scene == nullptr)
		{
			return;
		}

		/*
		씬 전체를 돈다
		→ Mesh가 있는 노드를 찾는다
		→ 그 Mesh에 Skin이 있는지 본다
		→ Skin 안의 Cluster들을 본다
		→ 각 Cluster의 Link Node를 bone으로 등록한다
		*/
		FbxNode* RootNode = Context.Scene->GetRootNode();
		TraverseFbxNodes(RootNode, [&Context](FbxNode* Node)
		{
			AppendClusterBonesFromMesh(Context, Node ? Node->GetMesh() : nullptr);
		});

		const int32 BoneCount = static_cast<int32>(Context.LoadedMesh.Bones.size());
		Context.LoadedMesh.InverseBindPoseMatrices.clear();
		Context.LoadedMesh.InverseBindPoseMatrices.resize(BoneCount, FMatrix::Identity);

		TArray<FTransform> ComponentSpaceRefPose;
		ComponentSpaceRefPose.resize(BoneCount, FTransform::Identity);

		for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
		{
			const FSkeletalBone& Bone = Context.LoadedMesh.Bones[BoneIndex];
			if (Bone.ParentIndex >= 0 && Bone.ParentIndex < BoneIndex)
			{
				ComponentSpaceRefPose[BoneIndex] = Bone.ReferenceLocalTransform * ComponentSpaceRefPose[Bone.ParentIndex];
			}
			else
			{
				ComponentSpaceRefPose[BoneIndex] = Bone.ReferenceLocalTransform;
			}
			Context.LoadedMesh.InverseBindPoseMatrices[BoneIndex] = ComponentSpaceRefPose[BoneIndex].ToMatrixWithScale().GetInverse();
		}

	}

	void AddMaterialSlotsForNode(FFbxLoadContext& Context, FbxNode* MeshNode, int32& OutMaterialBaseIndex, int32& OutMaterialCount)
	{
		OutMaterialBaseIndex = static_cast<int32>(Context.LoadedMesh.MaterialSlots.size());
		OutMaterialCount = 0;

		if (MeshNode == nullptr)
		{
			return;
		}

		const int32 NodeMaterialCount = static_cast<int32>(MeshNode->GetMaterialCount());
		if (NodeMaterialCount <= 0)
		{
			FSkeletalMeshMaterialSlot Slot;
			Slot.SlotName = FString("DefaultWhite");
			Slot.Material = FResourceManager::Get().GetMaterial("DefaultWhite");
			Context.LoadedMesh.MaterialSlots.push_back(Slot);
			OutMaterialCount = 1;
			return;
		}

		for (int32 MaterialIndex = 0; MaterialIndex < NodeMaterialCount; ++MaterialIndex)
		{
			FbxSurfaceMaterial* FbxMaterial = MeshNode->GetMaterial(MaterialIndex);

			FSkeletalMeshMaterialSlot Slot;
			Slot.SlotName = GetFbxObjectName(FbxMaterial, "Material");
			Slot.ExtractedDiffusePath = GetDiffuseTexturePath(Context.SourcePath, FbxMaterial);
			Slot.ExtractedNormalPath = GetNormalTexturePath(Context.SourcePath, FbxMaterial);
			Slot.ExtractedSpecularPath = GetSpecularTexturePath(Context.SourcePath, FbxMaterial);
			Slot.Material = GetOrCreateFbxMaterial(Context.SourcePath, FbxMaterial);

			Context.LoadedMesh.MaterialSlots.push_back(Slot);
		}

		OutMaterialCount = NodeMaterialCount;
	}

	int32 GetPolygonMaterialIndex(FbxMesh* Mesh, int32 PolygonIndex, int32 MaterialCount)
	{
		if (Mesh == nullptr || MaterialCount <= 1)
		{
			return 0;
		}

		FbxGeometryElementMaterial* MaterialElement = Mesh->GetElementMaterial();
		if (MaterialElement == nullptr)
		{
			return 0;
		}

		int32 MaterialIndex = 0;
		const FbxGeometryElement::EMappingMode MappingMode = MaterialElement->GetMappingMode();
		const FbxGeometryElement::EReferenceMode ReferenceMode = MaterialElement->GetReferenceMode();

		if (MappingMode == FbxGeometryElement::eByPolygon)
		{
			if (ReferenceMode == FbxGeometryElement::eIndexToDirect)
			{
				MaterialIndex = MaterialElement->GetIndexArray().GetAt(PolygonIndex);
			}
			else if (ReferenceMode == FbxGeometryElement::eDirect)
			{
				MaterialIndex = PolygonIndex;
			}
		}
		else if (MappingMode == FbxGeometryElement::eAllSame && MaterialElement->GetIndexArray().GetCount() > 0)
		{
			MaterialIndex = MaterialElement->GetIndexArray().GetAt(0);
		}

		if (MaterialIndex < 0 || MaterialIndex >= MaterialCount)
		{
			MaterialIndex = 0;
		}

		return MaterialIndex;
	}

	/**
	 * FBX의 Control Point에 어떤 Bone들이 얼마나 영향을 주는지 목록을 만들어줌.
	 */
	void BuildControlPointInfluences(FFbxLoadContext& Context, FbxMesh* Mesh, TArray<FControlPointInfluenceList>& OutControlPointInfluences)
	{
		OutControlPointInfluences.clear();

		if (Mesh == nullptr)
		{
			return;
		}

		const int32 ControlPointCount = static_cast<int32>(Mesh->GetControlPointsCount());
		OutControlPointInfluences.resize(ControlPointCount);

		const int32 SkinCount = static_cast<int32>(Mesh->GetDeformerCount(FbxDeformer::eSkin));
		for (int32 SkinIndex = 0; SkinIndex < SkinCount; ++SkinIndex)
		{
			FbxSkin* Skin = static_cast<FbxSkin*>(Mesh->GetDeformer(SkinIndex, FbxDeformer::eSkin));
			if (Skin == nullptr)
			{
				continue;
			}

			//가져온 skin(skin은 보통 1개임)에서 cluster을 꺼내고
			// 이 bone에 해당하는 control point와 weight를 읽음
			const int32 ClusterCount = static_cast<int32>(Skin->GetClusterCount());
			for (int32 ClusterIndex = 0; ClusterIndex < ClusterCount; ++ClusterIndex)
			{
				FbxCluster* Cluster = Skin->GetCluster(ClusterIndex);
				if (Cluster == nullptr || Cluster->GetLink() == nullptr)
				{
					continue;
				}

				auto BoneIt = Context.BoneIndexMap.find(Cluster->GetLink());
				if (BoneIt == Context.BoneIndexMap.end())
				{
					continue;
				}

				const int32 BoneIndex = BoneIt->second;

				// --- Bind Pose Correction ---
				// Mesh의 Bind 상태 Transform과 Bone의 Bind 상태 Transform을 각각 읽어, Mesh Local -> Bone Local 역행렬 계산
				FbxAMatrix FbxMeshMatrix;
				Cluster->GetTransformMatrix(FbxMeshMatrix);
				FbxAMatrix FbxLinkMatrix;
				Cluster->GetTransformLinkMatrix(FbxLinkMatrix);

				FMatrix MeshBindMatrix = ToEngineMatrix(FbxMeshMatrix);
				FMatrix BoneBindMatrix = ToEngineMatrix(FbxLinkMatrix);

				Context.LoadedMesh.InverseBindPoseMatrices[BoneIndex] = MeshBindMatrix * BoneBindMatrix.GetInverse();
				// ----------------------------

				//control point index, weight를 읽음
				const int* ControlPointIndices = Cluster->GetControlPointIndices();
				const double* ControlPointWeights = Cluster->GetControlPointWeights();

				const int32 InfluenceCount = static_cast<int32>(Cluster->GetControlPointIndicesCount());
				for (int32 InfluenceIndex = 0; InfluenceIndex < InfluenceCount; ++InfluenceIndex)
				{
					const int32 ControlPointIndex = static_cast<int32>(ControlPointIndices[InfluenceIndex]);
					if (ControlPointIndex < 0 || ControlPointIndex >= ControlPointCount)
					{
						continue;
					}

					const float Weight = static_cast<float>(ControlPointWeights[InfluenceIndex]);
					if (Weight <= 0.0f)
					{
						continue;
					}

					FBoneInfluence Influence;
					Influence.BoneIndex = BoneIndex;
					Influence.Weight = Weight;
					OutControlPointInfluences[ControlPointIndex].Influences.push_back(Influence);
				}
			}
		}
	}

	void NormalizeControlPointInfluences(TArray<FControlPointInfluenceList>& Influences, bool bHasSkeleton)
	{
		for (FControlPointInfluenceList& InfluenceList : Influences)
		{
			std::sort(InfluenceList.Influences.begin(), InfluenceList.Influences.end(), [](const FBoneInfluence& Left, const FBoneInfluence& Right)
			{
				return Left.Weight > Right.Weight;
			});

			if (InfluenceList.Influences.size() > MAX_SKELETAL_BONE_INFLUENCES)
			{
				InfluenceList.Influences.resize(MAX_SKELETAL_BONE_INFLUENCES);
			}

			float WeightSum = 0.0f;
			for (const FBoneInfluence& Influence : InfluenceList.Influences)
			{
				WeightSum += Influence.Weight;
			}

			if (WeightSum > 0.0f)
			{
				const float InvWeightSum = 1.0f / WeightSum;
				for (FBoneInfluence& Influence : InfluenceList.Influences)
				{
					Influence.Weight *= InvWeightSum;
				}
				continue;
			}

			InfluenceList.Influences.clear();
			if (bHasSkeleton)
			{
				InfluenceList.Influences.push_back({ 0, 1.0f });
			}
		}
	}

	void AssignPreparedBoneInfluencesToVertex(const TArray<FBoneInfluence>& Influences, bool bHasSkeleton, FSkeletalMeshVertex& OutVertex)
	{
		for (int32 InfluenceIndex = 0; InfluenceIndex < MAX_SKELETAL_BONE_INFLUENCES; ++InfluenceIndex)
		{
			OutVertex.BoneIndices[InfluenceIndex] = -1;
			OutVertex.BoneWeights[InfluenceIndex] = 0.0f;
		}

		const int32 UsedInfluenceCount = static_cast<int32>(std::min<size_t>(Influences.size(), MAX_SKELETAL_BONE_INFLUENCES));
		if (UsedInfluenceCount <= 0)
		{
			if (bHasSkeleton)
			{
				OutVertex.BoneIndices[0] = 0;
				OutVertex.BoneWeights[0] = 1.0f;
			}
			return;
		}

		for (int32 InfluenceIndex = 0; InfluenceIndex < UsedInfluenceCount; ++InfluenceIndex)
		{
			OutVertex.BoneIndices[InfluenceIndex] = Influences[InfluenceIndex].BoneIndex;
			OutVertex.BoneWeights[InfluenceIndex] = Influences[InfluenceIndex].Weight;
		}
	}

	FVector GetPolygonVertexNormal(FbxMesh* Mesh, int32 PolygonIndex, int32 PolygonVertexIndex)
	{
		FbxVector4 FbxNormal(0.0, 0.0, 1.0, 0.0);
		if (Mesh != nullptr)
		{
			Mesh->GetPolygonVertexNormal(PolygonIndex, PolygonVertexIndex, FbxNormal);
		}

		return ToEngineVector(FbxNormal).GetSafeNormal();
	}

	FVector2 GetPolygonVertexUV(FbxMesh* Mesh, int32 PolygonIndex, int32 PolygonVertexIndex, const char* UVSetName)
	{
		if (Mesh == nullptr || UVSetName == nullptr)
		{
			return FVector2(0.0f, 0.0f);
		}

		FbxVector2 FbxUV(0.0, 0.0);
		bool bUnmapped = false;
		const bool bHasUV = Mesh->GetPolygonVertexUV(PolygonIndex, PolygonVertexIndex, UVSetName, FbxUV, bUnmapped);
		if (!bHasUV || bUnmapped)
		{
			return FVector2(0.0f, 0.0f);
		}

		FVector2 UV = ToEngineVector2(FbxUV);
		UV.Y = 1.0f - UV.Y;
		return UV;
	}

	bool CalculateTriangleTangentBasis(
		const FSkeletalMeshVertex& V0,
		const FSkeletalMeshVertex& V1,
		const FSkeletalMeshVertex& V2,
		FVector& OutTangent,
		FVector& OutBitangent)
	{
		const FVector Edge1 = V1.Position - V0.Position;
		const FVector Edge2 = V2.Position - V0.Position;
		const FVector2 DeltaUV1 = V1.UVs - V0.UVs;
		const FVector2 DeltaUV2 = V2.UVs - V0.UVs;

		const float Determinant = DeltaUV1.X * DeltaUV2.Y - DeltaUV1.Y * DeltaUV2.X;
		if (std::fabs(Determinant) <= 1.e-8f)
		{
			OutTangent = FVector::ZeroVector;
			OutBitangent = FVector::ZeroVector;
			return false;
		}

		const float InvDeterminant = 1.0f / Determinant;
		OutTangent = ((Edge1 * DeltaUV2.Y) - (Edge2 * DeltaUV1.Y)) * InvDeterminant;
		OutBitangent = ((Edge2 * DeltaUV1.X) - (Edge1 * DeltaUV2.X)) * InvDeterminant;

		return !OutTangent.IsNearlyZero() && !OutBitangent.IsNearlyZero();
	}

	FVector MakeFallbackTangent(const FVector& Normal)
	{
		FVector Tangent = FVector::CrossProduct(FVector::UpVector, Normal).GetSafeNormal();
		if (!Tangent.IsNearlyZero())
		{
			return Tangent;
		}

		Tangent = FVector::CrossProduct(FVector::ForwardVector, Normal).GetSafeNormal();
		if (!Tangent.IsNearlyZero())
		{
			return Tangent;
		}

		return FVector::RightVector;
	}

	void RecalculateSkeletalMeshTangents(FSkeletalMesh& Mesh)
	{
		TArray<FVector> TangentAccum;
		TArray<FVector> BitangentAccum;

		TangentAccum.resize(Mesh.Vertices.size(), FVector::ZeroVector);
		BitangentAccum.resize(Mesh.Vertices.size(), FVector::ZeroVector);

		for (size_t IndexOffset = 0; IndexOffset + 2 < Mesh.Indices.size(); IndexOffset += 3)
		{
			const uint32 Index0 = Mesh.Indices[IndexOffset + 0];
			const uint32 Index1 = Mesh.Indices[IndexOffset + 1];
			const uint32 Index2 = Mesh.Indices[IndexOffset + 2];

			if (Index0 >= Mesh.Vertices.size() || Index1 >= Mesh.Vertices.size() || Index2 >= Mesh.Vertices.size())
			{
				continue;
			}

			FVector TriangleTangent = FVector::ZeroVector;
			FVector TriangleBitangent = FVector::ZeroVector;
			if (!CalculateTriangleTangentBasis(
				Mesh.Vertices[Index0],
				Mesh.Vertices[Index1],
				Mesh.Vertices[Index2],
				TriangleTangent,
				TriangleBitangent))
			{
				continue;
			}

			TangentAccum[Index0] += TriangleTangent;
			TangentAccum[Index1] += TriangleTangent;
			TangentAccum[Index2] += TriangleTangent;

			BitangentAccum[Index0] += TriangleBitangent;
			BitangentAccum[Index1] += TriangleBitangent;
			BitangentAccum[Index2] += TriangleBitangent;
		}

		for (size_t VertexIndex = 0; VertexIndex < Mesh.Vertices.size(); ++VertexIndex)
		{
			FSkeletalMeshVertex& Vertex = Mesh.Vertices[VertexIndex];
			FVector Normal = Vertex.Normal.GetSafeNormal();
			if (Normal.IsNearlyZero())
			{
				Normal = FVector::UpVector;
			}

			FVector Tangent = TangentAccum[VertexIndex];
			Tangent = Tangent - (Normal * FVector::DotProduct(Normal, Tangent));
			Tangent = Tangent.GetSafeNormal();
			if (Tangent.IsNearlyZero())
			{
				Tangent = MakeFallbackTangent(Normal);
			}

			FVector Bitangent = BitangentAccum[VertexIndex].GetSafeNormal();
			if (Bitangent.IsNearlyZero())
			{
				Bitangent = FVector::CrossProduct(Normal, Tangent).GetSafeNormal();
			}

			if (Bitangent.IsNearlyZero())
			{
				Bitangent = FVector::RightVector;
			}

			Vertex.Tangent = Tangent;
			Vertex.Bitangent = Bitangent;
		}
	}

	bool ExtractMeshNode(FFbxLoadContext& Context, FbxNode* MeshNode)
	{
		if (MeshNode == nullptr || MeshNode->GetMesh() == nullptr)
		{
			return false;
		}

		FbxMesh* Mesh = MeshNode->GetMesh();
		const int32 ControlPointCount = static_cast<int32>(Mesh->GetControlPointsCount());
		if (ControlPointCount <= 0 || Mesh->GetControlPoints() == nullptr)
		{
			return false;
		}

		int32 MaterialBaseIndex = 0;
		int32 MaterialCount = 0;
		AddMaterialSlotsForNode(Context, MeshNode, MaterialBaseIndex, MaterialCount);
		if (MaterialCount <= 0)
		{
			return false;
		}

		TArray<TArray<uint32>> IndicesByMaterial;
		IndicesByMaterial.resize(MaterialCount);

		const bool bHasSkeleton = !Context.LoadedMesh.Bones.empty();

		TArray<FControlPointInfluenceList> ControlPointInfluences;
		BuildControlPointInfluences(Context, Mesh, ControlPointInfluences);
		NormalizeControlPointInfluences(ControlPointInfluences, bHasSkeleton);

		FbxStringList UVSetNames;
		Mesh->GetUVSetNames(UVSetNames);
		const char* UVSetName = UVSetNames.GetCount() > 0 ? UVSetNames.GetStringAt(0) : nullptr;

		const int32 PolygonCount = static_cast<int32>(Mesh->GetPolygonCount());
		const size_t MaxAdditionalVertexCount = static_cast<size_t>(PolygonCount) * 3;
		Context.LoadedMesh.Vertices.reserve(Context.LoadedMesh.Vertices.size() + MaxAdditionalVertexCount);
		Context.UniqueVertexIndices.reserve(Context.UniqueVertexIndices.size() + MaxAdditionalVertexCount);

		for (int32 PolygonIndex = 0; PolygonIndex < PolygonCount; ++PolygonIndex)
		{
			const int32 PolygonSize = static_cast<int32>(Mesh->GetPolygonSize(PolygonIndex));
			if (PolygonSize != 3)
			{
				continue;
			}

			const int32 MaterialIndex = GetPolygonMaterialIndex(Mesh, PolygonIndex, MaterialCount);

			FSkeletalMeshVertex TriangleVertices[3] = {};
			bool bTriangleValid = true;
			for (int32 PolygonVertexIndex = 0; PolygonVertexIndex < 3; ++PolygonVertexIndex)
			{
				const int32 ControlPointIndex = static_cast<int32>(Mesh->GetPolygonVertex(PolygonIndex, PolygonVertexIndex));
				if (ControlPointIndex < 0 || ControlPointIndex >= ControlPointCount)
				{
					bTriangleValid = false;
					break;
				}

				FSkeletalMeshVertex& Vertex = TriangleVertices[PolygonVertexIndex];
				const FVector LocalPosition = ToEngineVector(Mesh->GetControlPointAt(ControlPointIndex));
				Vertex.Position = LocalPosition;
				Vertex.Normal = GetPolygonVertexNormal(Mesh, PolygonIndex, PolygonVertexIndex);
				Vertex.UVs = GetPolygonVertexUV(Mesh, PolygonIndex, PolygonVertexIndex, UVSetName);

				if (ControlPointIndex < static_cast<int32>(ControlPointInfluences.size()))
				{
					AssignPreparedBoneInfluencesToVertex(ControlPointInfluences[ControlPointIndex].Influences, bHasSkeleton, Vertex);
				}
				else
				{
					AssignPreparedBoneInfluencesToVertex(TArray<FBoneInfluence>(), bHasSkeleton, Vertex);
				}
			}

			if (!bTriangleValid)
			{
				continue;
			}

			Context.RawVertexCount += 3;

			for (int32 TriangleVertexIndex = 0; TriangleVertexIndex < 3; ++TriangleVertexIndex)
			{
				const uint32 VertexIndex = AddUniqueSkeletalMeshVertex(Context, TriangleVertices[TriangleVertexIndex]);
				IndicesByMaterial[MaterialIndex].push_back(VertexIndex);
			}
		}

		for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
		{
			const TArray<uint32>& SectionIndices = IndicesByMaterial[MaterialIndex];
			if (SectionIndices.empty())
			{
				continue;
			}

			FSkeletalMeshSection Section;
			Section.StartIndex = static_cast<uint32>(Context.LoadedMesh.Indices.size());
			Section.IndexCount = static_cast<uint32>(SectionIndices.size());
			Section.MaterialSlotIndex = MaterialBaseIndex + MaterialIndex;

			Context.LoadedMesh.Indices.insert(Context.LoadedMesh.Indices.end(), SectionIndices.begin(), SectionIndices.end());
			Context.LoadedMesh.Sections.push_back(Section);
		}

		return true;
	}

	bool ExtractSkeletalMeshes(FFbxLoadContext& Context)
	{
		Context.LoadedMesh.PathFileName = Context.SourcePath;
		Context.LoadedMesh.Vertices.clear();
		Context.LoadedMesh.Indices.clear();
		Context.LoadedMesh.Sections.clear();
		Context.LoadedMesh.MaterialSlots.clear();
		Context.UniqueVertexIndices.clear();
		Context.RawVertexCount = 0;

		if (Context.Scene == nullptr)
		{
			return false;
		}

		bool bExtractedAnyMesh = false;
		FbxNode* RootNode = Context.Scene->GetRootNode();
		TraverseFbxNodes(RootNode, [&Context, &bExtractedAnyMesh](FbxNode* Node)
		{
			if (Node != nullptr && Node->GetMesh() != nullptr)
			{
				bExtractedAnyMesh |= ExtractMeshNode(Context, Node);
			}
		});

		if (!bExtractedAnyMesh || Context.LoadedMesh.Vertices.empty() || Context.LoadedMesh.Indices.empty())
		{
			return false;
		}

		RecalculateSkeletalMeshTangents(Context.LoadedMesh);
		return true;
	}
} //또 졸라 커지는 네임스페이스..., 줴줴이야

USkeletalMesh* FFbxLoader::Load(const FString& Path)
{
    /*
    FBX 로딩 전체 흐름.
    
	1. FBX SDK 객체 생성
    2. 파일 import
    3. 씬 정규화
    4. 삼각형화
    5. Skeleton 수집
    6. Mesh 추출
    7. USkeletalMesh 생성
    8. FBX SDK 객체 정리

	각각 함수로 분리됨 -> local variable이 넘 많아지는 문제 해결
	*/

	// 1. FBX SDK 객체 생성
    FFbxLoadContext Context;
    if (!CreateFbxSdkContext(Context))
    {
        return nullptr;
    }
    Context.SourcePath = Path;

	//2. 파일 import
    if (!ImportFbxScene(Context, Path))
    {
        DestroyFbxSdkContext(Context);
        return nullptr;
    }

	//3. 씬 정규화
	ConvertSceneToEngineSpace(Context);

	// 4. 삼각형화(fbx 내에 triangle이 아닌 quad나 N-gon이 섞여 있을 수 있기 때문.)
    if (!TriangulateFbxScene(Context))
    {
        DestroyFbxSdkContext(Context);
        return nullptr;
	}

	//5. Skeleton 수집
    CollectSkeletons(Context);

	if (Context.LoadedMesh.Bones.empty())
	{
		UE_LOG("FBX skeleton not found. Path: %s", Context.SourcePath.c_str());
		DestroyFbxSdkContext(Context);
		return nullptr;
	}

	//6. Mesh 추출
    if (!ExtractSkeletalMeshes(Context))
    {
        DestroyFbxSdkContext(Context);
        return nullptr;
    }

	//7. USkeletalMesh 생성
	USkeletalMesh* SkeletalMesh = new USkeletalMesh();
	SkeletalMesh->SetMeshData(new FSkeletalMesh(Context.LoadedMesh));

	const uint32 UniqueVertexCount = static_cast<uint32>(Context.LoadedMesh.Vertices.size());
	const uint32 RemovedVertexCount = Context.RawVertexCount > UniqueVertexCount ? Context.RawVertexCount - UniqueVertexCount : 0;
	const float ReductionPercent = CalculateReductionPercent(Context.RawVertexCount, UniqueVertexCount);

	UE_LOG("FBX skeletal mesh loaded. Bones: %d, RawVertices: %d, UniqueVertices: %d, RemovedDuplicates: %d, Reduction: %.2f%%, Indices: %d, Sections: %d, Materials: %d, Path: %s",
		static_cast<int32>(Context.LoadedMesh.Bones.size()),
		static_cast<int32>(Context.RawVertexCount),
		static_cast<int32>(UniqueVertexCount),
		static_cast<int32>(RemovedVertexCount),
		ReductionPercent,
		static_cast<int32>(Context.LoadedMesh.Indices.size()),
		static_cast<int32>(Context.LoadedMesh.Sections.size()),
		static_cast<int32>(Context.LoadedMesh.MaterialSlots.size()),
		Context.SourcePath.c_str());
	
	//더는 사용하지 않은 친구 손절
	DestroyFbxSdkContext(Context);
	return SkeletalMesh;
}

bool FFbxLoader::SupportsExtension(const FString& Extension) const
{
    return ToLowerAscii(Extension) == "fbx";
}
