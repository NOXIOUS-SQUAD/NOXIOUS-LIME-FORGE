#include "engine.h"

#include <shlobj.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <knownfolders.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace lf {
namespace {

constexpr wchar_t kStateVersion[] = L"LF1";
constexpr wchar_t kSettingsVersion[] = L"LFCFG1";

std::wstring ToLower(std::wstring value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
  return value;
}

std::string WideToUtf8(const std::wstring& value) {
  if (value.empty()) return {};
  int needed = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                                   nullptr, 0, nullptr, nullptr);
  if (needed <= 0) return {};
  std::string out(static_cast<size_t>(needed), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                      out.data(), needed, nullptr, nullptr);
  return out;
}

std::wstring Utf8ToWide(const std::string& value) {
  if (value.empty()) return {};
  int needed = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                                   nullptr, 0);
  if (needed <= 0) return {};
  std::wstring out(static_cast<size_t>(needed), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                      out.data(), needed);
  return out;
}

std::string HexEncode(const std::vector<BYTE>& data) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  std::string out;
  out.reserve(data.size() * 2);
  for (BYTE b : data) {
    out.push_back(kHex[b >> 4]);
    out.push_back(kHex[b & 0x0F]);
  }
  return out;
}

std::vector<BYTE> HexDecode(const std::string& text) {
  std::vector<BYTE> out;
  if (text.size() % 2 != 0) return out;
  out.reserve(text.size() / 2);
  auto digit = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  for (size_t i = 0; i < text.size(); i += 2) {
    int hi = digit(text[i]);
    int lo = digit(text[i + 1]);
    if (hi < 0 || lo < 0) return {};
    out.push_back(static_cast<BYTE>((hi << 4) | lo));
  }
  return out;
}

std::string HexEncodeString(const std::wstring& text) {
  std::vector<BYTE> bytes;
  bytes.reserve(text.size() * 2);
  for (wchar_t c : text) {
    bytes.push_back(static_cast<BYTE>(c & 0xFF));
    bytes.push_back(static_cast<BYTE>((c >> 8) & 0xFF));
  }
  return HexEncode(bytes);
}

std::wstring HexDecodeString(const std::string& text) {
  std::vector<BYTE> bytes = HexDecode(text);
  std::wstring out;
  out.reserve(bytes.size() / 2);
  for (size_t i = 0; i + 1 < bytes.size(); i += 2) {
    wchar_t c = static_cast<wchar_t>(bytes[i] | (bytes[i + 1] << 8));
    out.push_back(c);
  }
  return out;
}

std::string Base64Encode(const std::vector<BYTE>& data) {
  static constexpr char kTable[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve(((data.size() + 2) / 3) * 4);
  size_t i = 0;
  while (i + 2 < data.size()) {
    uint32_t v = (static_cast<uint32_t>(data[i]) << 16) |
                 (static_cast<uint32_t>(data[i + 1]) << 8) |
                 static_cast<uint32_t>(data[i + 2]);
    out.push_back(kTable[(v >> 18) & 0x3F]);
    out.push_back(kTable[(v >> 12) & 0x3F]);
    out.push_back(kTable[(v >> 6) & 0x3F]);
    out.push_back(kTable[v & 0x3F]);
    i += 3;
  }
  if (i + 1 == data.size()) {
    uint32_t v = static_cast<uint32_t>(data[i]) << 16;
    out.push_back(kTable[(v >> 18) & 0x3F]);
    out.push_back(kTable[(v >> 12) & 0x3F]);
    out += "==";
  } else if (i + 2 == data.size()) {
    uint32_t v = (static_cast<uint32_t>(data[i]) << 16) |
                 (static_cast<uint32_t>(data[i + 1]) << 8);
    out.push_back(kTable[(v >> 18) & 0x3F]);
    out.push_back(kTable[(v >> 12) & 0x3F]);
    out.push_back(kTable[(v >> 6) & 0x3F]);
    out.push_back('=');
  }
  return out;
}

std::wstring WinErrorText(LONG code) {
  wchar_t* buffer = nullptr;
  DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                FORMAT_MESSAGE_IGNORE_INSERTS;
  DWORD length = FormatMessageW(flags, nullptr, static_cast<DWORD>(code), 0,
                                reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
  std::wstring text;
  if (length && buffer) {
    text.assign(buffer, length);
    while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' '))
      text.pop_back();
  }
  if (buffer) LocalFree(buffer);
  if (text.empty()) text = L"Código " + std::to_wstring(code);
  return text;
}

std::vector<BYTE> StringBytes(const std::wstring& value) {
  std::vector<BYTE> out;
  out.reserve((value.size() + 1) * sizeof(wchar_t));
  for (wchar_t c : value) {
    out.push_back(static_cast<BYTE>(c & 0xFF));
    out.push_back(static_cast<BYTE>((c >> 8) & 0xFF));
  }
  out.push_back(0);
  out.push_back(0);
  return out;
}

std::vector<BYTE> DwordBytes(const std::wstring& value) {
  wchar_t* end = nullptr;
  unsigned long long parsed = std::wcstoull(value.c_str(), &end, 10);
  DWORD dword = static_cast<DWORD>(parsed);
  std::vector<BYTE> out(4);
  std::memcpy(out.data(), &dword, 4);
  return out;
}

