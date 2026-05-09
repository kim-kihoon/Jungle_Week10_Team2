#pragma once

#include "Asset/IAssetLoader.h"

class USkeletalMesh;

class FFbxLoader : public IAssetLoader
{
public:
	FFbxLoader() = default;
	~FFbxLoader() override = default;

	USkeletalMesh* Load(const FString& Path);

	// 어떤 확장자를 지원하는지 반환하는 함수. 대문자 소문자 이런 것도 비교해야 함.
	bool SupportsExtension(const FString& Extension) const override;
	FString GetLoaderName() const override;
};
