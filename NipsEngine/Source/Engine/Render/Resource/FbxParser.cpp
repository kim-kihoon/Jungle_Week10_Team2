#include "FbxParser.h"
#include "fbxsdk.h"
#include "Asset/SkeletalMeshTypes.h"
#include "Core/Logger.h"
#include <vector>
#include <algorithm>
#include <map>

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

	int32 GetLayerElementIndex(FbxLayerElement::EMappingMode MappingMode, FbxLayerElement::EReferenceMode ReferenceMode,
		const FbxLayerElementArrayTemplate<int>& IndexArray, int32 ControlPointIndex, int32 PolygonVertexIndex)
	{
		int32 MappingIndex = 0;
		if (MappingMode == FbxLayerElement::eByControlPoint)
		{
			MappingIndex = ControlPointIndex;
		}
		else if (MappingMode == FbxLayerElement::eByPolygonVertex)
		{
			MappingIndex = PolygonVertexIndex;
		}
		else
		{
			return -1;
		}

		if (ReferenceMode == FbxLayerElement::eDirect)
		{
			return MappingIndex;
		}

		if (MappingIndex >= 0 && MappingIndex < IndexArray.GetCount())
		{
			return IndexArray.GetAt(MappingIndex);
		}

		return -1;
	}

	int32 GetMaterialIndex(FbxGeometryElementMaterial* MaterialElement, int32 PolygonIndex)
	{
		if (!MaterialElement)
		{
			return 0;
		}

		int32 MappingIndex = 0;
		if (MaterialElement->GetMappingMode() == FbxGeometryElement::eByPolygon)
		{
			MappingIndex = PolygonIndex;
		}
		else if (MaterialElement->GetMappingMode() != FbxGeometryElement::eAllSame)
		{
			return 0;
		}

		if (MaterialElement->GetReferenceMode() == FbxGeometryElement::eDirect)
		{
			return MappingIndex;
		}

		if (MappingIndex >= 0 && MappingIndex < MaterialElement->GetIndexArray().GetCount())
		{
			return MaterialElement->GetIndexArray().GetAt(MappingIndex);
		}

		return 0;
	}

}

FSkeletalMesh* FbxParser::ParseFbx(const std::string& FilePath)
{
	FSkeletalMesh* OutMesh = new FSkeletalMesh();
	OutMesh->PathFileName = FilePath.c_str();

	UE_LOG("Fbx Parser 파싱 시작: %s", FilePath.c_str());

	FbxManager* SdkManager = FbxManager::Create();
	FbxIOSettings* ios = FbxIOSettings::Create(SdkManager, IOSROOT);
	SdkManager->SetIOSettings(ios);

	FbxImporter* Importer = FbxImporter::Create(SdkManager, "");
	if (!Importer->Initialize(FilePath.c_str(), -1, SdkManager->GetIOSettings()))
	{
		UE_LOG("Fbx Parser 로드 실패 사유: %s", Importer->GetStatus().GetErrorString());
		SdkManager->Destroy();
		return OutMesh;
	}

	FbxScene* Scene = FbxScene::Create(SdkManager, "myScene");
	Importer->Import(Scene);
	Importer->Destroy();

	// 다각형 삼각형화
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
	UE_LOG("Fbx Parser 정점 총합: %zu", OutMesh->Vertices.size());
	UE_LOG("Fbx Parser 인덱스 총합: %zu", OutMesh->Indices.size());
	UE_LOG("Fbx Parser 섹션 개수: %zu", OutMesh->Sections.size());
	UE_LOG("Fbx Parser 고유 뼈대 개수: %zu", OutMesh->Bones.size());
	UE_LOG("Fbx Parser =======================================");

	return OutMesh;
}

void FbxParser::ProcessNode(FbxNode* Node, FSkeletalMesh* OutMesh, TMap<std::string, int32>& BoneMap)
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

