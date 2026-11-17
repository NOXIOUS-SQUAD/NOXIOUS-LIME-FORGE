#include "app.h"

#include <d2d1.h>
#include <dwrite.h>
#include <dwmapi.h>
#include <windowsx.h>
#include <wrl/client.h>

#include <algorithm>
#include <string>

using lf::App;
using lf::WM_APP_TASK;

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

using Microsoft::WRL::ComPtr;

namespace {

constexpr wchar_t kClassName[] = L"LimeForgeMainWindow";
constexpr wchar_t kWindowTitle[] = L"LimeForge — Optimizador para Windows";
constexpr UINT_PTR kTimerId = 1;

App* g_app = nullptr;
bool g_trackingLeave = false;

float DpiScale(HWND hwnd) {
  UINT dpi = GetDpiForWindow(hwnd);
  if (dpi == 0) dpi = 96;
  return static_cast<float>(dpi) / 96.0f;
}

LRESULT NcHitTest(HWND hwnd, LPARAM lParam) {
  POINT screenPoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
  RECT windowRect{};
  GetWindowRect(hwnd, &windowRect);
  const int windowWidth = windowRect.right - windowRect.left;
  const int windowHeight = windowRect.bottom - windowRect.top;
  const bool maximized = IsZoomed(hwnd) != FALSE;
  const float scale = DpiScale(hwnd);
  const int border = static_cast<int>(7.0f * scale);

  if (!maximized) {
    const int x = screenPoint.x - windowRect.left;
    const int y = screenPoint.y - windowRect.top;
    const bool left = x < border;
    const bool right = x >= windowWidth - border;
    const bool top = y < border;
    const bool bottom = y >= windowHeight - border;
    if (top && left) return HTTOPLEFT;
    if (top && right) return HTTOPRIGHT;
    if (bottom && left) return HTBOTTOMLEFT;
    if (bottom && right) return HTBOTTOMRIGHT;
    if (left) return HTLEFT;
    if (right) return HTRIGHT;
    if (top) return HTTOP;
    if (bottom) return HTBOTTOM;
  }

  POINT clientPoint = screenPoint;
  ScreenToClient(hwnd, &clientPoint);
  RECT client{};
  GetClientRect(hwnd, &client);
  const int clientWidth = client.right - client.left;
  const int titleHeight = static_cast<int>(58.0f * scale);
  const int controlsWidth = static_cast<int>(138.0f * scale);
  if (clientPoint.y >= 0 && clientPoint.y < titleHeight && clientPoint.x >= 0 &&
      clientPoint.x < clientWidth) {
    if (clientPoint.x >= clientWidth - controlsWidth) return HTCLIENT;
    return HTCAPTION;
  }
  return HTCLIENT;
}

void ApplyWindowAttributes(HWND hwnd) {
  const BOOL dark = TRUE;
  DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));  // DWMWA_USE_IMMERSIVE_DARK_MODE
  const DWORD corner = 2;                                // DWMWCP_ROUND
  DwmSetWindowAttribute(hwnd, 33, &corner, sizeof(corner));  // DWMWA_WINDOW_CORNER_PREFERENCE
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
  App* app = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

  switch (message) {
    case WM_NCCALCSIZE:
      if (wParam == TRUE) {
        if (IsZoomed(hwnd)) {
          auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
          MONITORINFO monitor{};
          monitor.cbSize = sizeof(monitor);
          if (GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor)) {
            params->rgrc[0] = monitor.rcWork;
          }
        }
        return 0;
      }
      break;

    case WM_NCHITTEST:
      return NcHitTest(hwnd, lParam);

    case WM_NCACTIVATE:
      return TRUE;

    case WM_NCPAINT:
      return 0;

    case WM_GETMINMAXINFO: {
      auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
      MONITORINFO monitor{};
      monitor.cbSize = sizeof(monitor);
      GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor);
      const float scale = DpiScale(hwnd);
      const int workWidth = monitor.rcWork.right - monitor.rcWork.left;
      const int workHeight = monitor.rcWork.bottom - monitor.rcWork.top;
      info->ptMinTrackSize.x =
          std::min(static_cast<int>(1024.0f * scale), workWidth);
      info->ptMinTrackSize.y =
          std::min(static_cast<int>(700.0f * scale), workHeight);
      return 0;
    }

    case WM_PAINT: {
      PAINTSTRUCT paint{};
      BeginPaint(hwnd, &paint);
      if (app) app->Paint();
      EndPaint(hwnd, &paint);
      return 0;
    }

    case WM_ERASEBKGND:
      return 1;

    case WM_SIZE:
      if (app && wParam != SIZE_MINIMIZED) {
        app->Resize(LOWORD(lParam), HIWORD(lParam), DpiScale(hwnd));
      }
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;

    case WM_DPICHANGED: {
      const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
      SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                   suggested->right - suggested->left, suggested->bottom - suggested->top,
                   SWP_NOZORDER | SWP_NOACTIVATE);
      if (app) app->OnDpiChanged(DpiScale(hwnd));
      return 0;
    }

    case WM_MOUSEMOVE:
      if (!g_trackingLeave) {
        TRACKMOUSEEVENT track{};
        track.cbSize = sizeof(track);
        track.dwFlags = TME_LEAVE;
        track.hwndTrack = hwnd;
        TrackMouseEvent(&track);
        g_trackingLeave = true;
      }
      if (app) app->OnMouseMove(static_cast<float>(GET_X_LPARAM(lParam)),
                                static_cast<float>(GET_Y_LPARAM(lParam)));
      return 0;

    case WM_MOUSELEAVE:
      g_trackingLeave = false;
      if (app) app->OnMouseLeave();
      return 0;

    case WM_LBUTTONDOWN:
      SetFocus(hwnd);
      SetCapture(hwnd);
      if (app) app->OnLButtonDown(static_cast<float>(GET_X_LPARAM(lParam)),
                                  static_cast<float>(GET_Y_LPARAM(lParam)));
      return 0;

    case WM_LBUTTONUP:
      if (app) app->OnLButtonUp(static_cast<float>(GET_X_LPARAM(lParam)),
                                static_cast<float>(GET_Y_LPARAM(lParam)));
      ReleaseCapture();
      return 0;

    case WM_MOUSEWHEEL:
      if (app) app->OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam));
      return 0;

    case WM_KEYDOWN:
      if (app)
        app->OnKeyDown(wParam, (GetKeyState(VK_CONTROL) & 0x8000) != 0);
      return 0;

    case WM_CHAR:
      if (app) app->OnChar(static_cast<wchar_t>(wParam));
      return 0;

    case WM_TIMER:
      if (wParam == kTimerId && app) app->OnTimer();
      return 0;

    case WM_APP_TASK:
      if (app) app->OnTaskDone(reinterpret_cast<void*>(lParam));
      return 0;

    case WM_SETCURSOR:
      if (LOWORD(lParam) == HTCLIENT && app) return TRUE;
      break;

    case WM_CLOSE:
      if (app && !app->CanClose()) {
        app->RequestCloseBlocked();
        return 0;
      }
      DestroyWindow(hwnd);
      return 0;

    case WM_DESTROY:
      KillTimer(hwnd, kTimerId);
      PostQuitMessage(0);
      return 0;

    default:
      break;
  }
  return DefWindowProcW(hwnd, message, wParam, lParam);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  const HRESULT comResult =
      CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  if (FAILED(comResult)) {
    MessageBoxW(nullptr, L"No se pudo inicializar COM.", L"LimeForge",
                MB_OK | MB_ICONERROR);
    return 1;
  }

  ComPtr<ID2D1Factory> d2dFactory;
  if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                               d2dFactory.GetAddressOf()))) {
    MessageBoxW(nullptr, L"No se pudo inicializar Direct2D.", L"LimeForge",
                MB_OK | MB_ICONERROR);
    CoUninitialize();
    return 1;
  }

  ComPtr<IDWriteFactory> dwriteFactory;
  if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                 reinterpret_cast<IUnknown**>(
                                     dwriteFactory.GetAddressOf()))) ||
      !dwriteFactory) {
    MessageBoxW(nullptr, L"No se pudo inicializar DirectWrite.", L"LimeForge",
                MB_OK | MB_ICONERROR);
    CoUninitialize();
    return 1;
  }

  WNDCLASSEXW windowClass{};
  windowClass.cbSize = sizeof(windowClass);
  windowClass.style = CS_HREDRAW | CS_VREDRAW;
  windowClass.lpfnWndProc = WndProc;
  windowClass.hInstance = instance;
  windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  windowClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
  windowClass.hIconSm = windowClass.hIcon;
  windowClass.hbrBackground = nullptr;
  windowClass.lpszClassName = kClassName;
  if (windowClass.hIcon == nullptr) windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  if (windowClass.hIconSm == nullptr) windowClass.hIconSm = windowClass.hIcon;
  if (!RegisterClassExW(&windowClass)) {
    MessageBoxW(nullptr, L"No se pudo registrar la ventana.", L"LimeForge",
                MB_OK | MB_ICONERROR);
    CoUninitialize();
    return 1;
  }

  RECT workArea{};
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
  const UINT systemDpi = GetDpiForSystem();
  const float scale = static_cast<float>(systemDpi) / 96.0f;
  int windowWidth = static_cast<int>(1360.0f * scale);
  int windowHeight = static_cast<int>(860.0f * scale);
  const int workWidth = workArea.right - workArea.left;
  const int workHeight = workArea.bottom - workArea.top;
  windowWidth = std::min(windowWidth, workWidth - static_cast<int>(40.0f * scale));
  windowHeight = std::min(windowHeight, workHeight - static_cast<int>(40.0f * scale));
  windowWidth = std::max(windowWidth, static_cast<int>(900.0f * scale));
  windowHeight = std::max(windowHeight, static_cast<int>(640.0f * scale));
  const int windowX = workArea.left + (workWidth - windowWidth) / 2;
  const int windowY = workArea.top + (workHeight - windowHeight) / 2;

  HWND hwnd = CreateWindowExW(
      WS_EX_APPWINDOW, kClassName, kWindowTitle,
      WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU |
          WS_CLIPCHILDREN,
      windowX, windowY, windowWidth, windowHeight, nullptr, nullptr, instance, nullptr);
  if (!hwnd) {
    MessageBoxW(nullptr, L"No se pudo crear la ventana principal.", L"LimeForge",
                MB_OK | MB_ICONERROR);
    CoUninitialize();
    return 1;
  }

  ApplyWindowAttributes(hwnd);
  SendMessageW(hwnd, WM_SETICON, ICON_BIG,
               reinterpret_cast<LPARAM>(LoadIconW(instance, MAKEINTRESOURCEW(1))));
  SendMessageW(hwnd, WM_SETICON, ICON_SMALL,
               reinterpret_cast<LPARAM>(LoadIconW(instance, MAKEINTRESOURCEW(1))));

  App app;
  g_app = &app;
  SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&app));
  app.Initialize(hwnd, d2dFactory.Get(), dwriteFactory.Get(), DpiScale(hwnd));

  ShowWindow(hwnd, SW_SHOW);
  UpdateWindow(hwnd);
  SetTimer(hwnd, kTimerId, 120, nullptr);

  MSG message{};
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }

  KillTimer(hwnd, kTimerId);
  SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
  g_app = nullptr;
  app.Shutdown();
  CoUninitialize();
  return static_cast<int>(message.wParam);
}
