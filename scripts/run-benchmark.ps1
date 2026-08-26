[CmdletBinding()]
param(
    [string]$ClientCounts = "1,8,16,32,64",

    [ValidateRange(1, 100)]
    [int]$Repetitions = 5,

    [ValidateRange(0, 1000000)]
    [int]$WarmupMovesPerClient = 100,

    [ValidateRange(1, 1000000)]
    [int]$MeasuredMovesPerClient = 2000,

    [ValidateRange(1, 65535)]
    [int]$Port = 7777,

    [string]$BuildDirectory = "out/build/vs2022/Release",
    [string]$OutputDirectory = "out/benchmark"
)

$ErrorActionPreference = "Stop"
$InvariantCulture = [System.Globalization.CultureInfo]::InvariantCulture

$parsedClientCounts = foreach ($text in ($ClientCounts -split ',')) {
    $value = 0
    if (-not [int]::TryParse($text.Trim(), [ref]$value) -or $value -lt 1 -or $value -gt 1024) {
        throw "ClientCounts must be comma-separated integers between 1 and 1024."
    }
    $value
}

function Get-MatchedValue {
    param(
        [string]$Text,
        [string]$Pattern,
        [string]$Label
    )

    $match = [regex]::Match($Text, $Pattern, [System.Text.RegularExpressions.RegexOptions]::Multiline)
    if (-not $match.Success) {
        throw "Could not parse $Label from load client output."
    }
    return $match.Groups[1].Value
}

function Convert-ToDouble {
    param([string]$Text)
    return [double]::Parse($Text, $InvariantCulture)
}

