#pragma once

#include <d2d1.h>

#include <cstdint>

namespace lf::theme {

inline D2D1_COLOR_F C(uint32_t rgb, float alpha = 1.0f) {
  return D2D1::ColorF(static_cast<float>((rgb >> 16) & 0xFF) / 255.0f,
                      static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
                      static_cast<float>(rgb & 0xFF) / 255.0f, alpha);
}

inline const D2D1_COLOR_F kTransparent = C(0x000000, 0.0f);
inline const D2D1_COLOR_F kBg = C(0x070A05);
inline const D2D1_COLOR_F kTitle = C(0x080C05);
inline const D2D1_COLOR_F kSidebar = C(0x0A0E07);
inline const D2D1_COLOR_F kSurface = C(0x10150B);
inline const D2D1_COLOR_F kSurface2 = C(0x0C1108);
inline const D2D1_COLOR_F kSurface3 = C(0x151C0E);
inline const D2D1_COLOR_F kBorder = C(0x232E18);
inline const D2D1_COLOR_F kBorderSoft = C(0x18210F);
inline const D2D1_COLOR_F kText = C(0xF2F8E8);
inline const D2D1_COLOR_F kTextDim = C(0xA2B095);
inline const D2D1_COLOR_F kTextFaint = C(0x6D7A62);
inline const D2D1_COLOR_F kLime = C(0xB8FF3E);
inline const D2D1_COLOR_F kLimeBright = C(0xD8FF82);
inline const D2D1_COLOR_F kLimeDim = C(0x83C728);
inline const D2D1_COLOR_F kLimeDark = C(0x1E2B0C);
inline const D2D1_COLOR_F kLimeGlow = C(0xB8FF3E, 0.13f);
inline const D2D1_COLOR_F kRed = C(0xFF6259);
inline const D2D1_COLOR_F kAmber = C(0xFFC857);
inline const D2D1_COLOR_F kBlue = C(0x74DFFF);
inline const D2D1_COLOR_F kBlack = C(0x050703);

}  // namespace lf::theme
