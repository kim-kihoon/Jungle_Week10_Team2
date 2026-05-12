#include "Editor/UI/EditorConsoleWidget.h"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "Editor/EditorEngine.h"
#include "Editor/Settings/EditorSettings.h"
#include "Editor/Viewport/ViewportLayout.h"
#include "Engine/Object/FName.h"

namespace
{
	constexpr float EditorBottomBarHeight = 28.0f;
	constexpr float ConsoleDrawerAnimationSpeed = 12.0f;

	float EaseOutCubic(float Value)
	{
		Value = std::clamp(Value, 0.0f, 1.0f);
		const float Inv = 1.0f - Value;
		return 1.0f - Inv * Inv * Inv;
	}
}

FEditorConsoleWidget::FEditorConsoleWidget()
{
	FLogger::SetMessage(&FEditorConsoleWidget::AddMessage);

	RegisterCommand("stat", [this](const TArray<FString>& Args) { CmdStat(Args); });
	RegisterCommand("shadow_filter", [this](const TArray<FString>& Args) { CmdShadowFilter(Args); });
	RegisterCommand("shader_filter", [this](const TArray<FString>& Args) { CmdShadowFilter(Args); });
}

FEditorConsoleWidget::~FEditorConsoleWidget()
{
	FLogger::ClearMessage(&FEditorConsoleWidget::AddMessage);
	Clear();
	ClearHistory();
}

void FEditorConsoleWidget::AddLog(const char* fmt, ...)
{
	char buf[1024];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	AddMessage(buf);
}

void FEditorConsoleWidget::AddMessage(const char* Message)
{
	Messages.push_back(_strdup(Message));
	if (AutoScroll)
	{
		ScrollToBottom = true;
	}
}

void FEditorConsoleWidget::SetOpen(bool bInOpen)
{
	if (bInOpen && !bOpen)
	{
		bOpenedThisFrame = true;
		bRequestFocusInput = true;
	}
	bOpen = bInOpen;
}

void FEditorConsoleWidget::ToggleOpen()
{
	SetOpen(!bOpen);
}

bool FEditorConsoleWidget::ConsumeOpenRequest()
{
	const bool bResult = bOpenedThisFrame;
	bOpenedThisFrame = false;
	return bResult;
}

bool FEditorConsoleWidget::ShouldRender() const
{
	return bOpen || DrawerAnimationAlpha > 0.0f;
}

void FEditorConsoleWidget::CloseImmediately()
{
	bOpen = false;
	bOpenedThisFrame = false;
	DrawerAnimationAlpha = 0.0f;
}

void FEditorConsoleWidget::OpenFromDrawerTakeover(float InDrawerHeight)
{
	bOpen = true;
	bOpenedThisFrame = true;
	if (InDrawerHeight > 0.0f)
	{
		DrawerHeight = InDrawerHeight;
	}
	DrawerAnimationAlpha = 1.0f;
	bRequestFocusInput = true;
}

