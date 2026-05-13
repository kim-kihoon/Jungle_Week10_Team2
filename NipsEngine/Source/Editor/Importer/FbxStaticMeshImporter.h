#pragma once

#include "Asset/BinarySerializer.h"
#include "Asset/StaticMeshTypes.h"

class FResourceManager;

enum class EFbxStaticMeshImportResult
{
	NotFbx,
	ImportedStaticMesh,
	SkippedNonStatic,
	Failed,
};

class FFbxStaticMeshImporter
{
public:
	FFbxStaticMeshImporter() = default;
	~FFbxStaticMeshImporter() = default;

	EFbxStaticMeshImportResult ImportIfStaticMeshNeeded(
		FResourceManager& ResourceManager,
		const FString& SourcePath,
		TArray<FString>* OutMaterialPaths = nullptr);

private:
	bool IsStaticMeshAssetValid(const FString& SourcePath, const FString& AssetPath) const;

private:
	FBinarySerializer BinarySerializer;
};
