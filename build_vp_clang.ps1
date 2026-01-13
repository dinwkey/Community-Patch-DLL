# Build VP with Clang - PowerShell version
# Equivalent to build_vp_clang.py
param(
    [Parameter(Position=0)]
    [ValidateSet('debug', 'release')]
    [string]$Config = 'debug'
)

$ErrorActionPreference = 'Stop'

# Configuration
$VS_2008_VARS_BAT = Join-Path $env:VS90COMNTOOLS 'vsvars32.bat'
$CORE_DLL = 'CvGameCore_Expansion2'
$PROJECT_DIR = $PSScriptRoot

if ($Config -eq 'release') {
    $BUILD_DIR = Join-Path $PROJECT_DIR 'clang-build\Release'
    $OUT_DIR = Join-Path $PROJECT_DIR 'clang-output\Release'
} else {
    $BUILD_DIR = Join-Path $PROJECT_DIR 'clang-build\Debug'
    $OUT_DIR = Join-Path $PROJECT_DIR 'clang-output\Debug'
}

# Libraries and dependencies
$LIBS = @(
    'CvWorldBuilderMap\lib\CvWorldBuilderMapWin32.obj',
    'CvGameCoreDLLUtil\lib\CvGameCoreDLLUtilWin32.lib',
    'CvLocalization\lib\CvLocalizationWin32.lib',
    'CvGameDatabase\lib\CvGameDatabaseWin32.lib',
    'FirePlace\lib\FireWorksWin32.obj',
    'FirePlace\lib\FLuaWin32.lib',
    'ThirdPartyLibs\Lua51\lib\lua51_Win32.lib'
)

$DEFAULT_LIBS = @(
    'winmm.lib', 'kernel32.lib', 'user32.lib', 'gdi32.lib',
    'winspool.lib', 'comdlg32.lib', 'advapi32.lib', 'shell32.lib',
    'ole32.lib', 'oleaut32.lib', 'uuid.lib', 'odbc32.lib', 'odbccp32.lib'
)

$DEF_FILE = 'CvGameCoreDLL_Expansion2\CvGameCoreDLL.def'

$INCLUDE_DIRS = @(
    'CvGameCoreDLL_Expansion2',
    'CvWorldBuilderMap\include',
    'CvGameCoreDLLUtil\include',
    'CvLocalization\include',
    'CvGameDatabase\include',
    'FirePlace\include',
    'FirePlace\include\FireWorks',
    'ThirdPartyLibs\Lua51\include'
)

$SHARED_PREDEFS = @(
    'FXS_IS_DLL', 'WIN32', '_WINDOWS', '_USRDLL',
    'EXTERNAL_PAUSING', 'CVGAMECOREDLL_EXPORTS',
    'FINAL_RELEASE', '_CRT_SECURE_NO_WARNINGS', '_WINDLL'
)

if ($Config -eq 'release') {
    $PREDEFS = $SHARED_PREDEFS + @('STRONG_ASSUMPTIONS', 'NDEBUG', 'VPRELEASE_ERRORMSG')
} else {
    $PREDEFS = $SHARED_PREDEFS + @('VPDEBUG')
}

$CL_SUPPRESS = @(
    'invalid-offsetof',
    'tautological-constant-out-of-range-compare',
    'comment',
    'enum-constexpr-conversion'
)

$PCH_CPP = 'CvGameCoreDLL_Expansion2\_precompile.cpp'
$PCH_H = 'CvGameCoreDLLPCH.h'
$PCH = 'CvGameCoreDLLPCH.pch'

