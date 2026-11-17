// Generates src/catalog_data.cpp from the CeitoTweaks catalog metadata dump.
// The generated source contains original metadata (id, names, descriptions)
// and independent open implementations of the Windows operations.
const fs = require('fs');
const path = require('path');

const ROOT = 'C:/Users/anony/AppData/Local/Temp/opencode/ceito/catalog_dump.json';
const OUT = path.join(__dirname, '..', 'src', 'catalog_data.cpp');
const src = JSON.parse(fs.readFileSync(ROOT, 'utf8')).items;
const HKCU = 'HKEY_CURRENT_USER';
const HKLM = 'HKEY_LOCAL_MACHINE';

const R = (root, sub, name, type, apply, revert, delA = false, delR = false) => ({
  root, sub, name, type, apply: String(apply), revert: String(revert), delA, delR,
});

const RISKY = new Set([
  'remove-builtin-power-plans', 'network-force-1gbps-full-duplex', 'remove-bloatware-apps',
  'fps-remove-xbox-game-bar', 'net-stack-reset', 'fps-disable-hpet',
  'fps-disable-spectre-meltdown-mitigations', 'disable-vbs-core-isolation',
  'disable-cpu-mitigations', 'disable-wpbt', 'disable-reserved-storage',
  'fps-exclude-game-folders-defender', 'disable-fullscreen-optimizations',
  'disable-fault-tolerant-heap', 'disable-8dot3-names', 'disable-device-power-management',
  'fps-disable-hibernation', 'ceito-power-plan', 'ceito-power-plan-max',
]);

const RESTART = new Set([
  'enable-hags', 'fps-enable-hags', 'fps-disable-dynamic-tick', 'fps-disable-hpet',
  'disable-vbs-core-isolation', 'fps-disable-spectre-meltdown-mitigations',
  'disable-cpu-mitigations', 'disable-wpbt', 'classic-context-menu',
  'network-force-1gbps-full-duplex', 'fps-ultimate-performance-plan',
]);

const RECOMMENDED = new Set([
  'disable-notifications', 'disable-animations', 'disable-transparency', 'enable-game-mode',
  'disable-startup-delay', 'disable-background-apps', 'disable-tips-suggestions',
  'disable-telemetry', 'disable-advertising-id', 'disable-feedback-requests',
  'disable-delivery-optimization', 'disable-activity-history', 'taskbar-align-left',
  'force-system-dark-theme', 'show-file-extensions', 'disable-network-throttling',
  'disable-game-dvr', 'disable-search-indexing', 'disable-onedrive-startup',
  'disable-diagtrack', 'disable-error-reporting', 'fps-mmcss-system-responsiveness',
  'fps-win32-priority-separation', 'disable-sysmain', 'enable-long-paths',
  'disable-lock-screen-spotlight', 'disable-app-install-suggestions',
  'disable-start-recommendations', 'disable-news-and-interests',
]);

const OPS = {};

// ---------------------------------------------------------------- General
OPS['disable-notifications'] = { reg: [
  R(HKCU, 'Software\\Microsoft\\Windows\\CurrentVersion\\PushNotifications', 'ToastEnabled', 'DWORD', 0, 1),
  R(HKCU, 'Software\\Microsoft\\Windows\\CurrentVersion\\Notifications\\Settings', 'NOC_GLOBAL_SETTING_TOASTS_ENABLED', 'DWORD', 0, 1),
]};

OPS['disable-animations'] = { reg: [
  R(HKCU, 'Control Panel\\Desktop\\WindowMetrics', 'MinAnimate', 'SZ', '0', '1'),
  R(HKCU, 'Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced', 'TaskbarAnimations', 'DWORD', 0, 1),
  R(HKCU, 'Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced', 'ListviewAlphaSelect', 'DWORD', 0, 1),
  R(HKCU, 'Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced', 'ListviewShadow', 'DWORD', 0, 1),
  R(HKCU, 'Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\VisualEffects', 'VisualFXSetting', 'DWORD', 2, 0),
]};

OPS['disable-transparency'] = { reg: [
  R(HKCU, 'Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize', 'EnableTransparency', 'DWORD', 0, 1),
]};

OPS['enable-game-mode'] = { reg: [
  R(HKCU, 'Software\\Microsoft\\GameBar', 'AutoGameModeEnabled', 'DWORD', 1, 0),
  R(HKCU, 'Software\\Microsoft\\GameBar', 'AllowAutoGameMode', 'DWORD', 1, 0),
]};

OPS['disable-startup-delay'] = { reg: [
  R(HKCU, 'Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Serialize', 'StartupDelayInMSec', 'DWORD', 0, 1),
]};

OPS['disable-fast-startup'] = { reg: [
  R(HKLM, 'SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Power', 'HiberbootEnabled', 'DWORD', 0, 1),
]};

OPS['disable-cortana'] = { reg: [
  R(HKLM, 'SOFTWARE\\Policies\\Microsoft\\Windows\\Windows Search', 'AllowCortana', 'DWORD', 0, 1),
  R(HKCU, 'Software\\Microsoft\\Windows\\CurrentVersion\\Search', 'CortanaConsent', 'DWORD', 0, 1),
]};

OPS['disable-background-apps'] = { reg: [
  R(HKCU, 'Software\\Microsoft\\Windows\\CurrentVersion\\BackgroundAccessApplications', 'GlobalUserDisabled', 'DWORD', 1, 0),
  R(HKCU, 'Software\\Microsoft\\Windows\\CurrentVersion\\Search', 'BackgroundAppGlobalToggle', 'DWORD', 0, 1),
]};

OPS['disable-tips-suggestions'] = { reg: [
  R(HKCU, 'Software\\Microsoft\\Windows\\CurrentVersion\\ContentDeliveryManager', 'SubscribedContent-338389Enabled', 'DWORD', 0, 1),
  R(HKCU, 'Software\\Microsoft\\Windows\\CurrentVersion\\ContentDeliveryManager', 'SoftLandingEnabled', 'DWORD', 0, 1),
  R(HKCU, 'Software\\Microsoft\\Windows\\CurrentVersion\\ContentDeliveryManager', 'SystemPaneSuggestionsEnabled', 'DWORD', 0, 1),
  R(HKCU, 'Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced', 'ShowSyncProviderNotifications', 'DWORD', 0, 1),
]};

OPS['disable-telemetry'] = { reg: [
  R(HKLM, 'SOFTWARE\\Policies\\Microsoft\\Windows\\DataCollection', 'AllowTelemetry', 'DWORD', 0, 1),
  R(HKLM, 'SYSTEM\\CurrentControlSet\\Services\\DiagTrack', 'Start', 'DWORD', 4, 2),
], admin: true };

OPS['disable-cortana-copilot'] = { reg: [
  R(HKCU, 'Software\\Policies\\Microsoft\\Windows\\WindowsCopilot', 'TurnOffWindowsCopilot', 'DWORD', 1, 0),
  R(HKCU, 'Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced', 'ShowCopilotButton', 'DWORD', 0, 1),
], ps: `$ErrorActionPreference='SilentlyContinue'
New-Item -Path 'HKLM:\\SOFTWARE\\Policies\\Microsoft\\Windows\\Windows Search' -Force | Out-Null
Set-ItemProperty -Path 'HKLM:\\SOFTWARE\\Policies\\Microsoft\\Windows\\Windows Search' -Name AllowCortana -Value 0 -Type DWord
Write-Output 'Cortana y Copilot desactivados.'`, rev: `$ErrorActionPreference='SilentlyContinue'
Remove-ItemProperty -Path 'HKLM:\\SOFTWARE\\Policies\\Microsoft\\Windows\\Windows Search' -Name AllowCortana
Write-Output 'Cortana y Copilot restaurados.'`, admin: true };

OPS['disable-delivery-optimization'] = { reg: [
  R(HKLM, 'SOFTWARE\\Policies\\Microsoft\\Windows\\DeliveryOptimization', 'DODownloadMode', 'DWORD', 1, 3),
]};

OPS['disable-widgets'] = { url: 'ms-settings:taskbar' };

OPS['disable-handwriting-speech-improvement'] = { reg: [
  R(HKCU, 'Software\\Microsoft\\InputPersonalization', 'RestrictImplicitTextCollection', 'DWORD', 1, 0),
  R(HKCU, 'Software\\Microsoft\\InputPersonalization', 'RestrictImplicitInkCollection', 'DWORD', 1, 0),
  R(HKCU, 'Software\\Microsoft\\InputPersonalization\\TrainedDataStore', 'HarvestContacts', 'DWORD', 0, 1),
  R(HKCU, 'Software\\Microsoft\\Personalization\\Settings', 'AcceptedPrivacyPolicy', 'DWORD', 0, 1),
  R(HKCU, 'Software\\Microsoft\\Speech_OneCore\\Settings\\OnlineSpeechPrivacy', 'HasAccepted', 'DWORD', 0, 1),
]};