void FEditorConsoleWidget::Render(float DeltaTime)
{
	const float TargetAlpha = bOpen ? 1.0f : 0.0f;
	const float AlphaStep = std::clamp(DeltaTime * ConsoleDrawerAnimationSpeed, 0.0f, 1.0f);
	DrawerAnimationAlpha += (TargetAlpha - DrawerAnimationAlpha) * AlphaStep;

	if (!bOpen && DrawerAnimationAlpha < 0.001f)
	{
		DrawerAnimationAlpha = 0.0f;
	}
	else if (bOpen && DrawerAnimationAlpha > 0.999f)
	{
		DrawerAnimationAlpha = 1.0f;
	}

	if (!ShouldRender())
	{
		return;
	}

	const ImGuiViewport* Viewport = ImGui::GetMainViewport();
	const ImVec2 WorkPos = Viewport->WorkPos;
	const ImVec2 WorkSize = Viewport->WorkSize;
	const float AvailableHeight = std::max(120.0f, WorkSize.y - EditorBottomBarHeight);
	const float MinHeight = std::min(220.0f, AvailableHeight * 0.8f);
	const float MaxHeight = std::max(MinHeight, AvailableHeight * 0.82f);

	if (DrawerHeight <= 0.0f)
	{
		DrawerHeight = AvailableHeight * 0.35f;
	}
	DrawerHeight = std::clamp(DrawerHeight, MinHeight, MaxHeight);

	const float EasedAlpha = EaseOutCubic(DrawerAnimationAlpha);
	const float ClosedY = WorkPos.y + WorkSize.y - EditorBottomBarHeight;
	const float OpenY = ClosedY - DrawerHeight;
	const float AnimatedY = ClosedY + (OpenY - ClosedY) * EasedAlpha;

	ImGui::SetNextWindowViewport(Viewport->ID);
	ImGui::SetNextWindowPos(ImVec2(WorkPos.x, AnimatedY), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(WorkSize.x, DrawerHeight), ImGuiCond_Always);
	if (bRequestFocusInput)
	{
		ImGui::SetNextWindowFocus();
	}

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);

	constexpr ImGuiWindowFlags WindowFlags =
		ImGuiWindowFlags_NoDocking |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoSavedSettings;

	bool bWindowOpen = bOpen;
	if (!ImGui::Begin("Console", &bWindowOpen, WindowFlags))
	{
		ImGui::End();
		ImGui::PopStyleVar(2);
		if (bOpen && !bWindowOpen)
		{
			SetOpen(false);
		}
		return;
	}

	RenderResizeHandle(AvailableHeight);

	if (ImGui::SmallButton("Clear"))
	{
		Clear();
	}

	ImGui::Separator();

	if (ImGui::BeginPopup("Options"))
	{
		ImGui::Checkbox("Auto-scroll", &AutoScroll);
		ImGui::EndPopup();
	}

	ImGui::SetNextItemShortcut(ImGuiMod_Ctrl | ImGuiKey_O, ImGuiInputFlags_Tooltip);
	if (ImGui::Button("Options"))
	{
		ImGui::OpenPopup("Options");
	}
	ImGui::SameLine();
	Filter.Draw("Filter (\"incl,-excl\") (\"error\")", 180);
	ImGui::Separator();

	const float WindowBottom = ImGui::GetWindowPos().y + ImGui::GetWindowSize().y;
	const float BottomBarTop = Viewport->WorkPos.y + Viewport->WorkSize.y - EditorBottomBarHeight;
	const float BottomSafePadding = WindowBottom > BottomBarTop ? EditorBottomBarHeight : 0.0f;
	const float FooterHeight = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing() + BottomSafePadding;
	if (ImGui::BeginChild("ScrollingRegion", ImVec2(0, -FooterHeight), false, ImGuiWindowFlags_HorizontalScrollbar))
	{
		for (auto& Item : Messages)
		{
			if (!Filter.PassFilter(Item))
			{
				continue;
			}

			ImVec4 Color;
			bool bHasColor = false;
			if (strncmp(Item, "[ERROR]", 7) == 0)
			{
				Color = ImVec4(1, 0.4f, 0.4f, 1);
				bHasColor = true;
			}
			else if (strncmp(Item, "[WARN]", 6) == 0)
			{
				Color = ImVec4(1, 0.8f, 0.2f, 1);
				bHasColor = true;
			}
			else if (strncmp(Item, "#", 1) == 0)
			{
				Color = ImVec4(1, 0.8f, 0.6f, 1);
				bHasColor = true;
			}

			if (bHasColor)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, Color);
			}
			ImGui::TextUnformatted(Item);
			if (bHasColor)
			{
				ImGui::PopStyleColor();
			}
		}

		if (ScrollToBottom || (AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()))
		{
			ImGui::SetScrollHereY(1.0f);
		}
		ScrollToBottom = false;
	}
	ImGui::EndChild();
	ImGui::Separator();

	ImGuiInputTextFlags Flags = ImGuiInputTextFlags_EnterReturnsTrue
		| ImGuiInputTextFlags_EscapeClearsAll
		| ImGuiInputTextFlags_CallbackHistory
		| ImGuiInputTextFlags_CallbackCompletion
		| ImGuiInputTextFlags_CallbackCharFilter;

	if (bRequestFocusInput)
	{
		ImGui::SetKeyboardFocusHere(0);
		bRequestFocusInput = false;
	}

	if (ImGui::InputText("Input", InputBuf, sizeof(InputBuf), Flags, &TextEditCallback, this))
	{
		ExecCommand(InputBuf);
		strcpy_s(InputBuf, "");
	}

	ImGui::SetItemDefaultFocus();

	ImGui::End();
	ImGui::PopStyleVar(2);

	if (bOpen && !bWindowOpen)
	{
		SetOpen(false);
	}
}

