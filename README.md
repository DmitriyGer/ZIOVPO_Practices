# ZIOVPO Antivirus

Проект состоит из двух нативных Windows-компонентов:

- `TrayApp.exe` — пользовательское tray-приложение.
- `TrayService.exe` — Windows-служба, которая запускает tray-приложение и фоновые проверки.

Installer и CI/CD для ветки `PR_5` описаны в [Installer/README.md](Installer/README.md).

## Локальная сборка приложения

Сборка solution:

```powershell
msbuild TrayApp.sln /m /nologo /verbosity:minimal /p:Configuration=Release /p:Platform=x64
```

Сборка в отдельную директорию, как в GitHub Actions:

```powershell
$outDir = Join-Path $PWD "out"
msbuild TrayApp.sln /m /nologo /verbosity:minimal /p:Configuration=Release /p:Platform=x64 /p:OutDir="$outDir\"
```

Запуск тестов:

```powershell
.\x64\Release\AntivirusEngineTests.exe
```

Если сборка шла с `OutDir`, запускайте `.\out\AntivirusEngineTests.exe`.

## Локальная сборка installer

1. Восстановить WiX tool manifest:

```powershell
dotnet tool restore
```

2. Установить WiX extensions:

```powershell
dotnet tool run wix extension add -g WixToolset.Util.wixext/5.0.2
dotnet tool run wix extension add -g WixToolset.BootstrapperApplications.wixext/5.0.2
```

3. Собрать installer:

```powershell
msbuild Installer\TrayInstaller.wixproj /m /nologo /verbosity:minimal /t:Build /p:Configuration=Release /p:Platform=x64 /p:ProductVersion=1.0.0
```

Если приложение собрано в нестандартную папку:

```powershell
msbuild Installer\TrayInstaller.wixproj /m /nologo /verbosity:minimal /t:Build /p:Configuration=Release /p:Platform=x64 /p:ProductVersion=1.0.0 /p:AppBuildOutputDir="$outDir"
```

Выходные installer-файлы создаются в `Installer\artifacts\Release\x64\output`.

## Зависимости installer

- Требуется только Microsoft Visual C++ Redistributable x64.
- `TrayApp.exe` и `TrayService.exe` собраны с динамическим runtime (`/MD`), что подтверждается `*_MD.tlog`.
- Qt не используется.
- Windows App SDK не используется.
- .NET runtime для самих `TrayApp.exe` и `TrayService.exe` не требуется.

Bundle устанавливает `vc_redist.x64.exe`, если на машине отсутствует подходящая версия VC++ runtime. MSI не пытается удалять shared/system dependencies вручную.

## Что публикует CI

Workflow [build-msbuild.yml](.github/workflows/build-msbuild.yml) публикует два artifact:

- `ziovpo-antivirus-app-x64` — сырые `TrayApp.exe` и `TrayService.exe`.
- `ziovpo-antivirus-installer-x64` — только installer-файлы: `*.msi` и bundle `*.exe`, если bundle включён.

В installer artifact не попадают `*.pdb`, `*.wixpdb`, тестовые бинарники и прочий build garbage.

## Ручная проверка install/uninstall

Запуск полной проверки installer из elevated PowerShell:

```powershell
powershell -ExecutionPolicy Bypass -File Installer\scripts\verify-install.ps1
```

Проверка конкретного bundle или MSI:

```powershell
powershell -ExecutionPolicy Bypass -File Installer\scripts\verify-install.ps1 -InstallerPath Installer\artifacts\Release\x64\output\ZIOVPOAntivirus-1.0.0-x64-setup.exe
```

`verify-install.ps1` проверяет:

- установку `TrayApp.exe` и `TrayService.exe` в `%ProgramFiles%\ZIOVPO Antivirus`;
- регистрацию службы `TrayService`;
- `StartMode = Auto`;
- `PathName` службы, указывающий на установленный `TrayService.exe`;
- создание `%ProgramData%\TrayApp\Antivirus\default.avdb`;
- uninstall с удалением службы, install directory и `%ProgramData%\TrayApp\Antivirus`.
