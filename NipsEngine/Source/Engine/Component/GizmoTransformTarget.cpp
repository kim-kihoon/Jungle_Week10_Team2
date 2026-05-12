#include "GizmoTransformTarget.h"
#include "GameFramework/AActor.h"
#include "Component/SceneComponent.h"
#include "Component/SkeletalMeshComponent.h"
#include "Asset/SkeletalMesh.h"
#include "Object/Object.h"

namespace
{
    bool IsLiveSceneComponent(USceneComponent* Component)
    {
        return UObject::IsValid(Component);
    }

    bool IsLiveActorWithRoot(AActor* Actor)
    {
        if (!UObject::IsValid(Actor))
        {
            return false;
        }

        return IsLiveSceneComponent(Actor->GetRootComponent());
    }
}

// FActorGizmoTarget
FActorGizmoTarget::FActorGizmoTarget(AActor* InActor)
    : TargetActor(InActor)
{
}

bool FActorGizmoTarget::IsValid() const
{
    return IsLiveActorWithRoot(TargetActor);
}

FTransform FActorGizmoTarget::GetWorldTransform() const
{
    if (IsLiveActorWithRoot(TargetActor))
    {
        return TargetActor->GetRootComponent()->GetWorldTransform();
    }
    return FTransform::Identity;
}

void FActorGizmoTarget::SetWorldTransform(const FTransform& NewWorldTransform)
{
    if (IsLiveActorWithRoot(TargetActor))
    {
        TargetActor->GetRootComponent()->SetWorldTransform(NewWorldTransform);
    }
}

// FSceneComponentGizmoTarget
FSceneComponentGizmoTarget::FSceneComponentGizmoTarget(USceneComponent* InComponent)
    : TargetComponent(InComponent)
{
}

bool FSceneComponentGizmoTarget::IsValid() const
{
    return IsLiveSceneComponent(TargetComponent);
}

FTransform FSceneComponentGizmoTarget::GetWorldTransform() const
{
    if (IsLiveSceneComponent(TargetComponent))
    {
        return TargetComponent->GetWorldTransform();
    }
    return FTransform::Identity;
}

void FSceneComponentGizmoTarget::SetWorldTransform(const FTransform& NewWorldTransform)
{
    if (IsLiveSceneComponent(TargetComponent))
    {
        TargetComponent->SetWorldTransform(NewWorldTransform);
    }
}

// FBoneGizmoTarget
FBoneGizmoTarget::FBoneGizmoTarget(USkeletalMeshComponent* InMeshComp, int32 InBoneIndex)
    : MeshComp(InMeshComp), BoneIndex(InBoneIndex)
{
}

bool FBoneGizmoTarget::IsValid() const
{
    if (!UObject::IsValid(MeshComp) || !MeshComp->HasValidMesh())
    {
        return false;
    }

    if (const USkeletalMesh* Mesh = MeshComp->GetSkeletalMesh())
    {
        return BoneIndex >= 0 && BoneIndex < static_cast<int32>(Mesh->GetBones().size());
    }

    return false;
}

FTransform FBoneGizmoTarget::GetWorldTransform() const
{
    if (IsValid())
    {
        return MeshComp->GetBoneWorldTransform(BoneIndex);
    }
    return FTransform::Identity;
}

void FBoneGizmoTarget::SetWorldTransform(const FTransform& NewWorldTransform)
{
    if (IsValid())
    {
        MeshComp->SetBoneWorldTransform(BoneIndex, NewWorldTransform);
    }
}