# All CPP files to compile
$CPP = @(
    'CvGameCoreDLL_Expansion2\Lua\CvLuaArea.cpp',
    'CvGameCoreDLL_Expansion2\Lua\CvLuaArgsHandle.cpp',
    'CvGameCoreDLL_Expansion2\Lua\CvLuaCity.cpp',
    'CvGameCoreDLL_Expansion2\Lua\CvLuaDeal.cpp',
    'CvGameCoreDLL_Expansion2\Lua\CvLuaEnums.cpp',
    'CvGameCoreDLL_Expansion2\Lua\CvLuaFractal.cpp',
    'CvGameCoreDLL_Expansion2\Lua\CvLuaGame.cpp',
    'CvGameCoreDLL_Expansion2\Lua\CvLuaGameInfo.cpp',
    'CvGameCoreDLL_Expansion2\Lua\CvLuaLeague.cpp',
    'CvGameCoreDLL_Expansion2\Lua\CvLuaMap.cpp',
    'CvGameCoreDLL_Expansion2\Lua\CvLuaPlayer.cpp',
    'CvGameCoreDLL_Expansion2\Lua\CvLuaPlot.cpp',
    'CvGameCoreDLL_Expansion2\Lua\CvLuaSupport.cpp',
    'CvGameCoreDLL_Expansion2\Lua\CvLuaTeam.cpp',
    'CvGameCoreDLL_Expansion2\Lua\CvLuaTeamTech.cpp',
    'CvGameCoreDLL_Expansion2\Lua\CvLuaUnit.cpp',
    'CvGameCoreDLL_Expansion2\CustomMods.cpp',
    'CvGameCoreDLL_Expansion2\CvAchievementInfo.cpp',
    'CvGameCoreDLL_Expansion2\CvAchievementUnlocker.cpp',
    'CvGameCoreDLL_Expansion2\CvAdvisorCounsel.cpp',
    'CvGameCoreDLL_Expansion2\CvAdvisorRecommender.cpp',
    'CvGameCoreDLL_Expansion2\CvAIOperation.cpp',
    'CvGameCoreDLL_Expansion2\CvArea.cpp',
    'CvGameCoreDLL_Expansion2\CvArmyAI.cpp',
    'CvGameCoreDLL_Expansion2\CvAStar.cpp',
    'CvGameCoreDLL_Expansion2\CvAStarNode.cpp',
    'CvGameCoreDLL_Expansion2\CvBarbarians.cpp',
    'CvGameCoreDLL_Expansion2\CvBeliefClasses.cpp',
    'CvGameCoreDLL_Expansion2\CvBuilderTaskingAI.cpp',
    'CvGameCoreDLL_Expansion2\CvBuildingClasses.cpp',
    'CvGameCoreDLL_Expansion2\CvBuildingProductionAI.cpp',
    'CvGameCoreDLL_Expansion2\CvCity.cpp',
    'CvGameCoreDLL_Expansion2\CvCityAI.cpp',
    'CvGameCoreDLL_Expansion2\CvCityCitizens.cpp',
    'CvGameCoreDLL_Expansion2\CvCityConnections.cpp',
    'CvGameCoreDLL_Expansion2\CvCityManager.cpp',
    'CvGameCoreDLL_Expansion2\CvCitySpecializationAI.cpp',
    'CvGameCoreDLL_Expansion2\CvCityStrategyAI.cpp',
    'CvGameCoreDLL_Expansion2\CvContractClasses.cpp',
    'CvGameCoreDLL_Expansion2\CvCorporationClasses.cpp',
    'CvGameCoreDLL_Expansion2\CvCultureClasses.cpp',
    'CvGameCoreDLL_Expansion2\CvDangerPlots.cpp',
    'CvGameCoreDLL_Expansion2\CvDatabaseUtility.cpp',
    'CvGameCoreDLL_Expansion2\CvDealAI.cpp',
    'CvGameCoreDLL_Expansion2\CvDealClasses.cpp',
    'CvGameCoreDLL_Expansion2\CvDiplomacyAI.cpp',
    'CvGameCoreDLL_Expansion2\CvDiplomacyRequests.cpp',
    'CvGameCoreDLL_Expansion2\CvDistanceMap.cpp',
    'CvGameCoreDLL_Expansion2\CvDllBuildInfo.cpp',
    'CvGameCoreDLL_Expansion2\CvDllBuildingInfo.cpp',
    'CvGameCoreDLL_Expansion2\CvDllCity.cpp',
    'CvGameCoreDLL_Expansion2\CvDllCivilizationInfo.cpp',
    'CvGameCoreDLL_Expansion2\CvDllColorInfo.cpp',
    'CvGameCoreDLL_Expansion2\CvDllCombatInfo.cpp',
    'CvGameCoreDLL_Expansion2\CvDllContext.cpp',
    'CvGameCoreDLL_Expansion2\CvDllDatabaseUtility.cpp',
    'CvGameCoreDLL_Expansion2\CvDllDeal.cpp',
    'CvGameCoreDLL_Expansion2\CvDllDealAI.cpp',
    'CvGameCoreDLL_Expansion2\CvDllDiplomacyAI.cpp',
    'CvGameCoreDLL_Expansion2\CvDllDlcPackageInfo.cpp',
    'CvGameCoreDLL_Expansion2\CvDllEraInfo.cpp',
    'CvGameCoreDLL_Expansion2\CvDllFeatureInfo.cpp',
    'CvGameCoreDLL_Expansion2\CvDllGame.cpp',
    'CvGameCoreDLL_Expansion2\CvDllGameAsynch.cpp',
    'CvGameCoreDLL_Expansion2\CvDllGameDeals.cpp',
    'CvGameCoreDLL_Expansion2\CvDllGameOptionInfo.cpp',
    'CvGameCoreDLL_Expansion2\CvDllGameSpeedInfo.cpp',
    'CvGameCoreDLL_Expansion2\CvDllHandicapInfo.cpp',
    'CvGameCoreDLL_Expansion2\CvDllImprovementInfo.cpp',
    'CvGameCoreDLL_Expansion2\CvDllInterfaceModeInfo.cpp',
    'CvGameCoreDLL_Expansion2\CvDllLeaderheadInfo.cpp',
    'CvGameCoreDLL_Expansion2\CvDllMap.cpp',
    'CvGameCoreDLL_Expansion2\CvDllMinorCivInfo.cpp',
    'CvGameCoreDLL_Expansion2\CvDllMissionData.cpp',
    'CvGameCoreDLL_Expansion2\CvDllMissionInfo.cpp',
    'CvGameCoreDLL_Expansion2\CvDllNetInitInfo.cpp',
    'CvGameCoreDLL_Expansion2\CvDllNetLoadGameInfo.cpp',
    'CvGameCoreDLL_Expansion2\CvDllNetMessageExt.cpp',
    'CvGameCoreDLL_Expansion2\CvDllNetMessageHandler.cpp',
    'CvGameCoreDLL_Expansion2\CvDllNetworkSyncronization.cpp',
    'CvGameCoreDLL_Expansion2\CvDllPathFinderUpdate.cpp',
    'CvGameCoreDLL_Expansion2\CvDllPlayer.cpp',
    'CvGameCoreDLL_Expansion2\CvDllPlayerColorInfo.cpp',
    'CvGameCoreDLL_Expansion2\CvDllPlayerOptionInfo.cpp',
    'CvGameCoreDLL_Expansion2\CvDllPlot.cpp',
    'CvGameCoreDLL_Expansion2\CvDllPolicyInfo.cpp',
    'CvGameCoreDLL_Expansion2\CvDllPreGame.cpp',
    'CvGameCoreDLL_Expansion2\CvDllPromotionInfo.cpp',
    'CvGameCoreDLL_Expansion2\CvDllRandom.cpp',
    'CvGameCoreDLL_Expansion2\CvDllResourceInfo.cpp',
    'CvGameCoreDLL_Expansion2\CvDllScriptSystemUtility.cpp',
    'CvGameCoreDLL_Expansion2\CvDllTeam.cpp',
    'CvGameCoreDLL_Expansion2\CvDllTechInfo.cpp',
    'CvGameCoreDLL_Expansion2\CvDllTerrainInfo.cpp',
    'CvGameCoreDLL_Expansion2\CvDllUnit.cpp',
    'CvGameCoreDLL_Expansion2\CvDllUnitCombatClassInfo.cpp',
    'CvGameCoreDLL_Expansion2\CvDllUnitInfo.cpp',
    'CvGameCoreDLL_Expansion2\CvDllVictoryInfo.cpp',
    'CvGameCoreDLL_Expansion2\CvDllWorldBuilderMapLoader.cpp',
    'CvGameCoreDLL_Expansion2\CvDllWorldInfo.cpp',
    'CvGameCoreDLL_Expansion2\CvEconomicAI.cpp',
    'CvGameCoreDLL_Expansion2\CvEmphasisClasses.cpp',
    'CvGameCoreDLL_Expansion2\CvEspionageClasses.cpp',
    'CvGameCoreDLL_Expansion2\CvEventLog.cpp',
    'CvGameCoreDLL_Expansion2\CvFlavorManager.cpp',
    'CvGameCoreDLL_Expansion2\CvFractal.cpp',
    'CvGameCoreDLL_Expansion2\CvGame.cpp',
    'CvGameCoreDLL_Expansion2\CvGameCoreDLL.cpp',
    'CvGameCoreDLL_Expansion2\CvGameCoreEnumSerialization.cpp',
    'CvGameCoreDLL_Expansion2\CvGameCoreStructs.cpp',
    'CvGameCoreDLL_Expansion2\CvGameCoreUtils.cpp',
    'CvGameCoreDLL_Expansion2\CvGameQueries.cpp',
    'CvGameCoreDLL_Expansion2\CvGameTextMgr.cpp',
    'CvGameCoreDLL_Expansion2\CvGlobals.cpp',
    'CvGameCoreDLL_Expansion2\CvGoodyHuts.cpp',
    'CvGameCoreDLL_Expansion2\CvGrandStrategyAI.cpp',
    'CvGameCoreDLL_Expansion2\CvGreatPersonInfo.cpp',
    'CvGameCoreDLL_Expansion2\CvHomelandAI.cpp',
    'CvGameCoreDLL_Expansion2\CvImprovementClasses.cpp',
    'CvGameCoreDLL_Expansion2\CvInfos.cpp',
    'CvGameCoreDLL_Expansion2\CvInfosSerializationHelper.cpp',
    'CvGameCoreDLL_Expansion2\CvInternalGameCoreUtils.cpp',
    'CvGameCoreDLL_Expansion2\CvLoggerCSV.cpp',
    'CvGameCoreDLL_Expansion2\CvMap.cpp',
    'CvGameCoreDLL_Expansion2\CvMapGenerator.cpp',
    'CvGameCoreDLL_Expansion2\CvMilitaryAI.cpp',
    'CvGameCoreDLL_Expansion2\CvMinorCivAI.cpp',
    'CvGameCoreDLL_Expansion2\CvNotificationClasses.cpp',
    'CvGameCoreDLL_Expansion2\CvNotifications.cpp',
    'CvGameCoreDLL_Expansion2\CvPlayer.cpp',
    'CvGameCoreDLL_Expansion2\CvPlayerAI.cpp',
    'CvGameCoreDLL_Expansion2\CvPlayerManager.cpp',
    'CvGameCoreDLL_Expansion2\CvPlot.cpp',
    'CvGameCoreDLL_Expansion2\CvPlotInfo.cpp',
    'CvGameCoreDLL_Expansion2\CvPlotManager.cpp',
    'CvGameCoreDLL_Expansion2\CvPolicyAI.cpp',
    'CvGameCoreDLL_Expansion2\CvPolicyClasses.cpp',
    'CvGameCoreDLL_Expansion2\CvPopupInfoSerialization.cpp',
    'CvGameCoreDLL_Expansion2\CvPreGame.cpp',
    'CvGameCoreDLL_Expansion2\CvProcessProductionAI.cpp',
    'CvGameCoreDLL_Expansion2\CvProjectClasses.cpp',
    'CvGameCoreDLL_Expansion2\CvProjectProductionAI.cpp',
    'CvGameCoreDLL_Expansion2\CvPromotionClasses.cpp',
    'CvGameCoreDLL_Expansion2\CvRandom.cpp',
    'CvGameCoreDLL_Expansion2\CvReligionClasses.cpp',
    'CvGameCoreDLL_Expansion2\CvReplayInfo.cpp',
    'CvGameCoreDLL_Expansion2\CvReplayMessage.cpp',
    'CvGameCoreDLL_Expansion2\CvSerialize.cpp',
    'CvGameCoreDLL_Expansion2\CvSiteEvaluationClasses.cpp',
    'CvGameCoreDLL_Expansion2\CvStartPositioner.cpp',
    'CvGameCoreDLL_Expansion2\cvStopWatch.cpp',
    'CvGameCoreDLL_Expansion2\CvTacticalAI.cpp',
    'CvGameCoreDLL_Expansion2\CvTacticalAnalysisMap.cpp',
    'CvGameCoreDLL_Expansion2\CvTargeting.cpp',
    'CvGameCoreDLL_Expansion2\CvTeam.cpp',
    'CvGameCoreDLL_Expansion2\CvTechAI.cpp',
    'CvGameCoreDLL_Expansion2\CvTechClasses.cpp',
    'CvGameCoreDLL_Expansion2\CvTradeClasses.cpp',
    'CvGameCoreDLL_Expansion2\CvTraitClasses.cpp',
    'CvGameCoreDLL_Expansion2\CvTreasury.cpp',
    'CvGameCoreDLL_Expansion2\CvTypes.cpp',
    'CvGameCoreDLL_Expansion2\CvUnit.cpp',
    'CvGameCoreDLL_Expansion2\CvUnitClasses.cpp',
    'CvGameCoreDLL_Expansion2\CvUnitCombat.cpp',
    'CvGameCoreDLL_Expansion2\CvUnitCycler.cpp',
    'CvGameCoreDLL_Expansion2\CvUnitMission.cpp',
    'CvGameCoreDLL_Expansion2\CvUnitMovement.cpp',
    'CvGameCoreDLL_Expansion2\CvUnitProductionAI.cpp',
    'CvGameCoreDLL_Expansion2\CvVotingClasses.cpp',
    'CvGameCoreDLL_Expansion2\CvWonderProductionAI.cpp',
    'CvGameCoreDLL_Expansion2\CvWorldBuilderMapLoader.cpp'
)