OPS['disable-usb-selective-suspend'] = { ps: `$ErrorActionPreference='SilentlyContinue'
$usb='2a737441-1930-4402-8d77-b2bebba308a3'; $sel='48e6b7a6-50f5-4782-a5d4-53bb8f07e226'
powercfg /setacvalueindex SCHEME_CURRENT $usb $sel 0
powercfg /setdcvalueindex SCHEME_CURRENT $usb $sel 0
powercfg /setactive SCHEME_CURRENT
Write-Output 'Suspensión selectiva de USB desactivada.'`, rev: `$ErrorActionPreference='SilentlyContinue'
$usb='2a737441-1930-4402-8d77-b2bebba308a3'; $sel='48e6b7a6-50f5-4782-a5d4-53bb8f07e226'
powercfg /setacvalueindex SCHEME_CURRENT $usb $sel 1
powercfg /setdcvalueindex SCHEME_CURRENT $usb $sel 1
powercfg /setactive SCHEME_CURRENT
Write-Output 'Suspensión selectiva de USB restaurada.'`, admin: true };

OPS['disable-activity-history'] = { reg: [
  R(HKLM, 'SOFTWARE\\Policies\\Microsoft\\Windows\\System', 'EnableActivityFeed', 'DWORD', 0, 1),
  R(HKLM, 'SOFTWARE\\Policies\\Microsoft\\Windows\\System', 'PublishUserActivities', 'DWORD', 0, 1),
  R(HKLM, 'SOFTWARE\\Policies\\Microsoft\\Windows\\System', 'UploadUserActivities', 'DWORD', 0, 1),
]};

OPS['disable-storage-sense'] = { reg: [
  R(HKCU, 'Software\\Microsoft\\Windows\\CurrentVersion\\StorageSense\\Parameters\\StoragePolicy', '01', 'DWORD', 0, 1),
]};

OPS['disable-location-tracking'] = { reg: [
  R(HKLM, 'SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\ConsentStore\\location', 'Value', 'SZ', 'Deny', 'Allow'),
  R(HKCU, 'Software\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\ConsentStore\\location', 'Value', 'SZ', 'Deny', 'Allow'),
  R(HKCU, 'Software\\Microsoft\\Windows\\CurrentVersion\\LocationAndSensors', 'DisableLocation', 'DWORD', 1, 0),
]};

OPS['prefer-ipv4-disable-teredo'] = { admin: true, reg: [
  R(HKLM, 'SYSTEM\\CurrentControlSet\\Services\\Tcpip6\\Parameters', 'DisabledComponents', 'DWORD', 32, 0),
], ps: `$ErrorActionPreference='SilentlyContinue'
netsh interface teredo set state disabled | Out-Null
Write-Output 'IPv4 preferido. Teredo desactivado.'`, rev: `$ErrorActionPreference='SilentlyContinue'
netsh interface teredo set state type=default | Out-Null
Write-Output 'Teredo restaurado.'` };

OPS['sync-clock'] = { admin: true, ps: `$ErrorActionPreference='SilentlyContinue'
Start-Service w32time
w32tm /resync /force | Out-Null
Write-Output 'Reloj sincronizado con el servidor de hora de Windows.'` };

OPS['taskbar-align-left'] = { reg: [
  R(HKCU, 'Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced', 'TaskbarAl', 'DWORD', 0, 1),
]};

OPS['force-system-dark-theme'] = { reg: [
  R(HKCU, 'Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize', 'AppsUseLightTheme', 'DWORD', 0, 1),
  R(HKCU, 'Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize', 'SystemUsesLightTheme', 'DWORD', 0, 1),
]};

OPS['disable-advertising-id'] = { reg: [
  R(HKCU, 'Software\\Microsoft\\Windows\\CurrentVersion\\AdvertisingInfo', 'Enabled', 'DWORD', 0, 1),
]};

OPS['disable-feedback-requests'] = { reg: [
  R(HKCU, 'Software\\Microsoft\\Siuf\\Rules', 'NumberOfSIUFInPeriod', 'DWORD', 0, 1, false, true),
  R(HKCU, 'Software\\Microsoft\\Siuf\\Rules', 'PeriodInNanoSeconds', 'DWORD', 0, 1, false, true),
]};

OPS['disable-startup-sound'] = { reg: [
  R(HKLM, 'SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Authentication\\LogonUI\\BootAnimation', 'DisableStartupSound', 'DWORD', 1, 0),
]};

OPS['show-file-extensions'] = { reg: [
  R(HKCU, 'Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced', 'HideFileExt', 'DWORD', 0, 1),
]};

OPS['install-rainmeter'] = { url: 'https://www.rainmeter.net/' };
OPS['rainmeter-skin-mond'] = { url: 'https://www.google.com/search?q=Mond+Rainmeter+skin+download' };

OPS['show-seconds-clock'] = { reg: [
  R(HKCU, 'Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced', 'ShowSecondsInSystemClock', 'DWORD', 1, 0),
]};

const ceitoPower = `$ErrorActionPreference='SilentlyContinue'
$src='8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c'
$out=powercfg /duplicatescheme $src
$m=[regex]::Match(($out -join ' '),'[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}')
if(-not $m.Success){ Write-Output 'No se pudo crear el plan de energía.'; exit 1 }
$g=$m.Value
powercfg /changename $g 'Ceito Rendimiento' | Out-Null
powercfg /setacvalueindex $g 54533251-82be-4824-96c1-47b60b740d00 893dee8e-2bef-41e0-89c6-b55d0929964c 100 | Out-Null
powercfg /setacvalueindex $g 54533251-82be-4824-96c1-47b60b740d00 bc5038f7-23e0-4960-96da-33abaf5935ec 100 | Out-Null
powercfg /setacvalueindex $g 54533251-82be-4824-96c1-47b60b740d00 5d76a2ca-e8c0-402f-a133-2158492d58ad 1 | Out-Null
powercfg /setacvalueindex $g 2a737441-1930-4402-8d77-b2bebba308a3 48e6b7a6-50f5-4782-a5d4-53bb8f07e226 0 | Out-Null
powercfg /setacvalueindex $g 19cbb8fa-5279-450e-9fac-8a3d5fedd0c1 12bbebe6-58d6-4636-95bb-3217ef867c1a 0 | Out-Null
powercfg /setacvalueindex $g 238c9fa8-0aad-41ed-83f4-97be242c8f20 29f6c1db-86da-48c5-9fdb-f2b67b1f44da 0 | Out-Null
powercfg /setactive $g | Out-Null
Write-Output 'Plan Ceito Rendimiento creado y activado.'`;

const ceitoPowerMax = `$ErrorActionPreference='SilentlyContinue'
$out=powercfg /duplicatescheme e9a42b02-d5df-448d-aa00-03f14749eb61
$m=[regex]::Match(($out -join ' '),'[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}')
if(-not $m.Success){ $out=powercfg /duplicatescheme 8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c; $m=[regex]::Match(($out -join ' '),'[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}') }
if(-not $m.Success){ Write-Output 'No se pudo crear el plan de energía.'; exit 1 }
$g=$m.Value
$cpu='54533251-82be-4824-96c1-47b60b740d00'
powercfg /changename $g 'Ceito Máximo Rendimiento' | Out-Null
powercfg /setacvalueindex $g $cpu 893dee8e-2bef-41e0-89c6-b55d0929964c 100 | Out-Null
powercfg /setacvalueindex $g $cpu bc5038f7-23e0-4960-96da-33abaf5935ec 100 | Out-Null
powercfg /setacvalueindex $g $cpu 5d76a2ca-e8c0-402f-a133-2158492d58ad 1 | Out-Null
powercfg /setacvalueindex $g $cpu 0cc5b647-c1df-4637-891a-dec35c318583 100 | Out-Null
powercfg /setacvalueindex $g $cpu 4b92d758-5a24-4851-a470-815d78aee119 100 | Out-Null
powercfg /setacvalueindex $g $cpu 7b224883-b3cc-4d79-819f-8374152cbe7c 100 | Out-Null
powercfg /setacvalueindex $g $cpu be337238-0d82-4146-a960-4f3749d470c7 2 | Out-Null
powercfg /setacvalueindex $g $cpu 45bcc044-d885-43e2-8605-ee0ec6e96b59 100 | Out-Null
powercfg /setacvalueindex $g 501a4d13-42af-4429-9fd1-a8218c268e20 ee12f906-d277-404b-b6da-e5fa1a576df5 0 | Out-Null
powercfg /setacvalueindex $g 0012ee47-9041-4b5d-9b77-535fba8b1442 6738e2c4-e8a5-4a42-b16a-e040e769756e 0 | Out-Null
powercfg /setacvalueindex $g 2a737441-1930-4402-8d77-b2bebba308a3 48e6b7a6-50f5-4782-a5d4-53bb8f07e226 0 | Out-Null
powercfg /setacvalueindex $g 19cbb8fa-5279-450e-9fac-8a3d5fedd0c1 12bbebe6-58d6-4636-95bb-3217ef867c1a 0 | Out-Null
powercfg /setacvalueindex $g 238c9fa8-0aad-41ed-83f4-97be242c8f20 29f6c1db-86da-48c5-9fdb-f2b67b1f44da 0 | Out-Null
powercfg /setacvalueindex $g 238c9fa8-0aad-41ed-83f4-97be242c8f20 9d7815a6-7ee4-497e-8888-515a05f02364 0 | Out-Null
powercfg /setactive $g | Out-Null
Write-Output 'Plan Ceito Máximo Rendimiento creado y activado.'`;

