#include "FbxLoader.h"

#include "Asset/SkeletalMesh.h"
#include "Core/Logger.h"
#include "Core/Paths.h"
#include "Object/ObjectFactory.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <fbxsdk.h>
#include <filesystem>
#include <cstdio>
#include <unordered_map>

namespace
{
struct FControlPointInfluence
{
    int32 BoneIndex = 0;
    float Weight = 0.0f;
};

struct FFbxImportPath
{
    FString ImportPath;
    std::filesystem::path TempPath;

    ~FFbxImportPath()
    {
        if (!TempPath.empty())
        {
            std::error_code ErrorCode;
            std::filesystem::remove(TempPath, ErrorCode);
        }
    }
};

struct FVertexKey
{
    int32 ControlPointIndex = -1;
    int32 NormalIndex = -1;
    int32 UVIndex = -1;
    int32 MaterialSlotIndex = -1;

    bool operator==(const FVertexKey& Other) const
    {
        return ControlPointIndex == Other.ControlPointIndex && NormalIndex == Other.NormalIndex && UVIndex == Other.UVIndex && MaterialSlotIndex == Other.MaterialSlotIndex;
    }
};

struct FVertexKeyHash
{
    size_t operator()(const FVertexKey& Key) const
    {
        size_t Hash = static_cast<size_t>(Key.ControlPointIndex);
        Hash = Hash * 16777619u ^ static_cast<size_t>(Key.NormalIndex);
        Hash = Hash * 16777619u ^ static_cast<size_t>(Key.UVIndex);
        Hash = Hash * 16777619u ^ static_cast<size_t>(Key.MaterialSlotIndex);
        return Hash;
    }
};

FString FbxNameToString(const char* Name)
{
    return Name ? FString(Name) : FString();
}

bool IsAsciiString(const FString& Text)
{
    for (const unsigned char Character : Text)
    {
        if (Character > 0x7f)
        {
            return false;
        }
    }
    return true;
}

std::filesystem::path MakeAsciiTempFbxPath(const FString& SourcePath)
{
    const uint64 TickCount = static_cast<uint64>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());

    std::filesystem::path TempDir = L"C:/Windows/Temp/NipsEngineFbxImport";
    std::error_code ErrorCode;
    std::filesystem::create_directories(TempDir, ErrorCode);
    if (ErrorCode)
    {
        ErrorCode.clear();
        TempDir = L"C:/Temp/NipsEngineFbxImport";
        std::filesystem::create_directories(TempDir, ErrorCode);
    }
    if (ErrorCode)
    {
        ErrorCode.clear();
        TempDir = std::filesystem::temp_directory_path() / L"NipsEngineFbxImport";
        std::filesystem::create_directories(TempDir, ErrorCode);
    }

    std::filesystem::path Extension = std::filesystem::path(FPaths::ToWide(SourcePath)).extension();
    if (Extension.empty())
    {
        Extension = L".fbx";
    }

    return TempDir / (L"fbx_import_" + std::to_wstring(TickCount) + Extension.wstring());
}

FFbxImportPath BuildImportPathForFbxSdk(const FString& SourcePath)
{
    FFbxImportPath Result;
    Result.ImportPath = SourcePath;

    // FbxImporter only exposes a narrow char path API in this SDK. Keep ASCII paths direct.
    if (IsAsciiString(SourcePath))
    {
        return Result;
    }

    const std::filesystem::path SourceAbsolutePath(FPaths::ToAbsolute(FPaths::ToWide(SourcePath)));
    std::error_code ErrorCode;
    if (!std::filesystem::exists(SourceAbsolutePath, ErrorCode))
    {
        return Result;
    }

    const std::filesystem::path TempPath = MakeAsciiTempFbxPath(SourcePath);
    std::filesystem::copy_file(SourceAbsolutePath, TempPath, std::filesystem::copy_options::overwrite_existing, ErrorCode);
    if (ErrorCode)
    {
        UE_LOG("[FbxImporter] Failed to create ASCII temp copy for path: %s", SourcePath.c_str());
        return Result;
    }

    Result.TempPath = TempPath;
    Result.ImportPath = FPaths::ToUtf8(TempPath.generic_wstring());
    return Result;
}

