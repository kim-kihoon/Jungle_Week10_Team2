#include "FbxLoader.h"
#include "SkeletalMesh.h"

USkeletalMesh* FFbxLoader::Load(const FString& Path)
{
	return nullptr;
}

bool FFbxLoader::SupportsExtension(const FString& Extension) const
{
	return Extension.ToLower() == "fbx";
}

FString FFbxLoader::GetLoaderName() const
{
	return FString("FFbxLoader");
}