OPS['ceito-power-plan'] = { admin: true, ps: ceitoPower };
OPS['ceito-power-plan-max'] = { admin: true, ps: ceitoPowerMax };

OPS['remove-builtin-power-plans'] = { admin: true, risky: true, ps: `$ErrorActionPreference='SilentlyContinue'
$active=(powercfg /getactivescheme) -join ' '
$ag=[regex]::Match($active,'[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}').Value
$ceito=powercfg /list | Where-Object { $_ -match 'Ceito' } | Select-Object -First 1
if($ceito){ $cg=[regex]::Match($ceito,'[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}').Value; if($cg){ powercfg /setactive $cg | Out-Null } }
foreach($g in @('a1841308-3541-4fab-bc81-f71556f20b4a','381b4222-f694-41f0-9685-ff5bb260df2e','8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c')){ powercfg /delete $g | Out-Null }
Write-Output 'Planes predeterminados eliminados.'` };

// ---------------------------------------------------------------- Hardware
OPS['enable-hags'] = { reg: [
  R(HKLM, 'SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers', 'HwSchMode', 'DWORD', 2, 1),
]};

OPS['increase-gpu-timeout'] = { reg: [
  R(HKLM, 'SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers', 'TdrDelay', 'DWORD', 10, 2),
  R(HKLM, 'SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers', 'TdrDdiDelay', 'DWORD', 10, 5),
]};

OPS['gpu-priority-games'] = { reg: [
  R(HKLM, 'SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Games', 'GPU Priority', 'DWORD', 8, 8),
  R(HKLM, 'SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Games', 'Priority', 'DWORD', 6, 2),
  R(HKLM, 'SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Games', 'Scheduling Category', 'SZ', 'High', 'Medium'),
  R(HKLM, 'SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Games', 'SFIO Priority', 'SZ', 'High', 'Normal'),
]};

OPS['download-drivers-nvidia'] = { url: 'https://www.nvidia.com/Download/index.aspx' };
OPS['download-drivers-amd'] = { url: 'https://www.amd.com/en/support' };
OPS['download-drivers-intel'] = { url: 'https://www.intel.com/content/www/us/en/support/intel-driver-support-assistant.html' };

OPS['cpu-priority-games'] = { reg: [
  R(HKLM, 'SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Games', 'Priority', 'DWORD', 6, 2),
  R(HKLM, 'SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Games', 'Scheduling Category', 'SZ', 'High', 'Medium'),
]};

const proc = { roblox: 'RobloxPlayerBeta.exe', fortnite: 'FortniteClient-Win64-Shipping.exe', cs2: 'cs2.exe', league: 'League of Legends.exe', minecraft: 'javaw.exe' };
OPS['priority-roblox'] = { special: 'priority:' + proc.roblox };
OPS['priority-fortnite'] = { special: 'priority:' + proc.fortnite };
OPS['priority-cs2'] = { special: 'priority:' + proc.cs2 };
OPS['priority-league'] = { special: 'priority:' + proc.league };
OPS['priority-minecraft'] = { special: 'priority:' + proc.minecraft };
OPS['priority-normal-roblox'] = { special: 'priority-normal:' + proc.roblox };
OPS['priority-normal-fortnite'] = { special: 'priority-normal:' + proc.fortnite };
OPS['priority-normal-cs2'] = { special: 'priority-normal:' + proc.cs2 };
OPS['priority-normal-league'] = { special: 'priority-normal:' + proc.league };
OPS['priority-normal-minecraft'] = { special: 'priority-normal:' + proc.minecraft };

OPS['disable-usb-power-saving'] = { admin: true, ps: `$ErrorActionPreference='SilentlyContinue'
$cpu='54533251-82be-4824-96c1-47b60b740d00'; $park='0cc5b647-c1df-4637-891a-dec35c318583'
powercfg /setacvalueindex SCHEME_CURRENT $cpu $park 100
powercfg /setdcvalueindex SCHEME_CURRENT $cpu $park 100
powercfg /setactive SCHEME_CURRENT
Write-Output 'Core Parking desactivado: todos los núcleos disponibles.'`, rev: `$ErrorActionPreference='SilentlyContinue'
$cpu='54533251-82be-4824-96c1-47b60b740d00'; $park='0cc5b647-c1df-4637-891a-dec35c318583'
powercfg /setacvalueindex SCHEME_CURRENT $cpu $park 0
powercfg /setdcvalueindex SCHEME_CURRENT $cpu $park 0
powercfg /setactive SCHEME_CURRENT
Write-Output 'Core Parking restaurado.'` };

OPS['disable-memory-compression'] = { admin: true, ps: `$ErrorActionPreference='SilentlyContinue'
Disable-MMAgent -mc -ErrorAction Stop
Write-Output 'Compresión de memoria desactivada.'`, rev: `$ErrorActionPreference='SilentlyContinue'
Enable-MMAgent -mc -ErrorAction Stop
Write-Output 'Compresión de memoria activada.'` };

OPS['disable-paging-executive'] = { reg: [
  R(HKLM, 'SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management', 'DisablePagingExecutive', 'DWORD', 1, 0),
]};

OPS['disable-pointer-precision'] = { reg: [
  R(HKCU, 'Control Panel\\Mouse', 'MouseSpeed', 'SZ', '0', '1'),
  R(HKCU, 'Control Panel\\Mouse', 'MouseThreshold1', 'SZ', '0', '6'),
  R(HKCU, 'Control Panel\\Mouse', 'MouseThreshold2', 'SZ', '0', '10'),
]};

OPS['keyboard-no-delay'] = { reg: [
  R(HKCU, 'Control Panel\\Keyboard', 'KeyboardDelay', 'SZ', '0', '1'),
  R(HKCU, 'Control Panel\\Keyboard', 'KeyboardSpeed', 'SZ', '31', '31'),
]};

OPS['disable-device-power-management'] = { admin: true, ps: `$ErrorActionPreference='SilentlyContinue'
$n=0
Get-CimInstance -Namespace root/WMI -ClassName MSPower_DeviceEnable -ErrorAction SilentlyContinue | Where-Object { $_.InstanceName -match 'HID|USB|Mouse|Keyboard|MOU|KBD' } | ForEach-Object {
  try { $_ | Set-CimInstance -Property @{Enable=$false} -ErrorAction Stop; $n++ } catch {}
}
Write-Output "Ahorro de energía desactivado en $n dispositivo(s)."`, rev: `$ErrorActionPreference='SilentlyContinue'
$n=0
Get-CimInstance -Namespace root/WMI -ClassName MSPower_DeviceEnable -ErrorAction SilentlyContinue | Where-Object { $_.InstanceName -match 'HID|USB|Mouse|Keyboard|MOU|KBD' } | ForEach-Object {
  try { $_ | Set-CimInstance -Property @{Enable=$true} -ErrorAction Stop; $n++ } catch {}
}
Write-Output "Ahorro de energía restaurado en $n dispositivo(s)."` };

OPS['disable-last-access-timestamp'] = { reg: [
  R(HKLM, 'SYSTEM\\CurrentControlSet\\Control\\FileSystem', 'NtfsDisableLastAccessUpdate', 'DWORD', 1, 2147483651),
]};

OPS['disable-network-throttling'] = { reg: [
  R(HKLM, 'SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile', 'NetworkThrottlingIndex', 'DWORD', 4294967295, 10),
]};

