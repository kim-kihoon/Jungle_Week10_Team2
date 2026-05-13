#include "Editor/Importer/FbxStaticMeshImporter.h"

#include "Core/Logger.h"
#include "Core/Paths.h"
#include "Core/PlatformTime.h"
#include "Core/ResourceManager.h"
#include "Math/Utils.h"
#include "Object/Object.h"

#include <fbxsdk.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>

namespace
{
	constexpr const char* DefaultUberLitShaderPath = "Shaders/UberLit.hlsl";

	struct FFbxStaticMaterialSlotSource
	{
		FString SlotName;
		FbxSurfaceMaterial* Material = nullptr;
	};

	struct FFbxStaticMeshBuild
	{
		FString MeshNodeName;
		FString AssetStem;
		FString AssetPath;
		FStaticMesh Mesh;
		TArray<FString> BuiltSlotNames;
		TArray<TArray<uint32>> IndicesBySlot;
		TArray<FFbxStaticMaterialSlotSource> SlotSources;
	};

	struct FFbxStaticImportContext
	{
		FString SourcePath;

		FbxManager* Manager = nullptr;
		FbxIOSettings* IOSettings = nullptr;
		FbxScene* Scene = nullptr;

		TArray<FFbxStaticMeshBuild> Meshes;
	};

	FString ToLowerAscii(FString Value)
	{
		std::transform(Value.begin(), Value.end(), Value.begin(), [](unsigned char Ch)
		{
			return static_cast<char>(std::tolower(Ch));
		});
		return Value;
	}

	bool IsFbxSourcePath(const FString& Path)
	{
		return ToLowerAscii(std::filesystem::path(FPaths::ToWide(Path)).extension().string()) == ".fbx";
	}

	FString SanitizeAssetFileToken(FString Value)
	{
		if (Value.empty())
		{
			return "DefaultWhite";
		}

		for (char& Ch : Value)
		{
			const unsigned char Byte = static_cast<unsigned char>(Ch);
			if (!std::isalnum(Byte) &&
				Ch != '_' &&
				Ch != '-' &&
				Ch != '.')
			{
				Ch = '_';
			}
		}

		return Value;
	}

	uint64 GetFileWriteTimeTicks(const FString& Path)
	{
		namespace fs = std::filesystem;

		fs::path FilePath(FPaths::ToAbsolute(FPaths::ToWide(Path)));
		if (!fs::exists(FilePath))
		{
			return 0;
		}

		auto WriteTime = fs::last_write_time(FilePath);
		auto Duration = WriteTime.time_since_epoch();
		return static_cast<uint64>(std::chrono::duration_cast<std::chrono::seconds>(Duration).count());
	}

	uint64 HashFileFNV1a(const FString& Path)
	{
		std::ifstream File(std::filesystem::path(FPaths::ToAbsolute(FPaths::ToWide(Path))), std::ios::binary);
		if (!File.is_open())
		{
			return 0;
		}

		uint64 Hash = 14695981039346656037ull;
		char Buffer[64 * 1024];
		while (File.good())
		{
			File.read(Buffer, sizeof(Buffer));
			const std::streamsize ReadBytes = File.gcount();
			for (std::streamsize Index = 0; Index < ReadBytes; ++Index)
			{
				Hash ^= static_cast<unsigned char>(Buffer[Index]);
				Hash *= 1099511628211ull;
			}
		}

		return Hash;
	}

	FString GetFbxObjectName(const FbxObject* Object, const char* FallbackName)
	{
		if (Object == nullptr || Object->GetName() == nullptr || Object->GetName()[0] == '\0')
		{
			return FString(FallbackName ? FallbackName : "Unnamed");
		}

		return FString(Object->GetName());
	}

	FString GetNodeAssetBaseName(const FbxNode* Node, const FString& SourcePath)
	{
		if (Node != nullptr && Node->GetName() != nullptr && Node->GetName()[0] != '\0')
		{
			return SanitizeAssetFileToken(FString(Node->GetName()));
		}

		const std::filesystem::path SourceFsPath(FPaths::ToWide(FPaths::Normalize(SourcePath)));
		return SanitizeAssetFileToken(FPaths::ToUtf8(SourceFsPath.stem().generic_wstring()));
	}

	FbxNode* GetMeshAssetOwnerNode(FbxNode* MeshNode, FbxNode* SceneRootNode)
	{
		if (MeshNode == nullptr)
		{
			return nullptr;
		}

		FbxNode* OwnerNode = MeshNode;
		FbxNode* ParentNode = OwnerNode->GetParent();
		while (ParentNode != nullptr && ParentNode != SceneRootNode)
		{
			OwnerNode = ParentNode;
			ParentNode = OwnerNode->GetParent();
		}

		return OwnerNode;
	}