# Build compiler argument list
function Build-ClangArgs {
    $args = @('-m32', '-msse3', '/c', '/MD', '/GS', '/EHsc', '/fp:precise', '/Zc:wchar_t', '/Z7')
    
    if ($Config -eq 'release') {
        $args += '/Ox', '/Ob2', '-flto'
    } else {
        $args += '/Od', '-g'
    }
    
    foreach ($predef in $PREDEFS) {
        $args += "/D$predef"
    }
    
    foreach ($include_dir in $INCLUDE_DIRS) {
        $args += "/I`"$(Join-Path $PROJECT_DIR $include_dir)`""
    }
    
    foreach ($suppress in $CL_SUPPRESS) {
        $args += "-Wno-$suppress"
    }
    
    return ($args -join ' ')
}

# Build linker argument list
function Build-LinkArgs {
    $args = @(
        '/MACHINE:x86', '/DLL', '/DEBUG', '/LTCG', '/DYNAMICBASE',
        '/NXCOMPAT', '/SUBSYSTEM:WINDOWS', '/MANIFEST:EMBED',
        '/FORCE:MULTIPLE', "/DEF:`"$(Join-Path $PROJECT_DIR $DEF_FILE)`""
    )
    
    if ($Config -eq 'release') {
        $args += '/OPT:REF', '/OPT:ICF'
    }
    
    return $args
}