OPS['network-force-1gbps-full-duplex'] = { admin: true, risky: true, ps: `$ErrorActionPreference='SilentlyContinue'
$n=0
Get-NetAdapter -Physical | Where-Object { $_.MediaType -eq '802.3' -and $_.Status -eq 'Up' } | ForEach-Object {
  $name=$_.Name
  Set-NetAdapterAdvancedProperty -Name $name -RegistryKeyword '*SpeedDuplex' -RegistryValue 6 -NoRestart -ErrorAction SilentlyContinue
  Restart-NetAdapter -Name $name -ErrorAction SilentlyContinue
  $n++
}
if($n -eq 0){ Write-Output 'No hay un adaptador Ethernet activo.' } else { Write-Output "Ethernet forzado a 1 Gbps Full Duplex en $n adaptador(es)." }` };

OPS['disable-nagle-algorithm'] = { admin: true, ps: `$ErrorActionPreference='SilentlyContinue'
$base='HKLM:\\SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters\\Interfaces'
Get-ChildItem $base | ForEach-Object {
  Set-ItemProperty -Path $_.PSPath -Name TcpAckFrequency -Value 1 -Type DWord
  Set-ItemProperty -Path $_.PSPath -Name TCPNoDelay -Value 1 -Type DWord
}
Write-Output 'Nagle desactivado en todas las interfaces.'`, rev: `$ErrorActionPreference='SilentlyContinue'
$base='HKLM:\\SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters\\Interfaces'
Get-ChildItem $base | ForEach-Object {
  Remove-ItemProperty -Path $_.PSPath -Name TcpAckFrequency -ErrorAction SilentlyContinue
  Remove-ItemProperty -Path $_.PSPath -Name TCPNoDelay -ErrorAction SilentlyContinue
}
Write-Output 'Nagle restaurado.'` };

OPS['disable-qos-bandwidth-reservation'] = { reg: [
  R(HKLM, 'SOFTWARE\\Policies\\Microsoft\\Windows\\Psched', 'NonBestEffortLimit', 'DWORD', 0, 20),
]};

const dnsScript = (a, b, label) => `$ErrorActionPreference='SilentlyContinue'
Get-DnsClientServerAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue | Set-DnsClientServerAddress -ServerAddresses ('${a}','${b}') -ErrorAction SilentlyContinue
Write-Output 'DNS cambiado a ${label}.'`;
OPS['dns-cloudflare'] = { admin: true, ps: dnsScript('1.1.1.1', '1.0.0.1', 'Cloudflare') };
OPS['dns-google'] = { admin: true, ps: dnsScript('8.8.8.8', '8.8.4.4', 'Google') };
OPS['dns-reset-automatic'] = { admin: true, ps: `$ErrorActionPreference='SilentlyContinue'
Get-DnsClientServerAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue | Set-DnsClientServerAddress -ResetServerAddresses -ErrorAction SilentlyContinue
Write-Output 'DNS automático restaurado.'` };
OPS['dns-quad9'] = { admin: true, ps: dnsScript('9.9.9.9', '149.112.112.112', 'Quad9') };
OPS['dns-adguard'] = { admin: true, ps: dnsScript('94.140.14.14', '94.140.15.15', 'AdGuard') };
OPS['dns-cloudflare-family'] = { admin: true, ps: dnsScript('1.1.1.2', '1.0.0.2', 'Cloudflare Familia') };

OPS['net-flush-dns-renew'] = { admin: true, ps: `$ErrorActionPreference='SilentlyContinue'
ipconfig /flushdns | Out-Null
ipconfig /release | Out-Null
ipconfig /renew | Out-Null
Write-Output 'Caché DNS vaciada e IP renovada.'` };

OPS['net-nic-power-saving'] = { admin: true, ps: `$ErrorActionPreference='SilentlyContinue'
$n=0
Get-NetAdapter -Physical | ForEach-Object {
  $pm=Get-NetAdapterPowerManagement -Name $_.Name -ErrorAction SilentlyContinue
  if($pm){ $pm | Set-NetAdapterPowerManagement -AllowComputerToTurnOffDevice 'Disabled' -ErrorAction SilentlyContinue; $n++ }
}
Write-Output "Ahorro de energía desactivado en $n adaptador(es)."`, rev: `$ErrorActionPreference='SilentlyContinue'
$n=0
Get-NetAdapter -Physical | ForEach-Object {
  $pm=Get-NetAdapterPowerManagement -Name $_.Name -ErrorAction SilentlyContinue
  if($pm){ $pm | Set-NetAdapterPowerManagement -AllowComputerToTurnOffDevice 'Enabled' -ErrorAction SilentlyContinue; $n++ }
}
Write-Output "Ahorro de energía restaurado en $n adaptador(es)."` };

OPS['net-disable-eee'] = { admin: true, ps: `$ErrorActionPreference='SilentlyContinue'
$n=0
Get-NetAdapter -Physical | ForEach-Object {
  $name=$_.Name
  Get-NetAdapterAdvancedProperty -Name $name -ErrorAction SilentlyContinue | Where-Object { $_.RegistryKeyword -match '(?i)EEE|Energy' } | ForEach-Object {
    Set-NetAdapterAdvancedProperty -Name $name -RegistryKeyword $_.RegistryKeyword -RegistryValue 0 -NoRestart -ErrorAction SilentlyContinue
    $n++
  }
}
Write-Output "Energy-Efficient Ethernet desactivado en $n propiedad(es)."`, rev: `$ErrorActionPreference='SilentlyContinue'
Get-NetAdapter -Physical | ForEach-Object {
  $name=$_.Name
  Get-NetAdapterAdvancedProperty -Name $name -ErrorAction SilentlyContinue | Where-Object { $_.RegistryKeyword -match '(?i)EEE|Energy' } | ForEach-Object {
    Set-NetAdapterAdvancedProperty -Name $name -RegistryKeyword $_.RegistryKeyword -RegistryValue 1 -NoRestart -ErrorAction SilentlyContinue
  }
}
Write-Output 'EEE restaurado.'` };

OPS['net-tcp-global-params'] = { admin: true, ps: `$ErrorActionPreference='SilentlyContinue'
netsh int tcp set global autotuninglevel=normal | Out-Null
netsh int tcp set global rss=enabled | Out-Null
netsh int tcp set global ecn=disabled | Out-Null
netsh int tcp set global timestamps=disabled | Out-Null
Write-Output 'Parámetros TCP ajustados.'`, rev: `$ErrorActionPreference='SilentlyContinue'
netsh int tcp set global autotuninglevel=normal | Out-Null
netsh int tcp set global rss=enabled | Out-Null
netsh int tcp set global ecn=default | Out-Null
netsh int tcp set global timestamps=default | Out-Null
Write-Output 'Parámetros TCP restaurados.'` };

OPS['net-disable-smb1'] = { admin: true, ps: `$ErrorActionPreference='SilentlyContinue'
Set-SmbServerConfiguration -EnableSMB1Protocol $false -Force
Write-Output 'SMB1 desactivado.'`, rev: `$ErrorActionPreference='SilentlyContinue'
Set-SmbServerConfiguration -EnableSMB1Protocol $true -Force
Write-Output 'SMB1 activado.'` };

OPS['net-stack-reset'] = { admin: true, risky: true, ps: `$ErrorActionPreference='SilentlyContinue'
netsh winsock reset | Out-Null
netsh int ip reset | Out-Null
ipconfig /flushdns | Out-Null
Write-Output 'Pila de red restablecida. Reinicia la PC para terminar.'` };

OPS['net-nic-offload-off'] = { admin: true, ps: `$ErrorActionPreference='SilentlyContinue'
$map=@{ '*InterruptModeration'=0; '*LsoV2IPv4'=0; '*LsoV2IPv6'=0; '*FlowControl'=0 }
Get-NetAdapter -Physical | ForEach-Object {
  $name=$_.Name
  foreach($k in $map.Keys){ Set-NetAdapterAdvancedProperty -Name $name -RegistryKeyword $k -RegistryValue $map[$k] -NoRestart -ErrorAction SilentlyContinue }
}
Write-Output 'Procesamiento diferido de red desactivado.'`, rev: `$ErrorActionPreference='SilentlyContinue'
$map=@{ '*InterruptModeration'=1; '*LsoV2IPv4'=1; '*LsoV2IPv6'=1; '*FlowControl'=3 }
Get-NetAdapter -Physical | ForEach-Object {
  $name=$_.Name
  foreach($k in $map.Keys){ Set-NetAdapterAdvancedProperty -Name $name -RegistryKeyword $k -RegistryValue $map[$k] -NoRestart -ErrorAction SilentlyContinue }
}
Write-Output 'Valores de red restaurados.'` };

