#include "SkeletalMeshComponent.h"
#include "Core/ResourceManager.h"

DEFINE_CLASS(USkeletalMeshComponent, USkinnedMeshComponent)
REGISTER_FACTORY(USkeletalMeshComponent)

void USkeletalMeshComponent::RefreshBoneTransforms()
{
    // 지금은 animation pose가 없으므로 reference pose 그대로 사용한다.
    // 나중에 animation system이 생기면 여기서 local pose를 먼저 갱신한 뒤
    // component-space로 누적하면 된다.
    USkinnedMeshComponent::RefreshBoneTransforms();
}

void USkeletalMeshComponent::TickComponent(float DeltaTime)
{
    UMeshComponent::TickComponent(DeltaTime);

    if (bBoneTransformsDirty || bSkinningDirty)
    {
        ComputeSkinnedVertices();
    }
}

void USkeletalMeshComponent::Serialize(FArchive& Ar)
{
    UMeshComponent::Serialize(Ar);

    Ar << "SkeletalMeshAsset" << SkeletalMeshAssetPath;

    if (Ar.IsLoading())
    {
        TArray<UMaterialInterface*> SavedMaterials = Materials;

        if (!SkeletalMeshAssetPath.empty())
        {
            SetSkeletalMesh(FResourceManager::Get().LoadSkeletalMesh(SkeletalMeshAssetPath));
        }
        else
        {
            SetSkeletalMesh(nullptr);
        }

        const int32 RestoreCount =
            static_cast<int32>(std::min(SavedMaterials.size(), Materials.size()));

        for (int32 i = 0; i < RestoreCount; ++i)
        {
            if (SavedMaterials[i] != nullptr)
            {
                SetMaterial(i, SavedMaterials[i]);
            }
        }
    }
}

void USkeletalMeshComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
    UMeshComponent::GetEditableProperties(OutProps);
    OutProps.push_back({ "SkeletalMesh", EPropertyType::String, &SkeletalMeshAssetPath });
}

void USkeletalMeshComponent::PostEditProperty(const char* PropertyName)
{
    UMeshComponent::PostEditProperty(PropertyName);

    if (std::strcmp(PropertyName, "SkeletalMesh") == 0)
    {
        if (SkeletalMeshAssetPath.empty())
        {
            SetSkeletalMesh(nullptr);
            return;
        }

        USkeletalMesh* Mesh = FResourceManager::Get().LoadSkeletalMesh(SkeletalMeshAssetPath);
        SetSkeletalMesh(Mesh);
    }
    else if (std::strcmp(PropertyName, "Materials") == 0)
    {
        for (int32 i = 0; i < static_cast<int32>(Materials.size()); ++i)
        {
            SetMaterial(i, Materials[i]);
        }
    }
}