#include "FbxLoader.h"
#include "SkeletalMesh.h"

#include <string>

namespace
{
	// StringUtils에 이런 함수들이 있어야 할 텐데 현재 obj importer 쪽에 네임스페이스로 존재.
	//따라서 리팩토링하며 해당 유틸들을 나중에 빼놔야 함. 현재는 개인 브랜치이므로 구조에 큰 변경을 주지 않고자 냅둠.
	FString ToLowerAscii(FString Value)
	{
		std::transform(Value.begin(), Value.end(), Value.begin(), [](unsigned char CharValue)
					   { return static_cast<char>(std::tolower(CharValue)); });
		return Value;
	}
}

USkeletalMesh* FFbxLoader::Load(const FString& Path)
{
	return nullptr;
}

bool FFbxLoader::SupportsExtension(const FString& Extension) const
{
    return ToLowerAscii(Extension) == "fbx";
}

FString FFbxLoader::GetLoaderName() const
{
	return FString("FFbxLoader");
}