std::vector<BYTE> DataForValue(const RegOp& op, const std::wstring& value) {
  if (op.type == REG_DWORD) return DwordBytes(value);
  if (op.type == REG_BINARY) return HexDecode(WideToUtf8(value));
  return StringBytes(value);
}

bool ReadRegistryValue(HKEY root, const std::wstring& subkey, const std::wstring& value,
                       DWORD* type, std::vector<BYTE>* data, bool* exists) {
  *exists = false;
  data->clear();
  HKEY key = nullptr;
  LONG result = RegOpenKeyExW(root, subkey.c_str(), 0, KEY_READ | KEY_WOW64_64KEY, &key);
  if (result != ERROR_SUCCESS) {
    if (result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND) return true;
    return false;
  }
  DWORD valueType = 0;
  DWORD size = 0;
  result = RegQueryValueExW(key, value.c_str(), nullptr, &valueType, nullptr, &size);
  if (result == ERROR_FILE_NOT_FOUND) {
    RegCloseKey(key);
    return true;
  }
  if (result != ERROR_SUCCESS) {
    RegCloseKey(key);
    return false;
  }
  data->resize(size);
  result = RegQueryValueExW(key, value.c_str(), nullptr, &valueType, data->data(), &size);
  RegCloseKey(key);
  if (result != ERROR_SUCCESS) return false;
  data->resize(size);
  *type = valueType;
  *exists = true;
  return true;
}

bool WriteRegistryValue(HKEY root, const std::wstring& subkey, const std::wstring& value,
                        DWORD type, const std::vector<BYTE>& data) {
  HKEY key = nullptr;
  DWORD disposition = 0;
  LONG result = RegCreateKeyExW(root, subkey.c_str(), 0, nullptr, 0,
                                KEY_SET_VALUE | KEY_WOW64_64KEY, nullptr, &key, &disposition);
  if (result != ERROR_SUCCESS) return false;
  result = RegSetValueExW(key, value.c_str(), 0, type, data.data(),
                          static_cast<DWORD>(data.size()));
  RegCloseKey(key);
  return result == ERROR_SUCCESS;
}

bool DeleteRegistryValue(HKEY root, const std::wstring& subkey, const std::wstring& value) {
  if (value.empty()) return true;
  LONG result = RegDeleteKeyValueW(root, subkey.c_str(), value.c_str());
  return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND ||
         result == ERROR_PATH_NOT_FOUND;
}

std::wstring RootTag(HKEY root) {
  if (root == HKEY_LOCAL_MACHINE) return L"HKLM";
  if (root == HKEY_CLASSES_ROOT) return L"HKCR";
  if (root == HKEY_USERS) return L"HKU";
  return L"HKCU";
}

HKEY RootFromTag(const std::wstring& tag) {
  if (tag == L"HKLM") return HKEY_LOCAL_MACHINE;
  if (tag == L"HKCR") return HKEY_CLASSES_ROOT;
  if (tag == L"HKU") return HKEY_USERS;
  return HKEY_CURRENT_USER;
}

std::vector<std::wstring> SplitTabs(const std::string& line) {
  std::vector<std::wstring> parts;
  size_t start = 0;
  while (true) {
    size_t pos = line.find('\t', start);
    if (pos == std::string::npos) {
      parts.push_back(Utf8ToWide(line.substr(start)));
      break;
    }
    parts.push_back(Utf8ToWide(line.substr(start, pos - start)));
    start = pos + 1;
  }
  return parts;
}

bool ReadWholeFile(const std::wstring& path, std::string* out) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return false;
  std::ostringstream buffer;
  buffer << file.rdbuf();
  *out = buffer.str();
  return true;
}

void WriteWholeFile(const std::wstring& path, const std::string& content) {
  std::wstring temp = path + L".tmp";
  {
    std::ofstream file(temp, std::ios::binary | std::ios::trunc);
    if (!file) return;
    file.write(content.data(), static_cast<std::streamsize>(content.size()));
  }
  MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}

std::wstring FirstLine(const std::wstring& text) {
  size_t pos = text.find_first_of(L"\r\n");
  std::wstring line = pos == std::wstring::npos ? text : text.substr(0, pos);
  while (!line.empty() && (line.back() == L' ' || line.back() == L'\t')) line.pop_back();
  while (!line.empty() && (line.front() == L' ' || line.front() == L'\t')) line.erase(line.begin());
  return line;
}

