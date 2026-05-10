#include "FbxLoader.h"
#include "SkeletalMesh.h"
#include "Core/Logger.h"

#include <fbxsdk.h> //FbxManager, FbxScene, FbxIOSettings 등 FBX SDK 관련 클래스 제공.
#include <string>
#include <algorithm>
#include <cmath>

namespace
{
	// StringUtils에 이런 함수들이 있어야 할 텐데 현재 obj importer 쪽에 네임스페이스로 존재.
	// 따라서 리팩토링하며 해당 유틸들을 나중에 빼놔야 함. 현재는 개인 브랜치이므로 구조에 큰 변경을 주지 않고자 냅둠.
	FString ToLowerAscii(FString Value)
	{
		std::transform(Value.begin(), Value.end(), Value.begin(), [](unsigned char CharValue)
					   { return static_cast<char>(std::tolower(CharValue)); });
		return Value;
	}

	struct FFbxLoadContext
	{
		FString SourcePath;

		FbxManager* Manager = nullptr;
		FbxIOSettings* IOSettings = nullptr;
		FbxScene* Scene = nullptr;

		FSkeletalMesh LoadedMesh;
		TMap<FbxNode*, int32> BoneIndexMap;
	};

	bool CreateFbxSdkContext(FFbxLoadContext& Context)
	{
		/* FbxManager : FBX SDK 전체의 루트 객체. */
		Context.Manager = FbxManager::Create();
		if (Context.Manager == nullptr)
		{
			return false;
		}

		/* FbxIOSettings : FBX import/export 설정을 담는 객체. */
		Context.IOSettings = FbxIOSettings::Create(Context.Manager, IOSROOT);
		if (Context.IOSettings == nullptr)
		{
			Context.Manager->Destroy();
			Context.Manager = nullptr;
			return false;
		}

		/* 만든 IOSettings를 Manager에 등록. 관련해 사용 ex) Manager->GetIOSettings() */
		Context.Manager->SetIOSettings(Context.IOSettings);

		/* FbxScene은 import된 FBX 데이터가 들어갈 컨테이너. 들어가는 애들 ex) FbxNode, FbxMesh, FbxSkeleton, FbxMaterial, FbxSkin, FbxCluster */
		Context.Scene = FbxScene::Create(Context.Manager, "FbxImportScene");

		if (Context.Scene == nullptr)
		{
			Context.Manager->Destroy();
			Context.Manager = nullptr;
			Context.IOSettings = nullptr;
			Context.Scene = nullptr;
			return false;
		}
		return true;
	}

	void DestroyFbxSdkContext(FFbxLoadContext& Context)
	{
		/* FbxManager를 Destroy하면, 해당 Manager를 통해 생성된 FBX SDK 객체들도 같이 정리된다. */
		if (Context.Manager != nullptr)
		{
			Context.Manager->Destroy();
		}

		Context.Manager = nullptr;
		Context.IOSettings = nullptr;
		Context.Scene = nullptr;

		Context.BoneIndexMap.clear();
	}

	/**
	 * FBX 파일을 FbxScene 객체에 로드.
	 */
	bool ImportFbxScene(FFbxLoadContext& Context, const FString& Path)
	{
		if (Context.Manager == nullptr || Context.Scene == nullptr)
		{
			return false;
		}

		FbxImporter* importer = FbxImporter::Create(Context.Manager, "TempImporter");
		if (importer == nullptr)
		{
			return false;
		}

		const FString FilePath = Path;
		const bool initializeResult = importer->Initialize(FilePath.c_str(), -1, Context.Manager->GetIOSettings());
		if (!initializeResult)
		{
			const char* ErrorString = importer->GetStatus().GetErrorString();
			UE_LOG("ErrorString is %s", ErrorString ? ErrorString : "Unknown FBX error");
			importer->Destroy();
			return false;
		}

		const bool bImportSuccess = importer->Import(Context.Scene);

		if (!bImportSuccess)
		{
			const char* ErrorString = importer->GetStatus().GetErrorString();
			UE_LOG("FBX import failed for '%s': %s", Path.c_str(), ErrorString ? ErrorString : "Unknown FBX error");

			importer->Destroy();
			return false;
		}
		importer->Destroy();

		return true;
	}

