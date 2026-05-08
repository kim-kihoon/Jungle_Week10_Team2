#include "FbxParser.h"
#include "fbxsdk.h"
#include "Asset/DynamicMeshTypes.h"
#include "Core/Logger.h"

namespace
{
	FMatrix ToMatrix(const FbxAMatrix& InMatrix)
	{
		FMatrix OutMatrix;
		for (int32 Row = 0; Row < 4; ++Row)
		{
			for (int32 Col = 0; Col < 4; ++Col)
			{
				OutMatrix.M[Row][Col] = static_cast<float>(InMatrix.Get(Row, Col));
			}
		}
		return OutMatrix;
	}
}

FDynamicMesh* FbxParser::ParseFbx(const std::string& FilePath)
{
	FDynamicMesh* OutMesh = new FDynamicMesh();
	OutMesh->PathFileName = FilePath.c_str();

	UE_LOG("Fbx Parser파싱 시작: %s", FilePath.c_str());

	// FbxSDK 내장 기능
	FbxManager* SdkManager = FbxManager::Create();
	FbxIOSettings* ios = FbxIOSettings::Create(SdkManager, IOSROOT);
	SdkManager->SetIOSettings(ios);

	FbxImporter* Importer = FbxImporter::Create(SdkManager, "");
	if (!Importer->Initialize(FilePath.c_str(), -1, SdkManager->GetIOSettings()))
	{
		UE_LOG("Fbx Parser  로드 실패 %s", Importer->GetStatus().GetErrorString());
		SdkManager->Destroy();
		return OutMesh;
	}

	FbxScene* Scene = FbxScene::Create(SdkManager, "myScene");
	Importer->Import(Scene);
	Importer->Destroy();

	FbxGeometryConverter GeometryConverter(SdkManager);
	GeometryConverter.Triangulate(Scene, true);

	TMap<std::string, int32> BoneMap;

	ProcessNode(Scene->GetRootNode(), OutMesh, BoneMap);
	BuildBoneHierarchy(Scene, OutMesh, BoneMap);
	SdkManager->Destroy();
	OutMesh->NormalizeVertexWeights();
	OutMesh->EnsureReferencePoseMatrices();
	OutMesh->CacheBounds();

	UE_LOG("Fbx Parser ========= [Fbx SDK 파싱 결과] =========");
	UE_LOG("Fbx Parser 정점 총합: %s", std::to_string(OutMesh->Vertices.size()).c_str());
	UE_LOG("Fbx Parser 인덱스 총합: %s", std::to_string(OutMesh->Indices.size()).c_str());
	UE_LOG("Fbx Parser 섹션(메시 파츠) 개수: %s", std::to_string(OutMesh->Sections.size()).c_str());
	UE_LOG("Fbx Parser 고유 뼈대 개수: %s", std::to_string(OutMesh->Bones.size()).c_str());
	UE_LOG("Fbx Parser =======================================");

	return OutMesh;
}

void FbxParser::ProcessNode(FbxNode* Node, FDynamicMesh* OutMesh, TMap<std::string, int32>& BoneMap)
{
	if (!Node) return;

	FbxNodeAttribute* Attribute = Node->GetNodeAttribute();
	if (Attribute && Attribute->GetAttributeType() == FbxNodeAttribute::eMesh)
	{
		FbxMesh* Mesh = Node->GetMesh();
		if (Mesh)
		{
			ProcessMesh(Node, Mesh, OutMesh, BoneMap);
		}
	}

	for (int32 i = 0; i < Node->GetChildCount(); i++)
	{
		ProcessNode(Node->GetChild(i), OutMesh, BoneMap);
	}
}

