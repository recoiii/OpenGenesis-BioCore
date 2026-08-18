[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$SourceArchive,

    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[0-9a-fA-F]{64}$')]
    [string]$ExpectedSourceSha256,

    [string]$VcpkgRoot = $env:VCPKG_ROOT,

    [string]$EvidenceDirectory = "artifacts/windows-final-closure"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$PSNativeCommandUseErrorActionPreference = $false

function Assert-True {
    param(
        [Parameter(Mandatory = $true)]
        [bool]$Condition,
        [Parameter(Mandatory = $true)]
        [string]$Message
    )
    if (-not $Condition) {
        throw $Message
    }
}

function Invoke-NativeCapture {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Executable,
        [AllowEmptyCollection()]
        [string[]]$Arguments = @()
    )

    # Windows PowerShell 5.1 turns native stderr redirected through 2>&1 into
    # non-terminating ErrorRecord objects. With the script-wide Stop policy, a
    # harmless CMake developer warning can otherwise abort an exit-0 command.
    # Native process success is therefore decided only by LASTEXITCODE.
    $previousErrorActionPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $output = @(& $Executable @Arguments 2>&1 | ForEach-Object { $_.ToString() })
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }

    return [pscustomobject]@{
        ExitCode = $exitCode
        Output = @($output)
    }
}

function Invoke-LoggedCommand {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name,
        [Parameter(Mandatory = $true)]
        [string]$Executable,
        [Parameter(Mandatory = $true)]
        [string[]]$Arguments
    )

    $logPath = Join-Path $script:EvidenceRoot ("{0}.log" -f $Name)
    Write-Host ("> {0} {1}" -f $Executable, ($Arguments -join ' '))
    $result = Invoke-NativeCapture -Executable $Executable -Arguments $Arguments
    @($result.Output) | Tee-Object -FilePath $logPath
    if ([int]$result.ExitCode -ne 0) {
        throw ("Command failed ({0}) with exit code {1}. See {2}" -f $Name, $result.ExitCode, $logPath)
    }
}

function Invoke-Capture {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Executable,
        [Parameter(Mandatory = $true)]
        [string[]]$Arguments
    )
    $result = Invoke-NativeCapture -Executable $Executable -Arguments $Arguments
    if ([int]$result.ExitCode -ne 0) {
        throw ("Command failed: {0} {1}`n{2}" -f $Executable, ($Arguments -join ' '), ($result.Output -join "`n"))
    }
    return ($result.Output -join "`n").Trim()
}

function Resolve-VcpkgContext {
    param([string]$RequestedRoot)

    $candidateRoots = @()
    if (-not [string]::IsNullOrWhiteSpace($RequestedRoot)) {
        $candidateRoots += $RequestedRoot
    }
    if (-not [string]::IsNullOrWhiteSpace($env:VCPKG_ROOT)) {
        $candidateRoots += $env:VCPKG_ROOT
    }
    if (-not [string]::IsNullOrWhiteSpace($env:CMAKE_TOOLCHAIN_FILE)) {
        $toolchainCandidate = [System.IO.Path]::GetFullPath($env:CMAKE_TOOLCHAIN_FILE)
        if ((Split-Path -Leaf $toolchainCandidate) -ieq 'vcpkg.cmake') {
            $candidateRoots += (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $toolchainCandidate)))
        }
    }
    $vcpkgCommand = Get-Command 'vcpkg.exe' -ErrorAction SilentlyContinue
    if ($null -ne $vcpkgCommand) {
        $candidateRoots += (Split-Path -Parent $vcpkgCommand.Source)
    }

    foreach ($candidate in @($candidateRoots | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Select-Object -Unique)) {
        $root = [System.IO.Path]::GetFullPath($candidate)
        $vcpkgExe = Join-Path $root 'vcpkg.exe'
        $toolchain = Join-Path $root 'scripts\buildsystems\vcpkg.cmake'
        if ((Test-Path -LiteralPath $vcpkgExe -PathType Leaf) -and (Test-Path -LiteralPath $toolchain -PathType Leaf)) {
            return [pscustomobject]@{
                Root = $root
                Executable = $vcpkgExe
                Toolchain = $toolchain
            }
        }
    }

    throw 'A valid vcpkg root is required. Pass -VcpkgRoot <path>, set VCPKG_ROOT, set CMAKE_TOOLCHAIN_FILE to vcpkg.cmake, or put vcpkg.exe on PATH.'
}