OPS['disable-search-indexing'] = { admin: true, reg: [
  R(HKLM, 'SYSTEM\\CurrentControlSet\\Services\\WSearch', 'Start', 'DWORD', 4, 2),
], ps: `$ErrorActionPreference='SilentlyContinue'
Stop-Service WSearch -Force
Write-Output 'Indexado de búsqueda detenido.'`, rev: `$ErrorActionPreference='SilentlyContinue'
Start-Service WSearch
Write-Output 'Indexado de búsqueda activado.'` };

OPS['disable-onedrive-startup'] = { reg: [
  R(HKCU, 'Software\\Microsoft\\Windows\\CurrentVersion\\Run', 'OneDrive', 'SZ', '', '', true, true),
], rev: `$ErrorActionPreference='SilentlyContinue'
$run='HKCU:\\Software\\Microsoft\\Windows\\CurrentVersion\\Run'
if(-not (Get-ItemProperty -Path $run -Name OneDrive -ErrorAction SilentlyContinue)){
  $exe=Join-Path $env:LOCALAPPDATA 'Microsoft\\OneDrive\\OneDrive.exe'
  if(Test-Path $exe){ Set-ItemProperty -Path $run -Name OneDrive -Value ('"'+$exe+'" /background') }
}
Write-Output 'OneDrive restaurado.'` };

OPS['disable-diagtrack'] = { admin: true, reg: [
  R(HKLM, 'SYSTEM\\CurrentControlSet\\Services\\DiagTrack', 'Start', 'DWORD', 4, 2),
], ps: `$ErrorActionPreference='SilentlyContinue'
Stop-Service DiagTrack -Force
Write-Output 'Servicio DiagTrack detenido.'`, rev: `$ErrorActionPreference='SilentlyContinue'
Start-Service DiagTrack
Write-Output 'Servicio DiagTrack activado.'` };

OPS['disable-error-reporting'] = { reg: [
  R(HKLM, 'SOFTWARE\\Microsoft\\Windows\\Windows Error Reporting', 'Disabled', 'DWORD', 1, 0),
  R(HKLM, 'SOFTWARE\\Policies\\Microsoft\\Windows\\Windows Error Reporting', 'Disabled', 'DWORD', 1, 0),
]};

OPS['disable-fullscreen-optimizations'] = { reg: [
  R(HKCU, 'System\\GameConfigStore', 'GameDVR_FSEBehaviorMode', 'DWORD', 2, 0),
  R(HKCU, 'System\\GameConfigStore', 'GameDVR_HonorUserFSEBehaviorMode', 'DWORD', 1, 0),
  R(HKCU, 'System\\GameConfigStore', 'GameDVR_DXGIHonorFSEWindowsCompatible', 'DWORD', 1, 0),
]};

OPS['disable-game-dvr'] = { reg: [
  R(HKCU, 'System\\GameConfigStore', 'GameDVR_Enabled', 'DWORD', 0, 1),
  R(HKCU, 'Software\\Microsoft\\Windows\\CurrentVersion\\GameDVR', 'AppCaptureEnabled', 'DWORD', 0, 1),
  R(HKLM, 'SOFTWARE\\Policies\\Microsoft\\Windows\\GameDVR', 'AllowGameDVR', 'DWORD', 0, 1),
]};

OPS['remove-bloatware-apps'] = { admin: true, ps: `$ErrorActionPreference='SilentlyContinue'
$apps=@('Microsoft.3DBuilder','Microsoft.MicrosoftSolitaireCollection','Microsoft.MixedReality.Portal','Microsoft.WindowsFeedbackHub','Microsoft.GetHelp','Microsoft.Getstarted','Microsoft.MicrosoftOfficeHub','Microsoft.People','Microsoft.SkypeApp','Microsoft.WindowsMaps','Microsoft.ZuneMusic','Microsoft.ZuneVideo','Microsoft.BingNews','Microsoft.BingWeather','Microsoft.YourPhone')
foreach($a in $apps){ Get-AppxPackage -Name $a -AllUsers -ErrorAction SilentlyContinue | Remove-AppxPackage -AllUsers -ErrorAction SilentlyContinue; Get-AppxPackage -Name $a -ErrorAction SilentlyContinue | Remove-AppxPackage -ErrorAction SilentlyContinue }
Write-Output 'Apps preinstaladas innecesarias eliminadas.'` };

OPS['fps-mmcss-system-responsiveness'] = { reg: [
  R(HKLM, 'SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile', 'SystemResponsiveness', 'DWORD', 10, 20),
]};

OPS['fps-win32-priority-separation'] = { reg: [
  R(HKLM, 'SYSTEM\\CurrentControlSet\\Control\\PriorityControl', 'Win32PrioritySeparation', 'DWORD', 38, 2),
]};

OPS['fps-exclude-game-folders-defender'] = { admin: true, ps: `$ErrorActionPreference='SilentlyContinue'
$paths=@()
$steam=(Get-ItemProperty 'HKCU:\\Software\\Valve\\Steam' -ErrorAction SilentlyContinue).SteamPath
if($steam){
  $paths += (Join-Path $steam 'steamapps\\common')
  $lf=Join-Path $steam 'steamapps\\libraryfolders.vdf'
  if(Test-Path $lf){ $txt=Get-Content $lf -Raw; foreach($m in [regex]::Matches($txt,'"path"\\s+"([^"]+)"')){ $p=$m.Groups[1].Value -replace '\\\\\\\\','\\'; $paths += (Join-Path $p 'steamapps\\common') } }
}
$epic='C:\\ProgramData\\Epic\\UnrealEngineLauncher\\LauncherInstalled.dat'
if(Test-Path $epic){ try { $j=Get-Content $epic -Raw | ConvertFrom-Json; foreach($i in $j.InstallationList){ if($i.InstallLocation){ $paths += $i.InstallLocation } } } catch {} }
$paths += @('C:\\Program Files (x86)\\Battle.net','C:\\Program Files\\Battle.net','C:\\Program Files (x86)\\StarCraft II','C:\\Program Files (x86)\\Hearthstone','C:\\Program Files (x86)\\Overwatch','C:\\Program Files (x86)\\Call of Duty')
$paths | Sort-Object -Unique | Where-Object { Test-Path $_ } | ForEach-Object { Add-MpPreference -ExclusionPath $_ -ErrorAction SilentlyContinue; Write-Output "Excluido: $_" }` };

OPS['fps-ultimate-performance-plan'] = { admin: true, ps: `$ErrorActionPreference='SilentlyContinue'
$out=powercfg /duplicatescheme e9a42b02-d5df-448d-aa00-03f14749eb61
$m=[regex]::Match(($out -join ' '),'[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}')
if(-not $m.Success){ $out=powercfg /duplicatescheme 8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c; $m=[regex]::Match(($out -join ' '),'[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}') }
if($m.Success){ powercfg /setactive $m.Value | Out-Null; Write-Output 'Plan Maximum Performance activado.' } else { Write-Output 'No se pudo activar el plan.'; exit 1 }` };

OPS['fps-disable-dynamic-tick'] = { admin: true, ps: `$ErrorActionPreference='SilentlyContinue'
bcdedit /set disabledynamictick yes | Out-Null
Write-Output 'Dynamic Tick desactivado. Reinicia para aplicarlo.'`, rev: `$ErrorActionPreference='SilentlyContinue'
bcdedit /set disabledynamictick no | Out-Null
Write-Output 'Dynamic Tick restaurado.'` };

OPS['fps-disable-hpet'] = { admin: true, ps: `$ErrorActionPreference='SilentlyContinue'
bcdedit /set useplatformclock false | Out-Null
Write-Output 'HPET desactivado. Reinicia para aplicarlo.'`, rev: `$ErrorActionPreference='SilentlyContinue'
bcdedit /deletevalue useplatformclock | Out-Null
Write-Output 'HPET restaurado.'` };

OPS['disable-sysmain'] = { admin: true, reg: [
  R(HKLM, 'SYSTEM\\CurrentControlSet\\Services\\SysMain', 'Start', 'DWORD', 4, 2),
], ps: `$ErrorActionPreference='SilentlyContinue'
Stop-Service SysMain -Force
Write-Output 'SysMain detenido.'`, rev: `$ErrorActionPreference='SilentlyContinue'
Start-Service SysMain
Write-Output 'SysMain activado.'` };