FMatrix ConvertFbxMatrixToEngineMatrix(const FbxAMatrix& Matrix)
{
    return FMatrix(
        static_cast<float>(Matrix.Get(0, 0)), static_cast<float>(Matrix.Get(0, 1)), static_cast<float>(Matrix.Get(0, 2)), static_cast<float>(Matrix.Get(0, 3)),
        static_cast<float>(Matrix.Get(1, 0)), static_cast<float>(Matrix.Get(1, 1)), static_cast<float>(Matrix.Get(1, 2)), static_cast<float>(Matrix.Get(1, 3)),
        static_cast<float>(Matrix.Get(2, 0)), static_cast<float>(Matrix.Get(2, 1)), static_cast<float>(Matrix.Get(2, 2)), static_cast<float>(Matrix.Get(2, 3)),
        static_cast<float>(Matrix.Get(3, 0)), static_cast<float>(Matrix.Get(3, 1)), static_cast<float>(Matrix.Get(3, 2)), static_cast<float>(Matrix.Get(3, 3)));
}

FVector ToEngineVector(const FbxVector4& Vector)
{
    return FVector(
        static_cast<float>(Vector[0]),
        static_cast<float>(Vector[1]),
        static_cast<float>(Vector[2]));
}

FVector2 ToEngineVector2(const FbxVector2& Vector)
{
    return FVector2(
        static_cast<float>(Vector[0]),
        1.0f - static_cast<float>(Vector[1]));
}

FMatrix GetFbxGeometricTransformMatrix(FbxNode* Node)
{
    if (Node == nullptr)
    {
        return FMatrix::Identity;
    }

    const FbxVector4 Translation = Node->GetGeometricTranslation(FbxNode::eSourcePivot);
    const FbxVector4 Rotation = Node->GetGeometricRotation(FbxNode::eSourcePivot);
    const FbxVector4 Scale = Node->GetGeometricScaling(FbxNode::eSourcePivot);

    FbxAMatrix Matrix;
    Matrix.SetT(Translation);
    Matrix.SetR(Rotation);
    Matrix.SetS(Scale);
    return ConvertFbxMatrixToEngineMatrix(Matrix);
}

int32 AddBoneRecursive(FbxNode* BoneNode, FSkeletalMesh& OutMesh)
{
    if (BoneNode == nullptr)
    {
        return -1;
    }

    // Check if this bone already exists in the skeleton.
    const FString BoneName = FbxNameToString(BoneNode->GetName());
    const int32 ExistingIndex = OutMesh.RefSkeleton.FindBoneIndex(BoneName);
    if (ExistingIndex >= 0)
    {
        return ExistingIndex;
    }

    // Recursively add parent bones first.
    int32 ParentIndex = -1;
    FbxNode* ParentNode = BoneNode->GetParent();
    if (ParentNode != nullptr)
    {
        FbxNodeAttribute* ParentAttribute = ParentNode->GetNodeAttribute();
        if (ParentAttribute != nullptr && ParentAttribute->GetAttributeType() == FbxNodeAttribute::eSkeleton)
        {
            ParentIndex = AddBoneRecursive(ParentNode, OutMesh);
        }
    }

    // Add this bone to the skeleton.
    FbxAMatrix LocalTransform = BoneNode->EvaluateLocalTransform();
    FBoneInfo BoneInfo;
    BoneInfo.Name = BoneName;
    BoneInfo.ParentIndex = ParentIndex;
    const int32 BoneIndex = OutMesh.RefSkeleton.Add(BoneInfo, FTransform(ConvertFbxMatrixToEngineMatrix(LocalTransform)));

    // Fallback inverse global bind pose matrix if not provided by skinning data.
    // This is needed to ensure the skeleton can be used even without skinning.
    if (BoneIndex >= static_cast<int32>(OutMesh.InverseBindGlobalMatrices.size()))
    {
        OutMesh.InverseBindGlobalMatrices.resize(BoneIndex + 1, FMatrix::Identity);
    }
    OutMesh.InverseBindGlobalMatrices[BoneIndex] = ConvertFbxMatrixToEngineMatrix(BoneNode->EvaluateGlobalTransform()).GetInverse();

    return BoneIndex;
}