void AddRunEntries(HKEY root, const wchar_t* subkey, const wchar_t* source,
                   std::vector<Engine::StartupEntry>* out) {
  HKEY key = nullptr;
  if (RegOpenKeyExW(root, subkey, 0, KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS) return;
  DWORD index = 0;
  wchar_t name[1024];
  BYTE data[8192];
  for (;;) {
    DWORD nameSize = static_cast<DWORD>(std::size(name));
    DWORD dataSize = static_cast<DWORD>(std::size(data));
    DWORD type = 0;
    LONG result = RegEnumValueW(key, index++, name, &nameSize, nullptr, &type, data, &dataSize);
    if (result == ERROR_NO_MORE_ITEMS) break;
    if (result != ERROR_SUCCESS) continue;
    if (nameSize == 0 || _wcsicmp(name, L"OneDriveSetup") == 0) continue;
    std::wstring command;
    if (type == REG_SZ || type == REG_EXPAND_SZ) {
      command.assign(reinterpret_cast<const wchar_t*>(data), dataSize / sizeof(wchar_t));
      while (!command.empty() && command.back() == L'\0') command.pop_back();
    }
    if (command.empty()) continue;
    Engine::StartupEntry entry;
    entry.name = name;
    entry.command = command;
    entry.source = source;
    out->push_back(std::move(entry));
  }
  RegCloseKey(key);
}

void AddStartupFolder(REFKNOWNFOLDERID folder, const wchar_t* source,
                      std::vector<Engine::StartupEntry>* out) {
  PWSTR path = nullptr;
  if (FAILED(SHGetKnownFolderPath(folder, 0, nullptr, &path))) return;
  std::wstring pattern = std::wstring(path) + L"\\*";
  CoTaskMemFree(path);
  WIN32_FIND_DATAW data{};
  HANDLE find = FindFirstFileW(pattern.c_str(), &data);
  if (find == INVALID_HANDLE_VALUE) return;
  do {
    if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
    std::wstring file = data.cFileName;
    if (file == L"desktop.ini") continue;
    std::wstring display = file;
    size_t dot = display.find_last_of(L'.');
    if (dot != std::wstring::npos) display = display.substr(0, dot);
    Engine::StartupEntry entry;
    entry.name = display;
    entry.command = file;
    entry.source = source;
    out->push_back(std::move(entry));
  } while (FindNextFileW(find, &data));
  FindClose(find);
}

}  // namespace

Engine::Engine() = default;

Engine::~Engine() { Shutdown(); }

bool Engine::Initialize(std::wstring& error) {
  PWSTR roaming = nullptr;
  HRESULT hr = SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming);
  if (FAILED(hr) || !roaming) {
    error = L"No se pudo obtener la carpeta de datos del usuario.";
    return false;
  }
  dataDir_ = std::wstring(roaming) + L"\\LimeForge";
  CoTaskMemFree(roaming);
  if (!CreateDirectoryW(dataDir_.c_str(), nullptr) &&
      GetLastError() != ERROR_ALREADY_EXISTS) {
    error = L"No se pudo crear la carpeta de datos: " + WinErrorText(GetLastError());
    return false;
  }
  LoadState();
  LoadBackups();
  LoadSettings();
  StartPriorityManager();
  initialized_ = true;
  return true;
}

void Engine::Shutdown() {
  StopPriorityManager();
  initialized_ = false;
}

void Engine::LoadState() {
  applied_.clear();
  std::string content;
  if (!ReadWholeFile(StatePath(), &content)) return;
  std::istringstream input(content);
  std::string line;
  std::getline(input, line);
  if (line.rfind("LF", 0) != 0) {
    input.clear();
    input.seekg(0);
  }
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::wstring id = Utf8ToWide(line.substr(0, eq));
    std::wstring value = Utf8ToWide(line.substr(eq + 1));
    if (!id.empty() && value == L"1") applied_.insert(id);
  }
}

void Engine::SaveState() {
  std::string out = WideToUtf8(kStateVersion) + "\n";
  for (const auto& id : applied_) out += WideToUtf8(id) + "=1\n";
  WriteWholeFile(StatePath(), out);
}

void Engine::LoadSettings() {
  settings_.clear();
  std::string content;
  if (!ReadWholeFile(SettingsPath(), &content)) return;
  std::istringstream input(content);
  std::string line;
  std::getline(input, line);
  if (line.rfind("LFCFG", 0) != 0) {
    input.clear();
    input.seekg(0);
  }
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    const size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    settings_[Utf8ToWide(line.substr(0, eq))] = line.substr(eq + 1) == "1";
  }
}

void Engine::SaveSettings() {
  std::string out = WideToUtf8(kSettingsVersion) + "\n";
  for (const auto& entry : settings_)
    out += WideToUtf8(entry.first) + "=" + (entry.second ? "1" : "0") + "\n";
  WriteWholeFile(SettingsPath(), out);
}

bool Engine::GetSetting(const std::wstring& key, bool fallback) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = settings_.find(key);
  return it == settings_.end() ? fallback : it->second;
}

void Engine::SetSetting(const std::wstring& key, bool value) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    settings_[key] = value;
  }
  SaveSettings();
}

bool Engine::IsStartWithWindows() const {
  wchar_t value[1024]{};
  DWORD size = sizeof(value);
  const LSTATUS result =
      RegGetValueW(HKEY_CURRENT_USER,
                   L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", L"LimeForge",
                   RRF_RT_REG_SZ, nullptr, value, &size);
  return result == ERROR_SUCCESS;
}

bool Engine::SetStartWithWindows(bool enabled) {
  const wchar_t* runKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
  if (!enabled) return DeleteRegistryValue(HKEY_CURRENT_USER, runKey, L"LimeForge");
  wchar_t exe[MAX_PATH]{};
  const DWORD length = GetModuleFileNameW(nullptr, exe, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) return false;
  const std::wstring command = L"\"" + std::wstring(exe, length) + L"\"";
  std::vector<BYTE> data((command.size() + 1) * sizeof(wchar_t), 0);
  std::memcpy(data.data(), command.c_str(), command.size() * sizeof(wchar_t));
  return WriteRegistryValue(HKEY_CURRENT_USER, runKey, L"LimeForge", REG_SZ, data);
}

