#pragma once

#include "Asset/SkeletalMeshTypes.h"
#include "Core/CoreTypes.h"

class USkeletalMesh;

class FFbxImporter
{
public:
    USkeletalMesh* ImportSkeletalMesh(const FString& Path, const FSkeletalMeshImportOptions& ImportOptions = {});
};