void CollectSkeletonNodes(FbxNode* Node, FSkeletalMesh& OutMesh)
{
    if (Node == nullptr)
    {
        return;
    }

    FbxNodeAttribute* Attribute = Node->GetNodeAttribute();
    if (Attribute != nullptr && Attribute->GetAttributeType() == FbxNodeAttribute::eSkeleton)
    {
        AddBoneRecursive(Node, OutMesh);
    }

    // Recursively collect from child nodes.
    for (int32 ChildIndex = 0; ChildIndex < Node->GetChildCount(); ++ChildIndex)
    {
        CollectSkeletonNodes(Node->GetChild(ChildIndex), OutMesh);
    }
}

int32 GetOrAddMaterial(FSkeletalMesh& OutMesh, FbxSurfaceMaterial* Material)
{
    FString SlotName = Material ? FbxNameToString(Material->GetName()) : FString("DefaultWhite");
    if (SlotName.empty())
    {
        SlotName = "DefaultWhite";
    }

    for (int32 Index = 0; Index < static_cast<int32>(OutMesh.Materials.size()); ++Index)
    {
        if (OutMesh.Materials[Index].MaterialSlotName == SlotName)
        {
            return Index;
        }
    }

    FSkeletalMaterial NewMaterial;
    NewMaterial.MaterialSlotName = SlotName;
    OutMesh.Materials.push_back(NewMaterial);
    return static_cast<int32>(OutMesh.Materials.size() - 1);
}

int32 GetPolygonMaterialSlotIndex(FbxMesh* Mesh, FbxNode* Node, int32 PolygonIndex, FSkeletalMesh& OutMesh)
{
    int32 NodeMaterialIndex = 0;
    FbxLayerElementMaterial* MaterialElement = Mesh->GetElementMaterial();
    if (MaterialElement != nullptr)
    {
        switch (MaterialElement->GetMappingMode())
        {
        case FbxLayerElement::eByPolygon:
            if (MaterialElement->GetIndexArray().GetCount() > PolygonIndex)
            {
                NodeMaterialIndex = MaterialElement->GetIndexArray().GetAt(PolygonIndex);
            }
            break;
        case FbxLayerElement::eAllSame:
            if (MaterialElement->GetIndexArray().GetCount() > 0)
            {
                NodeMaterialIndex = MaterialElement->GetIndexArray().GetAt(0);
            }
            break;
        default:
            break;
        }
    }

    FbxSurfaceMaterial* Material = Node ? Node->GetMaterial(NodeMaterialIndex) : nullptr;
    return GetOrAddMaterial(OutMesh, Material);
}

FVector GetPolygonNormal(FbxMesh* Mesh, int32 PolygonIndex, int32 VertexInPolygon, int32& OutNormalIndex)
{
    OutNormalIndex = PolygonIndex * 3 + VertexInPolygon;

    FbxVector4 Normal;
    if (Mesh->GetPolygonVertexNormal(PolygonIndex, VertexInPolygon, Normal))
    {
        return ToEngineVector(Normal).GetSafeNormal();
    }
    return FVector(0.0f, 0.0f, 1.0f);
}

