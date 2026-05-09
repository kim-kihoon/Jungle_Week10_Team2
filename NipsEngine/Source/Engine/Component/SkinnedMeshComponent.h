#pragma once

#include "MeshComponent.h"
#include "Render/Resource/VertexTypes.h"
#include "Render/Common/RenderTypes.h"

class USkeletalMesh;

/* ―――― USkinnedMeshComponent ―――― */
class USkinnedMeshComponent : public UMeshComponent
{
public:
	DECLARE_CLASS(USkinnedMeshComponent, UMeshComponent)

	USkinnedMeshComponent() = default;
	~USkinnedMeshComponent() override = default;

	//bone hierarchy의 local transform을 누적해 component-space transform을 갱신
	virtual void RefreshBoneTransforms();

	void ComputeSkinningMatrices();
	void ComputeSkinnedVertices();

	void SetSkeletalMesh(USkeletalMesh* InSkeletalMesh);
	USkeletalMesh* GetSkeletalMesh() { return SkeletalMesh; }
	const USkeletalMesh* GetSkeletalMesh() const { return SkeletalMesh; }

	const TArray<FNormalVertex>& GetSkinnedVertices() const;
	const TArray<FMatrix>& GetSkinningMatrices() const;
	const TArray<FTransform>& GetComponentSpaceBoneTransforms() const;

	EPrimitiveType GetPrimitiveType() const override { return EPrimitiveType::EPT_SkeletalMesh; }
	void UpdateWorldAABB() const override;
	bool RaycastMesh(const FRay& Ray, FHitResult& OutHitResult) override;
	bool HasValidMesh() const;

	bool ConsumeRenderStateDirty();

private:
	// 상속받은 skeletal mesh componen에서도 접근
	void MarkBoneTransformsDirty();
	void MarkSkinningDirty();
	void MarkBoundsDirty();
	void MarkRenderStateDirty();
	void EnsureBoundsUpdated() const;

protected:
	FString SkeletalMeshAssetPath;
    
	USkeletalMesh* SkeletalMesh = nullptr;          // skeletal mesh 이놈을 바꾸는데
	TArray<FTransform> ComponentSpaceBoneTransforms;// 이놈을 구성하는 뼈가 이렇습니다
    // 여기서 컴포넌트 공간란 건 뭐고 또시떼 이런 걸 따로 두는 걸까요?
    // Bone의 원래 transform은 보통 parent bone 기준의 local transform이다.
    // 하지만 skinning을 계산하려면 vertex에 영향을 주는 여러 bone transform들이
    // 모두 같은 기준 공간에 있어야 해요. root부터 parent transform을 누적해 각 bone의
    // component-space transform을 만들고, 이 transform과 inverse bind pose를 이용해 skinning matrix를 계산해요.
    TArray<FMatrix> SkinningMatrices;				// 정점의 최종 변환 행렬입니다.
    TArray<FNormalVertex> SkinnedVertices;			// skinning이 끝난 정점 위치입니다. 이걸로 렌더링할 거에요

	mutable bool bBoneTransformsDirty = true;
	mutable bool bSkinningDirty = true;
	mutable bool bBoundsDirty = true;
	bool bRenderStateDirty = true;
	bool bConsumedRenderStateDirty = false; //렌더 상태 변경 플래그
};