function Assert-ExactCTestCount {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Preset,
        [Parameter(Mandatory = $true)]
        [int]$ExpectedCount
    )
    $result = Invoke-NativeCapture -Executable 'ctest' -Arguments @('--preset', $Preset, '-N')
    if ([int]$result.ExitCode -ne 0) {
        throw ("ctest -N failed for preset {0}`n{1}" -f $Preset, ($result.Output -join "`n"))
    }
    $listing = @($result.Output)
    $line = $listing | Where-Object { $_ -match '^\s*Total Tests:\s+(\d+)\s*$' } | Select-Object -Last 1
    Assert-True ($null -ne $line) "Could not determine CTest count for $Preset"
    [int]$count = [regex]::Match($line, '(\d+)').Groups[1].Value
    Assert-True ($count -eq $ExpectedCount) "Expected $ExpectedCount CTests for $Preset but found $count"
}

function Get-InstalledPluginRecords {
    param([Parameter(Mandatory = $true)][string]$InstallRoot)

    $pluginRoot = Join-Path $InstallRoot 'share\biocore\plugins'
    Assert-True (Test-Path -LiteralPath $pluginRoot -PathType Container) "Installed plugin root is missing: $pluginRoot"

    $records = @()
    foreach ($manifestPath in Get-ChildItem -LiteralPath $pluginRoot -Filter plugin.json -File -Recurse) {
        $manifest = Get-Content -LiteralPath $manifestPath.FullName -Raw | ConvertFrom-Json
        $pluginDirectory = Split-Path -Parent $manifestPath.FullName
        $entrypoints = @()
        foreach ($module in @($manifest.modules)) {
            $windowsPath = $module.entrypoints.'windows-x64'
            Assert-True (-not [string]::IsNullOrWhiteSpace($windowsPath)) "Plugin $($manifest.id) module $($module.id) has no windows-x64 entrypoint"
            $entrypoints += [string]$windowsPath
        }
        $records += [pscustomobject]@{
            Id = [string]$manifest.id
            Directory = $pluginDirectory
            Entrypoints = @($entrypoints | Sort-Object -Unique)
        }
    }
    return @($records | Sort-Object Id)
}

function Test-InstalledLayout {
    param([Parameter(Mandatory = $true)][string]$InstallRoot)

    $biocore = Join-Path $InstallRoot 'bin\biocore.exe'
    $worker = Join-Path $InstallRoot 'bin\biocore-worker.exe'
    Assert-True (Test-Path -LiteralPath $biocore -PathType Leaf) "Missing installed biocore.exe"
    Assert-True (Test-Path -LiteralPath $worker -PathType Leaf) "Missing installed biocore-worker.exe"

    $version = Invoke-Capture $biocore @('--version')
    Assert-True ($version -eq '0.1.0') "Expected exact installed version 0.1.0, got '$version'"
    $health = Invoke-Capture $biocore @('--health') | ConvertFrom-Json
    Assert-True ($health.status -eq 'healthy') "Installed Core health is not healthy"
    Assert-True ($health.version -eq '0.1.0') "Installed Core health version is not 0.1.0"
    $workerHealth = Invoke-Capture $worker @('--self-test') | ConvertFrom-Json
    Assert-True ([int]$workerHealth.protocolVersion -eq 2) "Installed worker protocol is not 2"

    $expectedPluginIds = @(
        'org.biocore.demo',
        'org.biocore.fastaqc',
        'org.biocore.fastqqc',
        'org.biocore.align',
        'org.biocore.alignmentqc',
        'org.biocore.variantcall',
        'org.biocore.vcfqc',
        'org.biocore.variantannotate'
    )
    $plugins = @(Get-InstalledPluginRecords -InstallRoot $InstallRoot)
    $actualIds = @($plugins | ForEach-Object { $_.Id })
    Assert-True ($plugins.Count -eq $expectedPluginIds.Count) "Expected 8 installed native plugins, found $($plugins.Count)"
    foreach ($pluginId in $expectedPluginIds) {
        Assert-True ($actualIds -contains $pluginId) "Missing installed plugin: $pluginId"
    }

    $coreRuntimeDlls = @(Get-ChildItem -LiteralPath (Join-Path $InstallRoot 'bin') -File |
        Where-Object { $_.Name -match '^(vcruntime|msvcp|concrt|vccorlib).*\.dll$' } |
        Select-Object -ExpandProperty Name |
        Sort-Object -Unique)
    Assert-True (($coreRuntimeDlls | Where-Object { $_ -match '^vcruntime.*\.dll$' }).Count -gt 0) "No app-local vcruntime DLL found beside Core"
    Assert-True (($coreRuntimeDlls | Where-Object { $_ -match '^msvcp.*\.dll$' }).Count -gt 0) "No app-local msvcp DLL found beside Core"

    foreach ($plugin in $plugins) {
        foreach ($relativeEntrypoint in $plugin.Entrypoints) {
            $entrypoint = Join-Path $plugin.Directory ($relativeEntrypoint -replace '/', '\\')
            Assert-True (Test-Path -LiteralPath $entrypoint -PathType Leaf) "Missing plugin entrypoint: $entrypoint"
            $entrypointDirectory = Split-Path -Parent $entrypoint
            foreach ($runtimeDll in $coreRuntimeDlls) {
                Assert-True (Test-Path -LiteralPath (Join-Path $entrypointDirectory $runtimeDll) -PathType Leaf) "Plugin $($plugin.Id) is missing app-local runtime $runtimeDll"
            }
            $pluginResult = Invoke-NativeCapture -Executable $entrypoint -Arguments @()
            $pluginExitCode = [int]$pluginResult.ExitCode
            Assert-True ($pluginExitCode -eq 2) "Plugin $($plugin.Id) no-argument contract expected exit 2, got $pluginExitCode"
        }
    }

    $pipelineRoot = Join-Path $InstallRoot 'share\biocore\pipelines'
    $frontendRoot = Join-Path $InstallRoot 'share\biocore\frontend'
    Assert-True (Test-Path -LiteralPath $pipelineRoot -PathType Container) "Installed pipeline directory is missing"
    Assert-True (Test-Path -LiteralPath $frontendRoot -PathType Container) "Installed frontend directory is missing"
    Assert-True ((Get-ChildItem -LiteralPath $pipelineRoot -Filter '*.json' -File).Count -ge 11) "Expected the frozen analysis pipeline suite in the install tree"
    Assert-True (Test-Path -LiteralPath (Join-Path $frontendRoot 'index.html') -PathType Leaf) "Installed frontend index.html is missing"

    return [pscustomobject]@{
        Version = $version
        PluginCount = $plugins.Count
        RuntimeDlls = $coreRuntimeDlls
        PipelineCount = (Get-ChildItem -LiteralPath $pipelineRoot -Filter '*.json' -File).Count
    }
}