OPS['fps-tcp-ack-frequency'] = { admin: true, ps: `$ErrorActionPreference='SilentlyContinue'
$base='HKLM:\\SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters\\Interfaces'
Get-ChildItem $base | ForEach-Object {
  Set-ItemProperty -Path $_.PSPath -Name TcpAckFrequency -Value 1 -Type DWord
  Set-ItemProperty -Path $_.PSPath -Name TCPNoDelay -Value 1 -Type DWord
}
Write-Output 'TCP ACK Frequency ajustado.'`, rev: `$ErrorActionPreference='SilentlyContinue'
$base='HKLM:\\SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters\\Interfaces'
Get-ChildItem $base | ForEach-Object {
  Remove-ItemProperty -Path $_.PSPath -Name TcpAckFrequency -ErrorAction SilentlyContinue
  Remove-ItemProperty -Path $_.PSPath -Name TCPNoDelay -ErrorAction SilentlyContinue
}
Write-Output 'TCP ACK Frequency restaurado.'` };

const pauseUpdates = `$ErrorActionPreference='Stop'
$p='HKLM:\\SOFTWARE\\Microsoft\\WindowsUpdate\\UX\\Settings'
New-Item -Path $p -Force | Out-Null
$s=(Get-Date).ToUniversalTime(); $e=$s.AddDays(7)
$ini=$s.ToString("yyyy-MM-dd'T'HH:mm:ss'Z'"); $fin=$e.ToString("yyyy-MM-dd'T'HH:mm:ss'Z'")
Set-ItemProperty -Path $p -Name PauseFeatureUpdatesStartTime -Value $ini
Set-ItemProperty -Path $p -Name PauseFeatureUpdatesEndTime -Value $fin
Set-ItemProperty -Path $p -Name PauseQualityUpdatesStartTime -Value $ini
Set-ItemProperty -Path $p -Name PauseQualityUpdatesEndTime -Value $fin
Set-ItemProperty -Path $p -Name PauseUpdatesStartTime -Value $ini
Set-ItemProperty -Path $p -Name PauseUpdatesExpiryTime -Value $fin
Write-Output 'Windows Update pausado por 7 días.'`;
OPS['pause-windows-update-week'] = { admin: true, ps: pauseUpdates };
OPS['pause-windows-update-7days'] = { admin: true, ps: pauseUpdates };

OPS['fps-enable-hags'] = { reg: [
  R(HKLM, 'SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers', 'HwSchMode', 'DWORD', 2, 1),
]};

OPS['fps-disable-spectre-meltdown-mitigations'] = { reg: [
  R(HKLM, 'SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management', 'FeatureSettingsOverride', 'DWORD', 3, 0),
  R(HKLM, 'SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management', 'FeatureSettingsOverrideMask', 'DWORD', 3, 0),
]};

OPS['fps-best-performance-visuals'] = { reg: [
  R(HKCU, 'Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\VisualEffects', 'VisualFXSetting', 'DWORD', 2, 0),
  R(HKCU, 'Control Panel\\Desktop\\WindowMetrics', 'MinAnimate', 'SZ', '0', '1'),
  R(HKCU, 'Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced', 'TaskbarAnimations', 'DWORD', 0, 1),
  R(HKCU, 'Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced', 'ListviewAlphaSelect', 'DWORD', 0, 1),
  R(HKCU, 'Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced', 'ListviewShadow', 'DWORD', 0, 1),
  R(HKCU, 'Control Panel\\Desktop', 'DragFullWindows', 'SZ', '0', '1'),
  R(HKCU, 'Control Panel\\Desktop', 'FontSmoothing', 'SZ', '0', '2'),
]};

OPS['fps-disable-hibernation'] = { admin: true, ps: `$ErrorActionPreference='SilentlyContinue'
powercfg /h off
Write-Output 'Hibernación desactivada y hiberfil.sys eliminado.'`, rev: `$ErrorActionPreference='SilentlyContinue'
powercfg /h on
Write-Output 'Hibernación activada.'` };

OPS['fps-remove-xbox-game-bar'] = { ps: `$ErrorActionPreference='SilentlyContinue'
Get-AppxPackage -Name Microsoft.XboxGamingOverlay -ErrorAction SilentlyContinue | Remove-AppxPackage -ErrorAction SilentlyContinue
Get-AppxPackage -Name Microsoft.XboxGameOverlay -ErrorAction SilentlyContinue | Remove-AppxPackage -ErrorAction SilentlyContinue
Write-Output 'Xbox Game Bar desinstalada. Podes reinstalarla desde la Store.'` };

OPS['bg-close-background-bloat'] = { ps: `$ErrorActionPreference='SilentlyContinue'
$names=@('Widgets','WidgetService','ms-teams','Teams','YourPhone','PhoneExperienceHost','Copilot','GameBarPresenceWriter','GameBar')
Get-Process -Name $names -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Write-Output 'Procesos de fondo cerrados.'` };

OPS['bg-disable-widgets'] = { reg: [
  R(HKCU, 'Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced', 'TaskbarDa', 'DWORD', 0, 1),
  R(HKLM, 'SOFTWARE\\Policies\\Microsoft\\Dsh', 'AllowWidgets', 'DWORD', 0, 1),
]};

OPS['bg-disable-chat-icon'] = { reg: [
  R(HKCU, 'Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced', 'TaskbarMn', 'DWORD', 0, 1),
]};

OPS['bg-disable-copilot'] = { reg: [
  R(HKCU, 'Software\\Policies\\Microsoft\\Windows\\WindowsCopilot', 'TurnOffWindowsCopilot', 'DWORD', 1, 0),
  R(HKCU, 'Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced', 'ShowCopilotButton', 'DWORD', 0, 1),
]};

OPS['bg-disable-your-phone'] = { reg: [
  R(HKCU, 'Software\\Microsoft\\Windows\\CurrentVersion\\CrossDevice', 'PhoneLinkEnabled', 'DWORD', 0, 1),
  R(HKCU, 'Software\\Microsoft\\Windows\\CurrentVersion\\CrossDevice', 'EnableCloudSync', 'DWORD', 0, 1),
]};

OPS['bg-disable-search-web-highlights'] = { reg: [
  R(HKCU, 'Software\\Microsoft\\Windows\\CurrentVersion\\Search', 'BingSearchEnabled', 'DWORD', 0, 1),
  R(HKCU, 'Software\\Microsoft\\Windows\\CurrentVersion\\Search', 'CortanaConsent', 'DWORD', 0, 1),
  R(HKLM, 'SOFTWARE\\Policies\\Microsoft\\Windows\\Windows Search', 'DisableSearchBoxSuggestions', 'DWORD', 1, 0),
  R(HKLM, 'SOFTWARE\\Policies\\Microsoft\\Windows\\Windows Search', 'AllowSearchToUseLocation', 'DWORD', 0, 1),
]};

OPS['disable-8dot3-names'] = { admin: true, ps: `$ErrorActionPreference='SilentlyContinue'
fsutil behavior set disable8dot3 1 | Out-Null
Write-Output 'Nombres cortos 8.3 desactivados.'`, rev: `$ErrorActionPreference='SilentlyContinue'
fsutil behavior set disable8dot3 0 | Out-Null
Write-Output 'Nombres cortos 8.3 restaurados.'` };

OPS['disable-folder-type-discovery'] = { reg: [
  R(HKCU, 'Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\Shell\\Bags\\AllFolders\\Shell', 'FolderType', 'SZ', 'NotSpecified', '', false, true),
]};

OPS['disable-sleep-study'] = { reg: [
  R(HKLM, 'SYSTEM\\CurrentControlSet\\Control\\Power\\SleepStudy', 'Enabled', 'DWORD', 0, 1),
]};

OPS['disable-automatic-maintenance'] = { reg: [
  R(HKLM, 'SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Schedule\\Maintenance', 'MaintenanceDisabled', 'DWORD', 1, 0),
]};

OPS['disable-fault-tolerant-heap'] = { reg: [
  R(HKLM, 'SOFTWARE\\Microsoft\\FTH', 'Enabled', 'DWORD', 0, 1),
]};

OPS['disable-telemetry-scheduled-tasks'] = { admin: true, ps: `$ErrorActionPreference='SilentlyContinue'
Get-ScheduledTask -TaskPath '\\Microsoft\\Windows\\Application Experience\\*','\\Microsoft\\Windows\\Customer Experience Improvement Program\\*','\\Microsoft\\Windows\\Autochk\\*' -ErrorAction SilentlyContinue | Disable-ScheduledTask -ErrorAction SilentlyContinue | Out-Null
Write-Output 'Tareas de telemetría desactivadas.'`, rev: `$ErrorActionPreference='SilentlyContinue'
Get-ScheduledTask -TaskPath '\\Microsoft\\Windows\\Application Experience\\*','\\Microsoft\\Windows\\Customer Experience Improvement Program\\*','\\Microsoft\\Windows\\Autochk\\*' -ErrorAction SilentlyContinue | Enable-ScheduledTask -ErrorAction SilentlyContinue | Out-Null
Write-Output 'Tareas de telemetría reactivadas.'` };

