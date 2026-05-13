#pragma once

#include "Asset/IAssetLoader.h"

class USkeletalMesh;

class FFbxLoader : public IAssetLoader
{
public:
	FFbxLoader() = default;
	~FFbxLoader() override = default;

	USkeletalMesh* Load(const FString& Path);

	bool SupportsExtension(const FString& Extension) const override;
};