function Get-Median {
    param([double[]]$Values)

    $sorted = @($Values | Sort-Object)
    $middle = [int][math]::Floor($sorted.Count / 2)
    if (($sorted.Count % 2) -eq 1) {
        return $sorted[$middle]
    }
    return ($sorted[$middle - 1] + $sorted[$middle]) / 2.0
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$buildPath = Join-Path $repoRoot $BuildDirectory
$serverPath = Join-Path $buildPath "mcrs_server.exe"
$loadClientPath = Join-Path $buildPath "mcrs_load_client.exe"
$resultDirectory = Join-Path $repoRoot $OutputDirectory

if (-not (Test-Path -LiteralPath $serverPath -PathType Leaf)) {
    throw "Release server was not found: $serverPath"
}
if (-not (Test-Path -LiteralPath $loadClientPath -PathType Leaf)) {
    throw "Release load client was not found: $loadClientPath"
}

[void](New-Item -ItemType Directory -Path $resultDirectory -Force)
$runStamp = Get-Date -Format "yyyyMMdd-HHmmss"
$rawResultPath = Join-Path $resultDirectory "room-benchmark-$runStamp-raw.csv"
$summaryResultPath = Join-Path $resultDirectory "room-benchmark-$runStamp-summary.csv"
$serverOutputPath = Join-Path $resultDirectory "room-benchmark-$runStamp-server.out.log"
$serverErrorPath = Join-Path $resultDirectory "room-benchmark-$runStamp-server.err.log"

$serverProcess = $null
$results = [System.Collections.Generic.List[object]]::new()

try {
    $serverProcess = Start-Process `
        -FilePath $serverPath `
        -ArgumentList $Port `
        -RedirectStandardOutput $serverOutputPath `
        -RedirectStandardError $serverErrorPath `
        -WindowStyle Hidden `
        -PassThru

    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    $serverReady = $false
    while (-not $serverReady -and [DateTime]::UtcNow -lt $deadline) {
        if ($serverProcess.HasExited) {
            throw "Server exited before the benchmark started. See $serverErrorPath"
        }

        $probe = [System.Net.Sockets.TcpClient]::new()
        try {
            $probe.Connect("127.0.0.1", $Port)
            $serverReady = $true
        }
        catch {
            Start-Sleep -Milliseconds 100
        }
        finally {
            $probe.Dispose()
        }
    }

    if (-not $serverReady) {
        throw "Server did not listen on port $Port within 5 seconds."
    }

    foreach ($clientCount in $parsedClientCounts) {
        for ($run = 1; $run -le $Repetitions; ++$run) {
            Write-Host "[$clientCount clients] run $run/$Repetitions"
            $outputLines = & $loadClientPath `
                "127.0.0.1" `
                $Port `
                $clientCount `
                $WarmupMovesPerClient `
                $MeasuredMovesPerClient 2>&1

            if ($LASTEXITCODE -ne 0) {
                throw "Load client failed:`n$($outputLines -join [Environment]::NewLine)"
            }

            $output = ($outputLines | ForEach-Object { $_.ToString() }) -join "`n"
            $latencyMatch = [regex]::Match(
                $output,
                '^latency min/p50/p95/p99/max: ([0-9.]+) / ([0-9.]+) / ([0-9.]+) / ([0-9.]+) / ([0-9.]+) us$',
                [System.Text.RegularExpressions.RegexOptions]::Multiline)
            if (-not $latencyMatch.Success) {
                throw "Could not parse latency summary from load client output."
            }

            $result = [pscustomobject]@{
                Clients                       = $clientCount
                Run                           = $run
                WarmupMovesPerClient          = $WarmupMovesPerClient
                MeasuredMovesPerClient        = $MeasuredMovesPerClient
                Samples                       = [int64](Get-MatchedValue $output '^samples: ([0-9]+)$' 'samples')
                ElapsedMs                     = Convert-ToDouble (Get-MatchedValue $output '^elapsed: ([0-9.]+) ms$' 'elapsed')
                CommandsPerSecond             = Convert-ToDouble (Get-MatchedValue $output '^commands/sec: ([0-9.]+)$' 'commands/sec')
                EstimatedDeliveriesPerSecond  = Convert-ToDouble (Get-MatchedValue $output '^estimated deliveries/sec: ([0-9.]+)$' 'estimated deliveries/sec')
                MinUs                         = Convert-ToDouble $latencyMatch.Groups[1].Value
                P50Us                         = Convert-ToDouble $latencyMatch.Groups[2].Value
                P95Us                         = Convert-ToDouble $latencyMatch.Groups[3].Value
                P99Us                         = Convert-ToDouble $latencyMatch.Groups[4].Value
                MaxUs                         = Convert-ToDouble $latencyMatch.Groups[5].Value
            }
            $results.Add($result)

            Write-Host ("  {0:N0} commands/sec, p50 {1:N3} ms, p99 {2:N3} ms" -f `
                    $result.CommandsPerSecond, ($result.P50Us / 1000.0), ($result.P99Us / 1000.0))
        }
    }

    $results | Export-Csv -LiteralPath $rawResultPath -NoTypeInformation -Encoding UTF8

    $summary = foreach ($group in ($results | Group-Object Clients | Sort-Object { [int]$_.Name })) {
        $runs = @($group.Group)
        [pscustomobject]@{
            Clients                       = [int]$group.Name
            Repetitions                   = $runs.Count
            SamplesPerRun                 = $runs[0].Samples
            MedianElapsedMs               = Get-Median ([double[]]$runs.ElapsedMs)
            MedianCommandsPerSecond       = Get-Median ([double[]]$runs.CommandsPerSecond)
            MedianDeliveriesPerSecond     = Get-Median ([double[]]$runs.EstimatedDeliveriesPerSecond)
            MedianP50Us                   = Get-Median ([double[]]$runs.P50Us)
            MedianP95Us                   = Get-Median ([double[]]$runs.P95Us)
            MedianP99Us                   = Get-Median ([double[]]$runs.P99Us)
            LargestMaxUs                  = ($runs.MaxUs | Measure-Object -Maximum).Maximum
        }
    }

    $summary | Export-Csv -LiteralPath $summaryResultPath -NoTypeInformation -Encoding UTF8
    Write-Host ""
    $summary | Format-Table Clients, MedianCommandsPerSecond, MedianP50Us, MedianP95Us, MedianP99Us, LargestMaxUs -AutoSize
    Write-Host "Raw results: $rawResultPath"
    Write-Host "Summary:     $summaryResultPath"
}
finally {
    if ($null -ne $serverProcess -and -not $serverProcess.HasExited) {
        Stop-Process -Id $serverProcess.Id
        $serverProcess.WaitForExit()
    }
}