void Engine::LoadBackups() {
  backups_.clear();
  std::string content;
  if (!ReadWholeFile(BackupPath(), &content)) return;
  std::istringstream input(content);
  std::string line;
  std::getline(input, line);  // version
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.rfind("B\t", 0) != 0) continue;
    std::vector<std::wstring> p = SplitTabs(line);
    if (p.size() < 9) continue;
    BackupOp op;
    op.root = RootFromTag(p[3]);
    op.subkey = HexDecodeString(WideToUtf8(p[4]));
    op.value = HexDecodeString(WideToUtf8(p[5]));
    op.type = static_cast<DWORD>(std::wcstoul(p[6].c_str(), nullptr, 10));
    op.exists = p[7] == L"1";
    op.data = HexDecode(WideToUtf8(p[8]));
    op.index = static_cast<int>(std::wcstol(p[2].c_str(), nullptr, 10));
    backups_[p[1]].push_back(std::move(op));
  }
}

void Engine::SaveBackups() {
  std::string out = "LFB1\n";
  for (const auto& [id, ops] : backups_) {
    for (const BackupOp& op : ops) {
      out += "B\t" + WideToUtf8(id) + "\t" + std::to_string(op.index) + "\t" +
             WideToUtf8(RootTag(op.root)) + "\t" + HexEncodeString(op.subkey) + "\t" +
             HexEncodeString(op.value) + "\t" + std::to_string(op.type) + "\t" +
             (op.exists ? "1" : "0") + "\t" + HexEncode(op.data) + "\n";
    }
  }
  WriteWholeFile(BackupPath(), out);
}

void Engine::CaptureBackup(const std::wstring& id, int index, const RegOp& op) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = backups_.find(id);
    if (it != backups_.end()) {
      for (const BackupOp& existing : it->second)
        if (existing.index == index) return;
    }
  }
  DWORD type = op.type;
  std::vector<BYTE> data;
  bool exists = false;
  ReadRegistryValue(op.root, op.subkey, op.value, &type, &data, &exists);
  BackupOp backup;
  backup.index = index;
  backup.root = op.root;
  backup.subkey = op.subkey;
  backup.value = op.value;
  backup.type = exists ? type : op.type;
  backup.exists = exists;
  backup.data = std::move(data);
  std::lock_guard<std::mutex> lock(mutex_);
  backups_[id].push_back(std::move(backup));
  SaveBackups();
}

bool Engine::RestoreBackup(const std::wstring& id, const RegOp& op, int index,
                           std::wstring& error) {
  BackupOp backup;
  bool found = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = backups_.find(id);
    if (it != backups_.end()) {
      for (const BackupOp& candidate : it->second) {
        if (candidate.index == index) {
          backup = candidate;
          found = true;
          break;
        }
      }
    }
  }
  if (found) {
    if (backup.exists) {
      if (!WriteRegistryValue(backup.root, backup.subkey, backup.value, backup.type,
                              backup.data)) {
        error = L"No se pudo restaurar el valor original de " + op.value + L".";
        return false;
      }
    } else {
      if (!DeleteRegistryValue(backup.root, backup.subkey, backup.value)) {
        error = L"No se pudo eliminar el valor agregado " + op.value + L".";
        return false;
      }
    }
    return true;
  }
  if (op.deleteOnRevert) return DeleteRegistryValue(op.root, op.subkey, op.value);
  return WriteRegistryValue(op.root, op.subkey, op.value, op.type,
                            DataForValue(op, op.revertData));
}

void Engine::RemoveBackups(const std::wstring& id) {
  std::lock_guard<std::mutex> lock(mutex_);
  backups_.erase(id);
  SaveBackups();
}

bool Engine::DetectOpApplied(const RegOp& op) const {
  DWORD type = 0;
  std::vector<BYTE> data;
  bool exists = false;
  if (!ReadRegistryValue(op.root, op.subkey, op.value, &type, &data, &exists)) return false;
  if (op.deleteOnApply) return !exists;
  if (!exists) return false;
  if (type != op.type) {
    // REG_EXPAND_SZ and REG_SZ are compatible for our purposes.
    const bool stringPair = (type == REG_SZ || type == REG_EXPAND_SZ) &&
                            (op.type == REG_SZ || op.type == REG_EXPAND_SZ);
    if (!stringPair) return false;
  }
  std::vector<BYTE> expected = DataForValue(op, op.applyData);
  return data == expected;
}

bool Engine::DetectRegistryApplied(const Tweak& tweak) const {
  if (tweak.reg.empty()) return false;
  for (const RegOp& op : tweak.reg)
    if (!DetectOpApplied(op)) return false;
  return true;
}

bool Engine::IsApplied(const std::wstring& id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return applied_.count(id) != 0;
}

void Engine::MarkApplied(const std::wstring& id, bool applied) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (applied)
      applied_.insert(id);
    else
      applied_.erase(id);
    SaveState();
  }
  if (!applied && (id.rfind(L"priority-", 0) == 0) && id.rfind(L"priority-normal", 0) != 0) {
    std::wstring process = PriorityProcessForId(id);
    if (!process.empty()) SetPriorityRule(process, false);
  }
}