function Assert-NoSystemDirectoryPayload {
    param([Parameter(Mandatory = $true)][string]$Root)
    $bad = @(Get-ChildItem -LiteralPath $Root -Recurse -File | Where-Object {
        $_.FullName -match '(?i)[\\/](System32|SysWOW64)[\\/]'
    })
    Assert-True ($bad.Count -eq 0) "Package/install unexpectedly contains Windows System32/SysWOW64 payload paths"
}

if ($env:OS -ne 'Windows_NT') {
    throw 'This script must run natively on Windows.'
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Set-Location $repoRoot

$archivePath = (Resolve-Path -LiteralPath $SourceArchive).Path
$actualSourceSha = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
$expectedSourceSha = $ExpectedSourceSha256.ToLowerInvariant()
Assert-True ($actualSourceSha -eq $expectedSourceSha) "Source archive SHA-256 mismatch. Expected $expectedSourceSha, got $actualSourceSha"

$script:EvidenceRoot = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $EvidenceDirectory))
if (Test-Path -LiteralPath $script:EvidenceRoot) {
    Remove-Item -LiteralPath $script:EvidenceRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $script:EvidenceRoot -Force | Out-Null

$transcriptPath = Join-Path $script:EvidenceRoot 'windows-final-closure-transcript.log'
Start-Transcript -LiteralPath $transcriptPath -Force | Out-Null
try {
    Write-Host "OpenGenesis-BioCore v0.1.0 native Windows final closure"
    Write-Host "Source archive: $archivePath"
    Write-Host "Source SHA-256: $actualSourceSha"

    foreach ($tool in @('cmake', 'ctest', 'cpack')) {
        Assert-True ($null -ne (Get-Command $tool -ErrorAction SilentlyContinue)) "Required tool is missing from PATH: $tool"
    }

    $vcpkg = Resolve-VcpkgContext -RequestedRoot $VcpkgRoot
    Write-Host "vcpkg root: $($vcpkg.Root)"
    Write-Host "vcpkg toolchain: $($vcpkg.Toolchain)"
    Invoke-LoggedCommand '00-vcpkg-dependencies' $vcpkg.Executable @('install', 'sqlite3:x64-windows', 'drogon:x64-windows', 'zlib:x64-windows', '--disable-metrics')

    foreach ($buildDirectory in @('build\windows-msvc-debug', 'build\windows-msvc-release')) {
        $absoluteBuildDirectory = Join-Path $repoRoot $buildDirectory
        if (Test-Path -LiteralPath $absoluteBuildDirectory) {
            Remove-Item -LiteralPath $absoluteBuildDirectory -Recurse -Force
        }
    }

    $toolchainArgument = "-DCMAKE_TOOLCHAIN_FILE=$($vcpkg.Toolchain)"
    $tripletArgument = '-DVCPKG_TARGET_TRIPLET=x64-windows'
    # Final closure builds an immutable, SHA-256-sealed source tree. Disable the
    # Visual Studio regeneration target so parallel MSBuild cannot race multiple
    # configure/CPack regeneration passes against the same build tree.
    $suppressRegenerationArgument = '-DCMAKE_SUPPRESS_REGENERATION=ON'

    Invoke-LoggedCommand '01-configure-debug' 'cmake' @('--preset', 'windows-msvc-debug', $toolchainArgument, $tripletArgument, $suppressRegenerationArgument)
    Invoke-LoggedCommand '02-build-debug' 'cmake' @('--build', '--preset', 'windows-msvc-debug', '--parallel')
    Assert-ExactCTestCount 'windows-msvc-debug' 67
    Invoke-LoggedCommand '03-ctest-debug' 'ctest' @('--preset', 'windows-msvc-debug')

    Invoke-LoggedCommand '04-configure-release' 'cmake' @('--preset', 'windows-msvc-release', $toolchainArgument, $tripletArgument, $suppressRegenerationArgument)
    Invoke-LoggedCommand '05-build-release' 'cmake' @('--build', '--preset', 'windows-msvc-release', '--parallel')
    Assert-ExactCTestCount 'windows-msvc-release' 67
    Invoke-LoggedCommand '06-ctest-release' 'ctest' @('--preset', 'windows-msvc-release')

    $installRoot = Join-Path $script:EvidenceRoot 'installed-release'
    Invoke-LoggedCommand '07-install-release' 'cmake' @('--install', 'build/windows-msvc-release', '--config', 'Release', '--prefix', $installRoot)
    $installLayout = Test-InstalledLayout -InstallRoot $installRoot
    Assert-NoSystemDirectoryPayload -Root $installRoot

    $initRoot = Join-Path $script:EvidenceRoot 'init-project-smoke'
    $catalogRoot = Join-Path $script:EvidenceRoot 'catalog'
    New-Item -ItemType Directory -Path $catalogRoot -Force | Out-Null
    $catalogPath = Join-Path $catalogRoot 'catalog.sqlite'
    $installedBiocore = Join-Path $installRoot 'bin\biocore.exe'
    Invoke-LoggedCommand '08-init-project-smoke' $installedBiocore @('--init-project', $initRoot, '--name', 'OpenGenesis-BioCore Final Closure', '--catalog', $catalogPath)
    Assert-True (Test-Path -LiteralPath $initRoot -PathType Container) 'Init-project smoke did not create the project directory'

    $releaseBuild = Join-Path $repoRoot 'build\windows-msvc-release'
    $cpackOutput = Join-Path $script:EvidenceRoot 'cpack-output'
    New-Item -ItemType Directory -Path $cpackOutput -Force | Out-Null
    Invoke-LoggedCommand '09-cpack-release' 'cpack' @('-G', 'ZIP', '-C', 'Release', '-B', $cpackOutput, '--config', (Join-Path $releaseBuild 'CPackConfig.cmake'))

    $packages = @(Get-ChildItem -LiteralPath $cpackOutput -Filter 'OpenGenesis-BioCore-0.1.0-windows-x64*.zip' -File)
    Assert-True ($packages.Count -eq 1) "Expected exactly one OpenGenesis-BioCore-0.1.0-windows-x64 ZIP, found $($packages.Count)"
    $package = $packages[0]
    $packageSha = (Get-FileHash -LiteralPath $package.FullName -Algorithm SHA256).Hash.ToLowerInvariant()

    $distRoot = Join-Path $script:EvidenceRoot 'dist'
    New-Item -ItemType Directory -Path $distRoot -Force | Out-Null
    $distPackage = Join-Path $distRoot $package.Name
    Copy-Item -LiteralPath $package.FullName -Destination $distPackage -Force

    $extractParent = Join-Path $script:EvidenceRoot 'extracted'
    New-Item -ItemType Directory -Path $extractParent -Force | Out-Null
    Expand-Archive -LiteralPath $distPackage -DestinationPath $extractParent -Force
    $topLevelDirectories = @(Get-ChildItem -LiteralPath $extractParent -Directory)
    Assert-True ($topLevelDirectories.Count -eq 1) "Expected one top-level directory in CPack ZIP, found $($topLevelDirectories.Count)"
    $extractedRoot = $topLevelDirectories[0].FullName
    $extractedLayout = Test-InstalledLayout -InstallRoot $extractedRoot
    Assert-NoSystemDirectoryPayload -Root $extractedRoot

    $extractedInitRoot = Join-Path $script:EvidenceRoot 'extracted-init-project-smoke'
    $extractedCatalogRoot = Join-Path $script:EvidenceRoot 'extracted-catalog'
    New-Item -ItemType Directory -Path $extractedCatalogRoot -Force | Out-Null
    $extractedCatalogPath = Join-Path $extractedCatalogRoot 'catalog.sqlite'
    Invoke-LoggedCommand '10-extracted-init-project-smoke' (Join-Path $extractedRoot 'bin\biocore.exe') @('--init-project', $extractedInitRoot, '--name', 'OpenGenesis-BioCore Extracted Final Closure', '--catalog', $extractedCatalogPath)

    $systemDirPayloadNames = @(Get-ChildItem -LiteralPath $extractedRoot -Recurse -File | Where-Object {
        $_.Name -match '^(?i)(kernel32|user32|advapi32|shell32|ole32|ws2_32|bcrypt|ntdll)\.dll$'
    } | Select-Object -ExpandProperty FullName)
    Assert-True ($systemDirPayloadNames.Count -eq 0) 'Portable ZIP unexpectedly bundles core Windows system DLLs'

    $summary = [ordered]@{
        schemaVersion = 1
        release = '0.1.0'
        sourceArchive = [System.IO.Path]::GetFileName($archivePath)
        sourceSha256 = $actualSourceSha
        os = [System.Environment]::OSVersion.VersionString
        powershell = $PSVersionTable.PSVersion.ToString()
        cmake = (Invoke-Capture 'cmake' @('--version')).Split("`n")[0]
        vcpkgRoot = $vcpkg.Root
        vcpkgVersion = (Invoke-Capture $vcpkg.Executable @('version')).Split("`n")[0]
        vcpkgTriplet = 'x64-windows'
        debugCTestCount = 67
        debugCTestResult = 'PASS'
        releaseCTestCount = 67
        releaseCTestResult = 'PASS'
        installedVersion = $installLayout.Version
        installedPluginCount = $installLayout.PluginCount
        installedPipelineCount = $installLayout.PipelineCount
        appLocalMsvcRuntimeDlls = @($installLayout.RuntimeDlls)
        cpackFile = $package.Name
        cpackSha256 = $packageSha
        extractedVersion = $extractedLayout.Version
        extractedPluginCount = $extractedLayout.PluginCount
        extractedPipelineCount = $extractedLayout.PipelineCount
        systemDirectoryPayload = 'NONE'
        result = 'PASS'
    }
    $summary | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $script:EvidenceRoot 'windows-final-closure-summary.json') -Encoding utf8

    @(
        "$actualSourceSha  $([System.IO.Path]::GetFileName($archivePath))",
        "$packageSha  $($package.Name)"
    ) | Set-Content -LiteralPath (Join-Path $script:EvidenceRoot 'SHA256SUMS.txt') -Encoding ascii

    Write-Host ""
    Write-Host "BIOCORE_WINDOWS_FINAL_CLOSURE=PASS"
    Write-Host "SOURCE_SHA256=$actualSourceSha"
    Write-Host "PACKAGE=$($package.Name)"
    Write-Host "PACKAGE_SHA256=$packageSha"
    Write-Host "EVIDENCE=$script:EvidenceRoot"
}
finally {
    Stop-Transcript | Out-Null
}

$summaryPath = Join-Path $script:EvidenceRoot 'windows-final-closure-summary.json'
if (Test-Path -LiteralPath $summaryPath -PathType Leaf) {
    $bundleParent = Split-Path -Parent $script:EvidenceRoot
    $bundlePath = Join-Path $bundleParent 'OpenGenesis-BioCore-iteration-044-windows-evidence.zip'
    if (Test-Path -LiteralPath $bundlePath) {
        Remove-Item -LiteralPath $bundlePath -Force
    }
    $bundleFiles = @(Get-ChildItem -LiteralPath $script:EvidenceRoot -File | Where-Object {
        $_.Extension -in @('.log', '.json', '.txt')
    })
    Assert-True ($bundleFiles.Count -gt 0) 'No final Windows evidence files were found to bundle'
    Compress-Archive -LiteralPath @($bundleFiles.FullName) -DestinationPath $bundlePath -CompressionLevel Optimal
    Write-Host "EVIDENCE_BUNDLE=$bundlePath"
}
