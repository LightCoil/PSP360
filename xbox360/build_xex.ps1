# Сборка psp360Phoenix в формат XEX (Xbox 360)

# Требуется devkitXenon / libxenon. Проверить наличие:
#   Test-Path $ToolchainBin\powerpc-eabi-g++.exe

param(
    [string]$ToolchainRoot = "",
    [string]$LibxenonRoot  = "",
    [string]$Config        = "Release"
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path $PSScriptRoot -Parent

# ---- поиск тулчейна ------------------------------------------------------
if (-not $ToolchainRoot) {
    $candidates = @(
        $env:DEVKITXENON,
        $env:DEVKITPRO,
        "D:\libxenon\toolchain",
        "D:\devkitpro\devkitxenon",
        "$ProjectRoot\libxenon-master\toolchain"
    )
    foreach ($c in $candidates) {
        if ($c -and (Test-Path (Join-Path $c 'bin\powerpc-eabi-g++.exe'))) {
            $ToolchainRoot = $c; break
        }
    }
}

if (-not $ToolchainRoot) {
    Write-Host "ТУЛЧЕЙН НЕ НАЙДЕН" -ForegroundColor Red
    Write-Host "Установите devkitXenon или укажите -ToolchainRoot." -ForegroundColor Yellow
    Write-Host "Подробности: $PSScriptRoot\README.md" -ForegroundColor Yellow
    exit 1
}

$GCC = Join-Path $ToolchainRoot 'bin\powerpc-eabi-gcc.exe'
$GXX = Join-Path $ToolchainRoot 'bin\powerpc-eabi-g++.exe'
$AS  = Join-Path $ToolchainRoot 'bin\powerpc-eabi-as.exe'
$LD  = Join-Path $ToolchainRoot 'bin\powerpc-eabi-ld.exe'
$OBJCOPY = Join-Path $ToolchainRoot 'bin\powerpc-eabi-objcopy.exe'
$ELF2XEX = Join-Path $ToolchainRoot 'bin\xenon-elf-to-xex.exe'

Write-Host "Тулчейн: $ToolchainRoot" -ForegroundColor Cyan

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
# Xenon: PowerPC 750 с расширением VMX128, 32-бит, без ОС
$CFLAGS = @(
    '-mcpu=750', '-m32', '-O2',
    '-ffreestanding', '-fno-builtin', '-nostdlib',
    '-fno-exceptions', '-fno-rtti',
    "-I$ProjectRoot"
)
if ($IncDir) { $CFLAGS += "-I$IncDir" }

$objs = @()
$failed = 0

foreach ($src in $sources) {
    $obj = Join-Path $OutDir ($src.BaseName + '.o')
    $args = @($GXX) + $CFLAGS + @('-c', $src.FullName, '-o', $obj)
    Write-Host "  $($src.Name)" -ForegroundColor DarkGray
    & $args 2>&1 | Where-Object { $_ -match 'error|warning' } | ForEach-Object {
        Write-Host "    $_" -ForegroundColor Yellow
    }
    if ($LASTEXITCODE -ne 0) {
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
        $xex = Join-Path $OutDir 'psp360.xex'
        & $ELF2XEX $elf $xex
        Write-Host "XEX собран: $xex" -ForegroundColor Green
    } else {
        Write-Host "Линковка не удалась" -ForegroundColor Red
        exit 3
    }
} else {
    Write-Host "Объектные файлы собраны в $OutDir (линковка пропущена: нет libxenon)" -ForegroundColor Yellow
}