#include "Editor/Importer/FbxSkeletalMeshImporter.h"

#include "Asset/SkeletalMeshTypes.h"
#include "Asset/Skeleton.h"
#include "Core/Logger.h"
#include "Core/Paths.h"
#include "Core/PlatformTime.h"
#include "Core/ResourceManager.h"
#include "Editor/Importer/FbxSkeletalMeshExtractor.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>

namespace
{
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

	FString MakeSkeletalMeshMaterialAssetPath(const FString& SourcePath, const FString& MeshAssetPath, const FString& SlotName)
	{
		namespace fs = std::filesystem;

		const fs::path MeshAssetFsPath(FPaths::ToWide(FPaths::Normalize(MeshAssetPath)));
		const std::wstring MaterialFileName =
			MeshAssetFsPath.stem().wstring() + L"_" + FPaths::ToWide(SanitizeAssetFileToken(SlotName)) + L".mat";
		const fs::path MaterialPath = fs::path(FPaths::ToWide(FPaths::Normalize(SourcePath))).parent_path() / MaterialFileName;

		return FPaths::ToUtf8(MaterialPath.lexically_normal().generic_wstring());
	}

	bool ResolveMaterialAssetForSlot(
		FResourceManager& ResourceManager,
		const FString& SourcePath,
		const FString& MeshAssetPath,
		FSkeletalMeshMaterialSlot& Slot,
		TArray<FString>* OutMaterialPaths)
	{
		namespace fs = std::filesystem;

		const FString SlotName = Slot.SlotName.empty() ? FString("DefaultWhite") : Slot.SlotName;
		const FString MaterialPath = MakeSkeletalMeshMaterialAssetPath(SourcePath, MeshAssetPath, SlotName);
		fs::path AbsoluteMaterialPath(FPaths::ToAbsolute(FPaths::ToWide(MaterialPath)));
		fs::path AbsoluteMaterialInstancePath = AbsoluteMaterialPath;
		AbsoluteMaterialInstancePath.replace_extension(L".matinst");

		if (!fs::exists(AbsoluteMaterialPath) && fs::exists(AbsoluteMaterialInstancePath))
		{
			Slot.MaterialAssetPath = FPaths::ToUtf8(
				fs::path(FPaths::ToWide(MaterialPath)).replace_extension(L".matinst").generic_wstring());
			return true;
		}

		Slot.MaterialAssetPath = MaterialPath;
		if (!fs::exists(AbsoluteMaterialPath))
		{
			const UMaterial* SourceMaterial = Cast<UMaterial>(Slot.Material);
			if (SourceMaterial == nullptr)
			{
				SourceMaterial = ResourceManager.GetMaterial("DefaultWhite");
			}

			if (SourceMaterial == nullptr ||
				!ResourceManager.SerializeMaterial(MaterialPath, SourceMaterial))
			{
				Slot.MaterialAssetPath = "DefaultWhite";
				UE_LOG("[FbxSkeletalMeshImporter] Failed to serialize material asset: %s", MaterialPath.c_str());
				return false;
			}

			UE_LOG("[FbxSkeletalMeshImporter] Created material asset: %s", MaterialPath.c_str());
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
		const FString& SourcePath,
		const FString& MeshAssetPath,
		FSkeletalMesh& MeshData,
		TArray<FString>* OutMaterialPaths)
	{
		bool bAllOk = true;
		for (FSkeletalMeshMaterialSlot& Slot : MeshData.MaterialSlots)
		{
			bAllOk &= ResolveMaterialAssetForSlot(ResourceManager, SourcePath, MeshAssetPath, Slot, OutMaterialPaths);
			Slot.Material = nullptr;
		}

		return bAllOk;
	}
}

EFbxSkeletalMeshImportResult FFbxSkeletalMeshImporter::ImportIfSkeletalMeshNeeded(
	FResourceManager& ResourceManager,
	const FString& SourcePath,
	TArray<FString>* OutMaterialPaths)
{
	if (!IsFbxSourcePath(SourcePath))
	{
		return EFbxSkeletalMeshImportResult::NotFbx;
	}

	const double StartTime = FPlatformTime::Seconds();

	FFbxSkeletalMeshExtractor Extractor;
	FSkeletalMesh ParsedMeshData;
	if (!Extractor.Extract(SourcePath, ParsedMeshData))
	{
		UE_LOG("[FbxSkeletalMeshImporter] Skipped non-skeletal FBX: %s", SourcePath.c_str());
		return EFbxSkeletalMeshImportResult::SkippedNonSkeletal;
	}

	if (ParsedMeshData.Vertices.empty() ||
		ParsedMeshData.Indices.empty() ||
		ParsedMeshData.Bones.empty() ||
		ParsedMeshData.InverseBindPoseMatrices.size() != ParsedMeshData.Bones.size())
	{
		UE_LOG("[FbxSkeletalMeshImporter] Invalid skeletal data extracted: %s", SourcePath.c_str());
		return EFbxSkeletalMeshImportResult::Failed;
	}

	const FString SkeletalMeshAssetPath = ResourceManager.MakeSkeletalMeshAssetPath(SourcePath);
	const FString SkeletonAssetPath = ResourceManager.MakeSkeletonAssetPath(SourcePath);

	FSkeleton SkeletonData;
	SkeletonData.PathFileName = SkeletonAssetPath;
	SkeletonData.Bones = ParsedMeshData.Bones;
	SkeletonData.InverseBindPoseMatrices = ParsedMeshData.InverseBindPoseMatrices;

	FSkeletalMesh MeshData = ParsedMeshData;
	MeshData.PathFileName = SkeletalMeshAssetPath;
	MeshData.SkeletonAssetPath = SkeletonAssetPath;
	MeshData.Bones.clear();
	MeshData.InverseBindPoseMatrices.clear();

	bool bHadFailure = false;
	bHadFailure |= !EnsureMaterialAssets(ResourceManager, SourcePath, SkeletalMeshAssetPath, MeshData, OutMaterialPaths);
	if (!IsSkeletonAssetValid(SourcePath, SkeletonAssetPath))
	{
		const bool bSaveSkeletonOk = BinarySerializer.SaveSkeleton(SkeletonAssetPath, SourcePath, SkeletonData);
		UE_LOG("[FbxSkeletalMeshImporter] Skeleton Source=%s | Asset=%s | AssetSave=%s",
			SourcePath.c_str(),
			SkeletonAssetPath.c_str(),
			bSaveSkeletonOk ? "OK" : "FAIL");

		if (!bSaveSkeletonOk)
		{
			bHadFailure = true;
		}
	}

	if (!IsSkeletalMeshAssetValid(SourcePath, SkeletalMeshAssetPath))
	{
		const bool bSaveMeshOk = BinarySerializer.SaveSkeletalMesh(SkeletalMeshAssetPath, SourcePath, MeshData);
		UE_LOG("[FbxSkeletalMeshImporter] Mesh Source=%s | Asset=%s | Skeleton=%s | AssetSave=%s",
			SourcePath.c_str(),
			SkeletalMeshAssetPath.c_str(),
			SkeletonAssetPath.c_str(),
			bSaveMeshOk ? "OK" : "FAIL");

		if (!bSaveMeshOk)
		{
			bHadFailure = true;
		}
	}

	const bool bSkeletonRegistered = ResourceManager.RegisterSkeletonAsset(SkeletonAssetPath);
	const bool bMeshRegistered = ResourceManager.RegisterSkeletalMeshAsset(SkeletalMeshAssetPath);
	bHadFailure |= !bSkeletonRegistered || !bMeshRegistered;

	const double EndTime = FPlatformTime::Seconds();
	UE_LOG("[FbxSkeletalMeshImporter] Import %s | Source=%s | Mesh=%s | Skeleton=%s | Sec=%.3f",
		(!bHadFailure && bMeshRegistered) ? "OK" : "FAILED",
		SourcePath.c_str(),
		SkeletalMeshAssetPath.c_str(),
		SkeletonAssetPath.c_str(),
		EndTime - StartTime);

	return (!bHadFailure && bMeshRegistered) ?
		EFbxSkeletalMeshImportResult::ImportedSkeletalMesh :
		EFbxSkeletalMeshImportResult::Failed;
}

bool FFbxSkeletalMeshImporter::IsSkeletalMeshAssetValid(const FString& SourcePath, const FString& AssetPath) const
{
	FStaticMeshBinaryHeader Header;
	if (!BinarySerializer.ReadSkeletalMeshHeader(AssetPath, Header))
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

bool FFbxSkeletalMeshImporter::IsSkeletonAssetValid(const FString& SourcePath, const FString& AssetPath) const
{
	FStaticMeshBinaryHeader Header;
	if (!BinarySerializer.ReadSkeletonHeader(AssetPath, Header))
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
