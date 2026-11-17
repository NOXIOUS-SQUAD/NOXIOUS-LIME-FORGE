#pragma once

#include "catalog.h"
#include "engine.h"

#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <atomic>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace lf {

constexpr UINT WM_APP_TASK = WM_APP + 1;

struct TaskResult {
  enum class Kind {
    Tweak, Action, RestoreAll, Pack, Clean, Report, Startup, Elevate, Custom, History
  };
  Kind kind = Kind::Tweak;
  bool ok = false;
  std::wstring message;
  std::wstring id;
  bool applied = false;
  bool closeAfter = false;
  bool wasApply = false;
  bool historyCandidate = false;
  std::vector<std::wstring> changedIds;
  std::vector<Engine::StartupEntry> startup;
};

class App {
 public:
  App();
  ~App();

  bool Initialize(HWND hwnd, ID2D1Factory* d2dFactory, IDWriteFactory* dwriteFactory,
                  float scale);
  void Shutdown();
  void Resize(UINT pixelWidth, UINT pixelHeight, float scale);
  void Paint();
  void OnMouseMove(float pixelX, float pixelY);
  void OnMouseLeave();
  void OnLButtonDown(float pixelX, float pixelY);
  void OnLButtonUp(float pixelX, float pixelY);
  void OnMouseWheel(int delta);
  void OnKeyDown(WPARAM key, bool ctrl);
  void OnChar(wchar_t ch);
  void OnTimer();
  void OnTaskDone(void* payload);
  void OnDpiChanged(float scale);

  bool CanClose() const { return !busy_; }
  bool CloseRequested() const { return closeRequested_; }
  bool ConsumeCloseRequested();
  void ShowToast(bool ok, const std::wstring& message);
  void RequestCloseBlocked();

 private:
  enum class View { Home, Tweaks, Cleaner, Startup, Diagnostics, Settings };
  enum class HitKind {
    None, Nav, Card, Toggle, Action, Detail, Button, Search, WindowMin, WindowMax,
    WindowClose, Filter, Modal, Clean
  };
  struct Hit {
    D2D1_RECT_F rect{};
    HitKind kind = HitKind::None;
    std::wstring payload;
    bool inContent = false;
    bool inModal = false;
  };
  enum class ModalKind { None, Confirm, Detail, Elevate, RestoreAll, Pack, Info };
  struct Modal {
    ModalKind kind = ModalKind::None;
    std::wstring title;
    std::wstring body;
    std::wstring primaryLabel;
    std::wstring secondaryLabel;
    std::wstring primaryPayload;
    std::wstring id;
    bool destructive = false;
  };
  struct HistoryOp {
    std::wstring id;
    bool apply = false;
  };
  struct HistoryEntry {
    std::vector<HistoryOp> ops;
    std::wstring label;
  };

  template <typename T>
  using ComPtr = Microsoft::WRL::ComPtr<T>;

  void CreateDeviceResources();
  void DiscardDeviceResources();
  void UpdateMetrics();
  void ReloadApplied();
  void SpawnTask(std::function<TaskResult()> task);

  void StartToggle(const Tweak& tweak, bool apply);
  void StartAction(const Tweak& tweak);
  void StartPack();
  void StartRestoreAll();
  void StartClean(const std::wstring& key);
  void StartReport();
  void StartStartupScan();
  void StartElevate();
  void StartCustom(const std::wstring& label, const std::wstring& script, bool needsAdmin);
  void StartHistory(const HistoryEntry& entry, bool undo);
  void UndoLast();
  void RedoLast();
  void PushUndo(HistoryEntry entry);
  void PushRedo(HistoryEntry entry);
  void SampleMetrics();
  void ToggleSetting(const std::wstring& key);