	void ConvertSceneToEngineSpace(FFbxLoadContext& Context)
	{
		if (Context.Scene == nullptr)
		{
			return;
		}

		/*
			Axis System
			FbxAxisSystem의 두 번째 인자 eParityOdd / eParityEven은 "앞 방향 축"을 결정하는 값.
		*/
		const FbxAxisSystem EngineAxisSystem(
			FbxAxisSystem::eZAxis,
			FbxAxisSystem::eParityOdd,
			FbxAxisSystem::eLeftHanded);

		const FbxAxisSystem CurrentAxisSystem =
			Context.Scene->GetGlobalSettings().GetAxisSystem();

		if (CurrentAxisSystem != EngineAxisSystem)
		{
			EngineAxisSystem.ConvertScene(Context.Scene);

			UE_LOG("FBX axis system converted to engine axis system. Path: %s",
				   Context.SourcePath.c_str());
		}

		/*
			Unit System
		*/
		const FbxSystemUnit EngineUnitSystem = FbxSystemUnit::m; // meter 단위

		const FbxSystemUnit CurrentUnitSystem =
			Context.Scene->GetGlobalSettings().GetSystemUnit();

		if (CurrentUnitSystem != EngineUnitSystem)
		{
			EngineUnitSystem.ConvertScene(Context.Scene);

			UE_LOG(
				"FBX unit system converted to centimeter. Path: %s",
				Context.SourcePath.c_str());
		}
	}

	bool TriangulateFbxScene(FFbxLoadContext& Context)
	{
		// 여기서까지 오류 검사? Context에 문제가 있었으면 진작에 위에서 걸렸겠지요

		FbxGeometryConverter GeometryConverter(Context.Manager);
		bool bConvertResult = GeometryConverter.Triangulate(Context.Scene, true);
		if (!bConvertResult)
		{
			UE_LOG("FBX triangulation failed. Path: %s", Context.SourcePath.c_str());
			return false;
		}

		UE_LOG("FBX triangulation completed. Path: %s", Context.SourcePath.c_str());
		return true;
	}

	/**
	 * skin cluster link node 기반으로 bone 목록 생성.
	 */
	void CollectSkeletons(FFbxLoadContext& Context)
	{

	}
	void ExtractSkeletalMeshes(FFbxLoadContext& Context)
	{

	}
} //또 졸라 커지는 네임스페이스..., 줴줴이야

USkeletalMesh* FFbxLoader::Load(const FString& Path)
{
    /*
    FBX 로딩 전체 흐름.
    
	1. FBX SDK 객체 생성
    2. 파일 import
    3. 씬 정규화
    4. 삼각형화
    5. Skeleton 수집
    6. Mesh 추출
    7. USkeletalMesh 생성
    8. FBX SDK 객체 정리

	각각 함수로 분리됨 -> local variable이 넘 많아지는 문제 해결
	*/

	// 1. FBX SDK 객체 생성
    FFbxLoadContext Context;
    if (!CreateFbxSdkContext(Context))
    {
        return nullptr;
    }
    Context.SourcePath = Path;

	//2. 파일 import
    if (!ImportFbxScene(Context, Path))
    {
        DestroyFbxSdkContext(Context);
        return nullptr;
    }

	//3. 씬 정규화
	ConvertSceneToEngineSpace(Context);

	//4. 삼각형화
    if (!TriangulateFbxScene(Context))
    {
        DestroyFbxSdkContext(Context);
        return nullptr;
	}

	//5. Skeleton 수집
    CollectSkeletons(Context);

	//6. Mesh 추출
    ExtractSkeletalMeshes(Context);

	//7. USkeletalMesh 생성
	
	//더는 사용하지 않은 친구 손절
	DestroyFbxSdkContext(Context);
	return nullptr;
}

bool FFbxLoader::SupportsExtension(const FString& Extension) const
{
    return ToLowerAscii(Extension) == "fbx";
}

FString FFbxLoader::GetLoaderName() const
{
	return FString("FFbxLoader");
}
