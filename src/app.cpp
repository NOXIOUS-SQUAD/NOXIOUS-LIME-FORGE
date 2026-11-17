#include "app.h"

#include "theme.h"

#include <d2d1helper.h>
#include <psapi.h>
#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <memory>
#include <sstream>

namespace lf {
namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kCardGap = 16.0f;

D2D1_RECT_F MakeRect(float x, float y, float w, float h) {
  return D2D1::RectF(x, y, x + w, y + h);
}

bool ContainsPoint(const D2D1_RECT_F& rect, float x, float y) {
  return x >= rect.left && x <= rect.right && y >= rect.top && y <= rect.bottom;
}

float ClampF(float value, float low, float high) {
  return value < low ? low : (value > high ? high : value);
}

float RW(const D2D1_RECT_F& rect) { return rect.right - rect.left; }
float RH(const D2D1_RECT_F& rect) { return rect.bottom - rect.top; }

D2D1_COLOR_F WithAlpha(D2D1_COLOR_F color, float alpha) {
  color.a = alpha;
  return color;
}

std::wstring IntText(int value) { return std::to_wstring(value); }

std::wstring Fixed1(float value) {
  wchar_t buffer[32]{};
  swprintf_s(buffer, L"%.1f", value);
  return buffer;
}

std::wstring UptimeText(ULONGLONG milliseconds) {
  const ULONGLONG totalMinutes = milliseconds / 60000ULL;
  const ULONGLONG days = totalMinutes / (60ULL * 24ULL);
  const ULONGLONG hours = (totalMinutes / 60ULL) % 24ULL;
  const ULONGLONG minutes = totalMinutes % 60ULL;
  if (days > 0)
    return IntText(static_cast<int>(days)) + L"d " + IntText(static_cast<int>(hours)) + L"h";
  if (hours > 0)
    return IntText(static_cast<int>(hours)) + L"h " + IntText(static_cast<int>(minutes)) + L"m";
  return IntText(static_cast<int>(minutes)) + L" min";
}

std::wstring ShortenForCard(const std::wstring& text, size_t maxChars) {
  if (text.size() <= maxChars) return text;
  size_t cut = maxChars;
  while (cut > 0 && cut < text.size() && !iswspace(text[cut])) --cut;
  if (cut < maxChars / 2) cut = maxChars;
  std::wstring out = text.substr(0, cut);
  while (!out.empty() && (out.back() == L' ' || out.back() == L'\r' || out.back() == L'\n')) {
    out.pop_back();
  }
  return out + L"\u2026";
}

std::wstring FirstLineOf(const std::wstring& text) {
  size_t start = text.find_first_not_of(L"\r\n ");
  if (start == std::wstring::npos) return text;
  size_t end = text.find_first_of(L"\r\n", start);
  std::wstring line = text.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
  if (line.size() > 150) line = line.substr(0, 147) + L"...";
  return line;
}

int CategoryRank(const std::wstring& category) {
  static const wchar_t* order[] = {L"General",          L"Network",
                                   L"Hardware",         L"Priorizar juegos",
                                   L"Windows Custom",   L"Almacenamiento",
                                   L"Accesos directos"};
  for (int i = 0; i < static_cast<int>(sizeof(order) / sizeof(order[0])); ++i) {
    if (category == order[i]) return i;
  }
  return 100;
}

const wchar_t* CategoryGlyph(const std::wstring& category) {
  if (category == L"General") return L"\uE713";
  if (category == L"Network") return L"\uE774";
  if (category == L"Hardware") return L"\uE772";
  if (category == L"Priorizar juegos") return L"\uE7FC";
  if (category == L"Windows Custom") return L"\uE771";
  if (category == L"Almacenamiento") return L"\uE8B7";
  if (category == L"Accesos directos") return L"\uE71B";
  return L"\uE9D9";
}

const wchar_t* ToolGlyph(const std::wstring& key) {
  if (key == L"temp") return L"\uE74D";
  if (key == L"recycle") return L"\uE74D";
  if (key == L"dns") return L"\uE774";
  if (key == L"browsers") return L"\uE774";
  if (key == L"wu") return L"\uE895";
  if (key == L"prefetch") return L"\uE896";
  if (key == L"logs") return L"\uE8A5";
  return L"\uE74D";
}

float BadgeWidth(const wchar_t* glyph, const std::wstring& text) {
  float width = 18.0f + static_cast<float>(text.size()) * 6.35f;
  if (glyph && glyph[0]) width += 14.0f;
  return width;
}

void CopyTextToClipboard(HWND hwnd, const std::wstring& text) {
  if (text.empty() || !OpenClipboard(hwnd)) return;
  EmptyClipboard();
  const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
  HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
  if (memory) {
    void* target = GlobalLock(memory);
    if (target) {
      std::memcpy(target, text.c_str(), bytes);
      GlobalUnlock(memory);
      if (!SetClipboardData(CF_UNICODETEXT, memory)) GlobalFree(memory);
    } else {
      GlobalFree(memory);
    }
  }
  CloseClipboard();
}

}  // namespace

App::App() = default;

App::~App() {
  if (worker_.joinable()) worker_.join();
}

bool App::Initialize(HWND hwnd, ID2D1Factory* d2dFactory, IDWriteFactory* dwriteFactory,
                     float scale) {
  hwnd_ = hwnd;
  d2dFactory_ = d2dFactory;
  dwriteFactory_ = dwriteFactory;
  scale_ = scale > 0.0f ? scale : 1.0f;

  tweaks_ = BuildCatalog();
  categories_.clear();
  categoryCounts_.clear();
  toggleableCount_ = 0;
  for (size_t i = 0; i < tweaks_.size(); ++i) {
    indexById_[tweaks_[i].id] = i;
    const std::wstring& category = tweaks_[i].category;
    if (categoryCounts_.find(category) == categoryCounts_.end()) {
      categories_.push_back(category);
      categoryCounts_[category] = 0;
    }
    categoryCounts_[category] += 1;
    if (!tweaks_[i].actionOnly) toggleableCount_ += 1;
  }
  std::stable_sort(categories_.begin(), categories_.end(),
                   [](const std::wstring& a, const std::wstring& b) {
                     const int ra = CategoryRank(a);
                     const int rb = CategoryRank(b);
                     if (ra != rb) return ra < rb;
                     return a < b;
                   });

  RECT client{};
  GetClientRect(hwnd_, &client);
  pixelWidth_ = static_cast<UINT>(client.right - client.left);
  pixelHeight_ = static_cast<UINT>(client.bottom - client.top);
  width_ = static_cast<float>(pixelWidth_) / scale_;
  height_ = static_cast<float>(pixelHeight_) / scale_;
  isAdmin_ = Engine::IsProcessElevated();

  std::wstring error;
  engineReady_ = engine_.Initialize(error);
  if (!engineReady_) {
    ShowToast(false, error.empty() ? L"No se pudo inicializar el motor." : error);
  } else {
    applied_ = engine_.AppliedIds();
    for (const Tweak& tweak : tweaks_) {
      if (tweak.reg.empty()) continue;
      if (applied_.count(tweak.id)) continue;
      if (engine_.DetectRegistryApplied(tweak)) {
        applied_.insert(tweak.id);
        engine_.MarkApplied(tweak.id, true);
      }
    }
  }

  confirmRisky_ = engine_.GetSetting(L"confirmations", true);
  showDashboard_ = engine_.GetSetting(L"dashboard", true);
  startWithWindows_ = engine_.IsStartWithWindows();
  SampleMetrics();
  UpdateMetrics();
  return true;
}

void App::Shutdown() {
  if (worker_.joinable()) worker_.join();
  engine_.Shutdown();
  DiscardDeviceResources();
  tweaks_.clear();
  indexById_.clear();
  formats_.clear();
  hits_.clear();
  hwnd_ = nullptr;
}

void App::UpdateMetrics() {
  sidebarWidth_ = 252.0f;
  titleHeight_ = 58.0f;
  statusHeight_ = 34.0f;
  contentTop_ = titleHeight_;
  contentBottom_ = height_ - statusHeight_;
}

void App::SampleMetrics() {
  MEMORYSTATUSEX memory{};
  memory.dwLength = sizeof(memory);
  if (GlobalMemoryStatusEx(&memory)) {
    memPercent_ = static_cast<float>(memory.dwMemoryLoad);
    memTotalGb_ = static_cast<float>(memory.ullTotalPhys) / (1024.0f * 1024.0f * 1024.0f);
    memUsedGb_ = static_cast<float>(memory.ullTotalPhys - memory.ullAvailPhys) /
                 (1024.0f * 1024.0f * 1024.0f);
  }

  FILETIME idleTime{}, kernelTime{}, userTime{};
  if (GetSystemTimes(&idleTime, &kernelTime, &userTime)) {
    ULARGE_INTEGER idle{}, kernel{}, user{};
    idle.LowPart = idleTime.dwLowDateTime;
    idle.HighPart = idleTime.dwHighDateTime;
    kernel.LowPart = kernelTime.dwLowDateTime;
    kernel.HighPart = kernelTime.dwHighDateTime;
    user.LowPart = userTime.dwLowDateTime;
    user.HighPart = userTime.dwHighDateTime;
    if (cpuPrimed_) {
      const unsigned long long kernelDelta = kernel.QuadPart - lastCpuKernel_;
      const unsigned long long userDelta = user.QuadPart - lastCpuUser_;
      const unsigned long long idleDelta = idle.QuadPart - lastCpuIdle_;
      const unsigned long long total = kernelDelta + userDelta;
      if (total > 0) {
        const double busy =
            static_cast<double>(total - std::min(total, idleDelta));
        cpuPercent_ = static_cast<float>(busy * 100.0 / static_cast<double>(total));
      }
    }
    lastCpuIdle_ = idle.QuadPart;
    lastCpuKernel_ = kernel.QuadPart;
    lastCpuUser_ = user.QuadPart;
    cpuPrimed_ = true;
  }

  SYSTEM_INFO systemInfo{};
  GetNativeSystemInfo(&systemInfo);
  logicalCpus_ = static_cast<int>(systemInfo.dwNumberOfProcessors);

  PERFORMANCE_INFORMATION performance{};
  if (GetPerformanceInfo(&performance, sizeof(performance)))
    processCount_ = static_cast<int>(performance.ProcessCount);

  wchar_t windowsDir[MAX_PATH]{};
  const UINT length = GetWindowsDirectoryW(windowsDir, MAX_PATH);
  if (length >= 3) {
    const std::wstring root(windowsDir, 3);
    ULARGE_INTEGER freeBytes{}, totalBytes{}, totalFreeBytes{};
    if (GetDiskFreeSpaceExW(root.c_str(), &freeBytes, &totalBytes, &totalFreeBytes)) {
      const double total = static_cast<double>(totalBytes.QuadPart);
      const double free = static_cast<double>(freeBytes.QuadPart);
      diskTotalGb_ = static_cast<float>(total / (1024.0 * 1024.0 * 1024.0));
      diskFreeGb_ = static_cast<float>(free / (1024.0 * 1024.0 * 1024.0));
      diskUsedPercent_ =
          total > 0.0 ? static_cast<float>((total - free) * 100.0 / total) : 0.0f;
    }
  }

  uptimeText_ = UptimeText(GetTickCount64());

  cpuHistory_.push_back(cpuPercent_);
  memHistory_.push_back(memPercent_);
  constexpr size_t kHistoryLimit = 90;
  if (cpuHistory_.size() > kHistoryLimit) cpuHistory_.erase(cpuHistory_.begin());
  if (memHistory_.size() > kHistoryLimit) memHistory_.erase(memHistory_.begin());
}

void App::CreateDeviceResources() {
  if (rt_) return;
  if (!d2dFactory_ || !hwnd_) return;

  RECT client{};
  GetClientRect(hwnd_, &client);
  D2D1_SIZE_U size = D2D1::SizeU(static_cast<UINT>(client.right),
                                 static_cast<UINT>(client.bottom));
  D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties();
  properties.dpiX = 96.0f * scale_;
  properties.dpiY = 96.0f * scale_;
  D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProperties =
      D2D1::HwndRenderTargetProperties(hwnd_, size, D2D1_PRESENT_OPTIONS_NONE);
  if (FAILED(d2dFactory_->CreateHwndRenderTarget(properties, hwndProperties, &rt_))) {
    rt_.Reset();
    return;
  }
  rt_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
  rt_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
  rt_->CreateSolidColorBrush(theme::kText, &brush_);
}

void App::DiscardDeviceResources() {
  brush_.Reset();
  rt_.Reset();
}

void App::Resize(UINT pixelWidth, UINT pixelHeight, float scale) {
  pixelWidth_ = pixelWidth;
  pixelHeight_ = pixelHeight;
  scale_ = scale > 0.0f ? scale : 1.0f;
  width_ = static_cast<float>(pixelWidth) / scale_;
  height_ = static_cast<float>(pixelHeight) / scale_;
  UpdateMetrics();
  if (rt_) {
    rt_->SetDpi(96.0f * scale_, 96.0f * scale_);
    rt_->Resize(D2D1::SizeU(pixelWidth, pixelHeight));
  }
}