FVector2 GetPolygonUV(FbxMesh* Mesh, int32 PolygonIndex, int32 VertexInPolygon, int32& OutUVIndex)
{
    OutUVIndex = PolygonIndex * 3 + VertexInPolygon;

    FbxStringList UVSetNames;
    Mesh->GetUVSetNames(UVSetNames);
    if (UVSetNames.GetCount() <= 0)
    {
        return FVector2(0.0f, 0.0f);
    }

    FbxVector2 UV;
    bool bUnmapped = false;
    if (Mesh->GetPolygonVertexUV(PolygonIndex, VertexInPolygon, UVSetNames.GetStringAt(0), UV, bUnmapped) && !bUnmapped)
    {
        return ToEngineVector2(UV);
    }
    return FVector2(0.0f, 0.0f);
}

void AssignTopNormalizedBoneInfluences(
    FSkeletalMeshVertex& Vertex,
    const TArray<FControlPointInfluence>& Influences,
    bool bNormalizeWeights)
{
    std::array<FControlPointInfluence, 4> TopInfluences = {};
    for (FControlPointInfluence Influence : Influences)
    {
        if (Influence.Weight <= 0.0f)
        {
            continue;
        }

        for (int32 Index = 0; Index < 4; ++Index)
        {
            if (Influence.Weight > TopInfluences[Index].Weight)
            {
                for (int32 Shift = 3; Shift > Index; --Shift)
                {
                    TopInfluences[Shift] = TopInfluences[Shift - 1];
                }
                TopInfluences[Index] = Influence;
                break;
            }
        }
    }

    float WeightSum = 0.0f;
    for (const FControlPointInfluence& Influence : TopInfluences)
    {
        WeightSum += Influence.Weight;
    }

    if (WeightSum <= 0.0f)
    {
        Vertex.BoneIndices[0] = 0;
        Vertex.BoneWeights[0] = 1.0f;
        for (int32 Index = 1; Index < 4; ++Index)
        {
            Vertex.BoneIndices[Index] = 0;
            Vertex.BoneWeights[Index] = 0.0f;
        }
        return;
    }

    const float WeightScale = bNormalizeWeights ? (1.0f / WeightSum) : 1.0f;
    for (int32 Index = 0; Index < 4; ++Index)
    {
        Vertex.BoneIndices[Index] = static_cast<uint16>(std::max(0, TopInfluences[Index].BoneIndex));
        Vertex.BoneWeights[Index] = TopInfluences[Index].Weight * WeightScale;
    }
}

void BuildSkinClusterBindings(
    FbxMesh* Mesh,
    FSkeletalMesh& OutMesh,
    TArray<TArray<FControlPointInfluence>>& OutInfluences)
{
    OutInfluences.clear();
    OutInfluences.resize(Mesh->GetControlPointsCount());

    // Iterate through skin deformers to collect bone influences for each control point.
    for (int32 DeformerIndex = 0; DeformerIndex < Mesh->GetDeformerCount(FbxDeformer::eSkin); ++DeformerIndex)
    {
        FbxSkin* Skin = static_cast<FbxSkin*>(Mesh->GetDeformer(DeformerIndex, FbxDeformer::eSkin));
        if (Skin == nullptr)
        {
            continue;
        }

        for (int32 ClusterIndex = 0; ClusterIndex < Skin->GetClusterCount(); ++ClusterIndex)
        {
            FbxCluster* Cluster = Skin->GetCluster(ClusterIndex);
            if (Cluster == nullptr || Cluster->GetLink() == nullptr)
            {
                continue;
            }

            const int32 BoneIndex = AddBoneRecursive(Cluster->GetLink(), OutMesh);
            if (BoneIndex < 0)
            {
                continue;
            }

            // Store the inverse bind pose global matrix for this bone.
            FbxAMatrix BindGlobalMatrix;
            Cluster->GetTransformLinkMatrix(BindGlobalMatrix);
            if (BoneIndex >= static_cast<int32>(OutMesh.InverseBindGlobalMatrices.size()))
            {
                OutMesh.InverseBindGlobalMatrices.resize(BoneIndex + 1, FMatrix::Identity);
            }
            OutMesh.InverseBindGlobalMatrices[BoneIndex] = ConvertFbxMatrixToEngineMatrix(BindGlobalMatrix).GetInverse();

            const int32* ControlPointIndices = Cluster->GetControlPointIndices();
            const double* ControlPointWeights = Cluster->GetControlPointWeights();
            const int32 ControlPointIndexCount = Cluster->GetControlPointIndicesCount();

            for (int32 Index = 0; Index < ControlPointIndexCount; ++Index)
            {
                const int32 ControlPointIndex = ControlPointIndices[Index];
                if (ControlPointIndex < 0 || ControlPointIndex >= static_cast<int32>(OutInfluences.size()))
                {
                    continue;
                }

                FControlPointInfluence Influence;
                Influence.BoneIndex = BoneIndex;
                Influence.Weight = static_cast<float>(ControlPointWeights[Index]);
                OutInfluences[ControlPointIndex].push_back(Influence);
            }
        }
    }
}