int Engine::AppliedCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return static_cast<int>(applied_.size());
}

std::set<std::wstring> Engine::AppliedIds() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return applied_;
}

EngineResult Engine::Apply(const Tweak& tweak) {
  if (!tweak.special.empty()) {
    if (tweak.special.rfind(L"priority-normal:", 0) == 0) {
      std::wstring process = tweak.special.substr(16);
      SetPriorityRule(process, false);
      MarkApplied(tweak.id, false);
      return {true, L"Prioridad normal restaurada para " + process + L"."};
    }
    if (tweak.special.rfind(L"priority:", 0) == 0) {
      std::wstring process = tweak.special.substr(9);
      SetPriorityRule(process, true);
      MarkApplied(tweak.id, true);
      return {true, L"Prioridad alta activada para " + process + L" mientras esté abierto."};
    }
  }

  if (tweak.actionOnly) return RunAction(tweak);

  for (size_t i = 0; i < tweak.reg.size(); ++i)
    CaptureBackup(tweak.id, static_cast<int>(i), tweak.reg[i]);

  for (size_t i = 0; i < tweak.reg.size(); ++i) {
    const RegOp& op = tweak.reg[i];
    bool ok = op.deleteOnApply
                  ? DeleteRegistryValue(op.root, op.subkey, op.value)
                  : WriteRegistryValue(op.root, op.subkey, op.value, op.type,
                                       DataForValue(op, op.applyData));
    if (!ok) {
      std::wstring message = L"No se pudo escribir " + op.value + L". " +
                             WinErrorText(GetLastError());
      if (GetLastError() == ERROR_ACCESS_DENIED)
        message += L" Ejecutá LimeForge como administrador.";
      // Roll back any values already changed.
      std::wstring ignored;
      for (size_t j = 0; j <= i; ++j) RestoreBackup(tweak.id, tweak.reg[j], static_cast<int>(j), ignored);
      RemoveBackups(tweak.id);
      return {false, message};
    }
  }

  std::wstring scriptOutput;
  if (!tweak.applyPs.empty()) {
    int exitCode = 0;
    scriptOutput = RunPowerShell(tweak.applyPs, 180000, &exitCode);
    if (exitCode != 0) {
      std::wstring ignored;
      for (size_t i = 0; i < tweak.reg.size(); ++i)
        RestoreBackup(tweak.id, tweak.reg[i], static_cast<int>(i), ignored);
      RemoveBackups(tweak.id);
      std::wstring detail = FirstLine(scriptOutput);
      if (detail.empty()) detail = L"PowerShell devolvió un error.";
      return {false, detail};
    }
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    applied_.insert(tweak.id);
    SaveState();
  }

  std::wstring message = FirstLine(scriptOutput);
  if (message.empty()) message = tweak.name + L" aplicado.";
  return {true, message};
}

EngineResult Engine::Revert(const Tweak& tweak) {
  if (!tweak.special.empty()) {
    if (tweak.special.rfind(L"priority:", 0) == 0) {
      std::wstring process = tweak.special.substr(9);
      SetPriorityRule(process, false);
    }
    MarkApplied(tweak.id, false);
    return {true, tweak.name + L" revertido."};
  }

  if (tweak.actionOnly) return {false, L"Esta acción no se puede revertir automáticamente."};

  std::wstring error;
  for (size_t i = 0; i < tweak.reg.size(); ++i) {
    if (!RestoreBackup(tweak.id, tweak.reg[i], static_cast<int>(i), error))
      return {false, error};
  }

  std::wstring scriptOutput;
  if (!tweak.revertPs.empty()) {
    int exitCode = 0;
    scriptOutput = RunPowerShell(tweak.revertPs, 180000, &exitCode);
    if (exitCode != 0) {
      std::wstring detail = FirstLine(scriptOutput);
      if (detail.empty()) detail = L"PowerShell devolvió un error al revertir.";
      return {false, detail};
    }
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    applied_.erase(tweak.id);
    backups_.erase(tweak.id);
    SaveState();
    SaveBackups();
  }

  std::wstring message = FirstLine(scriptOutput);
  if (message.empty()) message = tweak.name + L" revertido.";
  return {true, message};
}

EngineResult Engine::RunAction(const Tweak& tweak) {
  if (!tweak.special.empty()) {
    if (tweak.special.rfind(L"priority-normal:", 0) == 0) {
      std::wstring process = tweak.special.substr(16);
      SetPriorityRule(process, false);
      return {true, L"Prioridad normal restaurada para " + process + L"."};
    }
    if (tweak.special.rfind(L"priority:", 0) == 0) {
      std::wstring process = tweak.special.substr(9);
      SetPriorityRule(process, true);
      return {true, L"Prioridad alta activada para " + process + L"."};
    }
  }
  if (!tweak.openUrl.empty()) {
    if (OpenTarget(tweak.openUrl)) return {true, L"Se abrió " + tweak.name + L"."};
    return {false, L"No se pudo abrir " + tweak.openUrl + L"."};
  }
  if (!tweak.applyPs.empty()) {
    int exitCode = 0;
    std::wstring output = RunPowerShell(tweak.applyPs, 180000, &exitCode);
    std::wstring detail = FirstLine(output);
    if (exitCode != 0) {
      if (detail.empty()) detail = L"La acción devolvió un error.";
      return {false, detail};
    }
    if (detail.empty()) detail = tweak.name + L" ejecutado.";
    return {true, detail};
  }
  return {false, L"Esta entrada no tiene una acción disponible."};
}