# Prepare build directories
function Prepare-Dirs {
    New-Item -ItemType Directory -Force -Path $BUILD_DIR | Out-Null
    New-Item -ItemType Directory -Force -Path $OUT_DIR | Out-Null
    
    foreach ($cpp in $CPP) {
        $cpp_dir = Join-Path $BUILD_DIR (Split-Path $cpp -Parent)
        New-Item -ItemType Directory -Force -Path $cpp_dir | Out-Null
    }
}

# Check if a file needs rebuilding (target older than source)
function Test-NeedsRebuild {
    param(
        [string]$Target,
        [string]$Source
    )
    
    if (-not (Test-Path $Target)) {
        return $true  # Target doesn't exist, needs rebuild
    }
    
    $targetTime = (Get-Item $Target).LastWriteTime
    $sourceTime = (Get-Item $Source).LastWriteTime
    
    return $sourceTime -gt $targetTime  # Source newer than target, needs rebuild
}

# Execute command with VS2008 environment
function Invoke-VsCommand {
    param(
        [string]$Command,
        [string]$LogFile
    )
    
    $fullCommand = "`"`"$VS_2008_VARS_BAT`">NUL && $Command`""
    
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = 'cmd.exe'
    $psi.Arguments = "/c $fullCommand"
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    
    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $psi
    $process.Start() | Out-Null
    
    $stdout = $process.StandardOutput.ReadToEnd()
    $stderr = $process.StandardError.ReadToEnd()
    $process.WaitForExit()
    
    if ($LogFile) {
        Add-Content -Path $LogFile -Value $stdout -Encoding UTF8
        Add-Content -Path $LogFile -Value $stderr -Encoding UTF8
    }
    
    return @{
        ExitCode = $process.ExitCode
        Output = $stdout + $stderr
    }
}

