#pragma once
#include <string>

#include "fbxsdk.h"
#include "Core/CoreTypes.h"
#include "Core/Containers/Map.h"

struct FSkeletalMesh;

class FbxParser
{
public:
	static FSkeletalMesh* ParseFbx(const std::string& FilePath);

private:
	static void ProcessNode(FbxNode* Node, FSkeletalMesh* OutMesh, TMap<std::string, int32>& BoneMap);
	static void ProcessMesh(FbxNode* Node, FbxMesh* Mesh, FSkeletalMesh* OutMesh, TMap<std::string, int32>& BoneMap);
	static void BuildBoneHierarchy(FbxScene* Scene, FSkeletalMesh* OutMesh, const TMap<std::string, int32>& BoneMap);
};