OPS['classic-context-menu'] = { ps: `$ErrorActionPreference='SilentlyContinue'
$key='HKCU:\\Software\\Classes\\CLSID\\{86ca1aa0-34aa-4e8b-a509-50c905bae2a2}\\InprocServer32'
New-Item -Path $key -Force | Out-Null
Set-ItemProperty -Path $key -Name '(default)' -Value ''
Stop-Process -Name explorer -Force -ErrorAction SilentlyContinue
Write-Output 'Menú contextual clásico activado. El Explorador se reinició.'`, rev: `$ErrorActionPreference='SilentlyContinue'
Remove-Item 'HKCU:\\Software\\Classes\\CLSID\\{86ca1aa0-34aa-4e8b-a509-50c905bae2a2}' -Recurse -Force -ErrorAction SilentlyContinue
Stop-Process -Name explorer -Force -ErrorAction SilentlyContinue
Write-Output 'Menú contextual moderno restaurado.'` };

OPS['restrict-sam-anonymous-enum'] = { reg: [
  R(HKLM, 'SYSTEM\\CurrentControlSet\\Control\\Lsa', 'RestrictAnonymous', 'DWORD', 1, 0),
  R(HKLM, 'SYSTEM\\CurrentControlSet\\Control\\Lsa', 'RestrictAnonymousSAM', 'DWORD', 1, 0),
]};

OPS['disable-remote-assistance'] = { reg: [
  R(HKLM, 'SYSTEM\\CurrentControlSet\\Control\\Remote Assistance', 'fAllowToGetHelp', 'DWORD', 0, 1),
]};

OPS['disable-vbs-core-isolation'] = { admin: true, risky: true, reg: [
  R(HKLM, 'SYSTEM\\CurrentControlSet\\Control\\DeviceGuard', 'EnableVirtualizationBasedSecurity', 'DWORD', 0, 1),
  R(HKLM, 'SYSTEM\\CurrentControlSet\\Control\\DeviceGuard\\Scenarios\\HypervisorEnforcedCodeIntegrity', 'Enabled', 'DWORD', 0, 1),
], ps: `$ErrorActionPreference='SilentlyContinue'
bcdedit /set hypervisorlaunchtype off | Out-Null
Write-Output 'VBS e integridad de memoria desactivados. Reinicia la PC.'`, rev: `$ErrorActionPreference='SilentlyContinue'
bcdedit /set hypervisorlaunchtype auto | Out-Null
Write-Output 'VBS restaurado.'` };

OPS['disable-cpu-mitigations'] = { admin: true, reg: [
  R(HKLM, 'SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management', 'FeatureSettingsOverride', 'DWORD', 3, 0),
  R(HKLM, 'SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management', 'FeatureSettingsOverrideMask', 'DWORD', 3, 0),
]};

OPS['disable-reserved-storage'] = { admin: true, ps: `$ErrorActionPreference='SilentlyContinue'
DISM.exe /Online /Set-ReservedStorageState /State:Disabled | Out-Null
Write-Output 'Almacenamiento reservado desactivado.'`, rev: `$ErrorActionPreference='SilentlyContinue'
DISM.exe /Online /Set-ReservedStorageState /State:Enabled | Out-Null
Write-Output 'Almacenamiento reservado activado.'` };

OPS['disable-wpbt'] = { reg: [
  R(HKLM, 'SYSTEM\\CurrentControlSet\\Control\\Session Manager', 'DisableWpbtExecution', 'DWORD', 1, 0),
]};

OPS['enable-long-paths'] = { reg: [
  R(HKLM, 'SYSTEM\\CurrentControlSet\\Control\\FileSystem', 'LongPathsEnabled', 'DWORD', 1, 0),
]};

OPS['utc-clock'] = { reg: [
  R(HKLM, 'SYSTEM\\CurrentControlSet\\Control\\TimeZoneInformation', 'RealTimeIsUniversal', 'DWORD', 1, 0),
]};

OPS['verbose-status-messages'] = { reg: [
  R(HKLM, 'SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\System', 'VerboseStatus', 'DWORD', 1, 0),
]};

// ---------------------------------------------------------------- LimeForge additions
const NEW_ITEMS = [
  {
    id: 'disable-lock-screen-spotlight', category: 'General', subcategory: 'Apariencia',
    name: 'Desactivar imágenes de Spotlight en la pantalla de bloqueo',
    description: 'Evita que Windows descargue y rote imágenes y sugerencias de Windows Spotlight en la pantalla de bloqueo. Usa un fondo fijo y reduce el consumo de red en segundo plano.',
    perfImpact: 1,
  },
  {
    id: 'disable-notification-sounds', category: 'General', subcategory: 'Notificaciones',
    name: 'Silenciar los sonidos de notificaciones',
    description: 'Desactiva el sonido de las notificaciones del sistema sin ocultar los avisos. Útil para concentrarte o grabar sin interrupciones.',
    perfImpact: 0,
  },
  {
    id: 'disable-app-install-suggestions', category: 'General', subcategory: 'Privacidad',
    name: 'Impedir la instalación automática de apps sugeridas',
    description: 'Bloquea las apps promocionadas que Windows instala o sugiere automáticamente desde la Store. No elimina las apps que ya tenés instaladas.',
    perfImpact: 1,
  },
  {
    id: 'disable-start-recommendations', category: 'General', subcategory: 'Privacidad',
    name: 'Ocultar recomendaciones en el menú Inicio',
    description: 'Desactiva las recomendaciones personalizadas de archivos, apps y sitios que aparecen en el menú Inicio de Windows 11.',
    perfImpact: 0,
  },
  {
    id: 'disable-update-auto-restart', category: 'General', subcategory: 'Sistema',
    name: 'Evitar reinicios automáticos de Windows Update',
    description: 'Impide que Windows reinicie el equipo por su cuenta para terminar de instalar actualizaciones mientras hay una sesión iniciada. Vas a poder reiniciar cuando quieras.',
    warning: 'Las actualizaciones pueden quedar pendientes hasta que reinicies manualmente.',
    perfImpact: 0,
  },
  {
    id: 'disable-teams-startup', category: 'General', subcategory: 'Arranque',
    name: 'Quitar Microsoft Teams del inicio automático',
    description: 'Elimina las entradas de inicio de Microsoft Teams para el usuario actual. Teams seguirá instalado y podés abrirlo manualmente cuando lo necesites.',
    perfImpact: 1,
  },
  {
    id: 'disable-news-and-interests', category: 'General', subcategory: 'Interfaz',
    name: 'Ocultar Noticias e intereses de la barra de tareas',
    description: 'Quita el panel de noticias y el clima de la barra de tareas de Windows 11 para liberar espacio y evitar contenido en segundo plano.',
    perfImpact: 0,
  },
  {
    id: 'disable-power-throttling', category: 'Hardware', subcategory: 'Rendimiento',
    name: 'Desactivar el limitador de energía (Power Throttling)',
    description: 'Evita que Windows reduzca la frecuencia de la CPU para ahorrar energía cuando detecta cargas de trabajo. Puede mejorar la fluidez en juegos y tareas pesadas.',
    warning: 'Puede aumentar el consumo eléctrico y la temperatura, especialmente en portátiles.',
    perfImpact: 3,
  },
  {
    id: 'show-end-task-taskbar', category: 'Windows Custom', subcategory: 'Barra de tareas',
    name: 'Agregar "Finalizar tarea" al menú de la barra de tareas',
    description: 'Habilita la opción Finalizar tarea al hacer clic derecho sobre una app en la barra de tareas de Windows 11. Cierra procesos colgados sin abrir el Administrador de tareas.',
    perfImpact: 0,
  },
  {
    id: 'hide-task-view-button', category: 'Windows Custom', subcategory: 'Barra de tareas',
    name: 'Ocultar el botón Vista de tareas',
    description: 'Quita el botón de Vista de tareas de la barra de tareas para dejar más espacio a las apps abiertas. La función sigue disponible con Win+Tab.',
    perfImpact: 0,
  },
  {
    id: 'create-restore-point', category: 'General', subcategory: 'Sistema',
    name: 'Crear un punto de restauración', actionOnly: true, admin: true,
    buttonLabel: 'Crear punto',
    description: 'Crea un punto de restauración de Windows antes de hacer cambios importantes. Requiere que la protección del sistema esté disponible en la unidad del sistema.',
    perfImpact: 0,
  },
  {
    id: 'create-god-mode', category: 'Accesos directos', subcategory: 'Utilidades',
    name: 'Crear God Mode en el Escritorio', actionOnly: true,
    buttonLabel: 'Crear acceso',
    description: 'Agrega un acceso directo en el Escritorio con todos los paneles de configuración de Windows reunidos en una sola carpeta. Se puede borrar como cualquier otro acceso.',
    perfImpact: 0,
  },
];

