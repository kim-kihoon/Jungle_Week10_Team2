#pragma once

#include "Asset/BinarySerializer.h"

class FResourceManager;

enum class EFbxSkeletalMeshImportResult
{
	NotFbx,
	ImportedSkeletalMesh,
	SkippedNonSkeletal,
	Failed,
};

class FFbxSkeletalMeshImporter
{
public:
	FFbxSkeletalMeshImporter() = default;
	~FFbxSkeletalMeshImporter() = default;

	EFbxSkeletalMeshImportResult ImportIfSkeletalMeshNeeded(
		FResourceManager& ResourceManager,
		const FString& SourcePath,
		TArray<FString>* OutMaterialPaths = nullptr);

private:
	bool IsSkeletalMeshAssetValid(const FString& SourcePath, const FString& AssetPath) const;
	bool IsSkeletonAssetValid(const FString& SourcePath, const FString& AssetPath) const;

private:
	FBinarySerializer BinarySerializer;
};