# Update commit ID
function Update-CommitId {
    param([string]$LogFile)
    
    Write-Host "Updating commit id..."
    $startTime = Get-Date
    
    Add-Content -Path $LogFile -Value "==== update_commit_id.bat ====" -Encoding UTF8

    $updateBat = Join-Path $PROJECT_DIR 'update_commit_id.bat'
    if (-not (Test-Path $updateBat)) {
        Write-Host "Could not find update_commit_id.bat at $updateBat"
        exit 1
    }
    $quoted = "`"$updateBat`""
    $result = Invoke-VsCommand -Command $quoted -LogFile $LogFile
    
    if ($result.ExitCode -ne 0) {
        Write-Host "Failed to update commit id - see build log"
        exit 1
    }
    
    $elapsed = (Get-Date) - $startTime
    Write-Host "Commit id update finished after $($elapsed.TotalSeconds) seconds"
}

# Build clang.cpp
function Build-ClangCpp {
    param(
        [string]$ClangCl,
        [string]$ClArgs,
        [string]$LogFile
    )
    
    Write-Host "Building clang.cpp..."
    $startTime = Get-Date
    
    $src = Join-Path $PROJECT_DIR 'clang.cpp'
    $out = Join-Path $BUILD_DIR 'clang.obj'
    $command = "$ClangCl `"$src`" /Fo`"$out`" $ClArgs"
    
    Add-Content -Path $LogFile -Value "==== $src ====" -Encoding UTF8
    
    $result = Invoke-VsCommand -Command $command -LogFile $LogFile
    
    if ($result.ExitCode -ne 0) {
        Write-Host "Failed to build clang.cpp - see build log"
        exit 1
    }
    
    $elapsed = (Get-Date) - $startTime
    Write-Host "clang.cpp build finished after $($elapsed.TotalSeconds) seconds"
}