void BuildBoundsAndFallbackTangents(FSkeletalMeshLODRenderData& LODData)
{
    LODData.Bounds.Reset();
    for (FSkeletalMeshVertex& Vertex : LODData.Vertices)
    {
        LODData.Bounds.Expand(Vertex.Position);
        Vertex.Tangent = FVector(1.0f, 0.0f, 0.0f);
        Vertex.Bitangent = FVector(0.0f, 1.0f, 0.0f);
    }
}

void RebuildLocalTransforms(FSkeletalMesh& OutMesh)
{
    const int32 BoneCount = OutMesh.RefSkeleton.GetNum();
    if (BoneCount <= 0)
    {
        return;
    }

    if (OutMesh.InverseBindGlobalMatrices.size() < static_cast<size_t>(BoneCount))
    {
        OutMesh.InverseBindGlobalMatrices.resize(BoneCount, FMatrix::Identity);
    }

    OutMesh.RefSkeleton.LocalTransforms.resize(BoneCount, FTransform::Identity);

    // Compute local bind pose transforms from inverse global bind pose matrices.
    TArray<FMatrix> BindGlobalMatrices;
    BindGlobalMatrices.resize(BoneCount, FMatrix::Identity);
    for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
    {
        BindGlobalMatrices[BoneIndex] = OutMesh.InverseBindGlobalMatrices[BoneIndex].GetInverse();
    }

    for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
    {
        const int32 ParentIndex = OutMesh.RefSkeleton.BoneInfo[BoneIndex].ParentIndex;
        FMatrix LocalBindMatrix = BindGlobalMatrices[BoneIndex];
        if (ParentIndex >= 0 && ParentIndex < BoneCount)
        {
            // Row-vector convention: ChildGlobal = Local * ParentGlobal.
            LocalBindMatrix = BindGlobalMatrices[BoneIndex] * BindGlobalMatrices[ParentIndex].GetInverse();
        }

        OutMesh.RefSkeleton.LocalTransforms[BoneIndex] = FTransform(LocalBindMatrix);
    }
}

float GetMaxAbsMatrixDifference(const FMatrix& A, const FMatrix& B)
{
    float MaxDiff = 0.0f;
    for (int32 Row = 0; Row < 4; ++Row)
    {
        for (int32 Col = 0; Col < 4; ++Col)
        {
            MaxDiff = std::max(MaxDiff, std::fabs(A.M[Row][Col] - B.M[Row][Col]));
        }
    }
    return MaxDiff;
}

