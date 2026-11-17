#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace lf {

struct RegOp {
  HKEY root = HKEY_CURRENT_USER;
  std::wstring subkey;
  std::wstring value;
  DWORD type = REG_DWORD;
  std::wstring applyData;
  std::wstring revertData;
  bool deleteOnApply = false;
  bool deleteOnRevert = false;
};

struct Tweak {
  std::wstring id;
  std::wstring category;
  std::wstring subcategory;
  std::wstring name;
  std::wstring description;
  std::wstring warning;
  std::wstring buttonLabel;
  std::wstring confirmMessage;

  std::vector<RegOp> reg;
  std::wstring applyPs;
  std::wstring revertPs;
  std::wstring openUrl;
  std::wstring special;

  bool actionOnly = false;
  bool premium = false;
  bool remoteOnly = false;
  bool laptopWarning = false;
  bool risky = false;
  bool needsAdmin = false;
  bool restart = false;
  bool recommended = false;
  int perfImpact = 0;

  bool HasRegistry() const { return !reg.empty(); }
  bool HasApplyScript() const { return !applyPs.empty(); }
  bool IsCommandOnly() const { return actionOnly; }
};

std::vector<Tweak> BuildCatalog();

}  // namespace lf