void FbxParser::ProcessMesh(FbxNode* Node, FbxMesh* Mesh, FSkeletalMesh* OutMesh, TMap<std::string, int32>& BoneMap)
{
	int32 VertexOffset = static_cast<int32>(OutMesh->Vertices.size());

	Mesh->GenerateTangentsDataForAllUVSets();

	int32 ControlPointCount = Mesh->GetControlPointsCount();
	struct FTempWeight
	{
		TArray<std::pair<int32, float>> Weights;

		void Add(int32 Index, float Weight) { Weights.push_back({ Index, Weight }); }

		void GetTop4(uint32 OutIndices[4], float OutWeights[4])
		{
			std::sort(Weights.begin(), Weights.end(), [](const auto& a, const auto& b) {
				return a.second > b.second;
				});

			float TotalWeight = 0.0f;
			int32 Count = std::min(4, static_cast<int32>(Weights.size()));

			for (int32 i = 0; i < Count; ++i) TotalWeight += Weights[i].second;

			for (int32 i = 0; i < 4; ++i)
			{
				if (i < Count && TotalWeight > 0.0001f)
				{
					OutIndices[i] = static_cast<uint32>(Weights[i].first);
					OutWeights[i] = Weights[i].second / TotalWeight;
				}
				else
				{
					OutIndices[i] = 0;
					OutWeights[i] = 0.0f;
				}
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

			std::string BoneName = BoneNode->GetName();
			int32 BoneIndex = -1;

			if (BoneMap.find(BoneName) == BoneMap.end())
			{
				BoneIndex = static_cast<int32>(OutMesh->Bones.size());
				BoneMap[BoneName] = BoneIndex;

				FSkeletalBone NewBone;
				NewBone.Name = BoneName;
				NewBone.ParentIndex = -1;

				FbxAMatrix TransformMatrix, TransformLinkMatrix;
				Cluster->GetTransformMatrix(TransformMatrix);
				Cluster->GetTransformLinkMatrix(TransformLinkMatrix);

				FMatrix RefGlobalMatrix = ToMatrix(TransformLinkMatrix * TransformMatrix.Inverse());
				NewBone.RefGlobalMatrix = RefGlobalMatrix;
				NewBone.InverseBindPose = RefGlobalMatrix.GetInverse();

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
				if (Indices[i] >= 0 && Indices[i] < ControlPointCount)
				{
					CntrlPtWeights[Indices[i]].Add(BoneIndex, static_cast<float>(Weights[i]));
				}
			}
		}
	}

	FbxVector4* ControlPoints = Mesh->GetControlPoints();
	FbxGeometryElementNormal* NormalElement = Mesh->GetElementNormal(0);
	FbxGeometryElementTangent* TangentElement = Mesh->GetElementTangent(0);
	FbxGeometryElementUV* UVElement = Mesh->GetElementUV(0);
	FbxGeometryElementMaterial* MaterialElement = Mesh->GetElementMaterial(0);

	int32 PolygonCount = Mesh->GetPolygonCount();
	TMap<int32, TArray<int32>> MaterialToIndices;
	int32 VertexCounter = 0;

	for (int32 p = 0; p < PolygonCount; p++)
	{
		int32 PolyMaterialIndex = GetMaterialIndex(MaterialElement, p);

		for (int32 v = 0; v < 3; v++)
		{
			int32 CtrlPointIndex = Mesh->GetPolygonVertex(p, v);
			if (CtrlPointIndex < 0 || CtrlPointIndex >= ControlPointCount)
			{
				continue;
			}

			FSkeletalMeshVertex NewVert;

			FbxVector4 Pos = ControlPoints[CtrlPointIndex];
			NewVert.Position = FVector(static_cast<float>(Pos[0]), static_cast<float>(Pos[1]), static_cast<float>(Pos[2]));

			if (NormalElement)
			{
				int32 DirectIndex = GetLayerElementIndex(NormalElement->GetMappingMode(), NormalElement->GetReferenceMode(),
					NormalElement->GetIndexArray(), CtrlPointIndex, VertexCounter);

				if (DirectIndex >= 0 && DirectIndex < NormalElement->GetDirectArray().GetCount())
				{
					FbxVector4 Normal = NormalElement->GetDirectArray().GetAt(DirectIndex);
					Normal.Normalize();
					NewVert.Normal = FVector(static_cast<float>(Normal[0]), static_cast<float>(Normal[1]), static_cast<float>(Normal[2]));
				}
			}

			if (TangentElement)
			{
				int32 DirectIndex = GetLayerElementIndex(TangentElement->GetMappingMode(), TangentElement->GetReferenceMode(),
					TangentElement->GetIndexArray(), CtrlPointIndex, VertexCounter);

				if (DirectIndex >= 0 && DirectIndex < TangentElement->GetDirectArray().GetCount())
				{
					FbxVector4 Tangent = TangentElement->GetDirectArray().GetAt(DirectIndex);
					Tangent.Normalize();
					NewVert.Tangent = FVector4(static_cast<float>(Tangent[0]), static_cast<float>(Tangent[1]), static_cast<float>(Tangent[2]), static_cast<float>(Tangent[3]));
				}
			}
			else
			{
				NewVert.Tangent = FVector4(1, 0, 0, 1);
			}

			if (UVElement)
			{
				int32 DirectIndex = GetLayerElementIndex(UVElement->GetMappingMode(), UVElement->GetReferenceMode(),
					UVElement->GetIndexArray(), CtrlPointIndex, VertexCounter);

				if (DirectIndex >= 0 && DirectIndex < UVElement->GetDirectArray().GetCount())
				{
					FbxVector2 UV = UVElement->GetDirectArray().GetAt(DirectIndex);
					NewVert.UV = FVector2(static_cast<float>(UV[0]), 1.0f - static_cast<float>(UV[1]));
				}
			}
			NewVert.Color = FVector4(1, 1, 1, 1);

			CntrlPtWeights[CtrlPointIndex].GetTop4(NewVert.BoneIndices, NewVert.BoneWeights);

			OutMesh->Vertices.push_back(NewVert);
			MaterialToIndices[PolyMaterialIndex].push_back(VertexOffset + VertexCounter);
			VertexCounter++;
		}
	}

	for (const auto& MatPair : MaterialToIndices)
	{
		int32 MatIndex = MatPair.first;
		const TArray<int32>& Indices = MatPair.second;

		FString MaterialSlotName = "DefaultWhite";
		if (Node->GetMaterialCount() > MatIndex)
		{
			FbxSurfaceMaterial* Material = Node->GetMaterial(MatIndex);
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
			FSkeletalMeshMaterialSlot NewSlot;
			NewSlot.SlotName = MaterialSlotName;
			OutMesh->Slots.push_back(NewSlot);
			MaterialSlotIndex = static_cast<int32>(OutMesh->Slots.size()) - 1;
		}

		FSkeletalMeshSection NewSection;
		NewSection.MaterialIndex = MaterialSlotIndex;
		NewSection.FirstIndex = static_cast<int32>(OutMesh->Indices.size());
		NewSection.NumTriangles = static_cast<int32>(Indices.size()) / 3;
		NewSection.MaterialSlotName = MaterialSlotName;

		OutMesh->Indices.insert(OutMesh->Indices.end(), Indices.begin(), Indices.end());
		OutMesh->Sections.push_back(NewSection);
	}
}

void FbxParser::BuildBoneHierarchy(FbxScene* Scene, FSkeletalMesh* OutMesh, const TMap<std::string, int32>& BoneMap)
{
	for (int32 i = 0; i < static_cast<int32>(OutMesh->Bones.size()); i++)
	{
		FbxNode* BoneNode = Scene->FindNodeByName(OutMesh->Bones[i].Name.c_str());
		if (BoneNode && BoneNode->GetParent())
		{
			std::string ParentName = BoneNode->GetParent()->GetName();
			auto it = BoneMap.find(ParentName);
			if (it != BoneMap.end())
			{
				OutMesh->Bones[i].ParentIndex = it->second;
			}
		}
	}

	TArray<FSkeletalBone> SortedBones;
	TMap<int32, int32> OldToNewIndexMap;
	TArray<bool> Processed(OutMesh->Bones.size(), false);

	while (SortedBones.size() < OutMesh->Bones.size())
	{
		bool bProgress = false;
		for (size_t i = 0; i < OutMesh->Bones.size(); ++i)
		{
			if (!Processed[i])
			{
				int32 ParentIdx = OutMesh->Bones[i].ParentIndex;
				if (ParentIdx == -1 || ParentIdx < 0 || ParentIdx >= static_cast<int32>(OutMesh->Bones.size()) || Processed[ParentIdx])
				{
					OldToNewIndexMap[static_cast<int32>(i)] = static_cast<int32>(SortedBones.size());
					FSkeletalBone SortedBone = OutMesh->Bones[i];
					if (ParentIdx < -1 || ParentIdx >= static_cast<int32>(OutMesh->Bones.size()))
					{
						UE_LOG("Fbx Parser 잘못된 부모 인덱스를 루트로 처리: %s", SortedBone.Name.c_str());
						SortedBone.ParentIndex = -1;
					}
					SortedBones.push_back(SortedBone);
					Processed[i] = true;
					bProgress = true;
				}
			}
		}

		if (!bProgress)
		{
			UE_LOG("Fbx Parser 본 계층 정렬 실패: 순환 참조 또는 누락된 부모가 있어 남은 본을 루트로 처리합니다.");
			for (size_t i = 0; i < OutMesh->Bones.size(); ++i)
			{
				if (!Processed[i])
				{
					OldToNewIndexMap[static_cast<int32>(i)] = static_cast<int32>(SortedBones.size());
					FSkeletalBone SortedBone = OutMesh->Bones[i];
					SortedBone.ParentIndex = -1;
					SortedBones.push_back(SortedBone);
					Processed[i] = true;
				}
			}
		}
	}

	for (auto& Bone : SortedBones)
	{
		if (Bone.ParentIndex != -1)
		{
			auto ParentIt = OldToNewIndexMap.find(Bone.ParentIndex);
			if (ParentIt == OldToNewIndexMap.end())
			{
				Bone.ParentIndex = -1;
				Bone.RefLocalTransform = FTransform(Bone.RefGlobalMatrix);
				continue;
			}

			Bone.ParentIndex = ParentIt->second;

			FMatrix ParentGlobalInv = SortedBones[Bone.ParentIndex].RefGlobalMatrix.GetInverse();
			Bone.RefLocalTransform = FTransform(Bone.RefGlobalMatrix * ParentGlobalInv);
		}
		else
		{
			Bone.RefLocalTransform = FTransform(Bone.RefGlobalMatrix);
		}
	}

	OutMesh->Bones = SortedBones;

	for (auto& Vert : OutMesh->Vertices)
	{
		for (int32 w = 0; w < 4; w++)
		{
			if (OldToNewIndexMap.find(Vert.BoneIndices[w]) != OldToNewIndexMap.end())
			{
				Vert.BoneIndices[w] = OldToNewIndexMap[Vert.BoneIndices[w]];
			}
		}
	}
}
