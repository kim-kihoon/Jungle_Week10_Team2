#include "Editor/SkeletalMesh/SkeletalMeshPreviewScene.h"

#include "Editor/EditorEngine.h"
#include "GameFramework/PrimitiveActors.h"
#include "GameFramework/World.h"
#include "GameFramework/WorldContext.h"
#include "Component/SkeletalMeshComponent.h"
#include "Core/ResourceManager.h"

#include <string>
#include <windows.h>

namespace
{
int32 GPreviewWorldCounter = 0;
constexpr const char* DefaultSkeletalMeshPath = "Asset/Fbx/Quinn_UE5/SKM_Quinn_Simple.FBX";
}

FSkeletalMeshPreviewScene::~FSkeletalMeshPreviewScene()
{
	Shutdown();
}

void FSkeletalMeshPreviewScene::Initialize(UEditorEngine* InEditor)
{
	if (Editor != nullptr)
	{
		return;
	}

	Editor = InEditor;

	ViewportClient.SetPreviewScene(this);
	PreviewViewport.SetClient(&ViewportClient);
	ViewportClient.SetState(&PreviewViewport.GetState());
	ViewportClient.SetViewportType(EVT_Perspective);
	ViewportClient.ApplyCameraMode();

	PreviewInputController.SetViewportClient(&ViewportClient);
	PreviewInputRouter.SetEditorWorldController(&PreviewInputController);

	std::string ContextName = "SkeletalMeshViewer_Preview_" + std::to_string(GPreviewWorldCounter++);
	WorldHandle = FName(ContextName.c_str());

	FWorldContext& Context = Editor->CreateWorldContext(EWorldType::ViewerPreview, WorldHandle, ContextName);
	PreviewWorld = Context.World;
	PreviewWorld->SetWorldType(EWorldType::ViewerPreview);
	PreviewWorld->SetActiveCamera(ViewportClient.GetCamera());

	PreviewActor = PreviewWorld->SpawnActor<ASkeletalMeshActor>();
	PreviewActor->InitDefaultComponents();

	USkeletalMesh* Mesh = FResourceManager::Get().LoadSkeletalMesh(DefaultSkeletalMeshPath);
	SetSkeletalMesh(Mesh);

	ADirectionalLightActor* LightActor = PreviewWorld->SpawnActor<ADirectionalLightActor>();
	LightActor->InitDefaultComponents();

	AAmbientLightActor* AmbientLightActor = PreviewWorld->SpawnActor<AAmbientLightActor>();
	AmbientLightActor->InitDefaultComponents();
}

void FSkeletalMeshPreviewScene::Shutdown()
{
	if (Editor != nullptr && PreviewWorld != nullptr)
	{
		Editor->DestroyWorldContext(WorldHandle);

		PreviewWorld = nullptr;
		PreviewActor = nullptr;
		ViewportClient.SetState(nullptr);
		PreviewViewport.SetClient(nullptr);
		Editor = nullptr;
	}
}

void FSkeletalMeshPreviewScene::Tick(float DeltaTime)
{
	if (!Editor)
	{
		return;
	}

	const bool bMouseControlDown = FInputRouter::GetKey(VK_RBUTTON) || FInputRouter::GetKey(VK_MBUTTON);
	const bool bPreviewCaptureBegin =
		bPreviewHovered &&
		(FInputRouter::GetKeyDown(VK_RBUTTON) || FInputRouter::GetKeyDown(VK_MBUTTON));

	if (bPreviewCaptureBegin)
	{
		bPreviewInputCaptured = true;
	}
	else if (!bMouseControlDown)
	{
		bPreviewInputCaptured = false;
		PreviewInputController.OnRightMouseButtonUp();
	}

	FInputRouteContext Context;
	Context.Window = Editor->GetWindow();
	Context.ViewportRect = PreviewInputRect;
	Context.bHovered = bPreviewHovered || bPreviewInputCaptured;
	Context.bInputActive = true;
	Context.bControlLocked = false;
	Context.bHasActiveCamera = true;
	Context.bIgnoreGuiBlock = true;

	PreviewInputRouter.Tick(DeltaTime, Context);
	ViewportClient.Tick(DeltaTime);
}

void FSkeletalMeshPreviewScene::SetVisible(bool bInVisible)
{
	if (!Editor)
	{
		return;
	}

	if (FWorldContext* Context = Editor->GetWorldContextFromHandle(WorldHandle))
	{
		Context->bPaused = !bInVisible;
	}
}

void FSkeletalMeshPreviewScene::SetSkeletalMesh(USkeletalMesh* Mesh)
{
	if (!PreviewActor)
	{
		return;
	}

	if (USkeletalMeshComponent* PreviewMeshComponent = static_cast<USkeletalMeshComponent*>(PreviewActor->GetRootComponent()))
	{
		PreviewMeshComponent->SetSkeletalMesh(Mesh);
	}
	SelectBone(-1);
	ResetPose();
}

void FSkeletalMeshPreviewScene::ResetPose()
{
	if (USkeletalMeshComponent* Comp = GetPreviewMeshComponent())
	{
		Comp->ResetPose();
	}
}

USkeletalMeshComponent* FSkeletalMeshPreviewScene::GetPreviewMeshComponent() const
{
	if (PreviewActor)
	{
		return static_cast<USkeletalMeshComponent*>(PreviewActor->GetRootComponent());
	}
	return nullptr;
}

USkeletalMesh* FSkeletalMeshPreviewScene::GetCurrentSkeletalMesh() const
{
	if (USkeletalMeshComponent* Comp = GetPreviewMeshComponent())
	{
		return Comp->GetSkeletalMesh();
	}
	return nullptr;
}

void FSkeletalMeshPreviewScene::SelectBone(int32 BoneIndex)
{
	SelectedBoneIndex = BoneIndex;
}

int32 FSkeletalMeshPreviewScene::GetSelectedBoneIndex() const
{
	return SelectedBoneIndex;
}

void FSkeletalMeshPreviewScene::SetViewportSize(uint32 Width, uint32 Height)
{
	PreviewViewport.SetRect(FViewportRect(0, 0, Width, Height));
	ViewportClient.SetViewportSize(static_cast<float>(Width), static_cast<float>(Height));
}

void FSkeletalMeshPreviewScene::SetInputRectFromScreenRect(float MinX, float MinY, float MaxX, float MaxY)
{
	const int32 X = static_cast<int32>(MinX);
	const int32 Y = static_cast<int32>(MinY);
	const int32 Width = static_cast<int32>(MaxX - MinX);
	const int32 Height = static_cast<int32>(MaxY - MinY);

	PreviewInputRect = FViewportRect(X, Y, Width, Height);
}
