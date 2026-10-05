[CmdletBinding()]
param(
    [ValidateRange(1, 65535)][int]$Port = 17888,
    [string]$BuildDirectory = 'out/build/vs2022/Release'
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$build = Join-Path $repoRoot $BuildDirectory
$result = Join-Path $repoRoot ('out/metrics-validation/' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
[void](New-Item -ItemType Directory -Path $result)
$serverOut = Join-Path $result 'server.out.log'
$serverErr = Join-Path $result 'server.err.log'
$clientOut = Join-Path $result 'client.out.log'
$clientErr = Join-Path $result 'client.err.log'
$server = $null
$client = $null

function Assert-Equal {
    param($Actual, $Expected, [string]$Label)
    if ($Actual -ne $Expected) { throw "$Label expected $Expected, got $Actual" }
}

try {
    $server = Start-Process -FilePath (Join-Path $build 'mcrs_server.exe') `
        -ArgumentList @($Port, 100) -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput $serverOut -RedirectStandardError $serverErr
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    $ready = $false
    while (-not $ready -and [DateTime]::UtcNow -lt $deadline) {
        if ($server.HasExited) { throw 'Server exited during startup.' }
        $ready = [bool]((Get-Content -LiteralPath $serverOut -Raw) -match 'listening on')
        if (-not $ready) { Start-Sleep -Milliseconds 50 }
    }
    if (-not $ready) { throw 'Server startup timed out.' }

    # Fixed, known protocol workload. This validates counters, not performance.
    $clients = 8
    $warmup = 20
    $measured = 2000
    $client = Start-Process -FilePath (Join-Path $build 'mcrs_load_client.exe') `
        -ArgumentList @('127.0.0.1', $Port, $clients, $warmup, $measured) `
        -WindowStyle Hidden -PassThru -RedirectStandardOutput $clientOut -RedirectStandardError $clientErr
    # Keep the process handle open so Windows PowerShell retains the exit code after exit.
    $null = $client.Handle
    if (-not $client.WaitForExit(30000)) { throw 'Load client timed out.' }
    Assert-Equal $client.ExitCode 0 'Load client exit code'

    # One extra Move is the measurement barrier. Entry/exit have triangular fan-out.
    $moves = $clients * ($warmup + $measured) + 1
    $joins = $clients * ($clients + 1) / 2
    $leaves = $clients * ($clients - 1) / 2
    $expectedCommands = $moves + 2 * $clients
    $expectedPackets = $moves * $clients + $joins + $leaves
    $expectedBytes = ($moves * $clients + $joins) * 22 + $leaves * 16
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    $last = $null
    while ([DateTime]::UtcNow -lt $deadline) {
        if ($server.HasExited) { throw 'Server exited during metrics validation.' }
        # Get-Content may observe the currently-written final line; only parse complete JSON lines.
        $lines = @(Get-Content -LiteralPath $serverOut | Where-Object { $_.StartsWith('{') -and $_.EndsWith('}') })
        if ($lines.Count -gt 0) {
            $last = $lines[-1] | ConvertFrom-Json
            if ($last.room_commands_processed -eq $expectedCommands -and
                $last.registered_sessions -eq 0 -and @($last.sessions).Count -eq 0) { break }
        }
        Start-Sleep -Milliseconds 50
    }
    if ($null -eq $last) { throw 'No metrics JSON was recorded.' }
    Assert-Equal $last.room_commands_processed $expectedCommands 'Processed commands'
    Assert-Equal $last.broadcast_delivery_attempts $expectedPackets 'Delivery attempts'
    Assert-Equal $last.packets_batched $expectedPackets 'Batched packets'
    Assert-Equal $last.bytes_requested $expectedBytes 'Requested bytes'
    Assert-Equal $last.bytes_transferred $expectedBytes 'Transferred bytes'
    Assert-Equal $last.write_batches_started $last.write_batches_completed 'Batch completion balance'
    foreach ($field in @('room_commands_rejected', 'event_delivery_failures', 'write_batches_failed', 'write_batches_abandoned',
                        'post_handlers_abandoned', 'outbound_overflow_closes', 'registered_sessions')) {
        Assert-Equal $last.$field 0 $field
    }
    Assert-Equal @($last.sessions).Count 0 'Live Session metric objects'
    Assert-Equal $last.active_rooms 1 'Room still serving before process cleanup'
    Assert-Equal @($last.recent_closed_sessions).Count $clients 'Closed Session history'
    foreach ($closed in $last.recent_closed_sessions) {
        foreach ($field in @('posted_jobs','posted_bytes','queued_packets','queued_bytes','inflight_packets','inflight_bytes')) {
            Assert-Equal $closed.$field 0 "Session $($closed.session_id) $field"
        }
    }
    if ($last.write_batches_started -lt 1 -or $last.max_batch_packets -gt 64) {
        throw 'Invalid batch counters.'
    }
    Assert-Equal (Get-Item -LiteralPath $serverErr).Length 0 'Server error log bytes'
    $last | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $result 'verified-snapshot.json') -Encoding UTF8
    Write-Host "PASS: $expectedCommands commands, $expectedPackets deliveries, $expectedBytes bytes; all Session gauges zero."
    Write-Host "Batch starts: $($last.write_batches_started); average packets/batch: $($last.packets_batched / $last.write_batches_started); max: $($last.max_batch_packets)"
    Write-Host "Evidence: $result"
}
finally {
    foreach ($process in @($client, $server)) {
        if ($null -ne $process -and -not $process.HasExited) {
            Stop-Process -Id $process.Id
            $process.WaitForExit()
        }
    }
}