  void RequestToggle(const Tweak& tweak);
  void RequestAction(const Tweak& tweak);
  void OpenConfirm(const Tweak& tweak, bool apply);
  void OpenConfirmAction(const Tweak& tweak);
  void OpenDetail(const Tweak& tweak);
  void OpenElevate(const std::wstring& reason);
  void OpenPackConfirm();
  void OpenRestoreConfirm();
  void OpenInfo(const std::wstring& title, const std::wstring& body);
  void CloseModal();
  void RunModalPrimary();

  const Tweak* FindTweak(const std::wstring& id) const;
  bool IsAdminNow() const;
  bool AnyAppliedNeedsAdmin() const;
  bool RestartPending() const;
  bool NeedsConfirmation(const Tweak& tweak) const;
  std::vector<const Tweak*> FilteredTweaks() const;
  int AppliedCount() const { return static_cast<int>(applied_.size()); }
  int ToggleableCount() const { return toggleableCount_; }

  void GoHome();
  void GoTweaks(const std::wstring& category);
  void GoView(View view);
  void ChangeView(View view);
  void ClampScroll();

  void DrawTitleBar();
  void DrawSidebar();
  float DashboardHeight(float width) const;
  void DrawDashboard(float left, float top, float width, float shift);
  void DrawHome(const D2D1_RECT_F& content);
  void DrawTweaks(const D2D1_RECT_F& content);
  void DrawCleaner(const D2D1_RECT_F& content);
  void DrawStartup(const D2D1_RECT_F& content);
  void DrawDiagnostics(const D2D1_RECT_F& content);
  void DrawSettings(const D2D1_RECT_F& content);
  void DrawStatusBar();
  void DrawScrollbar(const D2D1_RECT_F& content);
  void DrawModal();
  void DrawBusyOverlay();
  void DrawToast();

  float DrawSectionTitle(const std::wstring& eyebrow, const std::wstring& title,
                         const std::wstring& subtitle, float x, float y, float width);
  void DrawButton(const D2D1_RECT_F& rect, const std::wstring& text, bool primary,
                  bool hovered, bool compact = false);
  void DrawToggle(const D2D1_RECT_F& rect, bool on, bool hovered);
  void DrawChip(const D2D1_RECT_F& rect, const std::wstring& text, bool active,
                bool hovered);
  void DrawBadge(float x, float y, const std::wstring& text, const std::wstring& glyph,
                 D2D1_COLOR_F color);
  float MeasureText(const std::wstring& text, float width, float size,
                    DWRITE_FONT_WEIGHT weight, int maxLines = 0, bool wrap = true);
  void DrawTextBlock(const std::wstring& text, const D2D1_RECT_F& rect, float size,
                     DWRITE_FONT_WEIGHT weight, D2D1_COLOR_F color,
                     DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING,
                     DWRITE_PARAGRAPH_ALIGNMENT valign = DWRITE_PARAGRAPH_ALIGNMENT_NEAR,
                     int maxLines = 0, bool wrap = true);
  void DrawIcon(const std::wstring& glyph, const D2D1_RECT_F& rect, float size,
                D2D1_COLOR_F color,
                DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_CENTER);
  void DrawBolt(const D2D1_RECT_F& rect, D2D1_COLOR_F color);
  IDWriteTextFormat* GetFormat(float size, DWRITE_FONT_WEIGHT weight,
                               DWRITE_TEXT_ALIGNMENT align,
                               DWRITE_PARAGRAPH_ALIGNMENT valign, bool wrap, bool icons);
  IDWriteTextFormat* GetMonoFormat(float size);
  void DrawRoundRect(const D2D1_RECT_F& rect, float radius, D2D1_COLOR_F fill,
                     bool border = false, D2D1_COLOR_F borderColor = D2D1::ColorF(0, 0),
                     float stroke = 1.0f);
  void DrawLine(float x1, float y1, float x2, float y2, D2D1_COLOR_F color,
                float stroke = 1.0f);
  void DrawCircle(float x, float y, float radius, D2D1_COLOR_F color, bool fill);
  void AddHit(HitKind kind, const D2D1_RECT_F& rect, const std::wstring& payload);
  Hit HitTest(float x, float y, bool modalOnly) const;
  void SetHoverFromMouse();
  void UpdateHover(const std::wstring& key);
  void UpdateCursor();
  void ClearHits();

