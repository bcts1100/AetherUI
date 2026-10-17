[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ArtifactDirectory,
 [ValidateRange(10,90)][int]$CpuSeconds=30,[ValidateRange(5,60)][int]$GpuSeconds=15,
 [string]$BaselineJsonPath='')
$ErrorActionPreference='Stop'
$stage=Split-Path -Parent $PSScriptRoot
$base=[IO.Path]::GetFullPath($ArtifactDirectory)
if(-not $base.StartsWith($stage+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $base)){throw 'Use a fresh absolute artifact leaf inside the staged project.'}
if(Get-Process AetherUI -ErrorAction SilentlyContinue){throw 'Close Aether normally before isolated measurements.'}
if(-not $BaselineJsonPath){$BaselineJsonPath=Join-Path $stage 'docs\v08-native-before.json'}
$baselineHash=(Get-FileHash -LiteralPath $BaselineJsonPath).Hash
New-Item -ItemType Directory -Path $base | Out-Null
$ps=Join-Path ([Environment]::SystemDirectory) 'WindowsPowerShell\v1.0\powershell.exe'
$binary=Join-Path $stage 'build-v03\Release\AetherTaskbarRecoveryTests.exe'
$launcher=Join-Path $stage 'build-v03\Release\AetherDesktopLauncher.exe'
foreach($mode in @('plain','glass','effects')){
 $run=Join-Path $base $mode;$runner=Join-Path $base ($mode+'-runner.cmd');$log=Join-Path $base ($mode+'-guardian.log');$status=Join-Path $base ($mode+'-exit.txt')
 $lines=@('@echo off',('"'+$binary+'" --app-sleek-measurement-guardian '+$mode+' "'+$run+'" >"'+$log+'" 2>&1'),'set "AetherMeasureExit=%errorlevel%"',('>"'+$status+'" echo %AetherMeasureExit%'),'exit /b %AetherMeasureExit%')
 [IO.File]::WriteAllLines($runner,$lines,[Text.UTF8Encoding]::new($false))
 & $launcher --binary (Join-Path ([Environment]::SystemDirectory) 'cmd.exe') --arguments ('/d /c "'+$runner+'"') --directory $stage
 if($LASTEXITCODE -ne 0){throw 'Desktop guardian launch rejected.'}
 $deadline=[DateTime]::UtcNow.AddSeconds(35);$ready=Join-Path $run 'ready.json'
 while(-not (Test-Path -LiteralPath $ready) -and -not (Test-Path -LiteralPath $status) -and [DateTime]::UtcNow -lt $deadline){Start-Sleep -Milliseconds 100}
 if(-not (Test-Path -LiteralPath $ready)){throw ('Guardian did not become ready: '+$log)}
 $owner=Get-Content -LiteralPath $ready -Raw | ConvertFrom-Json
 try{
  & $ps -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'Measure.ps1') -Seconds $CpuSeconds -RootProcessId $owner.OwnedAppPid -Scenario Idle -OutputPath (Join-Path $run 'cpu.json')
  if($LASTEXITCODE -ne 0){throw 'CPU measurement failed.'}
  & $ps -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'Measure-Gpu.ps1') -Seconds $GpuSeconds -RootProcessId $owner.OwnedAppPid -OutputPath (Join-Path $run 'gpu.json')
  if($LASTEXITCODE -ne 0){throw 'GPU measurement failed.'}
 }finally{[IO.File]::WriteAllText((Join-Path $run 'finish.json'),'{"Finish":true}')}
 $deadline=[DateTime]::UtcNow.AddSeconds(40)
 while(-not (Test-Path -LiteralPath $status) -and [DateTime]::UtcNow -lt $deadline){Start-Sleep -Milliseconds 100}
 if(-not (Test-Path -LiteralPath $status) -or [int](Get-Content -LiteralPath $status -Raw).Trim() -ne 0){throw ('Guardian cleanup failed: '+$log)}
 $cleanup=Get-Content -LiteralPath (Join-Path $run 'cleanup.json') -Raw | ConvertFrom-Json
 if(-not $cleanup.Success -or $cleanup.HiddenFailures -ne 0 -or -not $cleanup.NoFailsafeIntervention){throw 'Native stability or exact cleanup failed.'}
 & (Join-Path $PSScriptRoot 'Observe-TestDesktop.ps1') -OutputPath (Join-Path $run 'strict-post.json') -Samples 10 -IntervalMilliseconds 250 -BaselineJsonPath $BaselineJsonPath -BaselineSHA256 $baselineHash
 $strict=Get-Content -LiteralPath (Join-Path $run 'strict-post.json') -Raw | ConvertFrom-Json
 if(-not $strict.AllExact -or -not $strict.NoOwnedProcesses){throw 'Strict read-only post-check failed.'}
 Write-Output ($mode+': resource sampling, native hiding and exact cleanup passed.')
}