void ValidateBindPoseSkinningMatrices(const FSkeletalMesh& Mesh, const FString& Path)
{
    const int32 BoneCount = Mesh.RefSkeleton.GetNum();
    if (BoneCount <= 0)
    {
        return;
    }

    if (Mesh.RefSkeleton.LocalTransforms.size() < static_cast<size_t>(BoneCount) ||
        Mesh.InverseBindGlobalMatrices.size() < static_cast<size_t>(BoneCount))
    {
        UE_LOG("[FbxImporter] Bind pose validation skipped for %s: incomplete skeleton matrices.", Path.c_str());
        return;
    }

    TArray<FMatrix> BindGlobalMatrices;
    BindGlobalMatrices.resize(BoneCount, FMatrix::Identity);

    float MaxDeviation = 0.0f;
    int32 WorstBoneIndex = -1;

    for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
    {
        const FMatrix LocalMatrix = Mesh.RefSkeleton.LocalTransforms[BoneIndex].ToMatrixWithScale();
        const int32 ParentIndex = Mesh.RefSkeleton.BoneInfo[BoneIndex].ParentIndex;

        if (ParentIndex >= 0 && ParentIndex < BoneIndex)
        {
            BindGlobalMatrices[BoneIndex] = LocalMatrix * BindGlobalMatrices[ParentIndex];
        }
        else
        {
            BindGlobalMatrices[BoneIndex] = LocalMatrix;
        }

        const FMatrix SkinningMatrix = Mesh.InverseBindGlobalMatrices[BoneIndex] * BindGlobalMatrices[BoneIndex];
        const float Deviation = GetMaxAbsMatrixDifference(SkinningMatrix, FMatrix::Identity);
        if (Deviation > MaxDeviation)
        {
            MaxDeviation = Deviation;
            WorstBoneIndex = BoneIndex;
        }
    }

    constexpr float BindPoseTolerance = 1.0e-3f;
    if (MaxDeviation > BindPoseTolerance && WorstBoneIndex >= 0)
    {
        const FString& BoneName = Mesh.RefSkeleton.BoneInfo[WorstBoneIndex].Name;
        UE_LOG("[FbxImporter] Bind pose validation warning for %s: max deviation %.6f at bone %d (%s).",
               Path.c_str(),
               MaxDeviation,
               WorstBoneIndex,
               BoneName.c_str());
    }
}