# Build precompiled header
function Build-Pch {
    param(
        [string]$ClangCl,
        [string]$ClArgs,
        [string]$PchPath,
        [string]$LogFile
    )
    
    $pch_src = Join-Path $PROJECT_DIR $PCH_CPP
    $pch_header = Join-Path $PROJECT_DIR $PCH_H
    
    # Check if PCH needs rebuilding
    if (-not (Test-NeedsRebuild -Target $PchPath -Source $pch_src) -and (Test-Path $pch_header) -and -not (Test-NeedsRebuild -Target $PchPath -Source $pch_header)) {
        Write-Host "Precompiled header is up-to-date, skipping rebuild"
        return
    }
    
    Write-Host "Building precompiled header..."
    $startTime = Get-Date
    
    $out = Join-Path $BUILD_DIR ($PCH_CPP -replace '\.cpp$', '.obj')
    $command = "$ClangCl `"$pch_src`" /Fo`"$out`" /Yc`"$PCH_H`" /Fp`"$PchPath`" $ClArgs"
    
    Add-Content -Path $LogFile -Value "==== $pch_src ====" -Encoding UTF8
    
    $result = Invoke-VsCommand -Command $command -LogFile $LogFile
    
    if ($result.ExitCode -ne 0) {
        Write-Host "Failed to build precompiled header - see build log"
        exit 1
    }
    
    $elapsed = (Get-Date) - $startTime
    Write-Host "Precompiled header build finished after $($elapsed.TotalSeconds) seconds"
}

# Build all CPP files in parallel
function Build-Cpps {
    param(
        [string]$ClangCl,
        [string]$ClArgs,
        [string]$PchPath,
        [string]$LogFile
    )
    
    Write-Host "Building cpps..."
    $startTime = Get-Date
    
    # Count total files and files to rebuild
    $totalFiles = $CPP.Count
    $filesToBuild = 0
    $skippedFiles = 0
    
    $jobs = @()
    $tempLogs = @{}
    $filesToRebuild = @()
    
    # First pass: check which files need rebuilding
    foreach ($cpp in $CPP) {
        $cpp_src = Join-Path $PROJECT_DIR $cpp
        $out = Join-Path $BUILD_DIR ($cpp -replace '\.cpp$', '.obj')
        
        if (Test-NeedsRebuild -Target $out -Source $cpp_src) {
            $filesToRebuild += @{cpp = $cpp; src = $cpp_src; out = $out}
            $filesToBuild++
        } else {
            $skippedFiles++
        }
    }
    
    if ($skippedFiles -gt 0) {
        Write-Host "Skipping $skippedFiles up-to-date files ($filesToBuild files to rebuild)"
    }
    
    # Second pass: build files that need it in parallel
    $maxParallelJobs = [Math]::Max(1, [System.Environment]::ProcessorCount - 1)
    
    foreach ($fileInfo in $filesToRebuild) {
        $cpp_src = $fileInfo.src
        $out = $fileInfo.out
        $tempLog = [System.IO.Path]::GetTempFileName()
        $tempLogs[$cpp_src] = $tempLog
        
        $command = "`"`"$VS_2008_VARS_BAT`">NUL && $ClangCl `"$cpp_src`" /Fo`"$out`" /Yu`"$PCH_H`" /Fp`"$PchPath`" $ClArgs`""
        
        # Throttle job submissions to avoid resource exhaustion
        while (($jobs | Where-Object { $_.State -eq 'Running' }).Count -ge $maxParallelJobs) {
            Start-Sleep -Milliseconds 50
        }
        
        $job = Start-Job -ScriptBlock {
            param($cmd, $log, $timeout)
            $psi = New-Object System.Diagnostics.ProcessStartInfo
            $psi.FileName = 'cmd.exe'
            $psi.Arguments = "/c $cmd"
            $psi.RedirectStandardOutput = $true
            $psi.RedirectStandardError = $true
            $psi.UseShellExecute = $false
            $psi.CreateNoWindow = $true
            
            $process = New-Object System.Diagnostics.Process
            $process.StartInfo = $psi
            $process.Start() | Out-Null
            
            # Stream output to avoid buffer deadlock
            $output = New-Object System.Text.StringBuilder
            $stdoutTask = $process.StandardOutput.BaseStream.BeginRead([byte[]]::new(4096), 0, 4096, $null, $null)
            $stderrTask = $process.StandardError.BaseStream.BeginRead([byte[]]::new(4096), 0, 4096, $null, $null)
            
            # Wait for process with timeout
            $exitedInTime = $process.WaitForExit($timeout)
            if (-not $exitedInTime) {
                Write-Host "WARNING: Process timeout ($($timeout)ms) for compilation, killing job..."
                $process.Kill()
                $process.WaitForExit()
                Set-Content -Path $log -Value "TIMEOUT AFTER ${timeout}ms`n" -Encoding UTF8
                return 9999  # Timeout exit code
            }
            
            # Read output
            try {
                $stdout = $process.StandardOutput.ReadToEnd()
                $stderr = $process.StandardError.ReadToEnd()
                $output_text = $stdout + $stderr
                if ([string]::IsNullOrEmpty($output_text)) {
                    $output_text = "(no output)"
                }
                Set-Content -Path $log -Value $output_text -Encoding UTF8
            } catch {
                Set-Content -Path $log -Value "ERROR reading output: $_" -Encoding UTF8
                return 9998
            }
            
            return $process.ExitCode
        } -ArgumentList $command, $tempLog, 300000  # 5-minute timeout per file
        
        $jobs += $job
    }
    
    # Wait for all remaining jobs with timeout monitoring
    $jobTimeout = 600000  # 10-minute total timeout for all jobs
    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    
    while ($jobs | Where-Object { $_.State -eq 'Running' }) {
        if ($stopwatch.ElapsedMilliseconds -gt $jobTimeout) {
            Write-Host "ERROR: Build job timeout (10 minutes). Killing remaining jobs..."
            $jobs | Stop-Job -Force
            exit 1
        }
        Start-Sleep -Milliseconds 100
    }
    
    $stopwatch.Stop()
    
    # Collect results
    $failed = 0
    $results = @()
    foreach ($job in $jobs) {
        $result = $job | Receive-Job
        if ($result -is [int]) {
            $results += $result
        } else {
            $results += 0
        }
    }
    
    # Write logs to main log file
    foreach ($kvp in $tempLogs.GetEnumerator()) {
        Add-Content -Path $LogFile -Value "==== $($kvp.Key) ====" -Encoding UTF8
        if (Test-Path $kvp.Value) {
            $content = Get-Content -Path $kvp.Value -Raw
            Add-Content -Path $LogFile -Value $content -Encoding UTF8
            Remove-Item -Path $kvp.Value -Force
        }
    }
    
    foreach ($result in $results) {
        if ($result -ne 0) {
            $failed++
        }
    }
    
    # Clean up jobs
    $jobs | Remove-Job | Out-Null
    
    if ($failed -ne 0) {
        Write-Host "$failed cpp(s) failed to build - see build log"
        exit 1
    }
    
    $elapsed = (Get-Date) - $startTime
    if ($skippedFiles -gt 0) {
        Write-Host "cpps build finished after $($elapsed.TotalSeconds) seconds ($skippedFiles files were up-to-date)"
    } else {
        Write-Host "cpps build finished after $($elapsed.TotalSeconds) seconds"
    }
}