void FbxParser::ProcessMesh(FbxNode* Node, FbxMesh* Mesh, FDynamicMesh* OutMesh, TMap<FString, int32>& BoneMap)
{
	int32 VertexOffset = static_cast<int32>(OutMesh->Vertices.size());
	int32 IndexOffset = static_cast<int32>(OutMesh->Indices.size());

	int32 ControlPointCount = Mesh->GetControlPointsCount();
	struct FTempWeight
	{
		int32 BoneIndices[4] = { 0, 0, 0, 0 };
		float BoneWeights[4] = { 0.0f, 0.0f, 0.0f ,0.0f };
		int32 Count = 0;
		void Add(int32 Index, float Weight)
		{
			if (Count < 4)
			{
				BoneIndices[Count] = Index;
				BoneWeights[Count] = Weight;
				Count++;
			}
		}
	};

	TArray<FTempWeight> CntrlPtWeights(ControlPointCount);

	int32 SkinCount = Mesh->GetDeformerCount(FbxDeformer::eSkin);
	if (SkinCount > 0)
	{
		FbxSkin* Skin = static_cast<FbxSkin*>(Mesh->GetDeformer(0, FbxDeformer::eSkin));
		int32 ClusterCount = Skin->GetClusterCount();

		for (int32 c = 0; c < ClusterCount; c++)
		{
			FbxCluster* Cluster = Skin->GetCluster(c);
			FbxNode* BoneNode = Cluster->GetLink();
			if (!BoneNode) continue;

			FString BoneName = BoneNode->GetName();
			int32 BoneIndex = -1;

			if (!BoneMap.contains(BoneName))
			{
				BoneIndex = static_cast<int32>(OutMesh->Bones.size());
				BoneMap[BoneName] = BoneIndex;

				FSkeletalBone NewBone;
				NewBone.Name = FString(BoneName);
				NewBone.ParentIndex = -1;

				FbxAMatrix TransformMatrix, TransformLinkMatrix;
				Cluster->GetTransformMatrix(TransformMatrix);
				Cluster->GetTransformLinkMatrix(TransformLinkMatrix);
				FbxAMatrix GlobalBindPoseInverse = TransformLinkMatrix.Inverse() * TransformMatrix;

				NewBone.InverseBindPose = ToMatrix(GlobalBindPoseInverse);
				NewBone.RefGlobalMatrix = ToMatrix(TransformLinkMatrix);
				NewBone.RefLocalTransform = FTransform(ToMatrix(BoneNode->EvaluateLocalTransform()));

				OutMesh->Bones.push_back(NewBone);
			}
			else
			{
				BoneIndex = BoneMap[BoneName];
			}

			int32 IndexCount = Cluster->GetControlPointIndicesCount();
			int32* Indices = Cluster->GetControlPointIndices();
			double* Weights = Cluster->GetControlPointWeights();

			for (int32 i = 0; i < IndexCount; i++)
			{
				CntrlPtWeights[Indices[i]].Add(BoneIndex, (float)Weights[i]);
			}
		}
	}

	FbxVector4* ControlPoints = Mesh->GetControlPoints();
	FbxGeometryElementNormal* NormalElement = Mesh->GetElementNormal(0);
	FbxGeometryElementUV* UVElement = Mesh->GetElementUV(0);

	FbxAMatrix MeshGlobalTransform = Node->EvaluateGlobalTransform();

	int32 PolygonCount = Mesh->GetPolygonCount();
	int32 VertexCounter = 0;

	for (int32 p = 0; p < PolygonCount; p++)
	{
		for (int32 v = 0; v < 3; v++)
		{
			int32 CtrlPointIndex = Mesh->GetPolygonVertex(p, v);
			FDynamicMeshVertex NewVert;

			// Position
			FbxVector4 Pos = ControlPoints[CtrlPointIndex];
			Pos = MeshGlobalTransform.MultT(Pos);
			NewVert.Position = FVector(static_cast<float>(Pos[0]), static_cast<float>(Pos[1]), static_cast<float>(Pos[2]));

			// Normal
			if (NormalElement)
			{
				int32 NormalIndex = (NormalElement->GetMappingMode() == FbxGeometryElement::eByPolygonVertex) ? VertexCounter : CtrlPointIndex;
				int32 DirectIndex = (NormalElement->GetReferenceMode() == FbxGeometryElement::eDirect) ? NormalIndex : NormalElement->GetIndexArray().GetAt(NormalIndex);
				FbxVector4 Normal = NormalElement->GetDirectArray().GetAt(DirectIndex);

				Normal = MeshGlobalTransform.MultR(Normal);
				Normal.Normalize();
				NewVert.Normal = FVector(static_cast<float>(Normal[0]), static_cast<float>(Normal[1]), static_cast<float>(Normal[2]));
			}

			// UV
			if (UVElement)
			{
				int32 UVIndex = Mesh->GetTextureUVIndex(p, v);
				if (UVIndex >= 0)
				{
					int32 DirectIndex = (UVElement->GetReferenceMode() == FbxGeometryElement::eDirect) ? UVIndex : UVElement->GetIndexArray().GetAt(UVIndex);
					FbxVector2 UV = UVElement->GetDirectArray().GetAt(DirectIndex);
					NewVert.UV = FVector2(static_cast<float>(UV[0]), 1.0f - static_cast<float>(UV[1]));
				}
			}
			NewVert.Color = FVector4(1, 1, 1, 1);
			NewVert.Tangent = FVector4(1, 0, 0, 1);

			// 가중치
			for (int w = 0; w < 4; w++)
			{
				NewVert.BoneIndices[w] = CntrlPtWeights[CtrlPointIndex].BoneIndices[w];
				NewVert.BoneWeights[w] = CntrlPtWeights[CtrlPointIndex].BoneWeights[w];
			}

			OutMesh->Vertices.push_back(NewVert);

			OutMesh->Indices.push_back(VertexOffset + VertexCounter);
			VertexCounter++;
		}
	}

	FString MaterialSlotName = "DefaultWhite";
	if (Node->GetMaterialCount() > 0)
	{
		FbxSurfaceMaterial* Material = Node->GetMaterial(0);
		if (Material)
		{
			MaterialSlotName = Material->GetName();
		}
	}

	int32 MaterialSlotIndex = -1;
	for (int32 SlotIndex = 0; SlotIndex < static_cast<int32>(OutMesh->Slots.size()); ++SlotIndex)
	{
		if (OutMesh->Slots[SlotIndex].SlotName == MaterialSlotName)
		{
			MaterialSlotIndex = SlotIndex;
			break;
		}
	}

	if (MaterialSlotIndex < 0)
	{
		FDynamicMeshMaterialSlot NewSlot;
		NewSlot.SlotName = MaterialSlotName;
		OutMesh->Slots.push_back(NewSlot);
		MaterialSlotIndex = static_cast<int32>(OutMesh->Slots.size()) - 1;
	}

	FDynamicMeshSection NewSection;
	NewSection.MaterialIndex = MaterialSlotIndex;
	NewSection.FirstIndex = IndexOffset;
	NewSection.NumTriangles = VertexCounter / 3;
	NewSection.MaterialSlotName = MaterialSlotName;

	OutMesh->Sections.push_back(NewSection);
}

void FbxParser::BuildBoneHierarchy(FbxScene* Scene, FDynamicMesh* OutMesh, const TMap<std::string, int32>& BoneMap)
{
	for (int32 i = 0; i < static_cast<int32>(OutMesh->Bones.size()); i++)
	{
		FbxNode* BoneNode = Scene->FindNodeByName(OutMesh->Bones[i].Name.c_str());
		if (BoneNode && BoneNode->GetParent())
		{
			FString ParentName = BoneNode->GetParent()->GetName();

			auto it = BoneMap.find(ParentName);
			if (it != BoneMap.end())
			{
				OutMesh->Bones[i].ParentIndex = it->second;
			}
		}
	}
}
