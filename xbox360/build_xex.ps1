# Сборка psp360Phoenix в формат XEX (Xbox 360)

# Требуется devkitXenon / libxenon. Проверить наличие:
#   Test-Path $ToolchainBin\powerpc-eabi-g++.exe

param(
    [string]$ToolchainRoot = "",
    [string]$LibxenonRoot  = "",
    [string]$Config        = "Release",
    # Включить расширение VMX128 (-mvmx128). По умолчанию выключено:
    # на момент написания ни один исходник PSP360 VMX128 не использует.
    [switch]$UseVmx128,
    # Путь к инструменту упаковки ELF -> XEX. У LibXenon такого toolchain-
    # инструмента нет: цепочка ELF -> NXE (PE) -> XEX выполняется утилитой
    # xextool. Путь задаётся явно, иначе шаг упаковки честно пропускается.
    [string]$ElfToXex = ""
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path $PSScriptRoot -Parent

# ---- поиск тулчейна ------------------------------------------------------
# Имена инструментов зависят от того, как config.sub канонизировал цель:
# при --target=xenon          -> xenon-as.exe, xenon-gcc.exe
# при --target=powerpc-eabi   -> powerpc-eabi-as.exe, powerpc-eabi-gcc.exe
# Поэтому префикс определяется по фактически установленным файлам, а не
# предполагается. Проверяются оба порядка.
$ToolchainPrefixes = @('xenon-', 'powerpc-eabi-', '')

function Find-Toolchain([string]$root) {
    if (-not $root) { return $null }
    foreach ($p in $ToolchainPrefixes) {
        $as = Join-Path $root "bin\$p${as}.exe"
        if (Test-Path $as) {
            return [pscustomobject]@{ Root = $root; Prefix = $p }
        }
    }
    return $null
}

if (-not $ToolchainRoot) {
    $candidates = @(
        $env:DEVKITXENON,
        $env:DEVKITPRO,
        "D:\libxenon\toolchain",
        "D:\devkitpro\devkitxenon",
        "$ProjectRoot\libxenon-master\toolchain"
    )
    foreach ($c in $candidates) {
        if (-not $c) { continue }
        if (-not (Test-Path $c)) { continue }
        $found = Find-Toolchain $c
        if ($found) {
            $ToolchainRoot = $found.Root
            $ToolchainPrefix = $found.Prefix
            break
        }
    }
}

if (-not $ToolchainRoot) {
    Write-Host "ТУЛЧЕЙН НЕ НАЙДЕН" -ForegroundColor Red
    Write-Host "Ищем ассемблер с префиксами: $($ToolchainPrefixes -join ', ')" -ForegroundColor Yellow
    Write-Host "Укажите префикс вручную: -ToolchainRoot <путь>" -ForegroundColor Yellow
    Write-Host "Подробности: $PSScriptRoot\README.md" -ForegroundColor Yellow
    exit 1
}

# Если путь задан пользователем, префикс всё равно вычисляем.
if (-not $ToolchainPrefix) {
    $found = Find-Toolchain $ToolchainRoot
    if (-not $found) {
        Write-Host "В $ToolchainRoot\bin нет ассемблера с известным префиксом" -ForegroundColor Red
        Get-ChildItem (Join-Path $ToolchainRoot 'bin') -ErrorAction SilentlyContinue |
            Select-Object -First 10 -ExpandProperty Name | ForEach-Object { Write-Host "    $_" }
        exit 1
    }
    $ToolchainPrefix = $found.Prefix
}

$ToolBin = Join-Path $ToolchainRoot 'bin'
function Tool([string]$name) { return (Join-Path $ToolBin "$ToolchainPrefix$name.exe") }

$GCC  = Tool 'gcc'
$GXX  = Tool 'g++'
$AS   = Tool 'as'
$LD   = Tool 'ld'
$OBJCOPY = Tool 'objcopy'
$ELF2XEX = if ($ElfToXex) { $ElfToXex } else { "" }

Write-Host "Тулчейн: $ToolchainRoot (префикс '$ToolchainPrefix')" -ForegroundColor Cyan

# ---- определение каталогов libxenon --------------------------------------
$libPaths = @()
if ($LibxenonRoot) { $libPaths += $LibxenonRoot }
$libPaths += @(
    "$ToolchainRoot\libxenon",
    "$ProjectRoot\libxenon-master\libxenon",
    "D:\libxenon\toolchain\libxenon"
)
$LibDir = ($libPaths | Where-Object { Test-Path $_ } | Select-Object -First 1)
$IncDir = if ($LibDir) { Join-Path $LibDir 'include' } else { '' }

if (-not $LibDir) {
    Write-Host "Каталог libxenon не найден. Ядро соберётся, но линковка с -lxenon невозможна." -ForegroundColor Yellow
}

# ---- исходники ядра ------------------------------------------------------
$OutDir = Join-Path $ProjectRoot "build\xenon\$Config"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$sources = @()
foreach ($dir in @('core','interp','vfpu','Loader')) {
    $sources += Get-ChildItem -Path (Join-Path $ProjectRoot "$dir\*.cpp") -File -ErrorAction SilentlyContinue
}

if ($sources.Count -eq 0) { Write-Error "Исходники не найдены"; exit 1 }
Write-Host "Найдено исходников: $($sources.Count)" -ForegroundColor Cyan

# ---- флаги компиляции ----------------------------------------------------
# Целевая архитектура - Xenon (Gekko), 32-битный PowerPC с ABI powerpc64.
#
# Почему -mcpu=cell, а не -mcpu=750 (проверено по binutils 2.32,
# opcodes/ppc-dis.c, таблица ppc_opts[]):
#   cell = PPC_OPCODE_PPC | _64 | _POWER4 | _CELL | _ALTIVEC
#   750  = PPC_OPCODE_750 (Gekko/Broadway: tsr./tsuspend./tresume., efdcmpeq)
# Маска PPC_OPCODE_CELL НЕ входит в 750. Именно за ней закреплены
# 128-битные варианты load/store из патча VMX128:
#   lvlx128, lvrx128, stvlx128, stvrx128, lvlxl128, stvrxl128, lvrxl128, stvrxl128
# Без -mcpu=cell ассемблер их не распознает.
# Отдельно: опции -m750 в gas не существует (есть 750cl/gekko/broadway),
# то есть прежнее значение было бы отвергнуто ассемблером.
#
# -mpowerpc64 обязателен: тулчейн собирается с этим ABI (--with-cpu=cell),
# и libxenon собран под ним же. Без флага ABI не совпадёт.
# -m32 выбирает 32-битный мультилиб (Xenon - 32-битная система).
$CFLAGS = @(
    '-mcpu=cell', '-mpowerpc64', '-m32', '-O2',
    '-ffreestanding', '-fno-builtin', '-nostdlib',
    '-fno-exceptions', '-fno-rtti',
    "-I$ProjectRoot"
)
# VMX128 включается только явным флагом: -mvmx128 есть в собранном
# binutils (patches/binutils-2.32-vmx128.diff).
if ($UseVmx128) {
    $CFLAGS += '-mvmx128'
    Write-Host "  включён -mvmx128" -ForegroundColor DarkGray
}
if ($IncDir) { $CFLAGS += "-I$IncDir" }

# ---- проверка наличия инструментов ---------------------------------------
foreach ($tool in @(
    @{ Path = $GXX; Name = "${ToolchainPrefix}g++" },
    @{ Path = $AS;  Name = "${ToolchainPrefix}as"  },
    @{ Path = $LD;  Name = "${ToolchainPrefix}ld"  })) {
    if (-not (Test-Path $tool.Path)) {
        Write-Host "ОТСУТСТВУЕТ: $($tool.Name) ($($tool.Path))" -ForegroundColor Red
        exit 1
    }
}

$objs = @()
$failed = 0

foreach ($src in $sources) {
    $obj = Join-Path $OutDir ($src.BaseName + '.o')
    # ВАЖНО: не используем имя $args - это автоматическая переменная
    # PowerShell; присваивание в неё ломает поведение.
    $cmdArgs = @($GXX) + $CFLAGS + @('-c', $src.FullName, '-o', $obj)
    Write-Host "  $($src.Name)" -ForegroundColor DarkGray
    $compileOut = & $cmdArgs 2>&1
    $rc = $LASTEXITCODE
    $compileOut | Where-Object { $_ -match 'error|warning' } | ForEach-Object {
        Write-Host "    $_" -ForegroundColor Yellow
    }
    if ($rc -ne 0) {
        Write-Host "    ОШИБКА компиляции" -ForegroundColor Red
        $failed++
    } else {
        $objs += $obj
    }
}

if ($failed -gt 0) {
    Write-Host "Ошибок компиляции: $failed" -ForegroundColor Red
    exit 2
}

# ---- линковка ------------------------------------------------------------
if ($LibDir) {
    $LDFLAGS = @(
        '-Ttext=0x82000000', '-nostdlib', '-static',
        "-L$LibDir",
        '-lxenon', '-lxenon_xenos', '-lxenon_input', '-lxenon_sound'
    )
    $elf = Join-Path $OutDir 'psp360.elf'
    Write-Host "Линковка..." -ForegroundColor Cyan
    & $LD @($objs) @LDFLAGS -o $elf
    if ($LASTEXITCODE -eq 0 -and (Test-Path $elf)) {
        Write-Host "ELF собран: $elf" -ForegroundColor Green
        # Упаковка в XEX. Инструмент не входит в тулчейн LibXenon, поэтому
        # без явного -ElfToXex шаг пропускается явно, а не молча.
        if ($ELF2XEX -and (Test-Path $ELF2XEX)) {
            $xex = Join-Path $OutDir 'psp360.xex'
            & $ELF2XEX $elf $xex
            if ($LASTEXITCODE -eq 0 -and (Test-Path $xex)) {
                Write-Host "XEX собран: $xex" -ForegroundColor Green
            } else {
                Write-Host "Упаковка в XEX не удалась" -ForegroundColor Red
                exit 4
            }
        } else {
            Write-Host "Шаг ELF -> XEX пропущен." -ForegroundColor Yellow
            Write-Host "  В тулчейне LibXenon нет инструмента xenon-elf-to-xex." -ForegroundColor Yellow
            Write-Host "  Реальная цепочка: ELF -> NXE (PE) -> XEX (утилита xextool)." -ForegroundColor Yellow
            Write-Host "  Укажите его: -ElfToXex <путь к xextool.exe>" -ForegroundColor Yellow
            Write-Host "  Переименованный в .xex ELF не является рабочим XEX." -ForegroundColor DarkYellow
        }
    } else {
        Write-Host "Линковка не удалась" -ForegroundColor Red
        exit 3
    }
} else {
    Write-Host "Объектные файлы собраны в $OutDir (линковка пропущена: нет libxenon)" -ForegroundColor Yellow
}