void App::OnDpiChanged(float scale) {
  scale_ = scale > 0.0f ? scale : 1.0f;
  UpdateMetrics();
  if (rt_) rt_->SetDpi(96.0f * scale_, 96.0f * scale_);
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::Paint() {
  if (!hwnd_) return;
  UpdateMetrics();
  CreateDeviceResources();
  if (!rt_ || !brush_) return;

  hits_.clear();
  rt_->BeginDraw();
  rt_->SetTransform(D2D1::Matrix3x2F::Identity());
  rt_->Clear(theme::kBg);

  DrawTitleBar();
  DrawSidebar();

  D2D1_RECT_F content =
      D2D1::RectF(sidebarWidth_, contentTop_, width_, contentBottom_);
  rt_->PushAxisAlignedClip(content, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
  addingContent_ = true;
  switch (view_) {
    case View::Home:
      DrawHome(content);
      break;
    case View::Tweaks:
      DrawTweaks(content);
      break;
    case View::Cleaner:
      DrawCleaner(content);
      break;
    case View::Startup:
      DrawStartup(content);
      break;
    case View::Diagnostics:
      DrawDiagnostics(content);
      break;
    case View::Settings:
      DrawSettings(content);
      break;
  }
  addingContent_ = false;
  DrawScrollbar(content);
  rt_->PopAxisAlignedClip();

  DrawStatusBar();

  if (modal_.kind != ModalKind::None) DrawModal();
  if (busy_) DrawBusyOverlay();
  if (!toast_.empty() && GetTickCount64() < toastUntil_) DrawToast();

  HRESULT result = rt_->EndDraw();
  if (result == D2DERR_RECREATE_TARGET) {
    DiscardDeviceResources();
    InvalidateRect(hwnd_, nullptr, FALSE);
  }
}

void App::DrawTitleBar() {
  const D2D1_RECT_F bar = MakeRect(0.0f, 0.0f, width_, titleHeight_);
  DrawRoundRect(bar, 0.0f, theme::kTitle);
  DrawLine(0.0f, titleHeight_ - 0.5f, width_, titleHeight_ - 0.5f, theme::kBorderSoft);

  DrawRoundRect(MakeRect(16.0f, 14.0f, 29.0f, 29.0f), 8.0f, theme::kLime);
  DrawBolt(MakeRect(21.0f, 17.0f, 19.0f, 23.0f), theme::kBlack);

  DrawTextBlock(L"LimeForge", MakeRect(55.0f, 12.0f, 160.0f, 36.0f), 16.0f,
                DWRITE_FONT_WEIGHT_SEMI_BOLD, theme::kText,
                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
  DrawRoundRect(MakeRect(148.0f, 21.0f, 42.0f, 18.0f), 9.0f,
                WithAlpha(theme::kLime, 0.09f), true, WithAlpha(theme::kLime, 0.28f));
  DrawTextBlock(L"v1.1", MakeRect(148.0f, 20.0f, 42.0f, 19.0f), 9.0f,
                DWRITE_FONT_WEIGHT_SEMI_BOLD, theme::kLimeDim,
                DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

  auto hovered = [&](HitKind kind, const std::wstring& payload) {
    Hit hit;
    hit.kind = kind;
    hit.payload = payload;
    return hoverKey_ == HitKey(hit);
  };

  auto historyButton = [&](float x, float width, const wchar_t* glyph,
                           const wchar_t* label, bool enabled, const wchar_t* payload) {
    const D2D1_RECT_F rect = MakeRect(x, 16.0f, width, 26.0f);
    const bool hover = enabled && hovered(HitKind::Button, payload);
    DrawRoundRect(rect, 8.0f, hover ? theme::kSurface3 : theme::kSurface2, true,
                  hover ? WithAlpha(theme::kLime, 0.35f) : theme::kBorderSoft);
    DrawIcon(glyph, MakeRect(rect.left + 8.0f, rect.top + 6.0f, 14.0f, 14.0f), 10.0f,
             enabled ? theme::kLimeDim : theme::kTextFaint);
    DrawTextBlock(label, MakeRect(rect.left + 27.0f, rect.top, width - 32.0f, RH(rect)),
                  9.2f, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                  enabled ? theme::kTextDim : theme::kTextFaint,
                  DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, 1,
                  false);
    if (enabled) AddHit(HitKind::Button, rect, payload);
  };
  historyButton(230.0f, 102.0f, L"\uE7A7", L"Deshacer", !undoStack_.empty(), L"undo");
  historyButton(340.0f, 98.0f, L"\uE7A6", L"Rehacer", !redoStack_.empty(), L"redo");

  const float controlsWidth = 138.0f;
  if (isAdmin_) {
    const float chipWidth = 88.0f;
    const float chipX = width_ - controlsWidth - chipWidth - 12.0f;
    DrawRoundRect(MakeRect(chipX, 17.0f, chipWidth, 24.0f), 12.0f,
                  WithAlpha(theme::kLime, 0.08f), true, WithAlpha(theme::kLime, 0.28f));
    DrawIcon(L"\uE72E", MakeRect(chipX + 8.0f, 19.0f, 18.0f, 20.0f), 11.0f, theme::kLime);
    DrawTextBlock(L"ADMIN", MakeRect(chipX + 26.0f, 19.0f, 56.0f, 20.0f), 9.0f,
                  DWRITE_FONT_WEIGHT_SEMI_BOLD, theme::kLime);
  }

  const D2D1_RECT_F minRect = MakeRect(width_ - 138.0f, 0.0f, 46.0f, titleHeight_);
  const D2D1_RECT_F maxRect = MakeRect(width_ - 92.0f, 0.0f, 46.0f, titleHeight_);
  const D2D1_RECT_F closeRect = MakeRect(width_ - 46.0f, 0.0f, 46.0f, titleHeight_);

  const bool minHover = hovered(HitKind::WindowMin, L"min");
  const bool maxHover = hovered(HitKind::WindowMax, L"max");
  const bool closeHover = hovered(HitKind::WindowClose, L"close");

  if (minHover) DrawRoundRect(minRect, 0.0f, theme::kSurface3);
  if (maxHover) DrawRoundRect(maxRect, 0.0f, theme::kSurface3);
  if (closeHover) DrawRoundRect(closeRect, 0.0f, theme::kRed);

  const D2D1_COLOR_F controlColor = closeHover ? theme::kBlack : theme::kTextDim;
  DrawIcon(L"\uE921", MakeRect(minRect.left, 18.0f, 46.0f, 22.0f), 10.0f, controlColor);
  DrawIcon(IsZoomed(hwnd_) ? L"\uE923" : L"\uE922",
           MakeRect(maxRect.left, 18.0f, 46.0f, 22.0f), 10.0f, controlColor);
  DrawIcon(L"\uE711", MakeRect(closeRect.left, 18.0f, 46.0f, 22.0f), 11.0f, controlColor);

  AddHit(HitKind::WindowMin, minRect, L"min");
  AddHit(HitKind::WindowMax, maxRect, L"max");
  AddHit(HitKind::WindowClose, closeRect, L"close");
}

void App::DrawSidebar() {
  const D2D1_RECT_F panel =
      MakeRect(0.0f, titleHeight_, sidebarWidth_, contentBottom_ - titleHeight_);
  DrawRoundRect(panel, 0.0f, theme::kSidebar);
  DrawLine(sidebarWidth_ - 0.5f, titleHeight_, sidebarWidth_ - 0.5f, contentBottom_,
           theme::kBorderSoft);

  auto hovered = [&](HitKind kind, const std::wstring& payload) {
    Hit hit;
    hit.kind = kind;
    hit.payload = payload;
    return hoverKey_ == HitKey(hit);
  };

  struct NavDef {
    View view;
    const wchar_t* label;
    const wchar_t* glyph;
    const wchar_t* payload;
  };
  static const NavDef navs[] = {
      {View::Home, L"Inicio", L"\uE80F", L"view:Home"},
      {View::Tweaks, L"Tweaks", L"\uE7FC", L"view:Tweaks"},
      {View::Cleaner, L"Limpieza", L"\uE74D", L"view:Cleaner"},
      {View::Startup, L"Inicio de Windows", L"\uE768", L"view:Startup"},
      {View::Diagnostics, L"Diagnóstico", L"\uE9D9", L"view:Diagnostics"},
      {View::Settings, L"Ajustes", L"\uE713", L"view:Settings"},
  };

  float y = titleHeight_ + 18.0f;
  DrawTextBlock(L"PRINCIPAL", MakeRect(22.0f, y, 180.0f, 16.0f), 9.0f,
                DWRITE_FONT_WEIGHT_SEMI_BOLD, theme::kTextFaint);
  y += 20.0f;

  for (const NavDef& nav : navs) {
    const D2D1_RECT_F rect = MakeRect(14.0f, y, sidebarWidth_ - 28.0f, 34.0f);
    const bool active = view_ == nav.view;
    const bool hover = hovered(HitKind::Nav, nav.payload);
    if (active) {
      DrawRoundRect(rect, 10.0f, WithAlpha(theme::kLime, 0.09f));
      DrawRoundRect(MakeRect(rect.left, rect.top + 8.0f, 3.0f, 18.0f), 1.5f, theme::kLime);
    } else if (hover) {
      DrawRoundRect(rect, 10.0f, theme::kSurface);
    }

    const D2D1_COLOR_F fg = active ? theme::kLime : (hover ? theme::kText : theme::kTextDim);
    DrawIcon(nav.glyph, MakeRect(rect.left + 13.0f, rect.top + 7.0f, 20.0f, 20.0f), 13.0f, fg);
    DrawTextBlock(nav.label, MakeRect(rect.left + 42.0f, rect.top, 145.0f, 34.0f), 12.5f,
                  active ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL, fg,
                  DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    if (nav.view == View::Tweaks) {
      const std::wstring badge = IntText(static_cast<int>(tweaks_.size()));
      DrawTextBlock(badge, MakeRect(rect.right - 38.0f, rect.top, 28.0f, 34.0f), 10.0f,
                    DWRITE_FONT_WEIGHT_SEMI_BOLD, active ? theme::kLimeDim : theme::kTextFaint,
                    DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
    AddHit(HitKind::Nav, rect, nav.payload);
    y += 36.0f;
  }

  y += 12.0f;
  DrawTextBlock(L"CATEGORÍAS", MakeRect(22.0f, y, 180.0f, 16.0f), 9.0f,
                DWRITE_FONT_WEIGHT_SEMI_BOLD, theme::kTextFaint);
  y += 20.0f;

  for (const std::wstring& category : categories_) {
    const D2D1_RECT_F rect = MakeRect(22.0f, y, sidebarWidth_ - 38.0f, 26.0f);
    const bool active = view_ == View::Tweaks && category_ == category;
    const std::wstring payload = L"cat:" + category;
    const bool hover = hovered(HitKind::Nav, payload);
    if (active) {
      DrawRoundRect(rect, 8.0f, WithAlpha(theme::kLime, 0.08f));
    } else if (hover) {
      DrawRoundRect(rect, 8.0f, theme::kSurface);
    }
    DrawCircle(rect.left + 8.0f, rect.top + 13.0f, 2.6f,
               active ? theme::kLime : theme::kTextFaint, true);
    DrawTextBlock(category, MakeRect(rect.left + 18.0f, rect.top, 150.0f, 26.0f), 11.2f,
                  active ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
                  active ? theme::kLime : (hover ? theme::kText : theme::kTextDim),
                  DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    DrawTextBlock(IntText(categoryCounts_[category]),
                  MakeRect(rect.right - 32.0f, rect.top, 24.0f, 26.0f), 9.5f,
                  DWRITE_FONT_WEIGHT_NORMAL, theme::kTextFaint,
                  DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    AddHit(HitKind::Nav, rect, payload);
    y += 27.0f;
  }

  const float cardHeight = isAdmin_ ? 92.0f : 114.0f;
  const D2D1_RECT_F adminCard =
      MakeRect(14.0f, contentBottom_ - 14.0f - cardHeight, sidebarWidth_ - 28.0f, cardHeight);
  DrawRoundRect(adminCard, 12.0f, theme::kSurface2, true, theme::kBorderSoft);

  DrawRoundRect(MakeRect(adminCard.left + 14.0f, adminCard.top + 14.0f, 26.0f, 26.0f), 8.0f,
                WithAlpha(theme::kLime, 0.10f));
  DrawIcon(isAdmin_ ? L"\uE72E" : L"\uE72E",
           MakeRect(adminCard.left + 15.0f, adminCard.top + 15.0f, 24.0f, 24.0f), 13.0f,
           isAdmin_ ? theme::kLime : theme::kTextFaint);
  DrawTextBlock(isAdmin_ ? L"Administrador" : L"Modo estándar",
                MakeRect(adminCard.left + 48.0f, adminCard.top + 12.0f, 150.0f, 18.0f), 11.5f,
                DWRITE_FONT_WEIGHT_SEMI_BOLD, isAdmin_ ? theme::kText : theme::kTextDim);
  DrawTextBlock(isAdmin_ ? L"Permisos completos activos" : L"Para ajustes del sistema",
                MakeRect(adminCard.left + 48.0f, adminCard.top + 30.0f, 170.0f, 16.0f), 9.5f,
                DWRITE_FONT_WEIGHT_NORMAL, isAdmin_ ? theme::kLimeDim : theme::kTextFaint);

  if (isAdmin_) {
    DrawRoundRect(MakeRect(adminCard.left + 14.0f, adminCard.top + 56.0f,
                           RW(adminCard) - 28.0f, 20.0f),
                  10.0f, WithAlpha(theme::kLime, 0.06f));
    DrawTextBlock(std::wstring(L"") + IntText(AppliedCount()) + L" ajustes activos",
                  MakeRect(adminCard.left + 24.0f, adminCard.top + 56.0f,
                           RW(adminCard) - 48.0f, 20.0f),
                  9.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, theme::kLimeDim,
                  DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
  } else {
    const D2D1_RECT_F button =
        MakeRect(adminCard.left + 14.0f, adminCard.top + 58.0f, RW(adminCard) - 28.0f, 30.0f);
    DrawButton(button, L"Elevar permisos", true,
               hovered(HitKind::Button, L"elevate"), true);
    AddHit(HitKind::Button, button, L"elevate");
  }

  DrawTextBlock(L"LimeForge 1.0  ·  Windows",
                MakeRect(18.0f, contentBottom_ - 3.0f, 216.0f, 1.0f), 9.0f,
                DWRITE_FONT_WEIGHT_NORMAL, theme::kTransparent);
}

void App::DrawStatusBar() {
  const float top = height_ - statusHeight_;
  DrawRoundRect(MakeRect(0.0f, top, width_, statusHeight_), 0.0f, theme::kTitle);
  DrawLine(0.0f, top + 0.5f, width_, top + 0.5f, theme::kBorderSoft);

  DrawCircle(20.0f, top + 17.0f, 3.2f, busy_ ? theme::kAmber : theme::kLime, true);
  std::wstring status = busy_ ? busyLabel_ : L"Listo";
  if (!busy_) status += L"  ·  " + IntText(AppliedCount()) + L" ajustes activos";
  DrawTextBlock(status, MakeRect(31.0f, top, width_ - 400.0f, statusHeight_), 10.5f,
                DWRITE_FONT_WEIGHT_NORMAL, busy_ ? theme::kAmber : theme::kTextDim,
                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

  const int ram = static_cast<int>(std::lround(memPercent_));
  const int cpu = static_cast<int>(std::lround(cpuPercent_));
  std::wstring right = L"RAM " + IntText(ram) + L"%   ·   CPU " + IntText(cpu) +
                       L"%   ·   " + (isAdmin_ ? L"ADMIN" : L"ESTÁNDAR") + L"   ·   v1.1";
  DrawTextBlock(right, MakeRect(width_ - 388.0f, top, 368.0f, statusHeight_), 9.5f,
                DWRITE_FONT_WEIGHT_NORMAL, theme::kTextFaint,
                DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
}

float App::DashboardHeight(float width) const {
  const int columns = width >= 980.0f ? 4 : (width >= 620.0f ? 2 : 1);
  const int rows = (4 + columns - 1) / columns;
  return 80.0f + rows * 118.0f + (rows - 1) * 12.0f + 16.0f + 112.0f;
}

void App::DrawDashboard(float left, float top, float width, float shift) {
  DrawSectionTitle(L"RENDIMIENTO", L"Panel del sistema",
                   L"CPU, memoria, disco y procesos actualizados cada segundo.",
                   left, top + shift, width);

  const int columns = width >= 980.0f ? 4 : (width >= 620.0f ? 2 : 1);
  const float gap = 12.0f;
  const float cardWidth = (width - gap * (columns - 1)) / columns;
  const float cardsTop = top + shift + 80.0f;

  const float cpu = ClampF(cpuPercent_, 0.0f, 100.0f);
  const float mem = ClampF(memPercent_, 0.0f, 100.0f);
  const float disk = ClampF(diskUsedPercent_, 0.0f, 100.0f);
  const float processesFraction =
      ClampF(static_cast<float>(processCount_) / 320.0f, 0.0f, 1.0f);

  struct MetricDef {
    const wchar_t* glyph;
    const wchar_t* label;
    std::wstring value;
    std::wstring sub;
    float fraction;
  };
  const MetricDef metrics[4] = {
      {L"\uE9D9", L"CPU", IntText(static_cast<int>(std::lround(cpu))) + L"%",
       IntText(logicalCpus_) + L" hilos lógicos", cpu / 100.0f},
      {L"\uE950", L"Memoria RAM", IntText(static_cast<int>(std::lround(mem))) + L"%",
       Fixed1(memUsedGb_) + L" / " + Fixed1(memTotalGb_) + L" GB", mem / 100.0f},
      {L"\uE8B7", L"Disco del sistema", IntText(static_cast<int>(std::lround(disk))) + L"%",
       Fixed1(diskFreeGb_) + L" GB libres de " + Fixed1(diskTotalGb_) + L" GB",
       disk / 100.0f},
      {L"\uE716", L"Procesos", IntText(processCount_),
       L"Encendido hace " + (uptimeText_.empty() ? std::wstring(L"—") : uptimeText_),
       processesFraction},
  };

  for (int i = 0; i < 4; ++i) {
    const int column = i % columns;
    const int row = i / columns;
    const D2D1_RECT_F card =
        MakeRect(left + column * (cardWidth + gap), cardsTop + row * (118.0f + gap),
                 cardWidth, 118.0f);
    const D2D1_COLOR_F accent =
        metrics[i].fraction >= 0.9f
            ? theme::kRed
            : (metrics[i].fraction >= 0.75f ? theme::kAmber : theme::kLimeDim);
    DrawRoundRect(card, 14.0f, theme::kSurface2, true, theme::kBorderSoft);
    DrawRoundRect(MakeRect(card.left + 14.0f, card.top + 14.0f, 30.0f, 30.0f), 9.0f,
                  WithAlpha(theme::kLime, 0.07f));
    DrawIcon(metrics[i].glyph,
             MakeRect(card.left + 15.0f, card.top + 15.0f, 28.0f, 28.0f), 14.0f,
             theme::kLimeDim);
    DrawTextBlock(metrics[i].label, MakeRect(card.left + 54.0f, card.top + 15.0f,
                                             RW(card) - 66.0f, 22.0f),
                  9.4f, DWRITE_FONT_WEIGHT_SEMI_BOLD, theme::kTextFaint,
                  DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, 1,
                  false);
    DrawTextBlock(metrics[i].value, MakeRect(card.left + 14.0f, card.top + 42.0f,
                                             RW(card) - 28.0f, 32.0f),
                  25.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, theme::kText,
                  DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, 1,
                  false);
    DrawTextBlock(metrics[i].sub, MakeRect(card.left + 14.0f, card.top + 76.0f,
                                           RW(card) - 28.0f, 18.0f),
                  9.2f, DWRITE_FONT_WEIGHT_NORMAL, theme::kTextDim,
                  DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, 1,
                  false);
    const D2D1_RECT_F track =
        MakeRect(card.left + 14.0f, card.top + 101.0f, RW(card) - 28.0f, 4.0f);
    DrawRoundRect(track, 2.0f, WithAlpha(theme::kBorder, 0.7f));
    const float filled = (RW(card) - 28.0f) * ClampF(metrics[i].fraction, 0.0f, 1.0f);
    if (filled > 1.0f)
      DrawRoundRect(MakeRect(track.left, track.top, filled, 4.0f), 2.0f, accent);
  }

  const float rows = static_cast<float>((4 + columns - 1) / columns);
  const float activityTop =
      cardsTop + rows * 118.0f + (rows - 1.0f) * gap + 16.0f;
  const D2D1_RECT_F activity = MakeRect(left, activityTop, width, 112.0f);
  DrawRoundRect(activity, 14.0f, theme::kSurface2, true, theme::kBorderSoft);
  DrawTextBlock(L"Actividad en tiempo real", MakeRect(activity.left + 18.0f,
                                                      activity.top + 12.0f, 260.0f, 20.0f),
                11.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, theme::kText);
  DrawTextBlock(L"Historial de los últimos 90 segundos", MakeRect(activity.left + 18.0f,
                                                                 activity.top + 30.0f,
                                                                 300.0f, 16.0f),
                8.8f, DWRITE_FONT_WEIGHT_NORMAL, theme::kTextFaint);

  const std::wstring legend =
      L"CPU " + IntText(static_cast<int>(std::lround(cpu))) + L"%     RAM " +
      IntText(static_cast<int>(std::lround(mem))) + L"%";
  DrawTextBlock(legend, MakeRect(activity.right - 258.0f, activity.top + 13.0f, 238.0f, 18.0f),
                9.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, theme::kTextDim,
                DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, 1,
                false);

  const D2D1_RECT_F graph =
      MakeRect(activity.left + 18.0f, activity.top + 48.0f, RW(activity) - 36.0f, 48.0f);
  DrawLine(graph.left, graph.top, graph.right, graph.top, WithAlpha(theme::kBorder, 0.55f));
  DrawLine(graph.left, (graph.top + graph.bottom) / 2.0f, graph.right,
           (graph.top + graph.bottom) / 2.0f, WithAlpha(theme::kBorder, 0.35f));
  DrawLine(graph.left, graph.bottom, graph.right, graph.bottom,
           WithAlpha(theme::kBorder, 0.7f));

  auto drawSeries = [&](const std::vector<float>& series, D2D1_COLOR_F color,
                        float stroke) {
    if (series.size() < 2) return;
    const float denominator = static_cast<float>(series.size() - 1);
    for (size_t i = 1; i < series.size(); ++i) {
      const float x1 = graph.left + (static_cast<float>(i - 1) / denominator) * RW(graph);
      const float x2 = graph.left + (static_cast<float>(i) / denominator) * RW(graph);
      const float y1 = graph.bottom - ClampF(series[i - 1], 0.0f, 100.0f) / 100.0f * RH(graph);
      const float y2 = graph.bottom - ClampF(series[i], 0.0f, 100.0f) / 100.0f * RH(graph);
      DrawLine(x1, y1, x2, y2, color, stroke);
    }
  };
  drawSeries(memHistory_, WithAlpha(theme::kBlue, 0.55f), 1.2f);
  drawSeries(cpuHistory_, theme::kLime, 1.7f);

  if (cpuHistory_.size() < 2) {
    DrawTextBlock(L"Recolectando datos…", MakeRect(graph.left, graph.top, RW(graph), RH(graph)),
                  9.0f, DWRITE_FONT_WEIGHT_NORMAL, theme::kTextFaint,
                  DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, 1,
                  false);
  }

}

void App::DrawHome(const D2D1_RECT_F& content) {
  const float innerWidth = content.right - content.left - 64.0f;
  const float left = content.left + 32.0f;
  const float baseTop = content.top + 24.0f;
  int columns = innerWidth >= 980.0f ? 3 : (innerWidth >= 640.0f ? 2 : 1);
  const float categoryCardHeight = 86.0f;
  const int categoryRows = (static_cast<int>(categories_.size()) + columns - 1) / columns;
  const float categoryBlock =
      categoryRows * categoryCardHeight + (categoryRows - 1) * 14.0f;

  const float heroTop = baseTop;
  const float dashboardTop = heroTop + 214.0f + 26.0f;
  const float dashboardHeight = showDashboard_ ? DashboardHeight(innerWidth) : 0.0f;
  const float statsTop =
      showDashboard_ ? dashboardTop + dashboardHeight + 24.0f : heroTop + 214.0f + 18.0f;
  const float packHeaderTop = statsTop + 86.0f + 34.0f;
  const int total = static_cast<int>(tweaks_.size());
  const int active = AppliedCount();
  const float packTop = packHeaderTop + 80.0f;
  const float packHeight = 194.0f;
  const float categoriesHeaderTop = packTop + packHeight + 30.0f;
  const float categoriesTop = categoriesHeaderTop + 80.0f;
  const float toolsHeaderTop = categoriesTop + categoryBlock + 30.0f;
  const float toolsTop = toolsHeaderTop + 80.0f;
  const float contentEnd = toolsTop + 108.0f;

  contentHeight_ = contentEnd - content.top + 20.0f;
  ClampScroll();

  const float shift = -scroll_;
  const D2D1_RECT_F hero =
      MakeRect(left, heroTop + shift, innerWidth, 214.0f);
  DrawRoundRect(hero, 18.0f, theme::kSurface, true, theme::kBorder);

  const float glowX = hero.right - 132.0f;
  const float glowY = hero.top + 100.0f;
  DrawCircle(glowX, glowY, 92.0f, WithAlpha(theme::kLime, 0.035f), false);
  DrawCircle(glowX, glowY, 74.0f, WithAlpha(theme::kLime, 0.05f), false);
  DrawCircle(glowX, glowY, 116.0f, WithAlpha(theme::kLime, 0.02f), false);
  DrawBolt(MakeRect(hero.right - 214.0f, hero.top + 34.0f, 54.0f, 70.0f),
           WithAlpha(theme::kLime, 0.045f));

  DrawTextBlock(L"OPTIMIZACIÓN LOCAL PARA WINDOWS",
                MakeRect(hero.left + 28.0f, hero.top + 24.0f, RW(hero) - 320.0f, 16.0f), 9.5f,
                DWRITE_FONT_WEIGHT_SEMI_BOLD, theme::kLime);
  DrawTextBlock(L"Tu PC, al máximo.",
                MakeRect(hero.left + 28.0f, hero.top + 44.0f, RW(hero) - 320.0f, 48.0f), 33.0f,
                DWRITE_FONT_WEIGHT_SEMI_BOLD, theme::kText,
                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR, 1, false);
  DrawTextBlock(IntText(total) + L" ajustes reales de Windows, limpieza, diagnóstico y "
                L"prioridad de juegos. Todo se ejecuta localmente en tu equipo.",
                MakeRect(hero.left + 28.0f, hero.top + 92.0f, RW(hero) - 330.0f, 46.0f), 12.0f,
                DWRITE_FONT_WEIGHT_NORMAL, theme::kTextDim);

  auto hovered = [&](HitKind kind, const std::wstring& payload) {
    Hit hit;
    hit.kind = kind;
    hit.payload = payload;
    return hoverKey_ == HitKey(hit);
  };

  const D2D1_RECT_F packButton =
      MakeRect(hero.left + 28.0f, hero.top + RH(hero) - 62.0f, 210.0f, 40.0f);
  DrawButton(packButton, L"Aplicar pack recomendado", true,
             hovered(HitKind::Button, L"pack"));
  AddHit(HitKind::Button, packButton, L"pack");

  const D2D1_RECT_F exploreButton =
      MakeRect(packButton.right + 12.0f, packButton.top, 176.0f, 40.0f);
  DrawButton(exploreButton, L"Explorar tweaks", false,
             hovered(HitKind::Button, L"explore"));
  AddHit(HitKind::Button, exploreButton, L"explore");

  const float fraction = total > 0 ? ClampF(static_cast<float>(active) / total, 0.0f, 1.0f) : 0.0f;
  const int ticks = 52;
  for (int i = 0; i < ticks; ++i) {
    const float angle = -kPi * 0.5f + (2.0f * kPi * i) / ticks;
    const bool lit = static_cast<float>(i) < fraction * ticks;
    const float r1 = 58.0f;
    const float r2 = 65.0f;
    DrawLine(glowX + std::cos(angle) * r1, glowY + std::sin(angle) * r1,
             glowX + std::cos(angle) * r2, glowY + std::sin(angle) * r2,
             lit ? theme::kLime : WithAlpha(theme::kBorder, 0.9f), lit ? 2.2f : 1.5f);
  }
  DrawTextBlock(IntText(active), MakeRect(glowX - 70.0f, glowY - 40.0f, 140.0f, 42.0f), 32.0f,
                DWRITE_FONT_WEIGHT_SEMI_BOLD, theme::kText,
                DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, 1, false);
  DrawTextBlock(L"ajustes activos", MakeRect(glowX - 70.0f, glowY + 4.0f, 140.0f, 16.0f), 9.5f,
                DWRITE_FONT_WEIGHT_NORMAL, theme::kLimeDim,
                DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
  DrawTextBlock(L"de " + IntText(total) + L" disponibles",
                MakeRect(glowX - 80.0f, glowY + 22.0f, 160.0f, 16.0f), 9.0f,
                DWRITE_FONT_WEIGHT_NORMAL, theme::kTextFaint,
                DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

  if (showDashboard_) DrawDashboard(left, dashboardTop, innerWidth, shift);

  // Quick stats
  const float statGap = 12.0f;
  const float statWidth = (innerWidth - statGap * 3.0f) / 4.0f;
  struct StatDef {
    const wchar_t* glyph;
    std::wstring value;
    const wchar_t* label;
  };
  StatDef stats[] = {
      {L"\uE7FC", IntText(total), L"Tweaks disponibles"},
      {L"\uE73E", IntText(active), L"Ajustes activos"},
      {L"\uE713", IntText(static_cast<int>(categories_.size())), L"Categorías"},
      {L"\uE72E", isAdmin_ ? L"Sí" : L"No", L"Modo administrador"},
  };
  for (int i = 0; i < 4; ++i) {
    const D2D1_RECT_F card =
        MakeRect(left + i * (statWidth + statGap), statsTop + shift, statWidth, 86.0f);
    DrawRoundRect(card, 14.0f, theme::kSurface2, true, theme::kBorderSoft);
    DrawRoundRect(MakeRect(card.left + 14.0f, card.top + 15.0f, 30.0f, 30.0f), 9.0f,
                  WithAlpha(theme::kLime, 0.08f));
    DrawIcon(stats[i].glyph, MakeRect(card.left + 15.0f, card.top + 16.0f, 28.0f, 28.0f),
             15.0f, theme::kLimeDim);
    DrawTextBlock(stats[i].value, MakeRect(card.left + 54.0f, card.top + 12.0f,
                                           RW(card) - 64.0f, 30.0f),
                  21.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, theme::kText,
                  DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, 1, false);
    DrawTextBlock(stats[i].label, MakeRect(card.left + 54.0f, card.top + 43.0f,
                                           RW(card) - 64.0f, 28.0f),
                  9.8f, DWRITE_FONT_WEIGHT_NORMAL, theme::kTextDim);
  }

  // Recommended pack
  std::vector<const Tweak*> recommended;
  for (const Tweak& tweak : tweaks_) {
    if (tweak.recommended && !tweak.actionOnly) recommended.push_back(&tweak);
  }

  DrawSectionTitle(L"RECOMENDADO", L"Pack de rendimiento",
                   L"Una base segura y reversible para la mayoría de equipos.",
                   left, packHeaderTop + shift, innerWidth);

  const D2D1_RECT_F packButton2 =
      MakeRect(left + innerWidth - 184.0f, packHeaderTop + shift + 12.0f, 184.0f, 40.0f);
  DrawButton(packButton2, L"Aplicar pack", true, hovered(HitKind::Button, L"pack"));
  AddHit(HitKind::Button, packButton2, L"pack");

  const D2D1_RECT_F packPanel = MakeRect(left, packTop + shift, innerWidth, packHeight);
  DrawRoundRect(packPanel, 16.0f, theme::kSurface2, true, theme::kBorderSoft);
  DrawLine(packPanel.left + 20.0f, packPanel.top + packHeight - 34.0f,
           packPanel.right - 20.0f, packPanel.top + packHeight - 34.0f, theme::kBorderSoft);
  DrawTextBlock(IntText(static_cast<int>(recommended.size())) +
                    L" ajustes seleccionados  ·  reversibles desde Ajustes",
                MakeRect(packPanel.left + 22.0f, packPanel.top + packHeight - 28.0f,
                         RW(packPanel) - 44.0f, 20.0f),
                9.5f, DWRITE_FONT_WEIGHT_NORMAL, theme::kTextFaint);

  const float listColumnWidth = (RW(packPanel) - 48.0f) / 2.0f;
  const int shown = std::min<int>(10, static_cast<int>(recommended.size()));
  for (int i = 0; i < shown; ++i) {
    const int column = i % 2;
    const int row = i / 2;
    const float itemX = packPanel.left + 24.0f + column * listColumnWidth;
    const float itemY = packPanel.top + 16.0f + row * 27.0f;
    const Tweak& tweak = *recommended[i];
    const bool on = applied_.count(tweak.id) != 0;
    if (on) {
      DrawCircle(itemX + 6.0f, itemY + 8.0f, 6.0f, theme::kLime, true);
      DrawIcon(L"\uE73E", MakeRect(itemX + 1.0f, itemY + 3.0f, 10.0f, 10.0f), 7.0f, theme::kBlack);
    } else {
      DrawCircle(itemX + 6.0f, itemY + 8.0f, 6.0f, theme::kBorder, false);
    }
    DrawTextBlock(tweak.name, MakeRect(itemX + 20.0f, itemY, listColumnWidth - 30.0f, 18.0f),
                  10.5f, DWRITE_FONT_WEIGHT_NORMAL, on ? theme::kLimeDim : theme::kTextDim,
                  DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, 1, false);
  }
  if (recommended.empty()) {
    DrawTextBlock(L"No hay un pack recomendado disponible.",
                  MakeRect(packPanel.left + 24.0f, packPanel.top + 20.0f,
                           RW(packPanel) - 48.0f, 24.0f),
                  11.0f, DWRITE_FONT_WEIGHT_NORMAL, theme::kTextDim);
  }

  // Categories
  DrawSectionTitle(L"CATEGORÍAS", L"Explorá por área",
                   L"Cada categoría agrupa ajustes con el mismo objetivo.",
                   left, categoriesHeaderTop + shift, innerWidth);
  const float catGap = 14.0f;
  const float catWidth = (innerWidth - catGap * (columns - 1)) / columns;
  for (size_t i = 0; i < categories_.size(); ++i) {
    const int column = static_cast<int>(i) % columns;
    const int row = static_cast<int>(i) / columns;
    const D2D1_RECT_F card =
        MakeRect(left + column * (catWidth + catGap),
                 categoriesTop + shift + row * (categoryCardHeight + catGap),
                 catWidth, categoryCardHeight);
    const std::wstring& category = categories_[i];
    const std::wstring payload = L"cat:" + category;
    const bool hover = hovered(HitKind::Nav, payload);
    DrawRoundRect(card, 14.0f, hover ? theme::kSurface3 : theme::kSurface2, true,
                  hover ? WithAlpha(theme::kLime, 0.35f) : theme::kBorderSoft);
    DrawRoundRect(MakeRect(card.left + 14.0f, card.top + 16.0f, 32.0f, 32.0f), 9.0f,
                  WithAlpha(theme::kLime, 0.08f));
    DrawIcon(CategoryGlyph(category),
             MakeRect(card.left + 15.0f, card.top + 17.0f, 30.0f, 30.0f), 15.0f,
             theme::kLimeDim);
    DrawTextBlock(category, MakeRect(card.left + 56.0f, card.top + 13.0f,
                                     RW(card) - 92.0f, 22.0f),
                  12.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, theme::kText,
                  DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, 1, false);
    DrawTextBlock(IntText(categoryCounts_[category]) + L" tweaks",
                  MakeRect(card.left + 56.0f, card.top + 36.0f, RW(card) - 92.0f, 20.0f),
                  10.0f, DWRITE_FONT_WEIGHT_NORMAL, theme::kTextFaint);
    DrawIcon(L"\uE76C", MakeRect(card.right - 30.0f, card.top + 31.0f, 18.0f, 18.0f), 10.0f,
             theme::kTextFaint);
    AddHit(HitKind::Nav, card, payload);
  }

  // Tools
  DrawSectionTitle(L"HERRAMIENTAS", L"Más allá de los tweaks",
                   L"Limpieza, arranque y diagnóstico con un clic.",
                   left, toolsHeaderTop + shift, innerWidth);
  struct ToolDef {
    const wchar_t* glyph;
    const wchar_t* title;
    const wchar_t* description;
    const wchar_t* payload;
  };
  static const ToolDef tools[] = {
      {L"\uE74D", L"Limpieza", L"Temporales, papelera, DNS y cachés.", L"open:Cleaner"},
      {L"\uE768", L"Inicio de Windows", L"Revisá qué se abre con el sistema.", L"open:Startup"},
      {L"\uE9D9", L"Diagnóstico", L"Informe completo del hardware.", L"open:Diagnostics"},
  };
  const float toolGap = 14.0f;
  const float toolWidth = (innerWidth - toolGap * 2.0f) / 3.0f;
  for (int i = 0; i < 3; ++i) {
    const D2D1_RECT_F card =
        MakeRect(left + i * (toolWidth + toolGap), toolsTop + shift, toolWidth, 108.0f);
    const bool hover = hovered(HitKind::Nav, tools[i].payload);
    DrawRoundRect(card, 15.0f, hover ? theme::kSurface3 : theme::kSurface2, true,
                  hover ? WithAlpha(theme::kLime, 0.35f) : theme::kBorderSoft);
    DrawRoundRect(MakeRect(card.left + 16.0f, card.top + 16.0f, 32.0f, 32.0f), 9.0f,
                  WithAlpha(theme::kLime, 0.08f));
    DrawIcon(tools[i].glyph, MakeRect(card.left + 17.0f, card.top + 17.0f, 30.0f, 30.0f),
             15.0f, theme::kLimeDim);
    DrawTextBlock(tools[i].title, MakeRect(card.left + 58.0f, card.top + 15.0f,
                                           RW(card) - 74.0f, 22.0f),
                  12.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, theme::kText,
                  DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, 1, false);
    DrawTextBlock(tools[i].description, MakeRect(card.left + 16.0f, card.top + 56.0f,
                                                 RW(card) - 32.0f, 34.0f),
                  10.5f, DWRITE_FONT_WEIGHT_NORMAL, theme::kTextDim);
    DrawIcon(L"\uE76C", MakeRect(card.right - 30.0f, card.top + 24.0f, 16.0f, 16.0f), 9.0f,
             theme::kTextFaint);
    AddHit(HitKind::Nav, card, tools[i].payload);
  }
}

void App::DrawTweaks(const D2D1_RECT_F& content) {
  const float left = content.left + 32.0f;
  const float innerWidth = content.right - content.left - 64.0f;
  const float headerTop = content.top + 24.0f;
  const float chipsTop = headerTop + 78.0f;
  const float gridTop = chipsTop + 50.0f;
  const float cardHeight = 190.0f;

  int columns = innerWidth >= 980.0f ? 3 : (innerWidth >= 650.0f ? 2 : 1);
  const std::vector<const Tweak*> items = FilteredTweaks();
  const int itemCount = static_cast<int>(items.size());
  const int rows = itemCount == 0 ? 0 : (itemCount + columns - 1) / columns;
  const float gridHeight =
      rows == 0 ? 180.0f : rows * cardHeight + (rows - 1) * kCardGap;
  contentHeight_ = gridTop + gridHeight + 28.0f - content.top;
  ClampScroll();
  const float shift = -scroll_;

  const std::wstring title =
      category_ == L"Todas" ? std::wstring(L"Todos los tweaks") : category_;
  int activeVisible = 0;
  for (const Tweak* tweak : items)
    if (applied_.count(tweak->id)) activeVisible += 1;

  DrawTextBlock(L"CATÁLOGO", MakeRect(left, headerTop + shift, 300.0f, 16.0f), 9.0f,
                DWRITE_FONT_WEIGHT_SEMI_BOLD, theme::kLime);
  DrawTextBlock(title, MakeRect(left, headerTop + shift + 16.0f, innerWidth - 360.0f, 34.0f),
                25.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, theme::kText,
                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR, 1, false);
  auto plural = [](int value, const wchar_t* singular, const wchar_t* many) {
    return IntText(value) + L" " + (value == 1 ? singular : many);
  };
  std::wstring subtitle = plural(itemCount, L"resultado", L"resultados") + L"  ·  " +
                          plural(activeVisible, L"activo", L"activos") + L"  ·  " +
                          IntText(toggleableCount_) + L" reversibles";
  DrawTextBlock(subtitle, MakeRect(left, headerTop + shift + 52.0f, innerWidth - 360.0f, 20.0f),
                10.5f, DWRITE_FONT_WEIGHT_NORMAL, theme::kTextDim);

  auto hovered = [&](HitKind kind, const std::wstring& payload) {
    Hit hit;
    hit.kind = kind;
    hit.payload = payload;
    return hoverKey_ == HitKey(hit);
  };

  const D2D1_RECT_F searchRect =
      MakeRect(content.right - 32.0f - 300.0f, headerTop + shift + 4.0f, 300.0f, 40.0f);
  DrawRoundRect(searchRect, 11.0f, theme::kSurface2, true,
                searchFocused_ ? theme::kLime : theme::kBorder);
  DrawIcon(L"\uE721", MakeRect(searchRect.left + 12.0f, searchRect.top + 11.0f, 18.0f, 18.0f),
           12.0f, searchFocused_ ? theme::kLime : theme::kTextFaint);
  std::wstring searchText = search_;
  if (searchFocused_ && (tick_ / 5) % 2 == 0) searchText += L"|";
  DrawTextBlock(search_.empty() && !searchFocused_ ? L"Buscar por nombre o descripción..."
                                                   : searchText,
                MakeRect(searchRect.left + 38.0f, searchRect.top + 1.0f,
                         RW(searchRect) - 70.0f, RH(searchRect) - 2.0f),
                10.8f, DWRITE_FONT_WEIGHT_NORMAL,
                search_.empty() && !searchFocused_ ? theme::kTextFaint : theme::kText,
                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, 1, false);
  if (!search_.empty()) {
    const D2D1_RECT_F clearRect =
        MakeRect(searchRect.right - 32.0f, searchRect.top + 8.0f, 24.0f, 24.0f);
    DrawIcon(L"\uE711", MakeRect(clearRect.left, clearRect.top + 4.0f, 24.0f, 16.0f), 9.0f,
             hovered(HitKind::Button, L"clear-search") ? theme::kText : theme::kTextFaint);
  }
  AddHit(HitKind::Search, MakeRect(searchRect.left, searchRect.top, RW(searchRect) - 34.0f,
                                   RH(searchRect)), L"search");
  if (!search_.empty()) {
    const D2D1_RECT_F clearRect =
        MakeRect(searchRect.right - 32.0f, searchRect.top + 8.0f, 24.0f, 24.0f);
    AddHit(HitKind::Button, clearRect, L"clear-search");
  }

  struct ChipDef {
    std::wstring key;
    std::wstring label;
    bool active;
  };
  std::vector<ChipDef> chips;
  if (category_ != L"Todas") {
    chips.push_back({L"category:all", category_ + L"  ×", true});
  }
  static const wchar_t* filters[][2] = {
      {L"all", L"Todos"},          {L"active", L"Activos"},
      {L"recommended", L"Recomendados"}, {L"risky", L"Riesgosos"},
      {L"action", L"Acciones"},
  };
  for (const auto& filter : filters) {
    chips.push_back({std::wstring(L"filter:") + filter[0], filter[1],
                     filter_ == filter[0]});
  }

  float chipX = left;
  const float chipY = chipsTop + shift;
  for (const ChipDef& chip : chips) {
    const float chipWidth = 24.0f + static_cast<float>(chip.label.size()) * 6.2f;
    const D2D1_RECT_F rect = MakeRect(chipX, chipY, chipWidth, 30.0f);
    DrawChip(rect, chip.label, chip.active, hovered(HitKind::Filter, chip.key));
    AddHit(HitKind::Filter, rect, chip.key);
    chipX += chipWidth + 8.0f;
  }

  if (itemCount == 0) {
    const D2D1_RECT_F empty = MakeRect(left, gridTop + shift, innerWidth, 156.0f);
    DrawRoundRect(empty, 16.0f, theme::kSurface2, true, theme::kBorderSoft);
    DrawIcon(L"\uE721", MakeRect((empty.left + empty.right) / 2.0f - 14.0f,
                                 empty.top + 30.0f, 28.0f, 28.0f),
             20.0f, theme::kTextFaint);
    DrawTextBlock(L"Sin resultados", MakeRect(empty.left, empty.top + 66.0f,
                                              empty.right - empty.left, 24.0f),
                  14.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, theme::kTextDim,
                  DWRITE_TEXT_ALIGNMENT_CENTER);
    DrawTextBlock(L"Probá con otra búsqueda o cambiá el filtro.",
                  MakeRect(empty.left, empty.top + 94.0f, empty.right - empty.left, 20.0f),
                  10.5f, DWRITE_FONT_WEIGHT_NORMAL, theme::kTextFaint,
                  DWRITE_TEXT_ALIGNMENT_CENTER);
  } else {
    const float cardGap = kCardGap;
    const float cardWidth = (innerWidth - cardGap * (columns - 1)) / columns;
    for (int i = 0; i < itemCount; ++i) {
      const int column = i % columns;
      const int row = i / columns;
      const D2D1_RECT_F card =
          MakeRect(left + column * (cardWidth + cardGap),
                   gridTop + shift + row * (cardHeight + cardGap), cardWidth, cardHeight);
      const Tweak& tweak = *items[i];
      const bool active = applied_.count(tweak.id) != 0;
      const bool hover = hovered(HitKind::Card, tweak.id);
      const D2D1_COLOR_F border = active ? WithAlpha(theme::kLime, 0.55f)
                                         : (hover ? WithAlpha(theme::kLime, 0.30f)
                                                  : theme::kBorderSoft);
      DrawRoundRect(card, 16.0f, active ? theme::kSurface3 : theme::kSurface, true, border);
      AddHit(HitKind::Card, card, tweak.id);
      if (active) {
        DrawLine(card.left + 16.0f, card.top + 1.5f, card.right - 16.0f, card.top + 1.5f,
                 theme::kLime, 2.0f);
      }

      DrawRoundRect(MakeRect(card.left + 16.0f, card.top + 16.0f, 30.0f, 30.0f), 9.0f,
                    WithAlpha(theme::kLime, active ? 0.14f : 0.07f));
      DrawIcon(CategoryGlyph(tweak.category),
               MakeRect(card.left + 17.0f, card.top + 17.0f, 28.0f, 28.0f), 14.0f,
               active ? theme::kLime : theme::kLimeDim);

      std::wstring meta = tweak.category;
      if (!tweak.subcategory.empty()) meta += L"  ·  " + tweak.subcategory;
      DrawTextBlock(meta, MakeRect(card.left + 56.0f, card.top + 15.0f,
                                   card.right - card.left - 138.0f, 18.0f),
                    8.8f, DWRITE_FONT_WEIGHT_SEMI_BOLD, theme::kTextFaint,
                    DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, 1, false);

      if (active) {
        const D2D1_RECT_F badge =
            MakeRect(card.right - 72.0f, card.top + 20.0f, 56.0f, 20.0f);
        DrawRoundRect(badge, 10.0f, WithAlpha(theme::kLime, 0.12f), true,
                      WithAlpha(theme::kLime, 0.35f));
        DrawTextBlock(L"ACTIVO", MakeRect(badge.left, badge.top, badge.right - badge.left, 20.0f),
                      8.2f, DWRITE_FONT_WEIGHT_SEMI_BOLD, theme::kLime,
                      DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
      }

      const D2D1_RECT_F titleRect = MakeRect(
          card.left + 16.0f, card.top + 50.0f, card.right - card.left - 32.0f, 40.0f);
      rt_->PushAxisAlignedClip(titleRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
      DrawTextBlock(tweak.name, titleRect, 14.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, theme::kText,
                    DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR, 2);
      rt_->PopAxisAlignedClip();

      const D2D1_RECT_F descRect = MakeRect(
          card.left + 16.0f, card.top + 94.0f, card.right - card.left - 32.0f, 48.0f);
      rt_->PushAxisAlignedClip(descRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
      DrawTextBlock(ShortenForCard(tweak.description, 168), descRect, 10.3f,
                    DWRITE_FONT_WEIGHT_NORMAL, theme::kTextDim,
                    DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR, 3);
      rt_->PopAxisAlignedClip();

      float badgeX = card.left + 16.0f;
      const float badgeY = card.top + cardHeight - 38.0f;
      if (tweak.needsAdmin) {
        DrawBadge(badgeX, badgeY, L"Admin", L"\uE72E", theme::kAmber);
        badgeX += BadgeWidth(L"\uE72E", L"Admin") + 6.0f;
      }
      if (tweak.restart) {
        DrawBadge(badgeX, badgeY, L"Reinicio", L"\uE72C", theme::kBlue);
        badgeX += BadgeWidth(L"\uE72C", L"Reinicio") + 6.0f;
      }
      if (tweak.risky) {
        DrawBadge(badgeX, badgeY, L"Riesgo", L"\uE7BA", theme::kRed);
        badgeX += BadgeWidth(L"\uE7BA", L"Riesgo") + 6.0f;
      }
      if (tweak.actionOnly && badgeX < card.left + 80.0f) {
        DrawBadge(badgeX, badgeY, L"Acción", L"\uE768", theme::kTextDim);
      }

      if (tweak.actionOnly) {
        std::wstring label = tweak.buttonLabel.empty() ? L"Ejecutar" : tweak.buttonLabel;
        const float buttonWidth = std::max(86.0f, 26.0f + label.size() * 6.3f);
        const D2D1_RECT_F button =
            MakeRect(card.right - 16.0f - buttonWidth, card.top + cardHeight - 42.0f,
                     buttonWidth, 30.0f);
        DrawButton(button, label, true, hovered(HitKind::Action, tweak.id), true);
        AddHit(HitKind::Action, button, tweak.id);
      } else {
        const D2D1_RECT_F toggle =
            MakeRect(card.right - 58.0f, card.top + cardHeight - 39.0f, 42.0f, 24.0f);
        DrawToggle(toggle, active, hovered(HitKind::Toggle, tweak.id));
        AddHit(HitKind::Toggle, toggle, tweak.id);
      }
    }
  }
}

void App::DrawCleaner(const D2D1_RECT_F& content) {
  const float left = content.left + 32.0f;
  const float innerWidth = content.right - content.left - 64.0f;
  const float headerTop = content.top + 24.0f;
  const float bannerTop = headerTop + 84.0f;
  const float gridTop = bannerTop + 124.0f;
  const float cardHeight = 116.0f;
  const float cardGap = 14.0f;
  const int columns = innerWidth >= 760.0f ? 2 : 1;

  struct CleanDef {
    const wchar_t* key;
    const wchar_t* title;
    const wchar_t* description;
    const wchar_t* glyph;
    bool admin;
  };
  static const CleanDef defs[] = {
      {L"temp", L"Archivos temporales", L"Elimina temporales del usuario y del sistema.", L"\uE74D", false},
      {L"recycle", L"Papelera de reciclaje", L"Vacía la papelera y libera espacio al instante.", L"\uE74D", false},
      {L"dns", L"Caché DNS", L"Fuerza una resolución limpia de dominios.", L"\uE774", false},
      {L"browsers", L"Caché de navegadores", L"Edge, Chrome, Brave y Opera GX.", L"\uE774", false},
      {L"wu", L"Windows Update", L"Limpia descargas antiguas de actualizaciones.", L"\uE895", true},
      {L"prefetch", L"Prefetch", L"Borra los archivos de precarga de Windows.", L"\uE896", true},
      {L"logs", L"Registros de eventos", L"Vacía el visor de eventos del sistema.", L"\uE8A5", true},
      {L"thumbs", L"Miniaturas", L"Borra la caché de miniaturas del Explorador.", L"\uE8B9", false},
  };
  const int count = static_cast<int>(sizeof(defs) / sizeof(defs[0]));
  const int rows = (count + columns - 1) / columns;
  const float gridHeight = rows * cardHeight + (rows - 1) * cardGap;
  const float noteTop = gridTop + gridHeight + 18.0f;
  const float noteHeight = lastCleanMessage_.empty() ? 0.0f : 64.0f;
  contentHeight_ = noteTop + noteHeight + 24.0f - content.top;
  ClampScroll();
  const float shift = -scroll_;

  DrawTextBlock(L"HERRAMIENTAS", MakeRect(left, headerTop + shift, 300.0f, 16.0f), 9.0f,
                DWRITE_FONT_WEIGHT_SEMI_BOLD, theme::kLime);
  DrawTextBlock(L"Limpieza", MakeRect(left, headerTop + shift + 16.0f, innerWidth - 280.0f, 34.0f),
                25.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, theme::kText,
                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR, 1, false);
  DrawTextBlock(L"Liberá espacio y eliminá residuos sin tocar tus archivos personales.",
                MakeRect(left, headerTop + shift + 52.0f, innerWidth - 40.0f, 20.0f),
                10.5f, DWRITE_FONT_WEIGHT_NORMAL, theme::kTextDim);

  auto hovered = [&](HitKind kind, const std::wstring& payload) {
    Hit hit;
    hit.kind = kind;
    hit.payload = payload;
    return hoverKey_ == HitKey(hit);
  };

  const D2D1_RECT_F banner = MakeRect(left, bannerTop + shift, innerWidth, 104.0f);
  DrawRoundRect(banner, 16.0f, theme::kSurface, true, WithAlpha(theme::kLime, 0.35f));
  DrawBolt(MakeRect(banner.left + 20.0f, banner.top + 26.0f, 34.0f, 48.0f),
           WithAlpha(theme::kLime, 0.10f));
  DrawRoundRect(MakeRect(banner.left + 20.0f, banner.top + 24.0f, 40.0f, 40.0f), 11.0f,
                WithAlpha(theme::kLime, 0.12f));
  DrawIcon(L"\uE74D", MakeRect(banner.left + 27.0f, banner.top + 31.0f, 26.0f, 26.0f), 16.0f,
           theme::kLime);
  DrawTextBlock(L"Limpieza completa", MakeRect(banner.left + 76.0f, banner.top + 20.0f,
                                               RW(banner) - 300.0f, 24.0f),
                15.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, theme::kText);
  DrawTextBlock(L"Temporales, papelera, DNS y cachés en una sola pasada. No borra documentos.",
                MakeRect(banner.left + 76.0f, banner.top + 48.0f, RW(banner) - 300.0f, 36.0f),
                10.5f, DWRITE_FONT_WEIGHT_NORMAL, theme::kTextDim);
  const D2D1_RECT_F cleanAll =
      MakeRect(banner.right - 188.0f, banner.top + 32.0f, 168.0f, 40.0f);
  DrawButton(cleanAll, L"Limpiar todo", true, hovered(HitKind::Button, L"clean:all"));
  AddHit(HitKind::Button, cleanAll, L"clean:all");

  const float cardWidth = (innerWidth - cardGap * (columns - 1)) / columns;
  for (int i = 0; i < count; ++i) {
    const int column = i % columns;
    const int row = i / columns;
    const D2D1_RECT_F card =
        MakeRect(left + column * (cardWidth + cardGap),
                 gridTop + shift + row * (cardHeight + cardGap), cardWidth, cardHeight);
    const CleanDef& def = defs[i];
    const std::wstring payload = std::wstring(L"clean:") + def.key;
    DrawRoundRect(card, 15.0f, theme::kSurface2, true,
                  hovered(HitKind::Button, payload) ? WithAlpha(theme::kLime, 0.28f)
                                                    : theme::kBorderSoft);
    DrawRoundRect(MakeRect(card.left + 16.0f, card.top + 16.0f, 32.0f, 32.0f), 9.0f,
                  WithAlpha(theme::kLime, 0.08f));
    DrawIcon(def.glyph, MakeRect(card.left + 17.0f, card.top + 17.0f, 30.0f, 30.0f), 15.0f,
             theme::kLimeDim);
    DrawTextBlock(def.title, MakeRect(card.left + 58.0f, card.top + 15.0f,
                                      RW(card) - 72.0f, 22.0f),
                  12.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, theme::kText,
                  DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, 1, false);
    DrawTextBlock(def.description, MakeRect(card.left + 16.0f, card.top + 54.0f,
                                            RW(card) - 120.0f, 38.0f),
                  10.0f, DWRITE_FONT_WEIGHT_NORMAL, theme::kTextDim);
    if (def.admin && !isAdmin_) {
      DrawBadge(card.left + 16.0f, card.top + cardHeight - 26.0f, L"Admin", L"\uE72E",
                theme::kAmber);
    }
    const D2D1_RECT_F button =
        MakeRect(card.right - 100.0f, card.top + cardHeight - 42.0f, 84.0f, 30.0f);
    DrawButton(button, L"Limpiar", false, hovered(HitKind::Button, payload), true);
    AddHit(HitKind::Button, button, payload);
  }

  if (!lastCleanMessage_.empty()) {
    const D2D1_RECT_F note = MakeRect(left, noteTop + shift, innerWidth, noteHeight);
    DrawRoundRect(note, 12.0f, theme::kSurface2, true, theme::kBorderSoft);
    DrawIcon(L"\uE73E", MakeRect(note.left + 18.0f, note.top + 19.0f, 22.0f, 22.0f), 13.0f,
             theme::kLime);
    DrawTextBlock(lastCleanMessage_, MakeRect(note.left + 50.0f, note.top + 8.0f,
                                              RW(note) - 68.0f, RH(note) - 16.0f),
                  10.5f, DWRITE_FONT_WEIGHT_NORMAL, theme::kTextDim);
  }
}

void App::DrawStartup(const D2D1_RECT_F& content) {
  const float left = content.left + 32.0f;
  const float innerWidth = content.right - content.left - 64.0f;
  const float headerTop = content.top + 24.0f;
  const float listTop = headerTop + 88.0f;
  const float rowHeight = 64.0f;
  const float rowGap = 8.0f;
  const int count = static_cast<int>(startup_.size());
  const float listHeight =
      count == 0 ? 200.0f : count * rowHeight + (count - 1) * rowGap;
  contentHeight_ = listTop + listHeight + 28.0f - content.top;
  ClampScroll();
  const float shift = -scroll_;

  DrawTextBlock(L"SISTEMA", MakeRect(left, headerTop + shift, 300.0f, 16.0f), 9.0f,
                DWRITE_FONT_WEIGHT_SEMI_BOLD, theme::kLime);
  DrawTextBlock(L"Inicio de Windows", MakeRect(left, headerTop + shift + 16.0f,
                                               innerWidth - 440.0f, 34.0f),
                25.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, theme::kText,
                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR, 1, false);
  DrawTextBlock(L"Programas que se ejecutan al iniciar sesión. Desactivalos desde el "
                L"Administrador de tareas.",
                MakeRect(left, headerTop + shift + 52.0f, innerWidth - 460.0f, 20.0f),
                10.5f, DWRITE_FONT_WEIGHT_NORMAL, theme::kTextDim);

  auto hovered = [&](HitKind kind, const std::wstring& payload) {
    Hit hit;
    hit.kind = kind;
    hit.payload = payload;
    return hoverKey_ == HitKey(hit);
  };

  const float groupY = headerTop + shift + 10.0f;
  const float groupRight = content.right - 32.0f;
  const D2D1_RECT_F folder =
      MakeRect(groupRight - 132.0f, groupY, 132.0f, 36.0f);
  const D2D1_RECT_F taskMgr =
      MakeRect(folder.left - 8.0f - 174.0f, groupY, 174.0f, 36.0f);
  const D2D1_RECT_F refresh =
      MakeRect(taskMgr.left - 8.0f - 108.0f, groupY, 108.0f, 36.0f);

  DrawButton(refresh, L"Actualizar", false, hovered(HitKind::Button, L"refresh-startup"), true);
  AddHit(HitKind::Button, refresh, L"refresh-startup");

  DrawButton(taskMgr, L"Administrador de tareas", false,
             hovered(HitKind::Button, L"open-taskmgr"), true);
  AddHit(HitKind::Button, taskMgr, L"open-taskmgr");

  DrawButton(folder, L"Carpeta Inicio", false,
             hovered(HitKind::Button, L"open-startup-folder"), true);
  AddHit(HitKind::Button, folder, L"open-startup-folder");

  if (count == 0) {
    const D2D1_RECT_F empty = MakeRect(left, listTop + shift, innerWidth, 180.0f);
    DrawRoundRect(empty, 16.0f, theme::kSurface2, true, theme::kBorderSoft);
    DrawIcon(L"\uE768", MakeRect((empty.left + empty.right) / 2.0f - 18.0f,
                                 empty.top + 37.0f, 36.0f, 36.0f),
             26.0f, theme::kTextFaint);
    DrawTextBlock(startupLoading_ ? L"Leyendo el inicio de Windows..."
                                  : L"No hay entradas de inicio detectadas.",
                  MakeRect(empty.left, empty.top + 82.0f, RW(empty), 24.0f),
                  12.0f, DWRITE_FONT_WEIGHT_NORMAL, theme::kTextDim,
                  DWRITE_TEXT_ALIGNMENT_CENTER);
    DrawTextBlock(L"Las entradas de las carpetas Inicio y del registro aparecen acá.",
                  MakeRect(empty.left, empty.top + 110.0f, RW(empty), 20.0f),
                  9.8f, DWRITE_FONT_WEIGHT_NORMAL, theme::kTextFaint,
                  DWRITE_TEXT_ALIGNMENT_CENTER);
  } else {
    for (int i = 0; i < count; ++i) {
      const Engine::StartupEntry& entry = startup_[i];
      const D2D1_RECT_F row =
          MakeRect(left, listTop + shift + i * (rowHeight + rowGap), innerWidth, rowHeight);
      DrawRoundRect(row, 12.0f, theme::kSurface2, true, theme::kBorderSoft);
      DrawRoundRect(MakeRect(row.left + 14.0f, row.top + 15.0f, 32.0f, 32.0f), 9.0f,
                    WithAlpha(theme::kLime, 0.07f));
      DrawIcon(L"\uE768", MakeRect(row.left + 15.0f, row.top + 16.0f, 30.0f, 30.0f), 14.0f,
               theme::kLimeDim);
      DrawTextBlock(entry.name, MakeRect(row.left + 58.0f, row.top + 10.0f,
                                         RW(row) - 260.0f, 22.0f),
                    11.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, theme::kText,
                    DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, 1, false);
      DrawTextBlock(entry.command, MakeRect(row.left + 58.0f, row.top + 32.0f,
                                            RW(row) - 260.0f, 20.0f),
                    9.3f, DWRITE_FONT_WEIGHT_NORMAL, theme::kTextFaint,
                    DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, 1,
                    false);
      const float badgeWidth = BadgeWidth(nullptr, entry.source);
      const D2D1_RECT_F badge =
          MakeRect(row.right - 16.0f - badgeWidth, row.top + 21.0f, badgeWidth, 22.0f);
      DrawRoundRect(badge, 11.0f, WithAlpha(theme::kLime, 0.06f), true, theme::kBorderSoft);
      DrawTextBlock(entry.source, MakeRect(badge.left, badge.top, badgeWidth, 22.0f), 8.6f,
                    DWRITE_FONT_WEIGHT_SEMI_BOLD, theme::kLimeDim,
                    DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, 1,
                    false);
    }
  }
}

void App::DrawDiagnostics(const D2D1_RECT_F& content) {
  const float left = content.left + 32.0f;
  const float innerWidth = content.right - content.left - 64.0f;
  const float headerTop = content.top + 24.0f;
  const float panelTop = headerTop + 88.0f;
  int lines = 1;
  for (wchar_t ch : report_)
    if (ch == L'\n') lines += 1;
  const float panelHeight = std::max(360.0f, lines * 18.0f + 52.0f);
  const float noteTop = panelTop + panelHeight + 16.0f;
  contentHeight_ = noteTop + 70.0f + 24.0f - content.top;
  ClampScroll();
  const float shift = -scroll_;

  DrawTextBlock(L"SISTEMA", MakeRect(left, headerTop + shift, 300.0f, 16.0f), 9.0f,
                DWRITE_FONT_WEIGHT_SEMI_BOLD, theme::kLime);
  DrawTextBlock(L"Diagnóstico", MakeRect(left, headerTop + shift + 16.0f, innerWidth - 360.0f, 34.0f),
                25.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, theme::kText,
                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR, 1, false);
  DrawTextBlock(L"Un informe legible del equipo, el sistema y el plan de energía activo.",
                MakeRect(left, headerTop + shift + 52.0f, innerWidth - 360.0f, 20.0f),
                10.5f, DWRITE_FONT_WEIGHT_NORMAL, theme::kTextDim);

  auto hovered = [&](HitKind kind, const std::wstring& payload) {
    Hit hit;
    hit.kind = kind;
    hit.payload = payload;
    return hoverKey_ == HitKey(hit);
  };

  const D2D1_RECT_F generate =
      MakeRect(content.right - 32.0f - 262.0f, headerTop + shift + 10.0f, 152.0f, 36.0f);
  DrawButton(generate, L"Generar informe", true,
             hovered(HitKind::Button, L"refresh-report"), true);
  AddHit(HitKind::Button, generate, L"refresh-report");

  const D2D1_RECT_F copy = MakeRect(generate.right + 8.0f, generate.top, 98.0f, 36.0f);
  DrawButton(copy, L"Copiar", false, hovered(HitKind::Button, L"copy-report"), true);
  if (!report_.empty()) AddHit(HitKind::Button, copy, L"copy-report");

  const D2D1_RECT_F panel = MakeRect(left, panelTop + shift, innerWidth, panelHeight);
  DrawRoundRect(panel, 16.0f, theme::kSurface2, true, theme::kBorderSoft);
  DrawLine(panel.left + 0.5f, panel.top + 16.0f, panel.left + 0.5f, panel.bottom - 16.0f,
           WithAlpha(theme::kLime, 0.5f), 2.0f);
  if (report_.empty()) {
    DrawTextBlock(reportLoading_ ? L"Recolectando información del sistema..."
                                 : L"Generá un informe para ver el hardware y el estado actual.",
                  MakeRect(panel.left + 26.0f, panel.top + 24.0f, RW(panel) - 52.0f, 40.0f),
                  11.5f, DWRITE_FONT_WEIGHT_NORMAL, theme::kTextDim);
  } else {
    IDWriteTextFormat* mono = GetMonoFormat(12.0f);
    if (mono && brush_) {
      brush_->SetColor(theme::kText);
      rt_->DrawTextW(report_.c_str(), static_cast<UINT32>(report_.size()), mono,
                     MakeRect(panel.left + 26.0f, panel.top + 22.0f, RW(panel) - 52.0f,
                              RH(panel) - 44.0f),
                     brush_.Get());
    }
  }

  const D2D1_RECT_F note = MakeRect(left, noteTop + shift, innerWidth, 70.0f);
  DrawRoundRect(note, 12.0f, theme::kSurface, true, theme::kBorderSoft);
  DrawIcon(L"\uE946", MakeRect(note.left + 18.0f, note.top + 23.0f, 22.0f, 22.0f), 14.0f,
           theme::kLimeDim);
  DrawTextBlock(L"El informe se genera localmente con PowerShell y CIM. Copialo para soporte "
                L"o guardá una captura.",
                MakeRect(note.left + 50.0f, note.top + 13.0f, RW(note) - 68.0f, RH(note) - 26.0f),
                10.5f, DWRITE_FONT_WEIGHT_NORMAL, theme::kTextDim);
}

void App::DrawSettings(const D2D1_RECT_F& content) {
  const float left = content.left + 32.0f;
  const float innerWidth = content.right - content.left - 64.0f;
  const float headerTop = content.top + 24.0f;

  struct CardMetric {
    float y;
    float height;
  };
  float y = headerTop + 88.0f;
  const float appearanceTop = y;
  y += 92.0f + 14.0f;
  const float preferencesTop = y;
  y += 182.0f + 14.0f;
  const float dataTop = y;
  y += 116.0f + 14.0f;
  const float adminTop = y;
  y += 100.0f + 14.0f;
  const float restoreTop = y;
  y += 110.0f + 14.0f;
  const float aboutTop = y;
  y += 100.0f;
  contentHeight_ = y + 24.0f - content.top;
  ClampScroll();
  const float shift = -scroll_;

  DrawTextBlock(L"APLICACIÓN", MakeRect(left, headerTop + shift, 300.0f, 16.0f), 9.0f,
                DWRITE_FONT_WEIGHT_SEMI_BOLD, theme::kLime);
  DrawTextBlock(L"Ajustes", MakeRect(left, headerTop + shift + 16.0f, innerWidth - 360.0f, 34.0f),
                25.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, theme::kText,
                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR, 1, false);
  DrawTextBlock(L"Preferencias locales. No hay cuentas, nube ni telemetría propia.",
                MakeRect(left, headerTop + shift + 52.0f, innerWidth - 360.0f, 20.0f),
                10.5f, DWRITE_FONT_WEIGHT_NORMAL, theme::kTextDim);

  auto hovered = [&](HitKind kind, const std::wstring& payload) {
    Hit hit;
    hit.kind = kind;
    hit.payload = payload;
    return hoverKey_ == HitKey(hit);
  };

  auto card = [&](float top, float height) {
    const D2D1_RECT_F rect = MakeRect(left, top + shift, innerWidth, height);
    DrawRoundRect(rect, 14.0f, theme::kSurface2, true, theme::kBorderSoft);
    return rect;
  };

  // Appearance
  {
    const D2D1_RECT_F rect = card(appearanceTop, 92.0f);
    DrawRoundRect(MakeRect(rect.left + 16.0f, rect.top + 24.0f, 40.0f, 40.0f), 11.0f,
                  theme::kLimeDark, true, WithAlpha(theme::kLime, 0.5f));
    DrawBolt(MakeRect(rect.left + 26.0f, rect.top + 31.0f, 20.0f, 26.0f), theme::kLime);
    DrawTextBlock(L"Apariencia", MakeRect(rect.left + 72.0f, rect.top + 20.0f,
                                          RW(rect) - 260.0f, 24.0f),
                  13.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, theme::kText);
    DrawTextBlock(L"Tema Lima / Negro. Contraste alto y acentos lima en toda la interfaz.",
                  MakeRect(rect.left + 72.0f, rect.top + 44.0f, RW(rect) - 260.0f, 22.0f),
                  10.3f, DWRITE_FONT_WEIGHT_NORMAL, theme::kTextDim);
    const D2D1_RECT_F badge = MakeRect(rect.right - 96.0f, rect.top + 34.0f, 72.0f, 24.0f);
    DrawRoundRect(badge, 12.0f, WithAlpha(theme::kLime, 0.10f), true,
                  WithAlpha(theme::kLime, 0.28f));
    DrawTextBlock(L"FIJO", MakeRect(badge.left, badge.top, RW(badge), 24.0f), 8.6f,
                  DWRITE_FONT_WEIGHT_SEMI_BOLD, theme::kLime,
                  DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
  }

  // Preferences
  {
    const D2D1_RECT_F rect = card(preferencesTop, 182.0f);
    DrawTextBlock(L"Preferencias", MakeRect(rect.left + 20.0f, rect.top + 13.0f,
                                            RW(rect) - 40.0f, 22.0f),
                  12.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, theme::kText);
    DrawTextBlock(L"Se guardan en %APPDATA%\\LimeForge\\settings.ini y no salen de tu equipo.",
                  MakeRect(rect.left + 20.0f, rect.top + 35.0f, RW(rect) - 40.0f, 18.0f),
                  8.8f, DWRITE_FONT_WEIGHT_NORMAL, theme::kTextFaint);

    struct PrefRow {
      const wchar_t* key;
      const wchar_t* label;
      const wchar_t* description;
      bool value;
    };
    const PrefRow rows[] = {
        {L"setting:confirmations", L"Confirmar ajustes riesgosos",
         L"Pide confirmación antes de cambios marcados como riesgo.", confirmRisky_},
        {L"setting:dashboard", L"Dashboard de rendimiento en Inicio",
         L"Muestra CPU, RAM, disco y actividad en vivo.", showDashboard_},
        {L"setting:startup", L"Abrir LimeForge con Windows",
         L"Agrega la aplicación al inicio del usuario actual.", startWithWindows_},
    };
    for (int i = 0; i < 3; ++i) {
      const float rowTop = rect.top + 62.0f + i * 38.0f;
      if (i > 0)
        DrawLine(rect.left + 20.0f, rowTop - 9.0f, rect.right - 20.0f, rowTop - 9.0f,
                 WithAlpha(theme::kBorderSoft, 0.8f));
      DrawTextBlock(rows[i].label, MakeRect(rect.left + 20.0f, rowTop, RW(rect) - 110.0f, 18.0f),
                    10.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, theme::kTextDim,
                    DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, 1,
                    false);
      DrawTextBlock(rows[i].description,
                    MakeRect(rect.left + 20.0f, rowTop + 15.0f, RW(rect) - 110.0f, 16.0f),
                    8.4f, DWRITE_FONT_WEIGHT_NORMAL, theme::kTextFaint,
                    DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, 1,
                    false);
      const D2D1_RECT_F toggle =
          MakeRect(rect.right - 58.0f, rowTop + 1.0f, 42.0f, 24.0f);
      DrawToggle(toggle, rows[i].value, hovered(HitKind::Toggle, rows[i].key));
      AddHit(HitKind::Toggle, toggle, rows[i].key);
    }
  }

  // Data
  {
    const D2D1_RECT_F rect = card(dataTop, 116.0f);
    DrawRoundRect(MakeRect(rect.left + 16.0f, rect.top + 22.0f, 36.0f, 36.0f), 10.0f,
                  WithAlpha(theme::kLime, 0.08f));
    DrawIcon(L"\uE8B7", MakeRect(rect.left + 21.0f, rect.top + 27.0f, 26.0f, 26.0f), 15.0f,
             theme::kLimeDim);
    DrawTextBlock(L"Datos y copias de seguridad", MakeRect(rect.left + 66.0f, rect.top + 18.0f,
                                                           RW(rect) - 300.0f, 24.0f),
                  13.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, theme::kText);
    DrawTextBlock(L"Estado, backups del registro y configuraciones locales.",
                  MakeRect(rect.left + 66.0f, rect.top + 42.0f, RW(rect) - 300.0f, 20.0f),
                  10.2f, DWRITE_FONT_WEIGHT_NORMAL, theme::kTextDim);
    DrawTextBlock(engineReady_ ? engine_.DataDir() : L"Directorio de datos no disponible",
                  MakeRect(rect.left + 66.0f, rect.top + 70.0f, RW(rect) - 300.0f, 20.0f),
                  9.2f, DWRITE_FONT_WEIGHT_NORMAL, theme::kTextFaint,
                  DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, 1,
                  false);
    const D2D1_RECT_F button =
        MakeRect(rect.right - 178.0f, rect.top + 38.0f, 152.0f, 38.0f);
    DrawButton(button, L"Abrir carpeta", false,
               hovered(HitKind::Button, L"open-data"), true);
    AddHit(HitKind::Button, button, L"open-data");
  }

  // Administrator
  {
    const D2D1_RECT_F rect = card(adminTop, 100.0f);
    DrawRoundRect(MakeRect(rect.left + 16.0f, rect.top + 22.0f, 36.0f, 36.0f), 10.0f,
                  WithAlpha(theme::kLime, 0.08f));
    DrawIcon(L"\uE72E", MakeRect(rect.left + 21.0f, rect.top + 27.0f, 26.0f, 26.0f), 15.0f,
             isAdmin_ ? theme::kLime : theme::kTextFaint);
    DrawTextBlock(L"Permisos de administrador", MakeRect(rect.left + 66.0f, rect.top + 18.0f,
                                                         RW(rect) - 320.0f, 24.0f),
                  13.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, theme::kText);
    DrawTextBlock(isAdmin_ ? L"Activo. Los ajustes del sistema y servicios están disponibles."
                           : L"Modo estándar. Algunos ajustes de HKLM y servicios necesitan elevación.",
                  MakeRect(rect.left + 66.0f, rect.top + 42.0f, RW(rect) - 320.0f, 40.0f),
                  10.2f, DWRITE_FONT_WEIGHT_NORMAL, theme::kTextDim);
    if (isAdmin_) {
      const D2D1_RECT_F badge = MakeRect(rect.right - 106.0f, rect.top + 38.0f, 82.0f, 24.0f);
      DrawRoundRect(badge, 12.0f, WithAlpha(theme::kLime, 0.10f), true,
                    WithAlpha(theme::kLime, 0.28f));
      DrawTextBlock(L"ACTIVO", MakeRect(badge.left, badge.top, RW(badge), 24.0f), 8.4f,
                    DWRITE_FONT_WEIGHT_SEMI_BOLD, theme::kLime,
                    DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    } else {
      const D2D1_RECT_F button =
          MakeRect(rect.right - 188.0f, rect.top + 32.0f, 162.0f, 36.0f);
      DrawButton(button, L"Elevar permisos", true,
                 hovered(HitKind::Button, L"elevate"), true);
      AddHit(HitKind::Button, button, L"elevate");
    }
  }

  // Restore all
  {
    const D2D1_RECT_F rect = card(restoreTop, 110.0f);
    DrawRoundRect(MakeRect(rect.left + 16.0f, rect.top + 22.0f, 36.0f, 36.0f), 10.0f,
                  WithAlpha(theme::kRed, 0.08f));
    DrawIcon(L"\uE72C", MakeRect(rect.left + 21.0f, rect.top + 27.0f, 26.0f, 26.0f), 15.0f,
             theme::kRed);
    DrawTextBlock(L"Restaurar todos los ajustes", MakeRect(rect.left + 66.0f, rect.top + 18.0f,
                                                           RW(rect) - 330.0f, 24.0f),
                  13.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, theme::kText);
    DrawTextBlock(L"Devuelve cada valor respaldado a su estado original. No borra archivos "
                  L"personales.",
                  MakeRect(rect.left + 66.0f, rect.top + 42.0f, RW(rect) - 330.0f, 44.0f),
                  10.2f, DWRITE_FONT_WEIGHT_NORMAL, theme::kTextDim);
    const D2D1_RECT_F button =
        MakeRect(rect.right - 178.0f, rect.top + 36.0f, 152.0f, 38.0f);
    DrawRoundRect(button, 10.0f, theme::kRed);
    DrawTextBlock(L"Restaurar todo", MakeRect(button.left, button.top, RW(button),
                                              RH(button)),
                  10.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, theme::kBlack,
                  DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    AddHit(HitKind::Button, button, L"restore-all");
  }

  // About
  {
    const D2D1_RECT_F rect = card(aboutTop, 100.0f);
    DrawRoundRect(MakeRect(rect.left + 16.0f, rect.top + 22.0f, 36.0f, 36.0f), 10.0f,
                  WithAlpha(theme::kLime, 0.08f));
    DrawIcon(L"\uE946", MakeRect(rect.left + 21.0f, rect.top + 27.0f, 26.0f, 26.0f), 15.0f,
             theme::kLimeDim);
    DrawTextBlock(L"Acerca de LimeForge", MakeRect(rect.left + 66.0f, rect.top + 18.0f,
                                                   RW(rect) - 300.0f, 24.0f),
                  13.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, theme::kText);
    DrawTextBlock(L"v1.1  ·  C++20  ·  Win32  ·  Direct2D/DirectWrite  ·  129 ajustes",
                  MakeRect(rect.left + 66.0f, rect.top + 42.0f, RW(rect) - 300.0f, 20.0f),
                  10.2f, DWRITE_FONT_WEIGHT_NORMAL, theme::kTextDim);
    DrawTextBlock(L"Reimplementación independiente y local. Sin cuentas ni licencias.",
                  MakeRect(rect.left + 66.0f, rect.top + 62.0f, RW(rect) - 300.0f, 20.0f),
                  9.4f, DWRITE_FONT_WEIGHT_NORMAL, theme::kTextFaint);
    const D2D1_RECT_F button =
        MakeRect(rect.right - 148.0f, rect.top + 32.0f, 122.0f, 36.0f);
    DrawButton(button, L"Ver detalles", false,
               hovered(HitKind::Button, L"about"), true);
    AddHit(HitKind::Button, button, L"about");
  }
}

void App::DrawScrollbar(const D2D1_RECT_F& content) {
  const float viewport = content.bottom - content.top;
  if (contentHeight_ <= viewport + 1.0f) return;
  const float trackX = content.right - 8.0f;
  const float trackY = content.top + 8.0f;
  const float trackHeight = viewport - 16.0f;
  DrawRoundRect(MakeRect(trackX, trackY, 3.0f, trackHeight), 1.5f,
                WithAlpha(theme::kBorder, 0.6f));
  const float thumbHeight = std::max(40.0f, trackHeight * viewport / contentHeight_);
  const float maxScroll = std::max(1.0f, contentHeight_ - viewport);
  const float t = ClampF(scroll_ / maxScroll, 0.0f, 1.0f);
  const float thumbY = trackY + t * (trackHeight - thumbHeight);
  DrawRoundRect(MakeRect(trackX - 0.5f, thumbY, 4.0f, thumbHeight), 2.0f,
                WithAlpha(theme::kLime, 0.55f));
}

float App::DrawSectionTitle(const std::wstring& eyebrow, const std::wstring& title,
                            const std::wstring& subtitle, float x, float y, float width) {
  DrawTextBlock(eyebrow, MakeRect(x, y, width, 14.0f), 8.8f,
                DWRITE_FONT_WEIGHT_SEMI_BOLD, theme::kLime);
  DrawTextBlock(title, MakeRect(x, y + 15.0f, width, 30.0f), 22.5f,
                DWRITE_FONT_WEIGHT_SEMI_BOLD, theme::kText,
                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR, 1, false);
  DrawTextBlock(subtitle, MakeRect(x, y + 46.0f, width, 22.0f), 10.5f,
                DWRITE_FONT_WEIGHT_NORMAL, theme::kTextDim);
  return y + 80.0f;
}

void App::DrawButton(const D2D1_RECT_F& rect, const std::wstring& text, bool primary,
                     bool hovered, bool compact) {
  if (primary) {
    DrawRoundRect(rect, compact ? 9.0f : 11.0f, hovered ? theme::kLimeBright : theme::kLime);
    DrawTextBlock(text, MakeRect(rect.left + 4.0f, rect.top, RW(rect) - 8.0f, RH(rect)),
                  compact ? 9.8f : 10.8f, DWRITE_FONT_WEIGHT_SEMI_BOLD, theme::kBlack,
                  DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, 1, false);
  } else {
    DrawRoundRect(rect, compact ? 9.0f : 11.0f,
                  hovered ? theme::kSurface3 : theme::kSurface2, true,
                  hovered ? WithAlpha(theme::kLime, 0.45f) : theme::kBorder);
    DrawTextBlock(text, MakeRect(rect.left + 4.0f, rect.top, RW(rect) - 8.0f, RH(rect)),
                  compact ? 9.8f : 10.8f, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                  hovered ? theme::kLime : theme::kText,
                  DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, 1, false);
  }
}

void App::DrawToggle(const D2D1_RECT_F& rect, bool on, bool hovered) {
  const float height = rect.bottom - rect.top;
  const float radius = height / 2.0f;
  DrawRoundRect(rect, radius, on ? theme::kLime : theme::kSurface3, true,
                on ? theme::kLime : (hovered ? WithAlpha(theme::kLime, 0.45f) : theme::kBorder));
  const float knobRadius = radius - 3.5f;
  const float knobX = on ? rect.right - radius : rect.left + radius;
  const float knobY = rect.top + radius;
  DrawCircle(knobX, knobY, knobRadius, on ? theme::kBlack : theme::kTextFaint, true);
}

void App::DrawChip(const D2D1_RECT_F& rect, const std::wstring& text, bool active,
                   bool hovered) {
  DrawRoundRect(rect, (rect.bottom - rect.top) / 2.0f,
                active ? theme::kLime : (hovered ? theme::kSurface3 : theme::kSurface2), true,
                active ? theme::kLime : (hovered ? WithAlpha(theme::kLime, 0.35f) : theme::kBorder));
  DrawTextBlock(text, MakeRect(rect.left + 4.0f, rect.top, rect.right - rect.left - 8.0f,
                               rect.bottom - rect.top),
                9.4f, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                active ? theme::kBlack : (hovered ? theme::kText : theme::kTextDim),
                DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, 1, false);
}

void App::DrawBadge(float x, float y, const std::wstring& text, const std::wstring& glyph,
                    D2D1_COLOR_F color) {
  const float width = BadgeWidth(glyph.c_str(), text);
  const D2D1_RECT_F rect = MakeRect(x, y, width, 20.0f);
  DrawRoundRect(rect, 10.0f, WithAlpha(color, 0.08f), true, WithAlpha(color, 0.26f));
  float textLeft = x + 8.0f;
  if (!glyph.empty()) {
    DrawIcon(glyph, MakeRect(x + 6.0f, y + 4.0f, 12.0f, 12.0f), 8.0f, color);
    textLeft = x + 20.0f;
  }
  DrawTextBlock(text, MakeRect(textLeft, y, width - (textLeft - x) - 6.0f, 20.0f), 8.2f,
                DWRITE_FONT_WEIGHT_SEMI_BOLD, color, DWRITE_TEXT_ALIGNMENT_LEADING,
                DWRITE_PARAGRAPH_ALIGNMENT_CENTER, 1, false);
}

float App::MeasureText(const std::wstring& text, float width, float size,
                       DWRITE_FONT_WEIGHT weight, int maxLines, bool wrap) {
  if (!dwriteFactory_ || text.empty()) return 0.0f;
  IDWriteTextFormat* format =
      GetFormat(size, weight, DWRITE_TEXT_ALIGNMENT_LEADING,
                DWRITE_PARAGRAPH_ALIGNMENT_NEAR, wrap, false);
  if (!format) return 0.0f;
  IDWriteTextLayout* layout = nullptr;
  if (FAILED(dwriteFactory_->CreateTextLayout(text.c_str(),
                                             static_cast<UINT32>(text.size()), format, width,
                                             10000.0f, &layout)) ||
      !layout) {
    return 0.0f;
  }
  DWRITE_TEXT_METRICS metrics{};
  layout->GetMetrics(&metrics);
  layout->Release();
  float height = metrics.height;
  if (maxLines > 0) {
    const float lineHeight = size * 1.35f;
    height = std::min(height, lineHeight * maxLines);
  }
  return height;
}

void App::DrawTextBlock(const std::wstring& text, const D2D1_RECT_F& rect, float size,
                        DWRITE_FONT_WEIGHT weight, D2D1_COLOR_F color,
                        DWRITE_TEXT_ALIGNMENT align, DWRITE_PARAGRAPH_ALIGNMENT valign,
                        int maxLines, bool wrap) {
  if (!rt_ || !brush_ || !dwriteFactory_ || text.empty()) return;
  if (rect.right <= rect.left || rect.bottom <= rect.top) return;
  if (maxLines > 0 && wrap) {
    const float lineHeight = size * 1.35f;
    if (rect.bottom - rect.top > lineHeight * maxLines) {
      // Keep the requested clip height; DWrite clips automatically to the layout box.
    }
  }
  IDWriteTextFormat* format =
      GetFormat(size, weight, align, valign, wrap, false);
  if (!format) return;
  brush_->SetColor(color);
  rt_->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), format, rect, brush_.Get());
}

void App::DrawIcon(const std::wstring& glyph, const D2D1_RECT_F& rect, float size,
                   D2D1_COLOR_F color, DWRITE_TEXT_ALIGNMENT align) {
  if (!rt_ || !brush_) return;
  IDWriteTextFormat* format =
      GetFormat(size, DWRITE_FONT_WEIGHT_NORMAL, align, DWRITE_PARAGRAPH_ALIGNMENT_CENTER,
                false, true);
  if (!format) return;
  brush_->SetColor(color);
  rt_->DrawTextW(glyph.c_str(), static_cast<UINT32>(glyph.size()), format, rect, brush_.Get());
}

void App::DrawBolt(const D2D1_RECT_F& rect, D2D1_COLOR_F color) {
  if (!d2dFactory_ || !brush_ || !rt_) return;
  ComPtr<ID2D1PathGeometry> geometry;
  if (FAILED(d2dFactory_->CreatePathGeometry(&geometry))) return;
  ComPtr<ID2D1GeometrySink> sink;
  if (FAILED(geometry->Open(&sink))) return;
  const float x = rect.left;
  const float y = rect.top;
  const float w = rect.right - rect.left;
  const float h = rect.bottom - rect.top;
  const D2D1_POINT_2F points[] = {
      D2D1::Point2F(x + w * 0.62f, y),
      D2D1::Point2F(x + w * 0.20f, y + h * 0.55f),
      D2D1::Point2F(x + w * 0.47f, y + h * 0.55f),
      D2D1::Point2F(x + w * 0.38f, y + h),
      D2D1::Point2F(x + w * 0.84f, y + h * 0.42f),
      D2D1::Point2F(x + w * 0.56f, y + h * 0.42f),
  };
  sink->BeginFigure(points[0], D2D1_FIGURE_BEGIN_FILLED);
  sink->AddLines(points + 1, 5);
  sink->EndFigure(D2D1_FIGURE_END_CLOSED);
  if (SUCCEEDED(sink->Close())) {
    brush_->SetColor(color);
    rt_->FillGeometry(geometry.Get(), brush_.Get());
  }
}

IDWriteTextFormat* App::GetFormat(float size, DWRITE_FONT_WEIGHT weight,
                                  DWRITE_TEXT_ALIGNMENT align,
                                  DWRITE_PARAGRAPH_ALIGNMENT valign, bool wrap, bool icons) {
  if (!dwriteFactory_) return nullptr;
  const unsigned long long key =
      (static_cast<unsigned long long>(size * 10.0f) & 0xFFFFULL) |
      ((static_cast<unsigned long long>(weight) & 0x3FFULL) << 16) |
      ((static_cast<unsigned long long>(align) & 0x3ULL) << 26) |
      ((static_cast<unsigned long long>(valign) & 0x3ULL) << 28) |
      ((static_cast<unsigned long long>(wrap ? 1 : 0)) << 30) |
      ((static_cast<unsigned long long>(icons ? 1 : 0)) << 31);
  auto it = formats_.find(key);
  if (it != formats_.end()) return it->second.Get();
  const wchar_t* family = icons ? L"Segoe MDL2 Assets" : L"Segoe UI";
  ComPtr<IDWriteTextFormat> format;
  if (FAILED(dwriteFactory_->CreateTextFormat(family, nullptr, weight,
                                             DWRITE_FONT_STYLE_NORMAL,
                                             DWRITE_FONT_STRETCH_NORMAL, size, L"es-AR",
                                             &format)) ||
      !format) {
    return nullptr;
  }
  format->SetTextAlignment(align);
  format->SetParagraphAlignment(valign);
  format->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
  IDWriteTextFormat* raw = format.Get();
  formats_[key] = std::move(format);
  return raw;
}

IDWriteTextFormat* App::GetMonoFormat(float size) {
  if (!dwriteFactory_) return nullptr;
  const unsigned long long key =
      (1ULL << 63) | (static_cast<unsigned long long>(size * 10.0f) & 0xFFFFULL);
  auto it = formats_.find(key);
  if (it != formats_.end()) return it->second.Get();
  ComPtr<IDWriteTextFormat> format;
  if (FAILED(dwriteFactory_->CreateTextFormat(L"Consolas", nullptr,
                                             DWRITE_FONT_WEIGHT_NORMAL,
                                             DWRITE_FONT_STYLE_NORMAL,
                                             DWRITE_FONT_STRETCH_NORMAL, size, L"es-AR",
                                             &format)) ||
      !format) {
    return nullptr;
  }
  format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
  format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
  format->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
  IDWriteTextFormat* raw = format.Get();
  formats_[key] = std::move(format);
  return raw;
}

void App::DrawRoundRect(const D2D1_RECT_F& rect, float radius, D2D1_COLOR_F fill,
                        bool border, D2D1_COLOR_F borderColor, float stroke) {
  if (!rt_ || !brush_) return;
  if (fill.a > 0.0f) {
    brush_->SetColor(fill);
    rt_->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), brush_.Get());
  }
  if (border) {
    brush_->SetColor(borderColor);
    rt_->DrawRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), brush_.Get(), stroke);
  }
}

void App::DrawLine(float x1, float y1, float x2, float y2, D2D1_COLOR_F color,
                   float stroke) {
  if (!rt_ || !brush_) return;
  brush_->SetColor(color);
  rt_->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), brush_.Get(), stroke);
}

void App::DrawCircle(float x, float y, float radius, D2D1_COLOR_F color, bool fill) {
  if (!rt_ || !brush_) return;
  brush_->SetColor(color);
  if (fill) {
    rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, y), radius, radius), brush_.Get());
  } else {
    rt_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(x, y), radius, radius), brush_.Get(),
                     std::max(1.0f, radius * 0.035f));
  }
}

void App::ShowToast(bool ok, const std::wstring& message) {
  toast_ = message;
  toastOk_ = ok;
  toastUntil_ = GetTickCount64() + 5200;
  if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::RequestCloseBlocked() {
  ShowToast(false, L"Esperá a que termine la operación en curso.");
}

bool App::ConsumeCloseRequested() {
  const bool requested = closeRequested_;
  closeRequested_ = false;
  return requested;
}

void App::OnMouseMove(float pixelX, float pixelY) {
  mouseX_ = pixelX / scale_;
  mouseY_ = pixelY / scale_;
  if (busy_) {
    hoverKey_.clear();
    SetCursor(LoadCursorW(nullptr, IDC_ARROW));
    return;
  }
  SetHoverFromMouse();
  UpdateCursor();
}

void App::OnMouseLeave() {
  mouseX_ = -1000.0f;
  mouseY_ = -1000.0f;
  if (!hoverKey_.empty()) {
    hoverKey_.clear();
    InvalidateRect(hwnd_, nullptr, FALSE);
  }
  SetCursor(LoadCursorW(nullptr, IDC_ARROW));
}

void App::OnLButtonDown(float pixelX, float pixelY) {
  mouseX_ = pixelX / scale_;
  mouseY_ = pixelY / scale_;
  if (busy_) return;
  mouseDown_ = true;
  const bool modalOpen = modal_.kind != ModalKind::None;
  const Hit hit = HitTest(mouseX_, mouseY_, modalOpen);
  if (!modalOpen && hit.kind == HitKind::Search) {
    searchFocused_ = true;
  } else if (!modalOpen) {
    searchFocused_ = false;
  }
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::OnLButtonUp(float pixelX, float pixelY) {
  if (!mouseDown_) return;
  mouseDown_ = false;
  if (busy_) return;
  mouseX_ = pixelX / scale_;
  mouseY_ = pixelY / scale_;

  const bool modalOpen = modal_.kind != ModalKind::None;
  const Hit hit = HitTest(mouseX_, mouseY_, modalOpen);
  if (modalOpen) {
    if (hit.inModal && hit.kind == HitKind::Button) {
      if (hit.payload == L"modal:primary") {
        RunModalPrimary();
      } else if (hit.payload == L"modal:secondary") {
        CloseModal();
      }
      return;
    }
    if (hit.inModal && hit.kind == HitKind::Modal && hit.payload == L"outside") {
      CloseModal();
    }
    return;
  }

  switch (hit.kind) {
    case HitKind::WindowMin:
      ShowWindow(hwnd_, SW_MINIMIZE);
      return;
    case HitKind::WindowMax:
      ShowWindow(hwnd_, IsZoomed(hwnd_) ? SW_RESTORE : SW_MAXIMIZE);
      return;
    case HitKind::WindowClose:
      PostMessageW(hwnd_, WM_CLOSE, 0, 0);
      return;
    case HitKind::Search:
      searchFocused_ = true;
      InvalidateRect(hwnd_, nullptr, FALSE);
      return;
    case HitKind::Nav: {
      if (hit.payload.rfind(L"view:", 0) == 0) {
        const std::wstring view = hit.payload.substr(5);
        if (view == L"Home") GoView(View::Home);
        else if (view == L"Tweaks") GoView(View::Tweaks);
        else if (view == L"Cleaner") GoView(View::Cleaner);
        else if (view == L"Startup") GoView(View::Startup);
        else if (view == L"Diagnostics") GoView(View::Diagnostics);
        else if (view == L"Settings") GoView(View::Settings);
      } else if (hit.payload.rfind(L"cat:", 0) == 0) {
        GoTweaks(hit.payload.substr(4));
      } else if (hit.payload.rfind(L"open:", 0) == 0) {
        const std::wstring view = hit.payload.substr(5);
        if (view == L"Cleaner") GoView(View::Cleaner);
        else if (view == L"Startup") GoView(View::Startup);
        else if (view == L"Diagnostics") GoView(View::Diagnostics);
      }
      return;
    }
    case HitKind::Card:
    case HitKind::Detail: {
      const Tweak* tweak = FindTweak(hit.payload);
      if (tweak) OpenDetail(*tweak);
      return;
    }
    case HitKind::Toggle: {
      if (hit.payload.rfind(L"setting:", 0) == 0) {
        ToggleSetting(hit.payload.substr(8));
        return;
      }
      const Tweak* tweak = FindTweak(hit.payload);
      if (tweak) RequestToggle(*tweak);
      return;
    }
    case HitKind::Action: {
      const Tweak* tweak = FindTweak(hit.payload);
      if (tweak) RequestAction(*tweak);
      return;
    }
    case HitKind::Filter: {
      if (hit.payload.rfind(L"filter:", 0) == 0) {
        filter_ = hit.payload.substr(7);
        scroll_ = 0.0f;
      } else if (hit.payload == L"category:all") {
        category_ = L"Todas";
        scroll_ = 0.0f;
      }
      ClampScroll();
      InvalidateRect(hwnd_, nullptr, FALSE);
      return;
    }
    case HitKind::Clean: {
      StartClean(hit.payload);
      return;
    }
    case HitKind::Button: {
      const std::wstring& action = hit.payload;
      if (action == L"pack") {
        OpenPackConfirm();
      } else if (action == L"undo") {
        UndoLast();
      } else if (action == L"redo") {
        RedoLast();
      } else if (action == L"explore") {
        GoTweaks(L"Todas");
      } else if (action == L"clear-search") {
        search_.clear();
        searchFocused_ = true;
        scroll_ = 0.0f;
        InvalidateRect(hwnd_, nullptr, FALSE);
      } else if (action.rfind(L"clean:", 0) == 0) {
        const std::wstring key = action.substr(6);
        const bool needsAdmin = key == L"wu" || key == L"prefetch" || key == L"logs" ||
                                key == L"all";
        if (needsAdmin && !isAdmin_) {
          OpenElevate(L"Esta limpieza toca servicios, registros o carpetas protegidas de "
                      L"Windows y necesita permisos de administrador.");
        } else {
          StartClean(key);
        }
      } else if (action == L"refresh-startup") {
        StartStartupScan();
      } else if (action == L"open-taskmgr") {
        if (engine_.OpenTarget(L"taskmgr.exe"))
          ShowToast(true, L"Se abrió el Administrador de tareas.");
        else
          ShowToast(false, L"No se pudo abrir el Administrador de tareas.");
      } else if (action == L"open-startup-folder") {
        if (engine_.OpenTarget(L"shell:startup"))
          ShowToast(true, L"Se abrió la carpeta Inicio.");
        else
          ShowToast(false, L"No se pudo abrir la carpeta Inicio.");
      } else if (action == L"refresh-report") {
        StartReport();
      } else if (action == L"copy-report") {
        CopyTextToClipboard(hwnd_, report_);
        ShowToast(true, L"Informe copiado al portapapeles.");
      } else if (action == L"open-data") {
        if (engine_.OpenDataFolder())
          ShowToast(true, L"Se abrió la carpeta de datos.");
        else
          ShowToast(false, L"No se pudo abrir la carpeta de datos.");
      } else if (action == L"elevate") {
        OpenElevate(L"Algunos ajustes escriben en HKLM, servicios o tareas programadas. "
                    L"Reiniciá LimeForge con permisos elevados para aplicarlos.");
      } else if (action == L"restore-all") {
        OpenRestoreConfirm();
      } else if (action == L"about") {
        OpenInfo(L"Acerca de LimeForge",
                 L"LimeForge v1.1\n\nReimplementación independiente en C++20 con Win32, "
                 L"Direct2D y DirectWrite. Incluye 129 tweaks, limpieza, diagnóstico, "
                 L"prioridad de juegos, dashboard de rendimiento e historial de cambios.\n\n"
                 L"No usa cuentas, licencias, telemetría propia ni servicios remotos. Todo se "
                 L"ejecuta en tu equipo.\n\nLos ajustes del sistema se aplican bajo tu "
                 L"responsabilidad. Creá un punto de restauración antes de cambios importantes.");
      }
      return;
    }
    case HitKind::Modal:
      return;
    default:
      break;
  }
}

void App::OnMouseWheel(int delta) {
  if (busy_ || modal_.kind != ModalKind::None) return;
  const int steps = delta / WHEEL_DELTA;
  if (steps == 0) return;
  scroll_ -= static_cast<float>(steps) * 84.0f;
  ClampScroll();
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::OnKeyDown(WPARAM key, bool ctrl) {
  if (key == VK_ESCAPE) {
    if (modal_.kind != ModalKind::None) {
      CloseModal();
    } else if (searchFocused_) {
      searchFocused_ = false;
      InvalidateRect(hwnd_, nullptr, FALSE);
    }
    return;
  }
  if (modal_.kind != ModalKind::None) {
    if (key == VK_RETURN || key == VK_SPACE) RunModalPrimary();
    return;
  }
  if (busy_) return;

  if (ctrl && (key == L'Z' || key == L'z')) {
    UndoLast();
    return;
  }
  if (ctrl && (key == L'Y' || key == L'y')) {
    RedoLast();
    return;
  }

  if (ctrl && key >= L'1' && key <= L'6') {
    static const View views[] = {View::Home, View::Tweaks, View::Cleaner,
                                 View::Startup, View::Diagnostics, View::Settings};
    ChangeView(views[key - L'1']);
    return;
  }
  if (ctrl && (key == L'F' || key == L'f')) {
    if (view_ != View::Tweaks) GoTweaks(L"Todas");
    searchFocused_ = true;
    InvalidateRect(hwnd_, nullptr, FALSE);
    return;
  }
  if (!searchFocused_) {
    const float viewport = contentBottom_ - contentTop_;
    bool scrolled = false;
    if (key == VK_UP) {
      scroll_ -= 72.0f;
      scrolled = true;
    } else if (key == VK_DOWN) {
      scroll_ += 72.0f;
      scrolled = true;
    } else if (key == VK_PRIOR) {
      scroll_ -= viewport * 0.9f;
      scrolled = true;
    } else if (key == VK_NEXT) {
      scroll_ += viewport * 0.9f;
      scrolled = true;
    } else if (key == VK_HOME) {
      scroll_ = 0.0f;
      scrolled = true;
    } else if (key == VK_END) {
      scroll_ = contentHeight_;
      scrolled = true;
    }
    if (scrolled) {
      ClampScroll();
      InvalidateRect(hwnd_, nullptr, FALSE);
      return;
    }
  }
  if (searchFocused_) {
    if (key == VK_BACK) {
      if (!search_.empty()) search_.pop_back();
      scroll_ = 0.0f;
      ClampScroll();
      InvalidateRect(hwnd_, nullptr, FALSE);
    } else if (key == VK_RETURN) {
      const std::vector<const Tweak*> results = FilteredTweaks();
      if (!results.empty()) OpenDetail(*results.front());
    }
  }
}

void App::OnChar(wchar_t ch) {
  if (!searchFocused_ || modal_.kind != ModalKind::None || busy_) return;
  if (ch < 32 || ch == 127) return;
  if (search_.size() >= 80) return;
  search_.push_back(ch);
  scroll_ = 0.0f;
  ClampScroll();
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::OnTimer() {
  ++tick_;
  if (tick_ % 8 == 0) {
    SampleMetrics();
    InvalidateRect(hwnd_, nullptr, FALSE);
  }

  if (busy_ || searchFocused_) InvalidateRect(hwnd_, nullptr, FALSE);

  if (!toast_.empty() && GetTickCount64() >= toastUntil_) {
    toast_.clear();
    InvalidateRect(hwnd_, nullptr, FALSE);
  }
  if (!lastCleanMessage_.empty() && tick_ % 40 == 0) {
    // Keep the cleaner note visible; nothing to animate.
  }
}

void App::UpdateCursor() {
  const bool modalOpen = modal_.kind != ModalKind::None;
  const Hit hit = HitTest(mouseX_, mouseY_, modalOpen);
  if (!modalOpen && hit.kind == HitKind::Search) {
    SetCursor(LoadCursorW(nullptr, IDC_IBEAM));
    return;
  }
  switch (hit.kind) {
    case HitKind::Nav:
    case HitKind::Card:
    case HitKind::Toggle:
    case HitKind::Action:
    case HitKind::Detail:
    case HitKind::Button:
    case HitKind::Filter:
    case HitKind::Clean:
    case HitKind::WindowMin:
    case HitKind::WindowMax:
    case HitKind::WindowClose:
      SetCursor(LoadCursorW(nullptr, IDC_HAND));
      break;
    default:
      SetCursor(LoadCursorW(nullptr, IDC_ARROW));
      break;
  }
}

void App::SetHoverFromMouse() {
  const bool modalOpen = modal_.kind != ModalKind::None;
  const Hit hit = HitTest(mouseX_, mouseY_, modalOpen);
  UpdateHover(hit.kind == HitKind::None ? std::wstring() : HitKey(hit));
}

void App::UpdateHover(const std::wstring& key) {
  if (hoverKey_ == key) return;
  hoverKey_ = key;
  if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::ClearHits() { hits_.clear(); }

void App::AddHit(HitKind kind, const D2D1_RECT_F& rect, const std::wstring& payload) {
  Hit hit;
  hit.rect = rect;
  hit.kind = kind;
  hit.payload = payload;
  hit.inContent = addingContent_;
  hit.inModal = addingModal_;
  hits_.push_back(std::move(hit));
}

App::Hit App::HitTest(float x, float y, bool modalOnly) const {
  const D2D1_RECT_F contentRect =
      D2D1::RectF(sidebarWidth_, contentTop_, width_, contentBottom_);
  for (auto it = hits_.rbegin(); it != hits_.rend(); ++it) {
    if (modalOnly && !it->inModal) continue;
    if (!modalOnly && it->inModal) continue;
    if (!modalOnly && it->inContent && !ContainsPoint(contentRect, x, y)) continue;
    if (ContainsPoint(it->rect, x, y)) return *it;
  }
  return Hit{};
}

std::wstring App::HitKey(const Hit& hit) const {
  return std::to_wstring(static_cast<int>(hit.kind)) + L"|" + hit.payload;
}

void App::ReloadApplied() {
  if (engineReady_)
    applied_ = engine_.AppliedIds();
  else
    applied_.clear();
}

const Tweak* App::FindTweak(const std::wstring& id) const {
  auto it = indexById_.find(id);
  if (it == indexById_.end()) return nullptr;
  return &tweaks_[it->second];
}

bool App::IsAdminNow() const { return isAdmin_; }

bool App::AnyAppliedNeedsAdmin() const {
  for (const std::wstring& id : applied_) {
    const Tweak* tweak = FindTweak(id);
    if (tweak && tweak->needsAdmin) return true;
  }
  return false;
}

bool App::RestartPending() const {
  for (const std::wstring& id : applied_) {
    const Tweak* tweak = FindTweak(id);
    if (tweak && tweak->restart) return true;
  }
  return false;
}

bool App::NeedsConfirmation(const Tweak& tweak) const {
  if (!tweak.confirmMessage.empty()) return true;
  return confirmRisky_ && tweak.risky;
}

std::vector<const Tweak*> App::FilteredTweaks() const {
  std::vector<const Tweak*> out;
  const std::wstring needle = Lower(Trim(search_));
  for (const Tweak& tweak : tweaks_) {
    if (category_ != L"Todas" && tweak.category != category_) continue;
    if (filter_ == L"active" && !applied_.count(tweak.id)) continue;
    if (filter_ == L"recommended" && !tweak.recommended) continue;
    if (filter_ == L"risky" && !tweak.risky) continue;
    if (filter_ == L"action" && !tweak.actionOnly) continue;
    if (!needle.empty()) {
      const bool match =
          ContainsCI(tweak.name, needle) || ContainsCI(tweak.description, needle) ||
          ContainsCI(tweak.category, needle) || ContainsCI(tweak.subcategory, needle) ||
          ContainsCI(tweak.id, needle);
      if (!match) continue;
    }
    out.push_back(&tweak);
  }
  return out;
}

void App::GoHome() { GoView(View::Home); }

void App::GoTweaks(const std::wstring& category) {
  category_ = category.empty() ? L"Todas" : category;
  filter_ = L"all";
  view_ = View::Tweaks;
  scroll_ = 0.0f;
  searchFocused_ = false;
  contentHeight_ = 0.0f;
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::GoView(View view) { ChangeView(view); }

void App::ChangeView(View view) {
  view_ = view;
  scroll_ = 0.0f;
  searchFocused_ = false;
  contentHeight_ = 0.0f;
  if (view == View::Startup && !startupLoaded_ && !startupLoading_ && !busy_) {
    StartStartupScan();
  }
  if (view == View::Diagnostics && !reportLoaded_ && !reportLoading_ && !busy_) {
    StartReport();
  }
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::ClampScroll() {
  const float viewport = contentBottom_ - contentTop_;
  const float maxScroll = std::max(0.0f, contentHeight_ - viewport);
  scroll_ = ClampF(scroll_, 0.0f, maxScroll);
}

void App::OpenConfirm(const Tweak& tweak, bool apply) {
  modal_ = Modal{};
  modal_.kind = ModalKind::Confirm;
  modal_.title = apply ? L"Confirmar cambio" : L"Restaurar ajuste";
  modal_.body = tweak.confirmMessage.empty() ? tweak.description : tweak.confirmMessage;
  if (!apply) {
    modal_.body = L"Se restaurarán los valores originales guardados en la copia de "
                  L"seguridad de LimeForge.";
  }
  modal_.primaryLabel = apply ? L"Aplicar" : L"Revertir";
  modal_.secondaryLabel = L"Cancelar";
  modal_.primaryPayload = apply ? L"apply" : L"revert";
  modal_.id = tweak.id;
  modal_.destructive = apply && tweak.risky;
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::OpenConfirmAction(const Tweak& tweak) {
  modal_ = Modal{};
  modal_.kind = ModalKind::Confirm;
  modal_.title = L"Ejecutar acción";
  modal_.body = tweak.confirmMessage.empty() ? tweak.description : tweak.confirmMessage;
  modal_.primaryLabel = L"Continuar";
  modal_.secondaryLabel = L"Cancelar";
  modal_.primaryPayload = L"run";
  modal_.id = tweak.id;
  modal_.destructive = tweak.risky;
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::OpenDetail(const Tweak& tweak) {
  modal_ = Modal{};
  modal_.kind = ModalKind::Detail;
  modal_.title = tweak.name;
  modal_.id = tweak.id;
  modal_.secondaryLabel = L"Cerrar";
  if (tweak.actionOnly) {
    modal_.primaryLabel = tweak.buttonLabel.empty() ? L"Ejecutar" : tweak.buttonLabel;
    modal_.primaryPayload = L"run";
  } else if (applied_.count(tweak.id)) {
    modal_.primaryLabel = L"Revertir";
    modal_.primaryPayload = L"revert";
  } else {
    modal_.primaryLabel = L"Aplicar";
    modal_.primaryPayload = L"apply";
  }
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::OpenElevate(const std::wstring& reason) {
  modal_ = Modal{};
  modal_.kind = ModalKind::Elevate;
  modal_.title = L"Permisos de administrador";
  modal_.body = reason +
                L"\n\nLimeForge se cerrará y volverá a abrir con permisos elevados. "
                L"Los ajustes ya aplicados se conservan.";
  modal_.primaryLabel = L"Reiniciar como admin";
  modal_.secondaryLabel = L"Ahora no";
  modal_.primaryPayload = L"elevate";
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::OpenPackConfirm() {
  if (!isAdmin_) {
    OpenElevate(L"El pack recomendado incluye ajustes de HKLM, servicios y tareas del "
                L"sistema que requieren permisos de administrador.");
    return;
  }
  int pending = 0;
  for (const Tweak& tweak : tweaks_) {
    if (tweak.recommended && !tweak.actionOnly && !applied_.count(tweak.id)) pending += 1;
  }
  if (pending == 0) {
    ShowToast(true, L"Todos los ajustes del pack recomendado ya están activos.");
    return;
  }
  modal_ = Modal{};
  modal_.kind = ModalKind::Pack;
  modal_.title = L"Pack recomendado";
  modal_.body = L"Se aplicarán " + IntText(pending) +
                L" ajustes seguros y reversibles. Cada valor original queda respaldado "
                L"antes de tocarlo. El proceso puede tardar unos minutos.";
  modal_.primaryLabel = L"Aplicar pack";
  modal_.secondaryLabel = L"Cancelar";
  modal_.primaryPayload = L"pack";
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::OpenRestoreConfirm() {
  const int count = AppliedCount();
  if (count == 0) {
    ShowToast(true, L"No hay ajustes activos para restaurar.");
    return;
  }
  modal_ = Modal{};
  modal_.kind = ModalKind::RestoreAll;
  modal_.title = L"Restaurar todos los ajustes";
  modal_.body = L"Se restaurarán " + IntText(count) +
                L" ajustes activos usando las copias de seguridad del registro. Los "
                L"cambios de servicios, energía y red se revierten con sus comandos "
                L"originales.";
  modal_.primaryLabel = L"Restaurar todo";
  modal_.secondaryLabel = L"Cancelar";
  modal_.primaryPayload = L"restore";
  modal_.destructive = true;
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::OpenInfo(const std::wstring& title, const std::wstring& body) {
  modal_ = Modal{};
  modal_.kind = ModalKind::Info;
  modal_.title = title;
  modal_.body = body;
  modal_.primaryLabel = L"Entendido";
  modal_.primaryPayload = L"info";
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::CloseModal() {
  modal_ = Modal{};
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::RunModalPrimary() {
  if (busy_) return;
  const Modal modal = modal_;
  CloseModal();
  if (modal.kind == ModalKind::Info) return;
  if (modal.kind == ModalKind::Elevate) {
    StartElevate();
    return;
  }
  if (modal.kind == ModalKind::RestoreAll) {
    StartRestoreAll();
    return;
  }
  if (modal.kind == ModalKind::Pack) {
    StartPack();
    return;
  }
  const Tweak* tweak = FindTweak(modal.id);
  if (!tweak) return;
  if (tweak->needsAdmin && !isAdmin_) {
    OpenElevate(L"Este ajuste necesita permisos de administrador.");
    return;
  }
  if (modal.kind == ModalKind::Detail &&
      (modal.primaryPayload == L"apply" || modal.primaryPayload == L"run") &&
      NeedsConfirmation(*tweak)) {
    if (tweak->actionOnly) {
      OpenConfirmAction(*tweak);
    } else {
      OpenConfirm(*tweak, true);
    }
    return;
  }
  if (modal.primaryPayload == L"run") {
    StartAction(*tweak);
  } else if (modal.primaryPayload == L"apply") {
    StartToggle(*tweak, true);
  } else if (modal.primaryPayload == L"revert") {
    StartToggle(*tweak, false);
  }
}

void App::RequestToggle(const Tweak& tweak) {
  if (busy_ || !engineReady_) return;
  if (tweak.actionOnly) {
    RequestAction(tweak);
    return;
  }
  if (tweak.needsAdmin && !isAdmin_) {
    OpenElevate(L"Este ajuste escribe valores protegidos de Windows y necesita permisos "
                L"de administrador.");
    return;
  }
  const bool active = applied_.count(tweak.id) != 0;
  if (active) {
    StartToggle(tweak, false);
    return;
  }
  if (NeedsConfirmation(tweak)) {
    OpenConfirm(tweak, true);
  } else {
    StartToggle(tweak, true);
  }
}

void App::RequestAction(const Tweak& tweak) {
  if (busy_ || !engineReady_) return;
  if (tweak.needsAdmin && !isAdmin_) {
    OpenElevate(L"Esta acción necesita permisos de administrador.");
    return;
  }
  if (NeedsConfirmation(tweak)) {
    OpenConfirmAction(tweak);
  } else {
    StartAction(tweak);
  }
}

void App::StartToggle(const Tweak& tweak, bool apply) {
  if (busy_ || !engineReady_) return;
  busy_ = true;
  busyLabel_ = apply ? L"Aplicando: " + tweak.name : L"Restaurando: " + tweak.name;
  SpawnTask([this, tweak, apply]() {
    EngineResult result = apply ? engine_.Apply(tweak) : engine_.Revert(tweak);
    TaskResult out;
    out.kind = TaskResult::Kind::Tweak;
    out.ok = result.ok;
    out.message = result.ok ? result.message
                            : (result.message.empty() ? L"No se pudo completar el cambio."
                                                      : result.message);
    out.id = tweak.id;
    out.applied = apply && result.ok;
    out.wasApply = apply;
    out.historyCandidate = true;
    return out;
  });
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::StartAction(const Tweak& tweak) {
  if (busy_ || !engineReady_) return;
  busy_ = true;
  busyLabel_ = L"Ejecutando: " + tweak.name;
  SpawnTask([this, tweak]() {
    EngineResult result = engine_.RunAction(tweak);
    TaskResult out;
    out.kind = TaskResult::Kind::Action;
    out.ok = result.ok;
    out.message = result.message;
    out.id = tweak.id;
    return out;
  });
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::StartPack() {
  if (busy_ || !engineReady_) return;
  if (!isAdmin_) {
    OpenElevate(L"El pack recomendado necesita permisos de administrador.");
    return;
  }
  std::vector<Tweak> pack;
  for (const Tweak& tweak : tweaks_) {
    if (tweak.recommended && !tweak.actionOnly && !applied_.count(tweak.id))
      pack.push_back(tweak);
  }
  if (pack.empty()) {
    ShowToast(true, L"El pack recomendado ya está aplicado.");
    return;
  }
  busy_ = true;
  busyLabel_ = L"Aplicando pack recomendado…";
  SpawnTask([this, pack]() {
    int okCount = 0;
    int failCount = 0;
    std::wstring firstFailure;
    std::vector<std::wstring> appliedIds;
    for (const Tweak& tweak : pack) {
      EngineResult result = engine_.Apply(tweak);
      if (result.ok) {
        okCount += 1;
        appliedIds.push_back(tweak.id);
      } else {
        failCount += 1;
        if (firstFailure.empty())
          firstFailure = tweak.name + L": " + FirstLineOf(result.message);
      }
    }
    TaskResult out;
    out.kind = TaskResult::Kind::Pack;
    out.ok = failCount == 0;
    out.changedIds = std::move(appliedIds);
    out.message = L"Pack aplicado: " + IntText(okCount) + L" ajustes";
    if (failCount > 0)
      out.message += L", " + IntText(failCount) + L" fallaron. " + firstFailure;
    else
      out.message += L". Todo listo.";
    return out;
  });
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::StartRestoreAll() {
  if (busy_ || !engineReady_) return;
  const std::set<std::wstring> ids = engine_.AppliedIds();
  if (ids.empty()) {
    ShowToast(true, L"No hay ajustes activos para restaurar.");
    return;
  }
  busy_ = true;
  busyLabel_ = L"Restaurando todos los ajustes…";
  undoStack_.clear();
  redoStack_.clear();
  pendingHistory_.reset();
  SpawnTask([this, ids]() {
    int okCount = 0;
    int failCount = 0;
    std::wstring firstFailure;
    for (const std::wstring& id : ids) {
      const Tweak* tweak = FindTweak(id);
      if (!tweak) continue;
      EngineResult result = engine_.Revert(*tweak);
      if (result.ok) {
        okCount += 1;
      } else {
        failCount += 1;
        if (firstFailure.empty())
          firstFailure = tweak->name + L": " + FirstLineOf(result.message);
      }
    }
    TaskResult out;
    out.kind = TaskResult::Kind::RestoreAll;
    out.ok = failCount == 0;
    out.message = L"Restaurados " + IntText(okCount) + L" ajustes";
    if (failCount > 0)
      out.message += L". " + IntText(failCount) + L" fallaron. " + firstFailure;
    else
      out.message += L" a sus valores originales.";
    return out;
  });
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::StartClean(const std::wstring& key) {
  if (busy_ || !engineReady_) return;
  busy_ = true;
  busyLabel_ = L"Limpiando…";
  SpawnTask([this, key]() {
    const std::wstring output = engine_.Clean(key);
    TaskResult out;
    out.kind = TaskResult::Kind::Clean;
    out.ok = !output.empty();
    out.message = output.empty() ? L"La limpieza no devolvió resultado." : output;
    return out;
  });
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::StartReport() {
  if (busy_ || !engineReady_) return;
  reportLoading_ = true;
  busy_ = true;
  busyLabel_ = L"Generando informe del sistema…";
  SpawnTask([this]() {
    TaskResult out;
    out.kind = TaskResult::Kind::Report;
    out.message = engine_.SystemReport();
    out.ok = !out.message.empty();
    if (!out.ok) out.message = L"No se pudo generar el informe.";
    return out;
  });
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::StartStartupScan() {
  if (busy_ || !engineReady_ || startupLoading_) return;
  startupLoading_ = true;
  busy_ = true;
  busyLabel_ = L"Leyendo el inicio de Windows…";
  SpawnTask([this]() {
    TaskResult out;
    out.kind = TaskResult::Kind::Startup;
    out.ok = true;
    out.startup = engine_.StartupEntries();
    out.message = IntText(static_cast<int>(out.startup.size())) + L" entradas de inicio.";
    return out;
  });
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::StartElevate() {
  if (!engineReady_) return;
  if (Engine::IsProcessElevated()) {
    ShowToast(true, L"LimeForge ya se está ejecutando como administrador.");
    return;
  }
  if (engine_.RelaunchAsAdmin()) {
    ShowToast(true, L"Reiniciando con permisos de administrador…");
    closeRequested_ = true;
    PostMessageW(hwnd_, WM_CLOSE, 0, 0);
  } else {
    ShowToast(false, L"No se pudo solicitar la elevación de permisos.");
  }
}

void App::StartCustom(const std::wstring& label, const std::wstring& script, bool) {
  if (busy_ || !engineReady_) return;
  busy_ = true;
  busyLabel_ = label;
  SpawnTask([this, script]() {
    int exitCode = -1;
    const std::wstring output = engine_.RunPowerShell(script, 180000, &exitCode);
    TaskResult out;
    out.kind = TaskResult::Kind::Custom;
    out.ok = exitCode == 0;
    out.message = output.empty() ? (out.ok ? L"Acción completada." : L"La acción falló.")
                                 : output;
    return out;
  });
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::ToggleSetting(const std::wstring& key) {
  if (!engineReady_) return;
  if (key == L"confirmations") {
    confirmRisky_ = !confirmRisky_;
    engine_.SetSetting(L"confirmations", confirmRisky_);
    ShowToast(true, confirmRisky_ ? L"Confirmaciones activadas."
                                  : L"Confirmaciones desactivadas.");
  } else if (key == L"dashboard") {
    showDashboard_ = !showDashboard_;
    engine_.SetSetting(L"dashboard", showDashboard_);
    ShowToast(true, showDashboard_ ? L"Dashboard activado." : L"Dashboard oculto.");
  } else if (key == L"startup") {
    const bool target = !startWithWindows_;
    if (engine_.SetStartWithWindows(target)) {
      startWithWindows_ = target;
      ShowToast(true, target ? L"LimeForge se abrirá con Windows."
                             : L"LimeForge ya no se abrirá con Windows.");
    } else {
      ShowToast(false, L"No se pudo modificar el inicio automático.");
    }
  }
  scroll_ = 0.0f;
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::PushUndo(HistoryEntry entry) {
  if (entry.ops.empty()) return;
  undoStack_.push_back(std::move(entry));
  if (undoStack_.size() > 40) undoStack_.erase(undoStack_.begin());
  redoStack_.clear();
}

void App::PushRedo(HistoryEntry entry) {
  if (entry.ops.empty()) return;
  redoStack_.push_back(std::move(entry));
  if (redoStack_.size() > 40) redoStack_.erase(redoStack_.begin());
}

void App::StartHistory(const HistoryEntry& entry, bool undo) {
  if (busy_ || !engineReady_ || entry.ops.empty()) return;
  busy_ = true;
  busyLabel_ = (undo ? L"Deshaciendo: " : L"Rehaciendo: ") + entry.label;
  SpawnTask([this, entry, undo]() {
    int okCount = 0;
    int failCount = 0;
    std::wstring firstFailure;
    for (const HistoryOp& op : entry.ops) {
      const Tweak* tweak = FindTweak(op.id);
      if (!tweak) continue;
      EngineResult result = op.apply ? engine_.Apply(*tweak) : engine_.Revert(*tweak);
      if (result.ok) {
        okCount += 1;
      } else {
        failCount += 1;
        if (firstFailure.empty())
          firstFailure = tweak->name + L": " + FirstLineOf(result.message);
      }
    }
    TaskResult out;
    out.kind = TaskResult::Kind::History;
    out.ok = failCount == 0;
    out.message = (undo ? L"Deshecho: " : L"Rehecho: ") + entry.label + L" (" +
                  IntText(okCount) + L" cambios)";
    if (failCount > 0)
      out.message += L". " + IntText(failCount) + L" fallaron. " + firstFailure;
    else
      out.message += L".";
    return out;
  });
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::UndoLast() {
  if (busy_ || !engineReady_) return;
  if (undoStack_.empty()) {
    ShowToast(true, L"No hay cambios para deshacer.");
    return;
  }
  pendingHistory_ = undoStack_.back();
  undoStack_.pop_back();
  pendingHistoryUndo_ = true;
  StartHistory(*pendingHistory_, true);
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::RedoLast() {
  if (busy_ || !engineReady_) return;
  if (redoStack_.empty()) {
    ShowToast(true, L"No hay cambios para rehacer.");
    return;
  }
  pendingHistory_ = redoStack_.back();
  redoStack_.pop_back();
  pendingHistoryUndo_ = false;
  StartHistory(*pendingHistory_, false);
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::SpawnTask(std::function<TaskResult()> task) {
  if (worker_.joinable()) worker_.join();
  worker_ = std::thread([this, task = std::move(task)]() mutable {
    TaskResult result = task();
    PostMessageW(hwnd_, WM_APP_TASK, 0,
                 reinterpret_cast<LPARAM>(new TaskResult(std::move(result))));
  });
}

void App::OnTaskDone(void* payload) {
  std::unique_ptr<TaskResult> result(reinterpret_cast<TaskResult*>(payload));
  busy_ = false;
  busyLabel_.clear();
  if (worker_.joinable()) worker_.join();
  if (!result) {
    InvalidateRect(hwnd_, nullptr, FALSE);
    return;
  }

  if (result->kind == TaskResult::Kind::Startup) {
    startup_ = std::move(result->startup);
    startupLoaded_ = true;
    startupLoading_ = false;
    if (view_ == View::Startup) {
      // Nothing else to do; the list is rendered from startup_.
    }
  } else if (result->kind == TaskResult::Kind::Report) {
    report_ = result->message;
    reportLoaded_ = true;
    reportLoading_ = false;
    ShowToast(result->ok, result->ok ? L"Informe del sistema generado."
                                     : FirstLineOf(result->message));
  } else if (result->kind == TaskResult::Kind::Clean) {
    if (!result->message.empty()) lastCleanMessage_ = result->message;
    ShowToast(result->ok, FirstLineOf(result->message));
  } else if (result->kind == TaskResult::Kind::History) {
    ReloadApplied();
    if (pendingHistory_) {
      HistoryEntry inverse;
      inverse.label = pendingHistory_->label;
      for (const HistoryOp& op : pendingHistory_->ops)
        inverse.ops.push_back({op.id, !op.apply});
      if (result->ok) {
        if (pendingHistoryUndo_)
          PushRedo(std::move(inverse));
        else
          PushUndo(std::move(inverse));
      } else {
        if (pendingHistoryUndo_)
          undoStack_.push_back(*pendingHistory_);
        else
          redoStack_.push_back(*pendingHistory_);
      }
      pendingHistory_.reset();
    }
    ShowToast(result->ok, FirstLineOf(result->message));
  } else {
    ReloadApplied();
    if (result->kind == TaskResult::Kind::Pack && !result->changedIds.empty()) {
      HistoryEntry entry;
      entry.label = L"Pack recomendado (" +
                    IntText(static_cast<int>(result->changedIds.size())) + L" ajustes)";
      for (const std::wstring& id : result->changedIds)
        entry.ops.push_back({id, false});
      PushUndo(std::move(entry));
    } else if (result->kind == TaskResult::Kind::RestoreAll) {
      undoStack_.clear();
      redoStack_.clear();
      pendingHistory_.reset();
    } else if (result->ok && result->kind == TaskResult::Kind::Tweak &&
               result->historyCandidate) {
      const Tweak* tweak = FindTweak(result->id);
      if (tweak) {
        HistoryEntry entry;
        entry.label = tweak->name;
        entry.ops.push_back({result->id, !result->wasApply});
        PushUndo(std::move(entry));
      }
    }
    ShowToast(result->ok, FirstLineOf(result->message));
  }

  if (result->closeAfter) {
    closeRequested_ = true;
    PostMessageW(hwnd_, WM_CLOSE, 0, 0);
  }
  InvalidateRect(hwnd_, nullptr, FALSE);
}

bool App::ContainsCI(const std::wstring& haystack, const std::wstring& needle) {
  return Lower(haystack).find(Lower(needle)) != std::wstring::npos;
}

std::wstring App::Lower(std::wstring value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
  return value;
}

std::wstring App::Trim(const std::wstring& value) {
  size_t start = value.find_first_not_of(L" \t\r\n");
  if (start == std::wstring::npos) return {};
  size_t end = value.find_last_not_of(L" \t\r\n");
  return value.substr(start, end - start + 1);
}

std::wstring App::UriOpen() { return L""; }

void App::DrawModal() {
  if (modal_.kind == ModalKind::None) return;
  addingModal_ = true;

  const D2D1_RECT_F overlay = MakeRect(0.0f, 0.0f, width_, height_);
  DrawRoundRect(overlay, 0.0f, WithAlpha(theme::kBlack, 0.66f));
  AddHit(HitKind::Modal, overlay, L"outside");

  float panelWidth = 560.0f;
  float panelHeight = 360.0f;
  const Tweak* tweak = FindTweak(modal_.id);
  switch (modal_.kind) {
    case ModalKind::Confirm:
      panelWidth = 540.0f;
      panelHeight = 400.0f;
      break;
    case ModalKind::Detail:
      panelWidth = 690.0f;
      panelHeight = (tweak && !tweak->warning.empty()) ? 470.0f : 400.0f;
      break;
    case ModalKind::Elevate:
      panelWidth = 560.0f;
      panelHeight = 350.0f;
      break;
    case ModalKind::RestoreAll:
      panelWidth = 560.0f;
      panelHeight = 360.0f;
      break;
    case ModalKind::Pack:
      panelWidth = 660.0f;
      panelHeight = 490.0f;
      break;
    case ModalKind::Info:
      panelWidth = 590.0f;
      panelHeight = 400.0f;
      break;
    default:
      break;
  }
  panelWidth = std::min(panelWidth, width_ - 40.0f);
  panelHeight = std::min(panelHeight, height_ - 40.0f);
  const float x = (width_ - panelWidth) / 2.0f;
  const float y = (height_ - panelHeight) / 2.0f;
  const D2D1_RECT_F panel = MakeRect(x, y, panelWidth, panelHeight);

  DrawRoundRect(MakeRect(x - 6.0f, y + 8.0f, panelWidth + 12.0f, panelHeight + 14.0f), 24.0f,
                WithAlpha(theme::kBlack, 0.5f));
  DrawRoundRect(panel, 20.0f, theme::kSurface, true, theme::kBorder);
  DrawLine(panel.left + 20.0f, panel.top + 1.0f, panel.left + 74.0f, panel.top + 1.0f,
           theme::kLime, 2.0f);

  const wchar_t* headerGlyph = L"\uE946";
  D2D1_COLOR_F headerColor = theme::kLimeDim;
  if (modal_.kind == ModalKind::Detail && tweak) {
    headerGlyph = CategoryGlyph(tweak->category);
  } else if (modal_.kind == ModalKind::Elevate) {
    headerGlyph = L"\uE72E";
    headerColor = theme::kAmber;
  } else if (modal_.kind == ModalKind::RestoreAll) {
    headerGlyph = L"\uE72C";
    headerColor = theme::kRed;
  } else if (modal_.kind == ModalKind::Pack) {
    headerGlyph = L"\uE945";
  } else if (modal_.kind == ModalKind::Confirm) {
    headerGlyph = modal_.destructive ? L"\uE7BA" : L"\uE73E";
    headerColor = modal_.destructive ? theme::kRed : theme::kLime;
  }

  DrawRoundRect(MakeRect(x + 26.0f, y + 24.0f, 42.0f, 42.0f), 12.0f,
                WithAlpha(headerColor, 0.10f), true, WithAlpha(headerColor, 0.25f));
  DrawIcon(headerGlyph, MakeRect(x + 32.0f, y + 30.0f, 30.0f, 30.0f), 17.0f, headerColor);
  DrawTextBlock(modal_.title, MakeRect(x + 84.0f, y + 22.0f, panelWidth - 112.0f, 30.0f),
                19.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, theme::kText,
                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, 1, false);
  std::wstring sub;
  if (modal_.kind == ModalKind::Detail && tweak) {
    sub = tweak->category;
    if (!tweak->subcategory.empty()) sub += L"  ·  " + tweak->subcategory;
  } else if (modal_.kind == ModalKind::Elevate) {
    sub = L"CONTROL DE CUENTAS DE USUARIO";
  } else if (modal_.kind == ModalKind::Pack) {
    sub = L"APLICACIÓN POR LOTES";
  } else if (modal_.kind == ModalKind::RestoreAll) {
    sub = L"ACCIÓN DESTRUCTIVA";
  } else if (modal_.kind == ModalKind::Confirm) {
    sub = L"CONFIRMACIÓN";
  } else {
    sub = L"INFORMACIÓN";
  }
  DrawTextBlock(sub, MakeRect(x + 84.0f, y + 51.0f, panelWidth - 112.0f, 16.0f), 8.6f,
                DWRITE_FONT_WEIGHT_SEMI_BOLD,
                modal_.kind == ModalKind::RestoreAll ? theme::kRed : theme::kLimeDim,
                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, 1, false);
  AddHit(HitKind::Modal, panel, L"panel");

  auto hovered = [&](HitKind kind, const std::wstring& payload) {
    Hit hit;
    hit.kind = kind;
    hit.payload = payload;
    return hoverKey_ == HitKey(hit);
  };

  if (modal_.kind == ModalKind::Confirm) {
    if (tweak) {
      DrawTextBlock(tweak->name, MakeRect(x + 26.0f, y + 86.0f, panelWidth - 52.0f, 24.0f),
                    13.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, theme::kText,
                    DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, 1,
                    false);
    }
    const bool hasWarning = tweak && !tweak->warning.empty();
    const float bodyTop = y + 116.0f;
    const float bodyHeight = hasWarning ? panelHeight - 116.0f - 92.0f - 74.0f
                                        : panelHeight - 116.0f - 92.0f;
    DrawTextBlock(modal_.body, MakeRect(x + 26.0f, bodyTop, panelWidth - 52.0f, bodyHeight),
                  11.0f, DWRITE_FONT_WEIGHT_NORMAL, theme::kTextDim);
    if (hasWarning) {
      const D2D1_RECT_F warning =
          MakeRect(x + 26.0f, y + panelHeight - 166.0f, panelWidth - 52.0f, 64.0f);
      DrawRoundRect(warning, 10.0f, WithAlpha(theme::kAmber, 0.06f), true,
                    WithAlpha(theme::kAmber, 0.35f));
      DrawIcon(L"\uE7BA", MakeRect(warning.left + 10.0f, warning.top + 10.0f, 16.0f, 16.0f),
               10.0f, theme::kAmber);
      DrawTextBlock(tweak->warning,
                    MakeRect(warning.left + 32.0f, warning.top + 8.0f,
                             RW(warning) - 44.0f, RH(warning) - 16.0f),
                    9.4f, DWRITE_FONT_WEIGHT_NORMAL, theme::kAmber);
    }
  } else if (modal_.kind == ModalKind::Detail && tweak) {
    DrawTextBlock(tweak->description,
                  MakeRect(x + 26.0f, y + 108.0f, panelWidth - 52.0f, 96.0f), 11.2f,
                  DWRITE_FONT_WEIGHT_NORMAL, theme::kTextDim);
    float contentY = y + 212.0f;
    if (!tweak->warning.empty()) {
      const D2D1_RECT_F warning =
          MakeRect(x + 26.0f, contentY, panelWidth - 52.0f, 66.0f);
      DrawRoundRect(warning, 10.0f, WithAlpha(theme::kAmber, 0.06f), true,
                    WithAlpha(theme::kAmber, 0.35f));
      DrawIcon(L"\uE7BA", MakeRect(warning.left + 10.0f, warning.top + 11.0f, 16.0f, 16.0f),
               10.0f, theme::kAmber);
      DrawTextBlock(tweak->warning,
                    MakeRect(warning.left + 32.0f, warning.top + 9.0f,
                             RW(warning) - 44.0f, RH(warning) - 18.0f),
                    9.2f, DWRITE_FONT_WEIGHT_NORMAL, theme::kAmber);
      contentY += 80.0f;
    }

    float badgeX = x + 26.0f;
    if (tweak->needsAdmin) {
      DrawBadge(badgeX, contentY, L"Admin", L"\uE72E", theme::kAmber);
      badgeX += BadgeWidth(L"\uE72E", L"Admin") + 7.0f;
    }
    if (tweak->restart) {
      DrawBadge(badgeX, contentY, L"Requiere reinicio", L"\uE72C", theme::kBlue);
      badgeX += BadgeWidth(L"\uE72C", L"Requiere reinicio") + 7.0f;
    }
    if (tweak->risky) {
      DrawBadge(badgeX, contentY, L"Riesgoso", L"\uE7BA", theme::kRed);
      badgeX += BadgeWidth(L"\uE7BA", L"Riesgoso") + 7.0f;
    }
    if (tweak->actionOnly) {
      DrawBadge(badgeX, contentY, L"Acción única", L"\uE768", theme::kTextDim);
    }

    const bool active = applied_.count(tweak->id) != 0;
    const D2D1_RECT_F state =
        MakeRect(x + 26.0f, y + (tweak->warning.empty() ? 244.0f : 318.0f),
                 panelWidth - 52.0f, 60.0f);
    DrawRoundRect(state, 12.0f, theme::kSurface2, true, theme::kBorderSoft);
    DrawCircle(state.left + 18.0f, state.top + 30.0f, 4.0f,
               active ? theme::kLime : theme::kTextFaint, true);
    DrawTextBlock(active ? L"Ajuste activo" : (tweak->actionOnly ? L"Acción disponible"
                                                                : L"Ajuste inactivo"),
                  MakeRect(state.left + 32.0f, state.top + 10.0f, RW(state) - 200.0f, 20.0f),
                  11.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                  active ? theme::kLime : theme::kTextDim);
    DrawTextBlock(active ? L"Se puede revertir desde este panel."
                         : L"Se aplicará con copia de seguridad previa.",
                  MakeRect(state.left + 32.0f, state.top + 31.0f, RW(state) - 200.0f, 18.0f),
                  9.3f, DWRITE_FONT_WEIGHT_NORMAL, theme::kTextFaint);
    if (tweak->perfImpact > 0) {
      DrawTextBlock(L"Impacto " + IntText(tweak->perfImpact) + L"/5",
                    MakeRect(state.right - 150.0f, state.top, 130.0f, 60.0f), 9.5f,
                    DWRITE_FONT_WEIGHT_SEMI_BOLD, theme::kLimeDim,
                    DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
  } else if (modal_.kind == ModalKind::Elevate) {
    DrawTextBlock(modal_.body, MakeRect(x + 26.0f, y + 92.0f, panelWidth - 52.0f, 148.0f),
                  11.0f, DWRITE_FONT_WEIGHT_NORMAL, theme::kTextDim);
    const D2D1_RECT_F note =
        MakeRect(x + 26.0f, y + panelHeight - 148.0f, panelWidth - 52.0f, 58.0f);
    DrawRoundRect(note, 10.0f, WithAlpha(theme::kLime, 0.05f), true, theme::kBorderSoft);
    DrawIcon(L"\uE946", MakeRect(note.left + 12.0f, note.top + 19.0f, 18.0f, 18.0f), 11.0f,
             theme::kLimeDim);
    DrawTextBlock(L"La ventana actual se cerrará. La nueva instancia conserva el estado y "
                  L"las copias de seguridad.",
                  MakeRect(note.left + 38.0f, note.top + 10.0f, RW(note) - 50.0f,
                           RH(note) - 20.0f),
                  9.5f, DWRITE_FONT_WEIGHT_NORMAL, theme::kTextDim);
  } else if (modal_.kind == ModalKind::RestoreAll) {
    DrawTextBlock(modal_.body, MakeRect(x + 26.0f, y + 92.0f, panelWidth - 52.0f, 96.0f),
                  11.0f, DWRITE_FONT_WEIGHT_NORMAL, theme::kTextDim);
    const D2D1_RECT_F warning =
        MakeRect(x + 26.0f, y + panelHeight - 172.0f, panelWidth - 52.0f, 70.0f);
    DrawRoundRect(warning, 10.0f, WithAlpha(theme::kRed, 0.06f), true,
                  WithAlpha(theme::kRed, 0.35f));
    DrawIcon(L"\uE7BA", MakeRect(warning.left + 10.0f, warning.top + 12.0f, 16.0f, 16.0f),
             10.0f, theme::kRed);
    DrawTextBlock(L"Los ajustes que no tengan copia de seguridad se restauran con sus valores "
                  L"predeterminados. Revisá el resultado antes de reiniciar.",
                  MakeRect(warning.left + 32.0f, warning.top + 9.0f, RW(warning) - 44.0f,
                           RH(warning) - 18.0f),
                  9.3f, DWRITE_FONT_WEIGHT_NORMAL, theme::kRed);
  } else if (modal_.kind == ModalKind::Pack) {
    DrawTextBlock(modal_.body, MakeRect(x + 26.0f, y + 86.0f, panelWidth - 52.0f, 54.0f),
                  10.6f, DWRITE_FONT_WEIGHT_NORMAL, theme::kTextDim);
    std::vector<const Tweak*> recommended;
    for (const Tweak& candidate : tweaks_) {
      if (candidate.recommended && !candidate.actionOnly && !applied_.count(candidate.id))
        recommended.push_back(&candidate);
    }
    const float listTop = y + 146.0f;
    const float columnWidth = (panelWidth - 68.0f) / 2.0f;
    const int shown = std::min<int>(14, static_cast<int>(recommended.size()));
    for (int i = 0; i < shown; ++i) {
      const int column = i % 2;
      const int row = i / 2;
      const float itemX = x + 26.0f + column * columnWidth;
      const float itemY = listTop + row * 27.0f;
      DrawCircle(itemX + 6.0f, itemY + 8.0f, 6.0f, WithAlpha(theme::kLime, 0.14f), true);
      DrawIcon(L"\uE73E", MakeRect(itemX + 1.0f, itemY + 3.0f, 10.0f, 10.0f), 7.0f,
               theme::kLime);
      DrawTextBlock(recommended[i]->name,
                    MakeRect(itemX + 20.0f, itemY, columnWidth - 28.0f, 18.0f), 9.8f,
                    DWRITE_FONT_WEIGHT_NORMAL, theme::kTextDim,
                    DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, 1,
                    false);
    }
    if (static_cast<int>(recommended.size()) > shown) {
      DrawTextBlock(L"+" + IntText(static_cast<int>(recommended.size()) - shown) +
                        L" ajustes más en este pack",
                    MakeRect(x + 26.0f, y + panelHeight - 126.0f, panelWidth - 52.0f, 18.0f),
                    9.2f, DWRITE_FONT_WEIGHT_NORMAL, theme::kTextFaint);
    }
  } else if (modal_.kind == ModalKind::Info) {
    DrawTextBlock(modal_.body, MakeRect(x + 26.0f, y + 92.0f, panelWidth - 52.0f,
                                        panelHeight - 92.0f - 100.0f),
                  11.0f, DWRITE_FONT_WEIGHT_NORMAL, theme::kTextDim);
  }

  // Footer buttons
  const float footerY = y + panelHeight - 66.0f;
  const float primaryWidth =
      std::max(140.0f, 34.0f + static_cast<float>(modal_.primaryLabel.size()) * 6.8f);
  const D2D1_RECT_F primary =
      MakeRect(x + panelWidth - 26.0f - primaryWidth, footerY, primaryWidth, 42.0f);
  const bool primaryHover = hovered(HitKind::Button, L"modal:primary");
  if (modal_.destructive) {
    DrawRoundRect(primary, 11.0f, primaryHover ? WithAlpha(theme::kRed, 0.86f) : theme::kRed);
    DrawTextBlock(modal_.primaryLabel,
                  MakeRect(primary.left + 4.0f, primary.top, RW(primary) - 8.0f, 42.0f),
                  10.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, theme::kBlack,
                  DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, 1, false);
  } else {
    DrawButton(primary, modal_.primaryLabel, true, primaryHover);
  }
  AddHit(HitKind::Button, primary, L"modal:primary");

  if (!modal_.secondaryLabel.empty()) {
    const float secondaryWidth = std::max(
        100.0f, 30.0f + static_cast<float>(modal_.secondaryLabel.size()) * 6.4f);
    const D2D1_RECT_F secondary =
        MakeRect(primary.left - 10.0f - secondaryWidth, footerY, secondaryWidth, 42.0f);
    DrawButton(secondary, modal_.secondaryLabel, false,
               hovered(HitKind::Button, L"modal:secondary"));
    AddHit(HitKind::Button, secondary, L"modal:secondary");
  }

  addingModal_ = false;
}

void App::DrawBusyOverlay() {
  const D2D1_RECT_F overlay = MakeRect(0.0f, 0.0f, width_, height_);
  DrawRoundRect(overlay, 0.0f, WithAlpha(theme::kBlack, 0.58f));

  const float cardWidth = 392.0f;
  const float cardHeight = 112.0f;
  const D2D1_RECT_F card =
      MakeRect((width_ - cardWidth) / 2.0f, (height_ - cardHeight) / 2.0f, cardWidth,
               cardHeight);
  DrawRoundRect(MakeRect(card.left - 4.0f, card.top + 6.0f, cardWidth + 8.0f,
                         cardHeight + 8.0f),
                18.0f, WithAlpha(theme::kBlack, 0.5f));
  DrawRoundRect(card, 16.0f, theme::kSurface, true, WithAlpha(theme::kLime, 0.35f));

  const float centerX = card.left + 58.0f;
  const float centerY = card.top + 56.0f;
  const int dots = 8;
  for (int i = 0; i < dots; ++i) {
    const float angle = static_cast<float>(tick_) * 0.16f + i * (2.0f * kPi / dots);
    const int phase = (i + static_cast<int>(tick_ / 2)) % dots;
    const float alpha = 0.16f + 0.84f * (static_cast<float>(phase) / dots);
    DrawCircle(centerX + std::cos(angle) * 24.0f, centerY + std::sin(angle) * 24.0f, 3.2f,
               WithAlpha(theme::kLime, alpha), true);
  }
  DrawIcon(L"\uE945", MakeRect(centerX - 9.0f, centerY - 9.0f, 18.0f, 18.0f), 11.0f,
           WithAlpha(theme::kLime, 0.6f));

  DrawTextBlock(busyLabel_, MakeRect(card.left + 104.0f, card.top + 24.0f,
                                     RW(card) - 128.0f, 38.0f),
                11.2f, DWRITE_FONT_WEIGHT_SEMI_BOLD, theme::kText);
  DrawTextBlock(L"No cierres LimeForge. Esto puede tardar unos minutos.",
                MakeRect(card.left + 104.0f, card.top + 64.0f, RW(card) - 128.0f, 22.0f),
                9.2f, DWRITE_FONT_WEIGHT_NORMAL, theme::kTextFaint);
}

void App::DrawToast() {
  if (toast_.empty() || GetTickCount64() >= toastUntil_) return;
  const float toastWidth = std::min(440.0f, width_ - 40.0f);
  const float toastHeight = 58.0f;
  const D2D1_RECT_F card =
      MakeRect(width_ - 20.0f - toastWidth, height_ - statusHeight_ - 18.0f - toastHeight,
               toastWidth, toastHeight);
  DrawRoundRect(MakeRect(card.left - 3.0f, card.top + 4.0f, toastWidth + 6.0f,
                         toastHeight + 6.0f),
                14.0f, WithAlpha(theme::kBlack, 0.45f));
  DrawRoundRect(card, 12.0f, theme::kSurface3, true,
                toastOk_ ? WithAlpha(theme::kLime, 0.42f) : WithAlpha(theme::kRed, 0.5f));
  DrawRoundRect(MakeRect(card.left, card.top + 10.0f, 3.0f, toastHeight - 20.0f), 1.5f,
                toastOk_ ? theme::kLime : theme::kRed);
  DrawRoundRect(MakeRect(card.left + 18.0f, card.top + 18.0f, 22.0f, 22.0f), 11.0f,
                WithAlpha(toastOk_ ? theme::kLime : theme::kRed, 0.12f));
  DrawIcon(toastOk_ ? L"\uE73E" : L"\uE7BA",
           MakeRect(card.left + 22.0f, card.top + 22.0f, 14.0f, 14.0f), 9.0f,
           toastOk_ ? theme::kLime : theme::kRed);
  DrawTextBlock(toast_,
                MakeRect(card.left + 50.0f, card.top + 7.0f, RW(card) - 66.0f,
                         RH(card) - 14.0f),
                9.8f, DWRITE_FONT_WEIGHT_NORMAL, theme::kText, DWRITE_TEXT_ALIGNMENT_LEADING,
                DWRITE_PARAGRAPH_ALIGNMENT_CENTER, 2);
}

}  // namespace lf
