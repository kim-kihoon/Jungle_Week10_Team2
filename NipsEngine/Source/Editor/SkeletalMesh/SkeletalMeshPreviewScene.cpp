#include "SkeletalMeshPreviewScene.h"
#include "Editor/EditorEngine.h"
#include "GameFramework/World.h"
#include "GameFramework/WorldContext.h"
#include "GameFramework/PrimitiveActors.h"
#include "Engine/Core/ResourceManager.h"

#include "Component/SkeletalMeshComponent.h"

static int32 GPreviewWorldCounter = 0;

FSkeletalMeshPreviewScene::~FSkeletalMeshPreviewScene()
{
	Shutdown();
}

void FSkeletalMeshPreviewScene::Initialize(UEditorEngine* InEditor)
{
	if (Editor != nullptr)
	{
		return; // 이미 초기화됨
	}

	Editor = InEditor;

	ViewportClient.SetPreviewScene(this);
	PreviewViewport.SetClient(&ViewportClient);
	ViewportClient.SetState(&PreviewViewport.GetState());
	ViewportClient.SetViewportType(EVT_Perspective);
	ViewportClient.ApplyCameraMode();

	// 월드 생성
	std::string ContextName = "SkeletalMeshViewer_Preview_" + std::to_string(GPreviewWorldCounter++);
	WorldHandle = FName(ContextName.c_str());

	FWorldContext& Context = Editor->CreateWorldContext(EWorldType::ViewerPreview, WorldHandle, ContextName);
	PreviewWorld = Context.World;
	PreviewWorld->SetWorldType(EWorldType::ViewerPreview);
	PreviewWorld->SetActiveCamera(ViewportClient.GetCamera());

	// Preview용 액터 스폰
	PreviewActor = PreviewWorld->SpawnActor<ASkeletalMeshActor>();
	PreviewActor->InitDefaultComponents();

	USkeletalMeshComponent* PreviewMeshComponent = static_cast<USkeletalMeshComponent*>(PreviewActor->GetRootComponent());
	USkeletalMesh* Mesh = FResourceManager::Get().LoadSkeletalMesh("Asset/Fbx/SKM_Quinn_Simple.FBX");
	if (PreviewMeshComponent && Mesh)
	{
		PreviewMeshComponent->SetSkeletalMesh(Mesh);
	}

	// Directional Light 액터 스폰
	auto* LightActor = PreviewWorld->SpawnActor<ADirectionalLightActor>();
	LightActor->InitDefaultComponents();
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
	ViewportClient.Tick(DeltaTime);
}

void FSkeletalMeshPreviewScene::SetVisible(bool bInVisible)
{
	if (Editor == nullptr)
		return;

	// UEditorEngine::WorldTick 을 보면 Ctx.bPaused 가 true면 Tick을 건너뜀
	if (FWorldContext* Context = Editor->GetWorldContextFromHandle(WorldHandle))
	{
		Context->bPaused = !bInVisible;
	}
}

void FSkeletalMeshPreviewScene::SetSkeletalMesh(USkeletalMesh* Mesh)
{
	if (PreviewActor)
	{
		if (USkeletalMeshComponent* PreviewMeshComponent = static_cast<USkeletalMeshComponent*>(PreviewActor->GetRootComponent()))
		{
			PreviewMeshComponent->SetSkeletalMesh(Mesh);
		}
	}
	ResetPose();
}

void FSkeletalMeshPreviewScene::ResetPose()
{
	// SkeletalMesh runtime component가 추가되면 여기에서 reference pose로 리셋합니다.
}

void FSkeletalMeshPreviewScene::SetViewportSize(uint32 Width, uint32 Height)
{
	PreviewViewport.SetRect(FViewportRect(0, 0, Width, Height));
	ViewportClient.SetViewportSize(static_cast<float>(Width), static_cast<float>(Height));
}