void ImportSkeletalMeshData(FbxNode* Node, FbxMesh* Mesh, FSkeletalMesh& OutMesh, const FSkeletalMeshImportOptions& ImportOptions)
{
    if (Node == nullptr || Mesh == nullptr)
    {
        return;
    }

    // Ensure there is at least one LOD level in the skeletal mesh.
    if (OutMesh.RenderData.LODRenderData.empty())
    {
        OutMesh.RenderData.LODRenderData.emplace_back();
    }

    // Collect per-control-point bone influences and bind-pose link matrices from FBX skin clusters.
    FSkeletalMeshLODRenderData& LODData = OutMesh.RenderData.LODRenderData[0];
    TArray<TArray<FControlPointInfluence>> ControlPointInfluences;
    BuildSkinClusterBindings(Mesh, OutMesh, ControlPointInfluences);

    // Get the geometric transform of the node to apply to vertex positions and normals.
    const FMatrix FbxGeometricTransformMatrix = GetFbxGeometricTransformMatrix(Node);
    FbxVector4* ControlPoints = Mesh->GetControlPoints();
    std::unordered_map<FVertexKey, uint32, FVertexKeyHash> VertexMap;
    TArray<TArray<uint32>> IndicesByMaterial;

    // Iterate through polygons to build vertices and indices.
    const int32 PolygonCount = Mesh->GetPolygonCount();
    for (int32 PolygonIndex = 0; PolygonIndex < PolygonCount; ++PolygonIndex)
    {
        const int32 PolygonSize = Mesh->GetPolygonSize(PolygonIndex);
        if (PolygonSize != 3)
        {
            continue;
        }

        // Determine material slot index for this polygon.
        const int32 MaterialSlotIndex = GetPolygonMaterialSlotIndex(Mesh, Node, PolygonIndex, OutMesh);
        if (MaterialSlotIndex >= static_cast<int32>(IndicesByMaterial.size()))
        {
            IndicesByMaterial.resize(MaterialSlotIndex + 1);
        }

        for (int32 VertexInPolygon = 0; VertexInPolygon < 3; ++VertexInPolygon)
        {
            // Get the control point index for this vertex and validate it.
            const int32 ControlPointIndex = Mesh->GetPolygonVertex(PolygonIndex, VertexInPolygon);
            if (ControlPointIndex < 0)
            {
                continue;
            }

            // Retrieve normal and UV for this vertex, and track their indices for vertex uniqueness.
            int32 NormalIndex = -1;
            int32 UVIndex = -1;
            const FVector Normal = FbxGeometricTransformMatrix.TransformVector(GetPolygonNormal(Mesh, PolygonIndex, VertexInPolygon, NormalIndex)).GetSafeNormal();
            const FVector2 UV = GetPolygonUV(Mesh, PolygonIndex, VertexInPolygon, UVIndex);

            // Create a vertex key based on control point, normal, UV, and material to ensure uniqueness.
            FVertexKey Key;
            Key.ControlPointIndex = ControlPointIndex;
            Key.NormalIndex = NormalIndex;
            Key.UVIndex = UVIndex;
            Key.MaterialSlotIndex = MaterialSlotIndex;

            // Check if a vertex with the same key already exists to reuse it.
            auto Found = VertexMap.find(Key);
            if (Found != VertexMap.end())
            {
                IndicesByMaterial[MaterialSlotIndex].push_back(Found->second);
                continue;
            }

            // Create a new vertex and add it to the LOD data, then map the key to the new vertex index.
            FSkeletalMeshVertex Vertex;
            Vertex.Position = FbxGeometricTransformMatrix.TransformPosition(ToEngineVector(ControlPoints[ControlPointIndex]));
            Vertex.Normal = Normal;
            Vertex.UVs = UV;
            Vertex.Color = FColor::White();
            if (ControlPointIndex < static_cast<int32>(ControlPointInfluences.size()))
            {
                AssignTopNormalizedBoneInfluences(Vertex, ControlPointInfluences[ControlPointIndex], ImportOptions.bNormalizeWeights);
            }

            // Add the new vertex and update mappings.
            const uint32 NewVertexIndex = static_cast<uint32>(LODData.Vertices.size());
            LODData.Vertices.push_back(Vertex);
            VertexMap.emplace(Key, NewVertexIndex);
            IndicesByMaterial[MaterialSlotIndex].push_back(NewVertexIndex);
        }
    }

    for (int32 MaterialSlotIndex = 0; MaterialSlotIndex < static_cast<int32>(IndicesByMaterial.size()); ++MaterialSlotIndex)
    {
        TArray<uint32>& SectionIndices = IndicesByMaterial[MaterialSlotIndex];
        if (SectionIndices.empty())
        {
            continue;
        }

        FSkeletalMeshSection Section;
        Section.StartIndex = static_cast<uint32>(LODData.Indices.size());
        Section.IndexCount = static_cast<uint32>(SectionIndices.size());
        Section.MaterialSlotIndex = MaterialSlotIndex;

        LODData.Indices.insert(LODData.Indices.end(), SectionIndices.begin(), SectionIndices.end());
        LODData.Sections.push_back(Section);
    }

    BuildBoundsAndFallbackTangents(LODData);
}

void ImportSkeletalMeshDataRecursive(FbxNode* Node, FSkeletalMesh& OutMesh, const FSkeletalMeshImportOptions& ImportOptions)
{
    if (Node == nullptr)
    {
        return;
    }

    FbxMesh* Mesh = Node->GetMesh();
    if (Mesh != nullptr)
    {
        ImportSkeletalMeshData(Node, Mesh, OutMesh, ImportOptions);
    }

    for (int32 ChildIndex = 0; ChildIndex < Node->GetChildCount(); ++ChildIndex)
    {
        ImportSkeletalMeshDataRecursive(Node->GetChild(ChildIndex), OutMesh, ImportOptions);
    }
}
} // namespace