void FEditorConsoleWidget::RenderResizeHandle(float WorkAreaHeight)
{
	const float HandleHeight = 6.0f;
	ImGui::InvisibleButton("##ConsoleDrawerResizeHandle", ImVec2(-1.0f, HandleHeight));

	const bool bHovered = ImGui::IsItemHovered();
	const bool bActive = ImGui::IsItemActive();
	if (bHovered || bActive)
	{
		ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
	}

	if (bActive)
	{
		DrawerHeight = std::clamp(
			DrawerHeight - ImGui::GetIO().MouseDelta.y,
			std::min(220.0f, WorkAreaHeight * 0.8f),
			std::max(220.0f, WorkAreaHeight * 0.82f));
	}

	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	const ImVec2 Min = ImGui::GetItemRectMin();
	const ImVec2 Max = ImGui::GetItemRectMax();
	DrawList->AddRectFilled(
		Min,
		Max,
		(bHovered || bActive) ? IM_COL32(120, 150, 190, 160) : IM_COL32(85, 90, 100, 110));
}

void FEditorConsoleWidget::RegisterCommand(const FString& Name, CommandFn Fn)
{
	Commands[Name] = Fn;
}

void FEditorConsoleWidget::ExecCommand(const char* CommandLine)
{
	AddLog("# %s\n", CommandLine);
	History.push_back(_strdup(CommandLine));
	HistoryPos = -1;

	TArray<FString> Tokens;
	std::istringstream Iss(CommandLine);
	FString Token;
	while (Iss >> Token)
	{
		Tokens.push_back(Token);
	}
	if (Tokens.empty())
	{
		return;
	}

	auto It = Commands.find(Tokens[0]);
	if (It != Commands.end())
	{
		It->second(Tokens);
	}
	else
	{
		AddLog("[ERROR] Unknown command: '%s'\n", Tokens[0].c_str());
	}
}

int32 FEditorConsoleWidget::TextEditCallback(ImGuiInputTextCallbackData* Data)
{
	FEditorConsoleWidget* Console = static_cast<FEditorConsoleWidget*>(Data->UserData);

	if (Data->EventFlag == ImGuiInputTextFlags_CallbackCharFilter)
	{
		if (Data->EventChar == L'`')
		{
			return 1;
		}
		return 0;
	}

	if (Data->EventFlag == ImGuiInputTextFlags_CallbackHistory)
	{
		const int32 PrevPos = Console->HistoryPos;
		if (Data->EventKey == ImGuiKey_UpArrow)
		{
			if (Console->HistoryPos == -1)
			{
				Console->HistoryPos = Console->History.Size - 1;
			}
			else if (Console->HistoryPos > 0)
			{
				Console->HistoryPos--;
			}
		}
		else if (Data->EventKey == ImGuiKey_DownArrow)
		{
			if (Console->HistoryPos != -1 &&
				++Console->HistoryPos >= Console->History.Size)
			{
				Console->HistoryPos = -1;
			}
		}

		if (PrevPos != Console->HistoryPos)
		{
			const char* HistoryStr = (Console->HistoryPos >= 0)
				? Console->History[Console->HistoryPos] : "";
			Data->DeleteChars(0, Data->BufTextLen);
			Data->InsertChars(0, HistoryStr);
		}
	}

	if (Data->EventFlag == ImGuiInputTextFlags_CallbackCompletion)
	{
		const char* WordEnd = Data->Buf + Data->CursorPos;
		const char* WordStart = WordEnd;
		while (WordStart > Data->Buf && WordStart[-1] != ' ')
		{
			WordStart--;
		}

		ImVector<const char*> Candidates;
		for (auto& Pair : Console->Commands)
		{
			const FString& Name = Pair.first;
			if (strncmp(Name.c_str(), WordStart, WordEnd - WordStart) == 0)
			{
				Candidates.push_back(Name.c_str());
			}
		}

		if (Candidates.Size == 1)
		{
			Data->DeleteChars(static_cast<int32>(WordStart - Data->Buf), static_cast<int32>(WordEnd - WordStart));
			Data->InsertChars(Data->CursorPos, Candidates[0]);
			Data->InsertChars(Data->CursorPos, " ");
		}
		else if (Candidates.Size > 1)
		{
			Console->AddLog("Possible completions:\n");
			for (auto& C : Candidates)
			{
				Console->AddLog("  %s\n", C);
			}
		}
	}

	return 0;
}