for (const item of NEW_ITEMS) src.push(item);

OPS['disable-lock-screen-spotlight'] = { reg: [
  R(HKCU, 'Software\\Microsoft\\Windows\\CurrentVersion\\ContentDeliveryManager', 'RotatingLockScreenEnabled', 'DWORD', 0, 1),
  R(HKCU, 'Software\\Microsoft\\Windows\\CurrentVersion\\ContentDeliveryManager', 'RotatingLockScreenOverlayEnabled', 'DWORD', 0, 1),
  R(HKCU, 'Software\\Microsoft\\Windows\\CurrentVersion\\ContentDeliveryManager', 'SubscribedContent-338387Enabled', 'DWORD', 0, 1),
]};

OPS['disable-notification-sounds'] = { reg: [
  R(HKCU, 'Software\\Microsoft\\Windows\\CurrentVersion\\Notifications\\Settings', 'NOC_GLOBAL_SETTING_ALLOW_NOTIFICATION_SOUND', 'DWORD', 0, 1),
]};

OPS['disable-app-install-suggestions'] = { reg: [
  R(HKCU, 'Software\\Microsoft\\Windows\\CurrentVersion\\ContentDeliveryManager', 'SilentInstalledAppsEnabled', 'DWORD', 0, 1),
  R(HKCU, 'Software\\Microsoft\\Windows\\CurrentVersion\\ContentDeliveryManager', 'PreInstalledAppsEnabled', 'DWORD', 0, 1),
]};

OPS['disable-start-recommendations'] = { reg: [
  R(HKCU, 'Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced', 'Start_IrisRecommendations', 'DWORD', 0, 1),
]};

OPS['disable-update-auto-restart'] = { reg: [
  R(HKLM, 'SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsUpdate\\AU', 'NoAutoRebootWithLoggedOnUsers', 'DWORD', 1, 0),
]};

OPS['disable-teams-startup'] = { reg: [
  R(HKCU, 'Software\\Microsoft\\Windows\\CurrentVersion\\Run', 'com.squirrel.Teams.Teams', 'SZ', '', '', true, false),
  R(HKCU, 'Software\\Microsoft\\Windows\\CurrentVersion\\Run', 'MicrosoftTeams', 'SZ', '', '', true, false),
]};

OPS['disable-news-and-interests'] = { reg: [
  R(HKCU, 'Software\\Microsoft\\Windows\\CurrentVersion\\Feeds', 'ShellFeedsTaskbarViewMode', 'DWORD', 2, 0),
]};

OPS['disable-power-throttling'] = { admin: true, reg: [
  R(HKLM, 'SYSTEM\\CurrentControlSet\\Control\\Power\\PowerThrottling', 'PowerThrottlingOff', 'DWORD', 1, 0),
]};

OPS['show-end-task-taskbar'] = { reg: [
  R(HKCU, 'Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced', 'TaskbarEndTask', 'DWORD', 1, 0),
]};

OPS['hide-task-view-button'] = { reg: [
  R(HKCU, 'Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced', 'ShowTaskViewButton', 'DWORD', 0, 1),
]};

OPS['create-restore-point'] = { admin: true, actionOnly: true, ps: `$ErrorActionPreference='Stop'
try {
  Enable-ComputerRestore -Drive "$env:SystemDrive\" -ErrorAction SilentlyContinue
  Checkpoint-Computer -Description 'LimeForge' -RestorePointType 'MODIFY_SETTINGS'
  Write-Output 'Punto de restauración creado correctamente.'
} catch {
  Write-Output ("No se pudo crear el punto: " + $_.Exception.Message)
  exit 1
}` };

OPS['create-god-mode'] = { actionOnly: true, ps: `$ErrorActionPreference='SilentlyContinue'
$desktop=[Environment]::GetFolderPath('Desktop')
$path=Join-Path $desktop 'GodMode.{ED7BA470-8E54-465E-825C-99712043E01C}'
if(-not (Test-Path $path)){ New-Item -ItemType Directory -Path $path | Out-Null }
Write-Output 'God Mode disponible en el Escritorio.'` };

// ---------------------------------------------------------------- Emit
function wesc(s) {
  return String(s).replace(/\\/g, '\\\\').replace(/"/g, '\\"').replace(/\r?\n/g, '\\n');
}
function raw(s) {
  const t = String(s).replace(/\r\n/g, '\n');
  if (t.includes(')PS"')) throw new Error('raw string delimiter collision');
  return 'LR"PS(\n' + t + '\n)PS"';
}

const typeMap = { DWORD: 'REG_DWORD', SZ: 'REG_SZ', EXPAND: 'REG_EXPAND_SZ' };
const lines = [];
lines.push('// Generated by tools/generate_catalog.js — original catalog metadata + independent operations.');
lines.push('#include "catalog.h"');
lines.push('');
lines.push('namespace lf {');
lines.push('namespace {');
lines.push('RegOp R(HKEY root, const wchar_t* sub, const wchar_t* name, DWORD type, const wchar_t* apply, const wchar_t* revert, bool delA = false, bool delR = false) {');
lines.push('  return RegOp{root, sub, name, type, apply, revert, delA, delR};');
lines.push('}');
lines.push('}');
lines.push('');
lines.push('std::vector<Tweak> BuildCatalog() {');
lines.push('  std::vector<Tweak> out;');
lines.push(`  out.reserve(${src.length});`);
let missing = 0;
for (const item of src) {
  const op = OPS[item.id];
  if (!op) { missing++; console.error('Missing ops:', item.id); }
  const o = op || {};
  const needsAdmin = !!o.admin || (o.reg || []).some(r => r.root === 'HKEY_LOCAL_MACHINE');
  lines.push('  {');
  lines.push('    Tweak t;');
  lines.push(`    t.id = L"${wesc(item.id)}";`);
  lines.push(`    t.category = L"${wesc(item.category || 'General')}";`);
  lines.push(`    t.subcategory = L"${wesc(item.subcategory || '')}";`);
  lines.push(`    t.name = L"${wesc(item.name || item.id)}";`);
  lines.push(`    t.description = L"${wesc(item.description || '')}";`);
  lines.push(`    t.warning = L"${wesc(item.warning || '')}";`);
  lines.push(`    t.buttonLabel = L"${wesc(item.buttonLabel || '')}";`);
  lines.push(`    t.confirmMessage = L"${wesc(item.confirmMessage || '')}";`);
  for (const r of (o.reg || [])) {
    lines.push(`    t.reg.push_back(R(${r.root}, L"${wesc(r.sub)}", L"${wesc(r.name)}", ${typeMap[r.type]}, L"${wesc(r.apply)}", L"${wesc(r.revert)}", ${r.delA ? 'true' : 'false'}, ${r.delR ? 'true' : 'false'}));`);
  }
  if (o.ps) lines.push(`    t.applyPs = ${raw(o.ps)};`);
  if (o.rev) lines.push(`    t.revertPs = ${raw(o.rev)};`);
  if (o.url) lines.push(`    t.openUrl = L"${wesc(o.url)}";`);
  if (o.special) lines.push(`    t.special = L"${wesc(o.special)}";`);
  lines.push(`    t.actionOnly = ${item.actionOnly ? 'true' : 'false'};`);
  lines.push(`    t.premium = ${item.premium ? 'true' : 'false'};`);
  lines.push(`    t.remoteOnly = ${item.remoteOnly ? 'true' : 'false'};`);
  lines.push(`    t.laptopWarning = ${item.laptopWarning ? 'true' : 'false'};`);
  lines.push(`    t.perfImpact = ${item.perfImpact || 0};`);
  lines.push(`    t.risky = ${RISKY.has(item.id) ? 'true' : 'false'};`);
  lines.push(`    t.restart = ${RESTART.has(item.id) ? 'true' : 'false'};`);
  lines.push(`    t.recommended = ${RECOMMENDED.has(item.id) ? 'true' : 'false'};`);
  lines.push(`    t.needsAdmin = ${needsAdmin ? 'true' : 'false'};`);
  lines.push('    out.push_back(std::move(t));');
  lines.push('  }');
}
lines.push('  return out;');
lines.push('}');
lines.push('');
lines.push('} // namespace lf');
fs.writeFileSync(OUT, lines.join('\n'), 'utf8');
console.error('Wrote', OUT, 'items', src.length, 'missing', missing);