  std::wstring HitKey(const Hit& hit) const;
  static bool ContainsCI(const std::wstring& haystack, const std::wstring& needle);
  static std::wstring Lower(std::wstring value);
  static std::wstring Trim(const std::wstring& value);
  static std::wstring UriOpen();

  HWND hwnd_ = nullptr;
  ID2D1Factory* d2dFactory_ = nullptr;
  IDWriteFactory* dwriteFactory_ = nullptr;
  ComPtr<ID2D1HwndRenderTarget> rt_;
  ComPtr<ID2D1SolidColorBrush> brush_;

  float scale_ = 1.0f;
  float width_ = 1200.0f;
  float height_ = 800.0f;
  UINT pixelWidth_ = 0;
  UINT pixelHeight_ = 0;

  std::vector<Tweak> tweaks_;
  std::map<std::wstring, size_t> indexById_;
  std::vector<std::wstring> categories_;
  std::map<std::wstring, int> categoryCounts_;
  int toggleableCount_ = 0;
  Engine engine_;
  bool engineReady_ = false;
  bool isAdmin_ = false;

  std::set<std::wstring> applied_;
  bool confirmRisky_ = true;
  bool showDashboard_ = true;
  bool startWithWindows_ = false;
  View view_ = View::Home;
  std::wstring category_ = L"Todas";
  std::wstring search_;
  std::wstring filter_ = L"all";
  bool searchFocused_ = false;
  float scroll_ = 0.0f;
  float contentHeight_ = 0.0f;
  float contentTop_ = 0.0f;
  float contentBottom_ = 0.0f;
  float sidebarWidth_ = 252.0f;
  float titleHeight_ = 58.0f;
  float statusHeight_ = 34.0f;

  std::vector<Hit> hits_;
  std::wstring hoverKey_;
  float mouseX_ = -1000.0f;
  float mouseY_ = -1000.0f;
  bool mouseDown_ = false;
  bool addingContent_ = false;
  bool addingModal_ = false;
  ULONGLONG lastTitleClick_ = 0;

  Modal modal_;
  std::vector<HistoryEntry> undoStack_;
  std::vector<HistoryEntry> redoStack_;
  std::optional<HistoryEntry> pendingHistory_;
  bool pendingHistoryUndo_ = false;
  std::wstring toast_;
  bool toastOk_ = true;
  ULONGLONG toastUntil_ = 0;
  std::wstring lastCleanMessage_;

  std::vector<Engine::StartupEntry> startup_;
  bool startupLoaded_ = false;
  bool startupLoading_ = false;
  bool reportLoaded_ = false;
  std::wstring report_;
  bool reportLoading_ = false;

  std::atomic<bool> busy_{false};
  std::wstring busyLabel_;
  std::thread worker_;
  bool closeRequested_ = false;

  unsigned long long tick_ = 0;
  float memPercent_ = 0.0f;
  float cpuPercent_ = 0.0f;
  float memUsedGb_ = 0.0f;
  float memTotalGb_ = 0.0f;
  float diskUsedPercent_ = 0.0f;
  float diskFreeGb_ = 0.0f;
  float diskTotalGb_ = 0.0f;
  int processCount_ = 0;
  int logicalCpus_ = 0;
  std::wstring uptimeText_;
  std::vector<float> cpuHistory_;
  std::vector<float> memHistory_;
  ULONGLONG lastCpuIdle_ = 0;
  ULONGLONG lastCpuKernel_ = 0;
  ULONGLONG lastCpuUser_ = 0;
  bool cpuPrimed_ = false;

  std::map<unsigned long long, ComPtr<IDWriteTextFormat>> formats_;
};

}  // namespace lf
