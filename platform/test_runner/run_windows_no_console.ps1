# run_windows_no_console.ps1: the Windows executable started as Explorer starts
# a program, with no standard handles and no console to attach to, must write
# its messages to pokeemerald.log in the data directory (docs/DECISIONS.md, "No
# console window on Windows"; platform/console.h):
#
#   pwsh platform/test_runner/run_windows_no_console.ps1 build/native64-windows/pokeemerald-native.exe
#
# Win32_Process.Create starts the process from the WMI provider host, which has
# no console, so AttachConsole(ATTACH_PARENT_PROCESS) fails there as it does for
# Explorer's children (a Start-Process child would attach to this shell's).
# Passes, exit status 0, only if --version, started so, leaves exactly the
# version line --version prints through a file in <data dir>\pokeemerald.log,
# and does so again over a log left by an earlier start (the log is truncated
# at each start). No start-up failure is provoked: its message box would wait
# for a click that no one on a runner can give; the choice of the box is the
# unit tests'.
param(
    [Parameter(Mandatory = $true)][string]$Exe
)
$ErrorActionPreference = 'Stop'

$LimitSeconds = 60
$PollMilliseconds = 250

$exePath = (Resolve-Path -LiteralPath $Exe).Path
$scratch = Join-Path ([IO.Path]::GetTempPath()) ("pokeemerald-no-console-" + [guid]::NewGuid())
New-Item -ItemType Directory -Path $scratch | Out-Null
$failed = $false

function Fail([string]$message) {
    Write-Host "FAILED: $message"
    $script:failed = $true
}

# The version --version prints through a file handle, which the console-less start must reproduce in the log.
$versionFile = Join-Path $scratch 'version.txt'
Start-Process -FilePath $exePath -ArgumentList '--version' -RedirectStandardOutput $versionFile -Wait -NoNewWindow
$version = (Get-Content -LiteralPath $versionFile -Raw).TrimEnd("`r", "`n")
if ($version -eq '') {
    Fail '--version printed nothing through a file'
}

# Starts the executable with the arguments, detached, and waits for it to end.
function Start-Detached([string]$arguments) {
    $created = Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{
        CommandLine      = "`"$exePath`" $arguments"
        CurrentDirectory = $scratch
    }
    if ($created.ReturnValue -ne 0) {
        throw "Win32_Process.Create returned $($created.ReturnValue)"
    }
    $deadline = (Get-Date).AddSeconds($LimitSeconds)
    while ((Get-Process -Id $created.ProcessId -ErrorAction SilentlyContinue) -and (Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds $PollMilliseconds
    }
    if (Get-Process -Id $created.ProcessId -ErrorAction SilentlyContinue) {
        Stop-Process -Id $created.ProcessId -Force
        Fail "the executable did not end within $LimitSeconds seconds"
    }
}

$dataDir = Join-Path $scratch 'data'
$log = Join-Path $dataDir 'pokeemerald.log'

Write-Host '== started with no handles and no console: --version'
Start-Detached "--version --data-dir `"$dataDir`""
if (-not (Test-Path -LiteralPath $log)) {
    Fail "there is no $log"
}
else {
    $text = (Get-Content -LiteralPath $log -Raw).TrimEnd("`r", "`n")
    Write-Host "-- $log"
    Write-Host $text
    if ($text -ne $version) {
        Fail "the log does not hold exactly the version line '$version'"
    }
}

Write-Host '== again, over a log from an earlier start: truncated, not appended to'
Set-Content -LiteralPath $log -Value ('an earlier start wrote this line, longer than the version' * 4)
Start-Detached "--version --data-dir `"$dataDir`""
$text = (Get-Content -LiteralPath $log -Raw).TrimEnd("`r", "`n")
if ($text -ne $version) {
    Fail "the second start's log is not exactly the version line: $text"
}

Remove-Item -LiteralPath $scratch -Recurse -Force -ErrorAction SilentlyContinue
if ($failed) {
    exit 1
}
Write-Host 'PASS: started with no handles and no console, the executable wrote its messages to pokeemerald.log in the data directory, truncated at each start'