bool Engine::IsProcessElevated() {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
  TOKEN_ELEVATION elevation{};
  DWORD size = 0;
  bool elevated = false;
  if (GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size))
    elevated = elevation.TokenIsElevated != 0;
  CloseHandle(token);
  return elevated;
}

bool Engine::RelaunchAsAdmin() const {
  wchar_t exe[MAX_PATH] = {};
  if (!GetModuleFileNameW(nullptr, exe, MAX_PATH)) return false;
  HINSTANCE result = ShellExecuteW(nullptr, L"runas", exe, L"--elevated", nullptr,
                                   SW_SHOWNORMAL);
  return reinterpret_cast<INT_PTR>(result) > 32;
}

std::wstring Engine::RunPowerShell(const std::wstring& script, int timeoutMs,
                                   int* exitCode) const {
  if (exitCode) *exitCode = -1;
  std::wstring full = L"$ProgressPreference='SilentlyContinue'; "
                      L"[Console]::OutputEncoding=[System.Text.Encoding]::UTF8; " + script;
  std::vector<BYTE> utf16;
  utf16.reserve((full.size() + 1) * 2);
  for (wchar_t c : full) {
    utf16.push_back(static_cast<BYTE>(c & 0xFF));
    utf16.push_back(static_cast<BYTE>((c >> 8) & 0xFF));
  }
  std::string encoded = Base64Encode(utf16);
  std::wstring command = L"powershell.exe -NoLogo -NoProfile -NonInteractive "
                         L"-ExecutionPolicy Bypass -EncodedCommand " +
                         std::wstring(encoded.begin(), encoded.end());

  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;
  HANDLE readPipe = nullptr;
  HANDLE writePipe = nullptr;
  if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) return L"No se pudo crear la tubería.";
  SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  si.hStdOutput = writePipe;
  si.hStdError = writePipe;
  si.hStdInput = nullptr;
  PROCESS_INFORMATION pi{};
  std::vector<wchar_t> mutableCommand(command.begin(), command.end());
  mutableCommand.push_back(L'\0');
  BOOL created = CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, TRUE,
                                CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, nullptr,
                                nullptr, &si, &pi);
  CloseHandle(writePipe);
  if (!created) {
    CloseHandle(readPipe);
    return L"No se pudo iniciar PowerShell: " + WinErrorText(GetLastError());
  }

  std::string raw;
  DWORD start = GetTickCount();
  bool timedOut = false;
  for (;;) {
    DWORD available = 0;
    if (PeekNamedPipe(readPipe, nullptr, 0, nullptr, &available, nullptr) && available > 0) {
      char buffer[8192];
      DWORD read = 0;
      DWORD toRead = available < sizeof(buffer) ? available : sizeof(buffer);
      if (ReadFile(readPipe, buffer, toRead, &read, nullptr) && read > 0)
        raw.append(buffer, read);
      continue;
    }
    DWORD wait = WaitForSingleObject(pi.hProcess, 50);
    if (wait == WAIT_OBJECT_0) {
      for (;;) {
        DWORD left = 0;
        if (!PeekNamedPipe(readPipe, nullptr, 0, nullptr, &left, nullptr) || left == 0) break;
        char buffer[8192];
        DWORD read = 0;
        DWORD toRead = left < sizeof(buffer) ? left : sizeof(buffer);
        if (!ReadFile(readPipe, buffer, toRead, &read, nullptr) || read == 0) break;
        raw.append(buffer, read);
      }
      break;
    }
    if (GetTickCount() - start > static_cast<DWORD>(timeoutMs)) {
      TerminateProcess(pi.hProcess, 1);
      WaitForSingleObject(pi.hProcess, 5000);
      timedOut = true;
      break;
    }
  }
  DWORD code = 0;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  CloseHandle(readPipe);
  if (exitCode) *exitCode = static_cast<int>(code);

  std::wstring text;
  if (!raw.empty()) {
    int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, raw.data(),
                                     static_cast<int>(raw.size()), nullptr, 0);
    UINT codepage = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    if (needed <= 0) {
      codepage = CP_OEMCP;
      flags = 0;
      needed = MultiByteToWideChar(codepage, flags, raw.data(), static_cast<int>(raw.size()),
                                   nullptr, 0);
    }
    if (needed > 0) {
      text.resize(static_cast<size_t>(needed));
      MultiByteToWideChar(codepage, flags, raw.data(), static_cast<int>(raw.size()),
                          text.data(), needed);
    }
  }
  while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' '))
    text.pop_back();
  if (timedOut) {
    if (!text.empty()) text += L"\n";
    text += L"La operación tardó demasiado y fue cancelada.";
  }
  return text;
}

bool Engine::OpenTarget(const std::wstring& target) const {
  if (target.empty()) return false;
  HINSTANCE result = ShellExecuteW(nullptr, L"open", target.c_str(), nullptr, nullptr,
                                   SW_SHOWNORMAL);
  return reinterpret_cast<INT_PTR>(result) > 32;
}

