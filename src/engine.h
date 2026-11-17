#pragma once

#include "catalog.h"

#include <windows.h>

#include <atomic>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace lf {

struct EngineResult {
  bool ok = false;
  std::wstring message;
};

class Engine {
 public:
  struct StartupEntry {
    std::wstring name;
    std::wstring command;
    std::wstring source;
    bool enabled = true;
  };

  Engine();
  ~Engine();

  bool Initialize(std::wstring& error);
  void Shutdown();

  EngineResult Apply(const Tweak& tweak);
  EngineResult Revert(const Tweak& tweak);
  EngineResult RunAction(const Tweak& tweak);

  bool IsApplied(const std::wstring& id) const;
  void MarkApplied(const std::wstring& id, bool applied);
  bool DetectRegistryApplied(const Tweak& tweak) const;
  int AppliedCount() const;
  std::set<std::wstring> AppliedIds() const;

  static bool IsProcessElevated();
  bool RelaunchAsAdmin() const;

  std::wstring RunPowerShell(const std::wstring& script, int timeoutMs = 120000, int* exitCode = nullptr) const;
  bool OpenTarget(const std::wstring& target) const;

  void SetPriorityRule(const std::wstring& process, bool high);
  std::wstring PriorityProcessForId(const std::wstring& id) const;

  std::wstring Clean(const std::wstring& key);
  std::wstring SystemReport();
  std::vector<StartupEntry> StartupEntries();

  bool GetSetting(const std::wstring& key, bool fallback) const;
  void SetSetting(const std::wstring& key, bool value);
  bool IsStartWithWindows() const;
  bool SetStartWithWindows(bool enabled);

  std::wstring DataDir() const { return dataDir_; }
  std::wstring StatePath() const;
  std::wstring BackupPath() const;
  std::wstring SettingsPath() const;
  bool OpenDataFolder() const;

 private:
  struct BackupOp {
    int index = 0;
    HKEY root = HKEY_CURRENT_USER;
    std::wstring subkey;
    std::wstring value;
    DWORD type = REG_DWORD;
    bool exists = false;
    std::vector<BYTE> data;
  };

  void LoadState();
  void SaveState();
  void LoadBackups();
  void SaveBackups();
  void LoadSettings();
  void SaveSettings();
  void CaptureBackup(const std::wstring& id, int index, const RegOp& op);
  bool RestoreBackup(const std::wstring& id, const RegOp& op, int index, std::wstring& error);
  void RemoveBackups(const std::wstring& id);
  bool DetectOpApplied(const RegOp& op) const;

  void StartPriorityManager();
  void StopPriorityManager();
  void PriorityLoop();

  std::wstring dataDir_;
  mutable std::mutex mutex_;
  std::set<std::wstring> applied_;
  std::map<std::wstring, std::vector<BackupOp>> backups_;
  std::map<std::wstring, bool> settings_;

  std::thread priorityThread_;
  std::atomic<bool> priorityStop_{false};
  std::mutex rulesMutex_;
  std::map<std::wstring, bool> rules_;

  bool initialized_ = false;
};

}  // namespace lf
