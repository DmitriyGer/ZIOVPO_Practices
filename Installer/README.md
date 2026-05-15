# Installer

Каталог содержит WiX v5 installer для `TrayApp.exe` и `TrayService.exe`, а также bundle для установки Microsoft Visual C++ Redistributable x64.

## Какие файлы входят в installer

В `x64\Release` и в CI `out\` для runtime используются только:

- `TrayApp.exe`
- `TrayService.exe`

Дополнительно:

- `AntivirusEngineTests.exe` собирается только для тестов и в installer не включается.
- `.pdb`, `.wixpdb`, `.obj`, `.ilk`, `.ipdb`, `.iobj` и прочие build-артефакты исключаются из MSI и CI artifacts.
- Внешних project-built DLL рядом с приложением сейчас нет.
- Внешних `.json`, `.xml`, `.ini`, `.config` рядом с приложением сейчас нет.
- Иконки и Win32-ресурсы встроены в `TrayApp.exe` через `TrayApp.rc`.

## Antivirus database

- Репозиторий не поставляет готовый `default.avdb` как статический installer-файл.
- Служба создаёт и обновляет базу при первом старте в `%ProgramData%\TrayApp\Antivirus`.
- Ожидаемый runtime-файл после успешного запуска службы: `%ProgramData%\TrayApp\Antivirus\default.avdb`.

Это поведение проверяется скриптом `scripts/verify-install.ps1`.

## Зависимости

Найдена только одна обязательная сторонняя runtime-зависимость:

- Microsoft Visual C++ Redistributable x64.

Почему она нужна:

- `TrayApp.exe` и `TrayService.exe` собираются с динамическим runtime `/MD`.
- Это подтверждается файлами `*_MD.tlog` в build output.

Почему остальные зависимости не нужны:

- Qt не используется в проекте.
- Windows App SDK не используется.
- `.NET` runtime не нужен для `TrayApp.exe` и `TrayService.exe`, потому что это native C++ binaries.
- Системные DLL Windows (`KERNEL32.dll`, `USER32.dll`, `ADVAPI32.dll`, `RPCRT4.dll`, `WINHTTP.dll` и другие platform DLL) не должны упаковываться в MSI.

## Файлы installer-инфраструктуры

- `Product.wxs` — MSI authoring: файлы, upgrade policy, регистрация службы, uninstall cleanup.
- `Bundle.wxs` — Burn bundle для MSI и `vc_redist.x64.exe`.
- `TrayInstaller.wixproj` — MSBuild wrapper с очисткой output, staging runtime-файлов и вызовом WiX.
- `scripts/collect-artifacts.ps1` — staging runtime-файлов и загрузка `vc_redist.x64.exe`.
- `scripts/verify-install.ps1` — elevated install/uninstall verification.
- `..\.config\dotnet-tools.json` — tool manifest с зафиксированной версией `wix` `5.0.2`.

## Локальная сборка

1. Собрать приложение:

```powershell
msbuild TrayApp.sln /m /nologo /verbosity:minimal /p:Configuration=Release /p:Platform=x64
```

2. Восстановить WiX tool manifest:

```powershell
dotnet tool restore
```

3. Установить нужные WiX extensions:

```powershell
dotnet tool run wix extension add -g WixToolset.Util.wixext/5.0.2
dotnet tool run wix extension add -g WixToolset.BootstrapperApplications.wixext/5.0.2
```

4. Собрать MSI и bundle:

```powershell
msbuild Installer\TrayInstaller.wixproj /m /nologo /verbosity:minimal /t:Build /p:Configuration=Release /p:Platform=x64 /p:ProductVersion=1.0.0
```

Если приложение собрано в отдельную папку:

```powershell
msbuild Installer\TrayInstaller.wixproj /m /nologo /verbosity:minimal /t:Build /p:Configuration=Release /p:Platform=x64 /p:ProductVersion=1.0.0 /p:AppBuildOutputDir="$outDir"
```

Выходные файлы создаются в `Installer\artifacts\Release\x64\output`.

## Как installer регистрирует службу

MSI использует стандартный WiX mechanism:

- `ServiceInstall` на `TrayService.exe`
- service name: `TrayService`
- display name: `ZIOVPO Antivirus Service`
- `Start="auto"`
- `Account="LocalSystem"`

Так как `ServiceInstall` привязан к установленному `TrayService.exe` внутри `INSTALLFOLDER`, binary path службы указывает на `%ProgramFiles%\ZIOVPO Antivirus\TrayService.exe`.

## Как работает uninstall

- `ServiceControl` останавливает службу при upgrade/uninstall.
- `ServiceControl Remove="uninstall"` удаляет службу из SCM.
- MSI удаляет файлы приложения из `%ProgramFiles%\ZIOVPO Antivirus`.
- `RemoveFolder` удаляет install directory.
- `util:RemoveFolderEx` удаляет `%ProgramData%\TrayApp\Antivirus`.
- `vc_redist.x64.exe` bundle не удаляет принудительно, потому что это shared system dependency и её удаление небезопасно для других приложений.

## Как installer собирается в CI

Workflow [build-msbuild.yml](../.github/workflows/build-msbuild.yml):

- сохраняет текущую сборку `TrayApp`, `TrayService` и `Tests`;
- выполняет `dotnet tool restore` для WiX `5.0.2`;
- собирает `Installer\TrayInstaller.wixproj`;
- требует существование `TrayApp.exe`, `TrayService.exe`, MSI и bundle `*.exe`, если bundle включён;
- выводит список installer outputs отдельным PowerShell шагом;
- публикует отдельный installer artifact без build garbage;
- падает, если installer не собрался или отсутствует обязательный output.

## Какие artifacts публикует CI

- `ziovpo-antivirus-app-x64`:
  - `TrayApp.exe`
  - `TrayService.exe`
- `ziovpo-antivirus-installer-x64`:
  - `ZIOVPOAntivirus-<version>-x64.msi`
  - `ZIOVPOAntivirus-<version>-x64-setup.exe`, если bundle включён

`*.pdb`, `*.wixpdb`, тестовые бинарники и прочий мусор в installer artifact не публикуются.

## Ручная проверка install/uninstall

Запускать из elevated PowerShell:

```powershell
powershell -ExecutionPolicy Bypass -File Installer\scripts\verify-install.ps1
```

Явный путь до installer:

```powershell
powershell -ExecutionPolicy Bypass -File Installer\scripts\verify-install.ps1 -InstallerPath Installer\artifacts\Release\x64\output\ZIOVPOAntivirus-1.0.0-x64-setup.exe
```

Скрипт проверяет:

- установку `TrayApp.exe` и `TrayService.exe`;
- наличие и запуск службы `TrayService`;
- `StartMode = Auto`;
- корректный `PathName` службы;
- создание `%ProgramData%\TrayApp\Antivirus\default.avdb`;
- uninstall с удалением службы, install directory и `%ProgramData%\TrayApp\Antivirus`.