void Engine::SetPriorityRule(const std::wstring& process, bool high) {
  std::lock_guard<std::mutex> lock(rulesMutex_);
  std::wstring key = ToLower(process);
  if (high)
    rules_[key] = true;
  else
    rules_.erase(key);
}

std::wstring Engine::PriorityProcessForId(const std::wstring& id) const {
  if (id == L"priority-roblox") return L"RobloxPlayerBeta.exe";
  if (id == L"priority-fortnite") return L"FortniteClient-Win64-Shipping.exe";
  if (id == L"priority-cs2") return L"cs2.exe";
  if (id == L"priority-league") return L"League of Legends.exe";
  if (id == L"priority-minecraft") return L"javaw.exe";
  if (id == L"priority-normal-roblox") return L"RobloxPlayerBeta.exe";
  if (id == L"priority-normal-fortnite") return L"FortniteClient-Win64-Shipping.exe";
  if (id == L"priority-normal-cs2") return L"cs2.exe";
  if (id == L"priority-normal-league") return L"League of Legends.exe";
  if (id == L"priority-normal-minecraft") return L"javaw.exe";
  return {};
}

void Engine::StartPriorityManager() {
  if (priorityThread_.joinable()) return;
  priorityStop_ = false;
  priorityThread_ = std::thread([this]() { PriorityLoop(); });
}

void Engine::StopPriorityManager() {
  priorityStop_ = true;
  if (priorityThread_.joinable()) priorityThread_.join();
}

void Engine::PriorityLoop() {
  while (!priorityStop_) {
    std::map<std::wstring, bool> snapshot;
    {
      std::lock_guard<std::mutex> lock(rulesMutex_);
      snapshot = rules_;
    }
    if (!snapshot.empty()) {
      HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
      if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        if (Process32FirstW(snap, &entry)) {
          do {
            std::wstring name = ToLower(entry.szExeFile);
            auto it = snapshot.find(name);
            if (it == snapshot.end()) continue;
            HANDLE process = OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION,
                                         FALSE, entry.th32ProcessID);
            if (process) {
              SetPriorityClass(process, it->second ? HIGH_PRIORITY_CLASS : NORMAL_PRIORITY_CLASS);
              CloseHandle(process);
            }
          } while (Process32NextW(snap, &entry));
        }
        CloseHandle(snap);
      }
    }
    for (int i = 0; i < 15 && !priorityStop_; ++i)
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

std::wstring Engine::Clean(const std::wstring& key) {
  std::wstring script;
  if (key == L"temp") {
    script = LR"PS(
$ErrorActionPreference='SilentlyContinue'
$paths=@($env:TEMP, "$env:WINDIR\Temp")
$freed=0
foreach($p in $paths){
  if(Test-Path $p){
    $freed += (Get-ChildItem $p -Recurse -Force -ErrorAction SilentlyContinue | Measure-Object -Property Length -Sum).Sum
    Get-ChildItem $p -Force -ErrorAction SilentlyContinue | Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
  }
}
Write-Output ("Temporales liberados: {0:N1} MB" -f ($freed/1MB))
)PS";
  } else if (key == L"recycle") {
    script = LR"PS(
$ErrorActionPreference='SilentlyContinue'
Clear-RecycleBin -Force -ErrorAction SilentlyContinue
Write-Output 'Papelera vaciada.'
)PS";
  } else if (key == L"dns") {
    script = LR"PS(
$ErrorActionPreference='SilentlyContinue'
ipconfig /flushdns | Out-Null
Write-Output 'Caché DNS vaciada.'
)PS";
  } else if (key == L"browsers") {
    script = LR"PS(
$ErrorActionPreference='SilentlyContinue'
$roots=@(
  "$env:LOCALAPPDATA\Microsoft\Edge\User Data",
  "$env:LOCALAPPDATA\Google\Chrome\User Data",
  "$env:LOCALAPPDATA\BraveSoftware\Brave-Browser\User Data",
  "$env:APPDATA\Opera Software\Opera GX Stable"
)
$names=@('Cache','Code Cache','GPUCache','Service Worker\CacheStorage')
$n=0
foreach($root in $roots){
  if(Test-Path $root){
    foreach($d in @('Default','Profile 1','Profile 2')){
      foreach($sub in $names){
        $path=Join-Path $root (Join-Path $d $sub)
        if(Test-Path $path){ Remove-Item $path -Recurse -Force -ErrorAction SilentlyContinue; $n++ }
      }
    }
  }
}
Write-Output "Caché de navegadores limpiada ($n carpetas)."
)PS";
  } else if (key == L"wu") {
    script = LR"PS(
$ErrorActionPreference='SilentlyContinue'
Stop-Service wuauserv -Force -ErrorAction SilentlyContinue
Remove-Item "$env:WINDIR\SoftwareDistribution\Download\*" -Recurse -Force -ErrorAction SilentlyContinue
Start-Service wuauserv -ErrorAction SilentlyContinue
Write-Output 'Caché de Windows Update limpiada.'
)PS";
  } else if (key == L"prefetch") {
    script = LR"PS(
$ErrorActionPreference='SilentlyContinue'
Remove-Item "$env:WINDIR\Prefetch\*" -Recurse -Force -ErrorAction SilentlyContinue
Write-Output 'Prefetch limpiado.'
)PS";
  } else if (key == L"logs") {
    script = LR"PS(
$ErrorActionPreference='SilentlyContinue'
wevtutil el | ForEach-Object { wevtutil cl "$_" 2>$null | Out-Null }
Write-Output 'Registros de eventos vaciados.'
)PS";
  } else if (key == L"thumbs") {
    script = LR"PS(
$ErrorActionPreference='SilentlyContinue'
$freed=0
$roots=@("$env:LOCALAPPDATA\Microsoft\Windows\Explorer", "$env:LOCALAPPDATA\Microsoft\Windows\Explorer\ThumbCache")
foreach($root in $roots){
  if(Test-Path $root){
    Get-ChildItem $root -Filter 'thumbcache_*.db' -Force -ErrorAction SilentlyContinue | ForEach-Object {
      $freed += $_.Length
      Remove-Item $_.FullName -Force -ErrorAction SilentlyContinue
    }
  }
}
Write-Output ("Miniaturas liberadas: {0:N1} MB" -f ($freed/1MB))
)PS";
  } else if (key == L"all") {
    script = LR"PS(
$ErrorActionPreference='SilentlyContinue'
$freed=0
foreach($p in @($env:TEMP, "$env:WINDIR\Temp")){
  if(Test-Path $p){
    $freed += (Get-ChildItem $p -Recurse -Force -ErrorAction SilentlyContinue | Measure-Object -Property Length -Sum).Sum
    Get-ChildItem $p -Force -ErrorAction SilentlyContinue | Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
  }
}
Clear-RecycleBin -Force -ErrorAction SilentlyContinue
ipconfig /flushdns | Out-Null
Get-ChildItem "$env:LOCALAPPDATA\Microsoft\Windows\Explorer" -Filter 'thumbcache_*.db' -Force -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
$roots=@("$env:LOCALAPPDATA\Microsoft\Edge\User Data","$env:LOCALAPPDATA\Google\Chrome\User Data","$env:LOCALAPPDATA\BraveSoftware\Brave-Browser\User Data")
foreach($root in $roots){ if(Test-Path $root){ foreach($d in @('Default','Profile 1')){ Remove-Item (Join-Path $root (Join-Path $d 'Cache')) -Recurse -Force -ErrorAction SilentlyContinue } } }
Write-Output ("Limpieza completa: {0:N1} MB liberados (más papelera y caché)." -f ($freed/1MB))
)PS";
  } else {
    return L"Tipo de limpieza desconocido.";
  }
  return RunPowerShell(script, 180000);
}