	FString MakeUniqueAssetStem(const FString& BaseName, TMap<FString, int32>& AssetStemUseCounts)
	{
		const FString ResolvedBaseName = BaseName.empty() ? FString("UnnamedMesh") : BaseName;
		const FString StemKey = ToLowerAscii(ResolvedBaseName);

		int32& UseCount = AssetStemUseCounts[StemKey];
		if (UseCount == 0)
		{
			UseCount = 1;
			return ResolvedBaseName;
		}

		const FString UniqueStem = ResolvedBaseName + "_" + std::to_string(UseCount);
		++UseCount;
		return UniqueStem;
	}

	FString MakeFbxStaticMeshAssetPath(const FString& SourcePath, const FString& AssetStem)
	{
		namespace fs = std::filesystem;

		const fs::path SourceFsPath(FPaths::ToWide(FPaths::Normalize(SourcePath)));
		const std::wstring AssetFileName = FPaths::ToWide(SanitizeAssetFileToken(AssetStem)) + L".asset";
		const fs::path AssetPath = (SourceFsPath.parent_path() / AssetFileName).lexically_normal();
		fs::create_directories(fs::path(FPaths::ToAbsolute(AssetPath.parent_path().generic_wstring())));

		return FPaths::ToUtf8(AssetPath.generic_wstring());
	}

	FString MakeFbxMaterialAssetPath(const FString& SourcePath, const FString& AssetStem, const FString& SlotName)
	{
		namespace fs = std::filesystem;

		const fs::path SourceFsPath(FPaths::ToWide(FPaths::Normalize(SourcePath)));
		const std::wstring MaterialFileName =
			FPaths::ToWide(SanitizeAssetFileToken(AssetStem)) + L"_" + FPaths::ToWide(SanitizeAssetFileToken(SlotName)) + L".mat";
		fs::path MaterialPath = SourceFsPath.parent_path() / MaterialFileName;

		return FPaths::ToUtf8(MaterialPath.lexically_normal().generic_wstring());
	}

	bool TryLoadKnownMaterialShader(FResourceManager& ResourceManager, const FString& ShaderName)
	{
		if (ShaderName == "Shaders/UberLit.hlsl" || ShaderName == "Shaders/UberUnlit.hlsl")
		{
			return ResourceManager.LoadShader(ShaderName, "mainVS", "mainPS", static_cast<const D3D_SHADER_MACRO*>(nullptr));
		}

		if (ShaderName == "Shaders/OutlinePostProcess.hlsl")
		{
			return ResourceManager.LoadShader(ShaderName, "VS", "PS", static_cast<const D3D_SHADER_MACRO*>(nullptr));
		}

		return false;
	}