# Link DLL
function Link-Dll {
    param(
        [string]$Linker,
        [array]$LinkArgs,
        [string]$LogFile
    )
    
    Write-Host "Linking dll..."
    $startTime = Get-Date
    
    $link_response_file = Join-Path $BUILD_DIR 'link'
    $out_dll = Join-Path $OUT_DIR "$CORE_DLL.dll"
    $out_pdb = Join-Path $OUT_DIR "$CORE_DLL.pdb"
    
    $responseContent = @()
    $responseContent += "/OUT:`"$out_dll`""
    $responseContent += "/PDB:`"$out_pdb`""
    $responseContent += $LinkArgs
    
    foreach ($lib in $LIBS) {
        $lib_path = Join-Path $PROJECT_DIR $lib
        $responseContent += "`"$lib_path`""
    }
    
    foreach ($default_lib in $DEFAULT_LIBS) {
        $responseContent += "`"$default_lib`""
    }
    
    $clang_obj = Join-Path $BUILD_DIR 'clang.obj'
    $pch_obj = Join-Path $BUILD_DIR ($PCH_CPP -replace '\.cpp$', '.obj')
    $responseContent += "`"$clang_obj`""
    $responseContent += "`"$pch_obj`""
    
    foreach ($cpp in $CPP) {
        $cpp_obj = Join-Path $BUILD_DIR ($cpp -replace '\.cpp$', '.obj')
        $responseContent += "`"$cpp_obj`""
    }
    
    Set-Content -Path $link_response_file -Value ($responseContent -join "`n") -Encoding UTF8
    
    $command = "$Linker @`"$link_response_file`""
    
    Add-Content -Path $LogFile -Value "==== $CORE_DLL.dll ====" -Encoding UTF8
    
    # Use direct cmd.exe invocation for linker to avoid output buffering issues
    $fullCommand = "`"`"$VS_2008_VARS_BAT`">NUL && $command`""
    $tempLog = [System.IO.Path]::GetTempFileName()
    
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = 'cmd.exe'
    $psi.Arguments = "/c $fullCommand 2>&1"
    $psi.RedirectStandardOutput = $true
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    
    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $psi
    $process.Start() | Out-Null
    
    # Stream output to avoid buffering
    $output = New-Object System.Text.StringBuilder
    while (-not $process.StandardOutput.EndOfStream) {
        $line = $process.StandardOutput.ReadLine()
        [void]$output.AppendLine($line)
    }
    $process.WaitForExit()
    
    $outputStr = $output.ToString()
    Add-Content -Path $LogFile -Value $outputStr -Encoding UTF8
    
    if ($process.ExitCode -ne 0) {
        Write-Host "Linking dll failed - see build log"
        Write-Host $outputStr
        exit 1
    }
    
    $elapsed = (Get-Date) - $startTime
    Write-Host "Linking dll finished after $($elapsed.TotalSeconds) seconds"
}

# Main execution
Write-Host "Building VP with config: $Config"

$cl = 'clang-cl.exe'
$link = 'lld-link.exe'
$cl_args = Build-ClangArgs
$link_args = Build-LinkArgs
$pch_path = Join-Path $BUILD_DIR $PCH
$log_file = Join-Path $OUT_DIR 'build.log'

Prepare-Dirs

# Clear log file
Set-Content -Path $log_file -Value "" -Encoding UTF8

try {
    Update-CommitId -LogFile $log_file
    Build-ClangCpp -ClangCl $cl -ClArgs $cl_args -LogFile $log_file
    Build-Pch -ClangCl $cl -ClArgs $cl_args -PchPath $pch_path -LogFile $log_file
    Build-Cpps -ClangCl $cl -ClArgs $cl_args -PchPath $pch_path -LogFile $log_file
    Link-Dll -Linker $link -LinkArgs $link_args -LogFile $log_file
    
    Write-Host "Build completed successfully!"
    Write-Host "Output: $OUT_DIR\$CORE_DLL.dll"
} catch {
    Write-Host "Build failed: $_"
    exit 1
}