std::wstring Engine::SystemReport() {
  const wchar_t* script = LR"PS(
$ErrorActionPreference='SilentlyContinue'
$os=Get-CimInstance Win32_OperatingSystem
$cs=Get-CimInstance Win32_ComputerSystem
$cpu=Get-CimInstance Win32_Processor | Select-Object -First 1
$gpu=Get-CimInstance Win32_VideoController | Where-Object { $_.Name -notmatch 'Basic Display|Remote' } | Select-Object -First 1
$bb=Get-CimInstance Win32_BaseBoard
$disk=Get-CimInstance Win32_DiskDrive | Select-Object -First 1
$ram=[math]::Round($cs.TotalPhysicalMemory/1GB,1)
"Equipo      : $($cs.Manufacturer) $($cs.Model)"
"Procesador  : $($cpu.Name.Trim())"
"Núcleos     : $($cpu.NumberOfCores) núcleos / $($cpu.NumberOfLogicalProcessors) hilos"
"Memoria RAM : $ram GB"
"Gráfica     : $($gpu.Name)"
"Placa base  : $($bb.Manufacturer) $($bb.Product)"
"Disco       : $($disk.Model) ($([math]::Round($disk.Size/1GB)) GB)"
"Sistema     : $($os.Caption) $($os.Version) build $($os.BuildNumber)"
"Arranque    : $($os.LastBootUpTime)"
"Energía     : $((powercfg /getactivescheme) -join '')"
)PS";
  return RunPowerShell(script, 120000);
}

std::vector<Engine::StartupEntry> Engine::StartupEntries() {
  std::vector<StartupEntry> out;
  AddRunEntries(HKEY_CURRENT_USER,
                L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", L"Usuario (Run)", &out);
  AddRunEntries(HKEY_LOCAL_MACHINE,
                L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", L"Sistema (Run)", &out);
  AddStartupFolder(FOLDERID_Startup, L"Inicio (usuario)", &out);
  AddStartupFolder(FOLDERID_CommonStartup, L"Inicio (todos)", &out);
  std::sort(out.begin(), out.end(), [](const StartupEntry& a, const StartupEntry& b) {
    if (a.source != b.source) return a.source < b.source;
    return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
  });
  return out;
}

std::wstring Engine::StatePath() const { return dataDir_ + L"\\state.ini"; }
std::wstring Engine::BackupPath() const { return dataDir_ + L"\\backups.ini"; }
std::wstring Engine::SettingsPath() const { return dataDir_ + L"\\settings.ini"; }

bool Engine::OpenDataFolder() const {
  return OpenTarget(dataDir_);
}

}  // namespace lf
