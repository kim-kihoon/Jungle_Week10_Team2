#pragma once
#include <string>

#include "fbxsdk.h"
#include "Core/CoreTypes.h"
#include "Core/Containers/Map.h"

struct FDynamicMesh;

class FbxParser
{
public:
	static FDynamicMesh* ParseFbx(const std::string& FilePath);

private:
	static void ProcessNode(FbxNode* Node, FDynamicMesh* OutMesh, TMap<std::string, int32>& BoneMap);
	static void ProcessMesh(FbxNode* Node, FbxMesh* Mesh, FDynamicMesh* OutMesh, TMap<std::string, int32>& BoneMap);
	static void BuildBoneHierarchy(FbxScene* Scene, FDynamicMesh* OutMesh, const TMap<std::string, int32>& BoneMap);
};
