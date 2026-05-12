#pragma once

#include "Editor/UI/EditorWidget.h"

class FEditorViewportOverlayWidget;
class FEditorSceneWidget;
class FEditorPlayStreamWidget;
class FEditorContentDrawerWidget;

class FEditorToolbarWidget : public FEditorWidget
{
public:
	void SetViewportOverlayWidget(FEditorViewportOverlayWidget* InViewportOverlayWidget);
	void SetSceneWidget(FEditorSceneWidget* InSceneWidget);
	void SetPlayStreamWidget(FEditorPlayStreamWidget* InPlayStreamWidget);
	void SetContentDrawerWidget(FEditorContentDrawerWidget* InContentDrawerWidget);
	void SetPanelVisibilityRefs(
		bool* InShowConsole,
		bool* InShowControl,
		bool* InShowProperty,
		bool* InShowSceneManager,
		bool* InShowMaterialEditor,
		bool* InShowStatProfiler,
		bool* InShowSkeletalMeshViewer);
	virtual void Render(float DeltaTime) override;
	void RenderViewportToolBarItems(int32 ViewportIndex);

private:
	bool OpenSceneFileDialog(FString& OutFilePath) const;
	bool SaveSceneFileDialog(FString& OutFilePath) const;
	void RenderFilesMenu();
	void RenderViewMenu();
	void RenderEditMenu();
	void RenderHelpMenu();
	void RenderAddActorMenu(int32 ViewportIndex);
	void RenderGizmoTools();
	FVector GetActorPlacementLocation(int32 ViewportIndex) const;

	FEditorViewportOverlayWidget* ViewportOverlayWidget = nullptr;
	FEditorSceneWidget* SceneWidget = nullptr;
	FEditorPlayStreamWidget* PlayStreamWidget = nullptr;
	FEditorContentDrawerWidget* ContentDrawerWidget = nullptr;
	int32 SpawnCount = 1;

	bool* bShowConsole = nullptr;
	bool* bShowControl = nullptr;
	bool* bShowProperty = nullptr;
	bool* bShowSceneManager = nullptr;
	bool* bShowMaterialEditor = nullptr;
	bool* bShowStatProfiler = nullptr;
	bool* bShowSkeletalMeshViewer = nullptr;
};