USkeletalMesh* FFbxImporter::ImportSkeletalMesh(const FString& Path, const FSkeletalMeshImportOptions& ImportOptions)
{
    // Create an FBX Manager.
    FbxManager* Manager = FbxManager::Create();
    if (Manager == nullptr)
    {
        UE_LOG("[FbxImporter] Failed to create FbxManager.");
        return nullptr;
    }

    // Create an IOSettings object. This object holds all import/export settings.
    FbxIOSettings* IOSettings = FbxIOSettings::Create(Manager, IOSROOT);
    Manager->SetIOSettings(IOSettings);

    // Create an importer using the SDK manager.
    FbxImporter* Importer = FbxImporter::Create(Manager, "");

    // Build the import path, creating a temporary ASCII path if necessary for the FBX SDK.
    FFbxImportPath ImportPath = BuildImportPathForFbxSdk(Path);


    // Initialize the importer by providing a filename.
    if (Importer == nullptr || !Importer->Initialize(ImportPath.ImportPath.c_str(), -1, Manager->GetIOSettings()))
    {
        UE_LOG("[FbxImporter] Failed to initialize importer: %s", Path.c_str());
        if (Importer != nullptr)
        {
            Importer->Destroy();
        }
        Manager->Destroy();
        return nullptr;
    }

    // Create an FbxScene.
    FbxScene* Scene = FbxScene::Create(Manager, "ImportedScene");
    if (Scene == nullptr || !Importer->Import(Scene))
    {
        UE_LOG("[FbxImporter] Failed to import scene: %s", Path.c_str());
        Importer->Destroy();
        Manager->Destroy();
        return nullptr;
    }
    Importer->Destroy();

    // Triangulate the scene.
    FbxGeometryConverter GeometryConverter(Manager);
    GeometryConverter.Triangulate(Scene, true);

    // Convert the scene's axis system to match the engine's coordinate system (Y-up, left-handed).
    FSkeletalMesh* ImportedMesh = new FSkeletalMesh();
    ImportedMesh->PathFileName = Path;
    ImportedMesh->RenderData.LODRenderData.emplace_back();


    FbxNode* RootNode = Scene->GetRootNode();

    // Recursively collect skeleton nodes and import mesh data.
    CollectSkeletonNodes(RootNode, *ImportedMesh);


    // Recursively import mesh data for all nodes.
    // This allows for multiple meshes in the same FBX to be merged into one skeletal mesh asset.
    ImportSkeletalMeshDataRecursive(RootNode, *ImportedMesh, ImportOptions);

    // Ensure there is at least a root bone in the skeleton, as some FBX files may not have any skeleton data.
    if (ImportedMesh->RefSkeleton.GetNum() == 0)
    {
        FBoneInfo RootBone;
        RootBone.Name = "Root";
        RootBone.ParentIndex = -1;
        ImportedMesh->RefSkeleton.Add(RootBone, FTransform::Identity);
        ImportedMesh->InverseBindGlobalMatrices.push_back(FMatrix::Identity);
    }

	// Rebuild local transforms from inverse bind pose global matrices.
    RebuildLocalTransforms(*ImportedMesh);
    ValidateBindPoseSkinningMatrices(*ImportedMesh, Path);

    // Validate that we have valid vertex and index data before creating the skeletal mesh asset.
    const FSkeletalMeshLODRenderData& LODData = ImportedMesh->RenderData.LODRenderData[0];
    if (LODData.Vertices.empty() || LODData.Indices.empty())
    {
        UE_LOG("[FbxImporter] Imported scene has no valid skeletal mesh data: %s", Path.c_str());
        delete ImportedMesh;
        Manager->Destroy();
        return nullptr;
    }

    // Create a new skeletal mesh asset and assign the imported mesh data to it.
    USkeletalMesh* SkeletalMesh = UObjectManager::Get().CreateObject<USkeletalMesh>();
    SkeletalMesh->SetSkeletalMeshData(ImportedMesh);

    UE_LOG("[FbxImporter] Imported %s (Vertices: %zu, Indices: %zu, Bones: %zu, Materials: %zu)",
           Path.c_str(),
           LODData.Vertices.size(),
           LODData.Indices.size(),
           ImportedMesh->RefSkeleton.BoneInfo.size(),
           ImportedMesh->Materials.size());

    Manager->Destroy();
    return SkeletalMesh;
}