void FEditorConsoleWidget::CmdStat(const TArray<FString>& Args)
{
	if (Args.size() < 2)
	{
		AddLog("[WARN] Usage: stat <fps|memory|nametable|lightcull|shadow|none>\n");
		return;
	}

	if (!EditorEngine)
	{
		return;
	}

	FString Target = Args[1];
	std::transform(Target.begin(), Target.end(), Target.begin(), ::tolower);

	FEditorViewportLayout& Layout = EditorEngine->GetViewportLayout();
	const int32 FocusedIdx  = Layout.GetLastFocusedViewportIndex();

	if (Target == "fps")
	{
		bool& bFlag = Layout.GetViewportState(FocusedIdx).bShowStatFPS;
		bFlag = !bFlag;
		Layout.GetViewportState(FocusedIdx).UpdateStatOrder(EStatType::FPS, bFlag);
		AddLog("Stat FPS %s (viewport %d)\n", bFlag ? "Enabled" : "Disabled", FocusedIdx);
	}
	else if (Target == "memory")
	{
		bool& bFlag = Layout.GetViewportState(FocusedIdx).bShowStatMemory;
		bFlag = !bFlag;
		Layout.GetViewportState(FocusedIdx).UpdateStatOrder(EStatType::Memory, bFlag);
		AddLog("Stat Memory %s (viewport %d)\n", bFlag ? "Enabled" : "Disabled", FocusedIdx);
	}
	else if (Target == "nametable")
	{
		bool& bFlag = Layout.GetViewportState(FocusedIdx).bShowStatNameTable;
		bFlag = !bFlag;
		Layout.GetViewportState(FocusedIdx).UpdateStatOrder(EStatType::NameTable, bFlag);
		AddLog("Stat NameTable %s (viewport %d)\n", bFlag ? "Enabled" : "Disabled", FocusedIdx);
	}
	else if (Target == "lightcull")
	{
		bool& bFlag = Layout.GetViewportState(FocusedIdx).bShowStatLightCull;
		bFlag = !bFlag;
		Layout.GetViewportState(FocusedIdx).UpdateStatOrder(EStatType::LightCull, bFlag);
		AddLog("Stat LightCull %s (viewport %d)\n", bFlag ? "Enabled" : "Disabled", FocusedIdx);
	}
	else if (Target == "shadow")
	{
		bool& bFlag = Layout.GetViewportState(FocusedIdx).bShowStatShadow;
		bFlag = !bFlag;
		Layout.GetViewportState(FocusedIdx).UpdateStatOrder(EStatType::Shadow, bFlag);
		AddLog("Stat Shadow %s (viewport %d)\n", bFlag ? "Enabled" : "Disabled", FocusedIdx);
	}
	else if (Target == "shadowatlas")
	{
		bool& bFlag = Layout.GetViewportState(FocusedIdx).bShowStatShadowAtlas;
		bFlag = !bFlag;
		AddLog("Stat ShadowAtlas %s (viewport %d)\n", bFlag ? "Enabled" : "Disabled", FocusedIdx);
	}
	else if (Target == "none")
	{
		for (int32 i = 0; i < FEditorViewportLayout::MaxViewports; ++i)
		{
			auto& VS = Layout.GetViewportState(i);
			VS.bShowStatFPS = false;
			VS.bShowStatMemory = false;
			VS.bShowStatNameTable = false;
			VS.bShowStatLightCull = false;
			VS.bShowStatShadow = false;
			VS.bShowStatShadowAtlas = false;
			VS.ActiveStatOrder.clear();
		}
		AddLog("All Stats Disabled\n");
	}
}

void FEditorConsoleWidget::CmdShadowFilter(const TArray<FString>& Args)
{
	if (Args.size() < 2)
	{
		AddLog("[WARN] Usage: shadow_filter <pcf|vsm|esm>\n");
		return;
	}

	if (!EditorEngine)
	{
		return;
	}

	FString Target = Args[1];
	std::transform(Target.begin(), Target.end(), Target.begin(), ::tolower);
	FEditorSettings& Settings = EditorEngine->GetSettings();

	if (Target == "pcf")
	{
		Settings.ShadowFilterType = EShadowFilterType::PCF;
		AddLog("Shadow filter set to PCF\n");
	}
	else if (Target == "vsm")
	{
		Settings.ShadowFilterType = EShadowFilterType::VSM;
		AddLog("Shadow filter set to VSM\n");
	}
	else if (Target == "esm")
	{
		Settings.ShadowFilterType = EShadowFilterType::ESM;
		AddLog("Shadow filter set to ESM\n");
	}
	else
	{
		AddLog("[WARN] Usage: shadow_filter <pcf|vsm|esm>\n");
	}
}

ImVector<char*> FEditorConsoleWidget::Messages;
ImVector<char*> FEditorConsoleWidget::History;

bool FEditorConsoleWidget::AutoScroll = true;
bool FEditorConsoleWidget::ScrollToBottom = true;
