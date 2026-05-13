#pragma once

#include "Asset/SkeletalMeshTypes.h"

class FFbxSkeletalMeshExtractor
{
public:
	FFbxSkeletalMeshExtractor() = default;
	~FFbxSkeletalMeshExtractor() = default;

	bool Extract(const FString& Path, FSkeletalMesh& OutMesh);
};