	UShader* GetOrTryLoadMaterialShader(FResourceManager& ResourceManager, const FString& ShaderName)
	{
		if (UShader* Shader = ResourceManager.GetShader(ShaderName))
		{
			return Shader;
		}

		UE_LOG("[FbxStaticMeshImporter] Shader cache miss for imported material: %s. Attempting lazy load.", ShaderName.c_str());
		if (!TryLoadKnownMaterialShader(ResourceManager, ShaderName))
		{
			return nullptr;
		}

		return ResourceManager.GetShader(ShaderName);
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

	FVector ReadFbxColorProperty(FbxSurfaceMaterial* Material, const char* PropertyName, const FVector& DefaultValue)
	{
		if (Material == nullptr || PropertyName == nullptr)
		{
			return DefaultValue;
		}

		FbxProperty Property = Material->FindProperty(PropertyName);
		if (!Property.IsValid())
		{
			return DefaultValue;
		}

		const FbxDouble3 Value = Property.Get<FbxDouble3>();
		return FVector(static_cast<float>(Value[0]), static_cast<float>(Value[1]), static_cast<float>(Value[2]));
	}

	float ReadFbxFloatProperty(FbxSurfaceMaterial* Material, const char* PropertyName, float DefaultValue)
	{
		if (Material == nullptr || PropertyName == nullptr)
		{
			return DefaultValue;
		}

		FbxProperty Property = Material->FindProperty(PropertyName);
		if (!Property.IsValid())
		{
			return DefaultValue;
		}

		return static_cast<float>(Property.Get<FbxDouble>());
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

	bool CreateMaterialAsset(
		FResourceManager& ResourceManager,
		const FString& MaterialPath,
		const FString& SlotName,
		const FString& SourcePath,
		FbxSurfaceMaterial* FbxMaterial)
	{
		UShader* Shader = GetOrTryLoadMaterialShader(ResourceManager, DefaultUberLitShaderPath);
		if (Shader == nullptr)
		{
			UE_LOG("[FbxStaticMeshImporter] Failed to load material shader for %s", MaterialPath.c_str());
			return false;
		}

		const FString DiffuseTexturePath = GetDiffuseTexturePath(SourcePath, FbxMaterial);
		const FString NormalTexturePath = GetNormalTexturePath(SourcePath, FbxMaterial);
		const FString SpecularTexturePath = GetSpecularTexturePath(SourcePath, FbxMaterial);

		UTexture* DiffuseTexture = DiffuseTexturePath.empty() ? nullptr : ResourceManager.LoadTexture(DiffuseTexturePath);
		UTexture* NormalTexture = NormalTexturePath.empty() ? nullptr : ResourceManager.LoadTexture(NormalTexturePath);
		UTexture* SpecularTexture = SpecularTexturePath.empty() ? nullptr : ResourceManager.LoadTexture(SpecularTexturePath);

		UMaterial* Material = UObjectManager::Get().CreateObject<UMaterial>();
		Material->Name = SlotName;
		Material->FilePath = MaterialPath;
		Material->MaterialData.Name = SlotName;
		Material->MaterialData.BaseColor = ReadFbxColorProperty(FbxMaterial, FbxSurfaceMaterial::sDiffuse, FVector(0.8f, 0.8f, 0.8f));
		Material->MaterialData.SpecularColor = ReadFbxColorProperty(FbxMaterial, FbxSurfaceMaterial::sSpecular, FVector(0.0f, 0.0f, 0.0f));
		Material->MaterialData.EmissiveColor = ReadFbxColorProperty(FbxMaterial, FbxSurfaceMaterial::sEmissive, FVector(0.0f, 0.0f, 0.0f));
		Material->MaterialData.Shininess = ReadFbxFloatProperty(FbxMaterial, FbxSurfaceMaterial::sShininess, 30.0f);
		Material->MaterialData.Opacity = std::clamp(1.0f - ReadFbxFloatProperty(FbxMaterial, FbxSurfaceMaterial::sTransparencyFactor, 0.0f), 0.0f, 1.0f);
		Material->MaterialData.DiffuseTexPath = DiffuseTexturePath;
		Material->MaterialData.bHasDiffuseTexture = DiffuseTexture != nullptr && !DiffuseTexturePath.empty();
		Material->MaterialData.NormalTexPath = NormalTexturePath;
		Material->MaterialData.bHasNormalTexture = NormalTexture != nullptr && !NormalTexturePath.empty();
		Material->MaterialData.SpecularTexPath = SpecularTexturePath;
		Material->MaterialData.bHasSpecularTexture = SpecularTexture != nullptr && !SpecularTexturePath.empty();
		Material->SetShader(Shader);
		SetDefaultUberLitParams(Material, DiffuseTexture, NormalTexture, SpecularTexture);

		const bool bSerialized = ResourceManager.SerializeMaterial(MaterialPath, Material);
		UObjectManager::Get().DestroyObject(Material);

		if (!bSerialized)
		{
			UE_LOG("[FbxStaticMeshImporter] Failed to serialize material asset: %s", MaterialPath.c_str());
		}

		return bSerialized;
	}

	bool ResolveMaterialAssetForSlot(
		FResourceManager& ResourceManager,
		const FString& SourcePath,
		const FString& AssetStem,
		const FFbxStaticMaterialSlotSource& SlotSource,
		FString& OutMaterialPath,
		TArray<FString>* OutMaterialPaths)
	{
		namespace fs = std::filesystem;

		const FString MaterialPath = MakeFbxMaterialAssetPath(SourcePath, AssetStem, SlotSource.SlotName);
		fs::path AbsoluteMaterialPath(FPaths::ToAbsolute(FPaths::ToWide(MaterialPath)));
		fs::path AbsoluteMaterialInstancePath = AbsoluteMaterialPath;
		AbsoluteMaterialInstancePath.replace_extension(L".matinst");

		const bool bMaterialExists = fs::exists(AbsoluteMaterialPath);
		const bool bMaterialInstanceExists = fs::exists(AbsoluteMaterialInstancePath);

		if (!bMaterialExists && bMaterialInstanceExists)
		{
			OutMaterialPath = FPaths::ToUtf8(
				fs::path(FPaths::ToWide(MaterialPath)).replace_extension(L".matinst").generic_wstring());
			return true;
		}

		OutMaterialPath = MaterialPath;
		if (!bMaterialExists)
		{
			if (!CreateMaterialAsset(ResourceManager, MaterialPath, SlotSource.SlotName, SourcePath, SlotSource.Material))
			{
				OutMaterialPath = "DefaultWhite";
				return false;
			}

			UE_LOG("[FbxStaticMeshImporter] Created material asset: %s", MaterialPath.c_str());
		}

		if (OutMaterialPaths &&
			std::find(OutMaterialPaths->begin(), OutMaterialPaths->end(), MaterialPath) == OutMaterialPaths->end())
		{
			OutMaterialPaths->push_back(MaterialPath);
		}

		return true;
	}

	bool EnsureMaterialAssets(
		FResourceManager& ResourceManager,
		FFbxStaticImportContext& Context,
		FFbxStaticMeshBuild& MeshBuild,
		TMap<FString, FString>& OutMaterialAssetPaths,
		TArray<FString>* OutMaterialPaths)
	{
		bool bAllOk = true;
		for (const FFbxStaticMaterialSlotSource& SlotSource : MeshBuild.SlotSources)
		{
			FString MaterialAssetPath;
			bAllOk &= ResolveMaterialAssetForSlot(
				ResourceManager,
				Context.SourcePath,
				MeshBuild.AssetStem,
				SlotSource,
				MaterialAssetPath,
				OutMaterialPaths);

			OutMaterialAssetPaths[SlotSource.SlotName] = MaterialAssetPath.empty() ? FString("DefaultWhite") : MaterialAssetPath;
		}

		return bAllOk;
	}

	bool CreateFbxSdkContext(FFbxStaticImportContext& Context)
	{
		Context.Manager = FbxManager::Create();
		if (Context.Manager == nullptr)
		{
			return false;
		}

		Context.IOSettings = FbxIOSettings::Create(Context.Manager, IOSROOT);
		if (Context.IOSettings == nullptr)
		{
			Context.Manager->Destroy();
			Context.Manager = nullptr;
			return false;
		}

		Context.Manager->SetIOSettings(Context.IOSettings);
		Context.Scene = FbxScene::Create(Context.Manager, "FbxStaticMeshImportScene");
		if (Context.Scene == nullptr)
		{
			Context.Manager->Destroy();
			Context.Manager = nullptr;
			Context.IOSettings = nullptr;
			return false;
		}

		return true;
	}

	void DestroyFbxSdkContext(FFbxStaticImportContext& Context)
	{
		if (Context.Manager != nullptr)
		{
			Context.Manager->Destroy();
		}

		Context.Manager = nullptr;
		Context.IOSettings = nullptr;
		Context.Scene = nullptr;
		Context.Meshes.clear();
	}

	bool ImportFbxScene(FFbxStaticImportContext& Context, const FString& Path)
	{
		if (Context.Manager == nullptr || Context.Scene == nullptr)
		{
			return false;
		}

		FbxImporter* Importer = FbxImporter::Create(Context.Manager, "FbxStaticMeshTempImporter");
		if (Importer == nullptr)
		{
			return false;
		}

		const bool bInitialized = Importer->Initialize(Path.c_str(), -1, Context.Manager->GetIOSettings());
		if (!bInitialized)
		{
			const char* ErrorString = Importer->GetStatus().GetErrorString();
			UE_LOG("[FbxStaticMeshImporter] FBX initialize failed for %s: %s", Path.c_str(), ErrorString ? ErrorString : "Unknown FBX error");
			Importer->Destroy();
			return false;
		}

		const bool bImported = Importer->Import(Context.Scene);
		if (!bImported)
		{
			const char* ErrorString = Importer->GetStatus().GetErrorString();
			UE_LOG("[FbxStaticMeshImporter] FBX import failed for %s: %s", Path.c_str(), ErrorString ? ErrorString : "Unknown FBX error");
			Importer->Destroy();
			return false;
		}

		Importer->Destroy();
		return true;
	}

	void ConvertSceneToEngineSpace(FFbxStaticImportContext& Context)
	{
		if (Context.Scene == nullptr)
		{
			return;
		}

		FbxAxisSystem EngineAxisSystem;
		if (!FbxAxisSystem::ParseAxisSystem("yzx", EngineAxisSystem))
		{
			UE_LOG("[FbxStaticMeshImporter] FBX engine axis system parse failed. Path: %s", Context.SourcePath.c_str());
			return;
		}

		const FbxAxisSystem CurrentAxisSystem = Context.Scene->GetGlobalSettings().GetAxisSystem();
		if (CurrentAxisSystem != EngineAxisSystem)
		{
			EngineAxisSystem.DeepConvertScene(Context.Scene);
			UE_LOG("[FbxStaticMeshImporter] FBX axis system converted. Path: %s", Context.SourcePath.c_str());
		}

		const FbxSystemUnit EngineUnitSystem = FbxSystemUnit::m;
		const FbxSystemUnit CurrentUnitSystem = Context.Scene->GetGlobalSettings().GetSystemUnit();
		if (CurrentUnitSystem != EngineUnitSystem)
		{
			EngineUnitSystem.ConvertScene(Context.Scene);
			UE_LOG("[FbxStaticMeshImporter] FBX unit system converted to meter. Path: %s", Context.SourcePath.c_str());
		}
	}

	bool TriangulateFbxScene(FFbxStaticImportContext& Context)
	{
		if (Context.Manager == nullptr || Context.Scene == nullptr)
		{
			return false;
		}

		FbxGeometryConverter GeometryConverter(Context.Manager);
		const bool bConverted = GeometryConverter.Triangulate(Context.Scene, true);
		if (!bConverted)
		{
			UE_LOG("[FbxStaticMeshImporter] FBX triangulation failed. Path: %s", Context.SourcePath.c_str());
			return false;
		}

		return true;
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

	bool NodeHasSkeletonAttribute(const FbxNode* Node)
	{
		if (Node == nullptr)
		{
			return false;
		}

		const FbxNodeAttribute* Attribute = Node->GetNodeAttribute();
		return Attribute != nullptr && Attribute->GetAttributeType() == FbxNodeAttribute::eSkeleton;
	}

	bool MeshHasSkin(FbxMesh* Mesh)
	{
		return Mesh != nullptr && Mesh->GetDeformerCount(FbxDeformer::eSkin) > 0;
	}

	bool SceneHasSkeletonOrSkin(FFbxStaticImportContext& Context)
	{
		if (Context.Scene == nullptr)
		{
			return false;
		}

		bool bHasSkeletonOrSkin = false;
		TraverseFbxNodes(Context.Scene->GetRootNode(), [&bHasSkeletonOrSkin](FbxNode* Node)
		{
			if (bHasSkeletonOrSkin)
			{
				return;
			}

			bHasSkeletonOrSkin = NodeHasSkeletonAttribute(Node) || MeshHasSkin(Node ? Node->GetMesh() : nullptr);
		});

		return bHasSkeletonOrSkin;
	}

	int32 GetOrAddMaterialSlot(FFbxStaticMeshBuild& MeshBuild, const FString& SlotName, FbxSurfaceMaterial* Material)
	{
		const FString ResolvedSlotName = SlotName.empty() ? FString("DefaultWhite") : SlotName;

		for (int32 Index = 0; Index < static_cast<int32>(MeshBuild.BuiltSlotNames.size()); ++Index)
		{
			if (MeshBuild.BuiltSlotNames[Index] == ResolvedSlotName)
			{
				if (MeshBuild.SlotSources[Index].Material == nullptr && Material != nullptr)
				{
					MeshBuild.SlotSources[Index].Material = Material;
				}
				return Index;
			}
		}

		MeshBuild.BuiltSlotNames.push_back(ResolvedSlotName);
		MeshBuild.IndicesBySlot.emplace_back();

		FFbxStaticMaterialSlotSource SlotSource;
		SlotSource.SlotName = ResolvedSlotName;
		SlotSource.Material = Material;
		MeshBuild.SlotSources.push_back(SlotSource);

		return static_cast<int32>(MeshBuild.BuiltSlotNames.size() - 1);
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

	FVector GetPolygonVertexNormal(
		FbxMesh* Mesh,
		const FbxAMatrix& MeshGlobalTransform,
		int32 PolygonIndex,
		int32 PolygonVertexIndex)
	{
		FbxVector4 FbxNormal(0.0, 0.0, 1.0, 0.0);
		if (Mesh != nullptr)
		{
			Mesh->GetPolygonVertexNormal(PolygonIndex, PolygonVertexIndex, FbxNormal);
		}

		FbxNormal = MeshGlobalTransform.MultR(FbxNormal);
		FVector Normal = ToEngineVector(FbxNormal).GetSafeNormal();
		return Normal.IsNearlyZero() ? FVector(0.0f, 0.0f, 1.0f) : Normal;
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

	void BuildFallbackBasis(const FVector& InNormal, FVector& OutTangent, FVector& OutBitangent)
	{
		FVector Normal = InNormal.GetSafeNormal();
		if (Normal.IsNearlyZero())
		{
			Normal = FVector(0.0f, 0.0f, 1.0f);
		}

		Normal.FindBestAxisVectors(OutTangent, OutBitangent);
		OutTangent = OutTangent.GetSafeNormal();
		OutBitangent = OutBitangent.GetSafeNormal();
	}

	void CalculateTriangleTangent(FNormalVertex& V0, FNormalVertex& V1, FNormalVertex& V2)
	{
		const FVector Edge1 = V1.Position - V0.Position;
		const FVector Edge2 = V2.Position - V0.Position;
		const FVector2 DeltaUV1 = V1.UVs - V0.UVs;
		const FVector2 DeltaUV2 = V2.UVs - V0.UVs;

		const float Determinant = DeltaUV1.X * DeltaUV2.Y - DeltaUV1.Y * DeltaUV2.X;
		if (MathUtil::Abs(Determinant) <= MathUtil::Epsilon)
		{
			FVector Tangent;
			FVector Bitangent;
			BuildFallbackBasis(V0.Normal, Tangent, Bitangent);
			V0.Tangent = Tangent;
			V1.Tangent = Tangent;
			V2.Tangent = Tangent;
			V0.Bitangent = Bitangent;
			V1.Bitangent = Bitangent;
			V2.Bitangent = Bitangent;
			return;
		}

		const float InvDeterminant = 1.0f / Determinant;
		FVector Tangent = ((Edge1 * DeltaUV2.Y) - (Edge2 * DeltaUV1.Y)) * InvDeterminant;
		FVector Bitangent = ((Edge2 * DeltaUV1.X) - (Edge1 * DeltaUV2.X)) * InvDeterminant;

		if (!Tangent.Normalize())
		{
			BuildFallbackBasis(V0.Normal, Tangent, Bitangent);
		}
		else if (!Bitangent.Normalize())
		{
			Bitangent = FVector::CrossProduct(V0.Normal, Tangent);
			if (!Bitangent.Normalize())
			{
				BuildFallbackBasis(V0.Normal, Tangent, Bitangent);
			}
		}

		V0.Tangent = Tangent;
		V1.Tangent = Tangent;
		V2.Tangent = Tangent;
		V0.Bitangent = Bitangent;
		V1.Bitangent = Bitangent;
		V2.Bitangent = Bitangent;
	}

	bool ExtractMeshNode(FFbxStaticMeshBuild& MeshBuild, FbxNode* MeshNode)
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

		const int32 NodeMaterialCount = static_cast<int32>(MeshNode->GetMaterialCount());
		TArray<int32> NodeMaterialToSlot;
		if (NodeMaterialCount <= 0)
		{
			NodeMaterialToSlot.push_back(GetOrAddMaterialSlot(MeshBuild, "DefaultWhite", nullptr));
		}
		else
		{
			NodeMaterialToSlot.reserve(NodeMaterialCount);
			for (int32 MaterialIndex = 0; MaterialIndex < NodeMaterialCount; ++MaterialIndex)
			{
				FbxSurfaceMaterial* Material = MeshNode->GetMaterial(MaterialIndex);
				NodeMaterialToSlot.push_back(GetOrAddMaterialSlot(MeshBuild, GetFbxObjectName(Material, "Material"), Material));
			}
		}

		FbxStringList UVSetNames;
		Mesh->GetUVSetNames(UVSetNames);
		const char* UVSetName = UVSetNames.GetCount() > 0 ? UVSetNames.GetStringAt(0) : nullptr;
		const FbxAMatrix MeshGlobalTransform = MeshNode->EvaluateGlobalTransform();

		bool bExtractedAnyPolygon = false;
		const int32 PolygonCount = static_cast<int32>(Mesh->GetPolygonCount());
		for (int32 PolygonIndex = 0; PolygonIndex < PolygonCount; ++PolygonIndex)
		{
			const int32 PolygonSize = static_cast<int32>(Mesh->GetPolygonSize(PolygonIndex));
			if (PolygonSize != 3)
			{
				continue;
			}

			const int32 MaterialIndex = GetPolygonMaterialIndex(Mesh, PolygonIndex, static_cast<int32>(NodeMaterialToSlot.size()));
			const int32 SlotIndex = NodeMaterialToSlot[MaterialIndex];
			if (SlotIndex < 0 || SlotIndex >= static_cast<int32>(MeshBuild.IndicesBySlot.size()))
			{
				continue;
			}

			FNormalVertex TriangleVertices[3] = {};
			bool bTriangleValid = true;
			for (int32 PolygonVertexIndex = 0; PolygonVertexIndex < 3; ++PolygonVertexIndex)
			{
				const int32 ControlPointIndex = static_cast<int32>(Mesh->GetPolygonVertex(PolygonIndex, PolygonVertexIndex));
				if (ControlPointIndex < 0 || ControlPointIndex >= ControlPointCount)
				{
					bTriangleValid = false;
					break;
				}

				FNormalVertex& Vertex = TriangleVertices[PolygonVertexIndex];
				const FbxVector4 FbxPosition = MeshGlobalTransform.MultT(Mesh->GetControlPointAt(ControlPointIndex));
				Vertex.Position = ToEngineVector(FbxPosition);
				Vertex.Color = FColor{ 1.0f, 1.0f, 1.0f, 1.0f };
				Vertex.Normal = GetPolygonVertexNormal(Mesh, MeshGlobalTransform, PolygonIndex, PolygonVertexIndex);
				Vertex.UVs = GetPolygonVertexUV(Mesh, PolygonIndex, PolygonVertexIndex, UVSetName);
			}

			if (!bTriangleValid)
			{
				continue;
			}

			CalculateTriangleTangent(TriangleVertices[0], TriangleVertices[1], TriangleVertices[2]);

			const uint32 BaseVertexIndex = static_cast<uint32>(MeshBuild.Mesh.Vertices.size());
			MeshBuild.Mesh.Vertices.push_back(TriangleVertices[0]);
			MeshBuild.Mesh.Vertices.push_back(TriangleVertices[1]);
			MeshBuild.Mesh.Vertices.push_back(TriangleVertices[2]);

			MeshBuild.IndicesBySlot[SlotIndex].push_back(BaseVertexIndex + 0);
			MeshBuild.IndicesBySlot[SlotIndex].push_back(BaseVertexIndex + 1);
			MeshBuild.IndicesBySlot[SlotIndex].push_back(BaseVertexIndex + 2);
			bExtractedAnyPolygon = true;
		}

		return bExtractedAnyPolygon;
	}

	FAABB BuildLocalBounds(const FStaticMesh& StaticMesh)
	{
		FAABB Bounds;
		Bounds.Reset();

		for (const FNormalVertex& Vertex : StaticMesh.Vertices)
		{
			Bounds.Expand(Vertex.Position);
		}

		return Bounds;
	}

	void FinalizeStaticMeshBuild(FFbxStaticMeshBuild& MeshBuild)
	{
		MeshBuild.Mesh.PathFileName = MeshBuild.AssetPath;
		MeshBuild.Mesh.Slots.clear();
		MeshBuild.Mesh.Sections.clear();
		MeshBuild.Mesh.Indices.clear();

		for (const FString& SlotName : MeshBuild.BuiltSlotNames)
		{
			FStaticMeshMaterialSlot Slot;
			Slot.SlotName = SlotName;
			Slot.Material = nullptr;
			MeshBuild.Mesh.Slots.push_back(Slot);
		}

		for (int32 SlotIndex = 0; SlotIndex < static_cast<int32>(MeshBuild.IndicesBySlot.size()); ++SlotIndex)
		{
			const TArray<uint32>& SectionIndices = MeshBuild.IndicesBySlot[SlotIndex];
			if (SectionIndices.empty())
			{
				continue;
			}

			FStaticMeshSection Section;
			Section.StartIndex = static_cast<uint32>(MeshBuild.Mesh.Indices.size());
			Section.IndexCount = static_cast<uint32>(SectionIndices.size());
			Section.MaterialSlotIndex = SlotIndex;

			MeshBuild.Mesh.Indices.insert(
				MeshBuild.Mesh.Indices.end(),
				SectionIndices.begin(),
				SectionIndices.end());
			MeshBuild.Mesh.Sections.push_back(Section);
		}

		MeshBuild.Mesh.LocalBounds = BuildLocalBounds(MeshBuild.Mesh);
	}

	bool ExtractStaticMeshes(FFbxStaticImportContext& Context)
	{
		Context.Meshes.clear();

		if (Context.Scene == nullptr)
		{
			return false;
		}

		FbxNode* SceneRootNode = Context.Scene->GetRootNode();
		TMap<FString, int32> AssetStemUseCounts;
		TMap<FbxNode*, int32> MeshBuildIndexByOwnerNode;
		bool bExtractedAnyMesh = false;
		TraverseFbxNodes(SceneRootNode, [&Context, SceneRootNode, &AssetStemUseCounts, &MeshBuildIndexByOwnerNode, &bExtractedAnyMesh](FbxNode* Node)
		{
			if (Node != nullptr && Node->GetMesh() != nullptr)
			{
				FbxNode* OwnerNode = GetMeshAssetOwnerNode(Node, SceneRootNode);
				if (OwnerNode == nullptr)
				{
					return;
				}

				auto BuildIndexIt = MeshBuildIndexByOwnerNode.find(OwnerNode);
				if (BuildIndexIt == MeshBuildIndexByOwnerNode.end())
				{
					FFbxStaticMeshBuild MeshBuild;
					MeshBuild.MeshNodeName = GetNodeAssetBaseName(OwnerNode, Context.SourcePath);
					MeshBuild.AssetStem = MakeUniqueAssetStem(MeshBuild.MeshNodeName, AssetStemUseCounts);
					MeshBuild.AssetPath = MakeFbxStaticMeshAssetPath(Context.SourcePath, MeshBuild.AssetStem);

					const int32 NewBuildIndex = static_cast<int32>(Context.Meshes.size());
					Context.Meshes.push_back(MeshBuild);
					MeshBuildIndexByOwnerNode[OwnerNode] = NewBuildIndex;
					BuildIndexIt = MeshBuildIndexByOwnerNode.find(OwnerNode);
				}

				FFbxStaticMeshBuild& MeshBuild = Context.Meshes[BuildIndexIt->second];
				if (ExtractMeshNode(MeshBuild, Node))
				{
					bExtractedAnyMesh = true;
				}
			}
		});

		Context.Meshes.erase(
			std::remove_if(
				Context.Meshes.begin(),
				Context.Meshes.end(),
				[](const FFbxStaticMeshBuild& MeshBuild)
				{
					return MeshBuild.Mesh.Vertices.empty();
				}),
			Context.Meshes.end());

		for (FFbxStaticMeshBuild& MeshBuild : Context.Meshes)
		{
			FinalizeStaticMeshBuild(MeshBuild);
		}

		UE_LOG("[FbxStaticMeshImporter] FBX static meshes extracted. Meshes: %d, Path: %s",
			static_cast<int32>(Context.Meshes.size()),
			Context.SourcePath.c_str());

		return bExtractedAnyMesh && !Context.Meshes.empty();
	}
}

EFbxStaticMeshImportResult FFbxStaticMeshImporter::ImportIfStaticMeshNeeded(
	FResourceManager& ResourceManager,
	const FString& SourcePath,
	TArray<FString>* OutMaterialPaths)
{
	if (!IsFbxSourcePath(SourcePath))
	{
		return EFbxStaticMeshImportResult::NotFbx;
	}

	const double StartTime = FPlatformTime::Seconds();

	FFbxStaticImportContext Context;
	if (!CreateFbxSdkContext(Context))
	{
		UE_LOG("[FbxStaticMeshImporter] Failed to create FBX SDK context: %s", SourcePath.c_str());
		return EFbxStaticMeshImportResult::Failed;
	}
	Context.SourcePath = SourcePath;

	if (!ImportFbxScene(Context, SourcePath))
	{
		DestroyFbxSdkContext(Context);
		return EFbxStaticMeshImportResult::Failed;
	}

	ConvertSceneToEngineSpace(Context);

	if (SceneHasSkeletonOrSkin(Context))
	{
		UE_LOG("[FbxStaticMeshImporter] Skipped non-static FBX: %s", SourcePath.c_str());
		DestroyFbxSdkContext(Context);
		return EFbxStaticMeshImportResult::SkippedNonStatic;
	}

	if (!TriangulateFbxScene(Context))
	{
		DestroyFbxSdkContext(Context);
		return EFbxStaticMeshImportResult::Failed;
	}

	if (!ExtractStaticMeshes(Context))
	{
		UE_LOG("[FbxStaticMeshImporter] No static mesh data extracted: %s", SourcePath.c_str());
		DestroyFbxSdkContext(Context);
		return EFbxStaticMeshImportResult::Failed;
	}

	bool bRegisteredAny = false;
	bool bHadFailure = false;
	for (FFbxStaticMeshBuild& MeshBuild : Context.Meshes)
	{
		TMap<FString, FString> MaterialAssetPaths;
		EnsureMaterialAssets(ResourceManager, Context, MeshBuild, MaterialAssetPaths, OutMaterialPaths);

		if (!IsStaticMeshAssetValid(SourcePath, MeshBuild.AssetPath))
		{
			MeshBuild.Mesh.PathFileName = MeshBuild.AssetPath;
			for (FStaticMeshMaterialSlot& Slot : MeshBuild.Mesh.Slots)
			{
				auto It = MaterialAssetPaths.find(Slot.SlotName);
				Slot.MaterialAssetPath = (It != MaterialAssetPaths.end()) ? It->second : FString("DefaultWhite");
			}

			const bool bSaveAssetOk = BinarySerializer.SaveStaticMesh(MeshBuild.AssetPath, SourcePath, MeshBuild.Mesh);
			UE_LOG("[FbxStaticMeshImporter] Source=%s | Node=%s | Asset=%s | AssetSave=%s",
				SourcePath.c_str(),
				MeshBuild.MeshNodeName.c_str(),
				MeshBuild.AssetPath.c_str(),
				bSaveAssetOk ? "OK" : "FAIL");

			if (!bSaveAssetOk)
			{
				bHadFailure = true;
				continue;
			}
		}

		const bool bRegistered = ResourceManager.RegisterStaticMeshAsset(MeshBuild.AssetPath);
		bRegisteredAny |= bRegistered;
		bHadFailure |= !bRegistered;
	}

	const double EndTime = FPlatformTime::Seconds();
	UE_LOG("[FbxStaticMeshImporter] Import %s | Source=%s | Meshes=%d | Sec=%.3f",
		bRegisteredAny ? (bHadFailure ? "PARTIAL" : "OK") : "FAILED",
		SourcePath.c_str(),
		static_cast<int32>(Context.Meshes.size()),
		EndTime - StartTime);

	DestroyFbxSdkContext(Context);
	return bRegisteredAny ? EFbxStaticMeshImportResult::ImportedStaticMesh : EFbxStaticMeshImportResult::Failed;
}

bool FFbxStaticMeshImporter::IsStaticMeshAssetValid(const FString& SourcePath, const FString& AssetPath) const
{
	FStaticMeshBinaryHeader Header;
	if (!BinarySerializer.ReadStaticMeshHeader(AssetPath, Header))
	{
		return false;
	}

	const uint64 SourceWriteTime = GetFileWriteTimeTicks(SourcePath);
	if (SourceWriteTime == 0 || Header.SourceFileWriteTime != SourceWriteTime)
	{
		return false;
	}

	const uint64 SourceHash = HashFileFNV1a(SourcePath);
	if (SourceHash == 0 || Header.SourceFileHash != SourceHash)
	{
		return false;
	}

	return true;
}
