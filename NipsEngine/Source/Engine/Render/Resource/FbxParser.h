#pragma once
#include <string>

#include "fbxsdk.h"
#include "Core/CoreTypes.h"
#include "Core/Containers/Array.h"
#include "Core/Containers/Map.h"
#include "Core/Containers/String.h"
#include "Core/Containers/String.h"

struct FSkeletalMesh;
class UMaterialInterface;

class FbxParser
{
public:
	static FSkeletalMesh* ParseFbx(const std::string& FilePath);

private:
	static bool TextureFileExists(const FString& TexturePath);
	static FString FindExistingTexturePath(const TArray<FString>& Candidates);
	static FString ResolveFbxTexturePath(const FString& FbxFilePath, const char* TextureFilePath);
	static FString GetTexturePathFromProperty(const FString& FbxFilePath, FbxSurfaceMaterial* Material, const char* PropertyName);
	static FString BuildTextureStemFromMaterialName(const FString& MaterialName, const char* TextureSuffix);
	static FString FindAssetTextureByMaterialName(const FString& MaterialName, const char* TextureSuffix);
	static FString GetDiffuseTexturePath(const FString& FbxFilePath, FbxSurfaceMaterial* Material);
	static FString GetNormalTexturePath(const FString& FbxFilePath, FbxSurfaceMaterial* Material);
	static UMaterialInterface* GetOrCreateFbxMaterial(const FString& FbxFilePath, FbxSurfaceMaterial* FbxMaterial);

	static void ProcessNode(FbxNode* Node, FSkeletalMesh* OutMesh, TMap<std::string, int32>& BoneMap);
	static void ProcessMesh(FbxNode* Node, FbxMesh* Mesh, FSkeletalMesh* OutMesh, TMap<std::string, int32>& BoneMap);
	static void BuildBoneHierarchy(FbxScene* Scene, FSkeletalMesh* OutMesh, const TMap<std::string, int32>& BoneMap);
};
