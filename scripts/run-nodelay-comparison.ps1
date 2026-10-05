[CmdletBinding()]
param(
    [string]$ClientCounts = "1,8,16,32,64",
    [ValidateRange(1, 100)][int]$Repetitions = 5,
    [ValidateRange(0, 1000000)][int]$WarmupMovesPerClient = 100,
    [ValidateRange(1, 1000000)][int]$MeasuredMovesPerClient = 2000,
    [ValidateRange(1, 65535)][int]$Port = 17777,
    [string]$OutputDirectory = "out/nodelay-comparison"
)

$ErrorActionPreference = "Stop"
$counts = foreach ($countText in ($ClientCounts -split ',')) {
    $count = 0
    if (-not [int]::TryParse($countText.Trim(), [ref]$count) -or $count -lt 1 -or $count -gt 1024) {
        throw "ClientCounts must be comma-separated integers between 1 and 1024."
    }
    $count
}
$repoRoot = Split-Path -Parent $PSScriptRoot
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$resultRoot = Join-Path (Join-Path $repoRoot $OutputDirectory) $stamp
[void](New-Item -ItemType Directory -Path $resultRoot)
$utf8 = [System.Text.UTF8Encoding]::new($false)
$asioSource = Join-Path $repoRoot "out/build/vs2022/_deps/asio-src"
$variants = @(
    @{ Name = "single"; Commit = "f53903456b01fb5370346f1f8c7c2f3d84efec70" },
    @{ Name = "batched"; Commit = "daf639baedb5f2f629243778d5d2e7a683feddbd" }
)

function Replace-Once {
    param([string]$Path, [string]$Before, [string]$After)
    $content = [System.IO.File]::ReadAllText($Path).Replace("`r`n", "`n")
    if ($content.Split([string[]]@($Before), [System.StringSplitOptions]::None).Count -ne 2) {
        throw "Expected one patch location in $Path"
    }
    [System.IO.File]::WriteAllText($Path, $content.Replace($Before, $After), $utf8)
}

# Export committed sources so unrelated working-tree edits cannot affect the comparison.
foreach ($variant in $variants) {
    $source = Join-Path $resultRoot $variant.Name
    $archive = Join-Path $resultRoot "$($variant.Name).zip"
    & git -C $repoRoot archive --format=zip "--output=$archive" $variant.Commit
    if ($LASTEXITCODE -ne 0) { throw "git archive failed" }
    Expand-Archive -LiteralPath $archive -DestinationPath $source

    $serverBefore = '    auto session = std::make_shared<Session>(std::move(socket), session_id, room_worker);'
    $serverAfter = @'
    socket.set_option(asio::ip::tcp::no_delay{true});
    asio::ip::tcp::no_delay no_delay;
    socket.get_option(no_delay);
    if (!no_delay.value())
    {
        throw std::runtime_error{"server TCP_NODELAY was not enabled"};
    }
    auto session = std::make_shared<Session>(std::move(socket), session_id, room_worker);
'@
    Replace-Once (Join-Path $source "src/network/session.cpp") $serverBefore $serverAfter
    Replace-Once (Join-Path $source "src/network/session.cpp") '#include <span>' "#include <span>`n#include <stdexcept>"

    $clientBefore = '    co_await send_packet(socket, protocol::PacketType::join_room, {});'
    $clientAfter = @'
    socket.set_option(tcp::no_delay{true});
    tcp::no_delay no_delay;
    socket.get_option(no_delay);
    if (!no_delay.value())
    {
        throw std::runtime_error{"client TCP_NODELAY was not enabled"};
    }
    co_await send_packet(socket, protocol::PacketType::join_room, {});
'@
    Replace-Once (Join-Path $source "src/benchmark/load_client.cpp") $clientBefore $clientAfter

    Push-Location $source
    try {
        $configureArgs = @("--preset", "vs2022")
        if (Test-Path -LiteralPath $asioSource) {
            $configureArgs += "-DFETCHCONTENT_SOURCE_DIR_ASIO=$asioSource"
        }
        & cmake @configureArgs
        if ($LASTEXITCODE -ne 0) { throw "CMake configure failed for $($variant.Name)" }
        & cmake --build --preset release
        if ($LASTEXITCODE -ne 0) { throw "Build failed for $($variant.Name)" }
        & ctest --preset release
        if ($LASTEXITCODE -ne 0) { throw "Tests failed for $($variant.Name)" }
    }
    finally { Pop-Location }
}

$rows = [System.Collections.Generic.List[object]]::new()
foreach ($count in $counts) {
    for ($round = 1; $round -le $Repetitions; ++$round) {
        # Alternate which variant runs first to reduce systematic time/order bias.
        $order = if (($round % 2) -eq 1) { @(0, 1) } else { @(1, 0) }
        foreach ($index in $order) {
            $variant = $variants[$index]
            $source = Join-Path $resultRoot $variant.Name
            $relativeResults = "out/comparison-c$count-r$round"
            Write-Host "COMPARISON: $($variant.Name), TCP_NODELAY=1 on both ends, $count clients, round $round/$Repetitions"
            & (Join-Path $source "scripts/run-benchmark.ps1") -ClientCounts "$count" `
                -Repetitions 1 -WarmupMovesPerClient $WarmupMovesPerClient `
                -MeasuredMovesPerClient $MeasuredMovesPerClient -Port $Port `
                -OutputDirectory $relativeResults
            $raw = Get-ChildItem -LiteralPath (Join-Path $source $relativeResults) -Filter '*-raw.csv'
            if (@($raw).Count -ne 1) { throw "Expected one raw result file" }
            $row = Import-Csv -LiteralPath $raw.FullName
            if ([int64]$row.Samples -ne ([int64]$count * $MeasuredMovesPerClient)) {
                throw "Incomplete sample count"
            }
            $row.Run = $round
            $row | Add-Member NoteProperty Variant $variant.Name
            $row | Add-Member NoteProperty Commit $variant.Commit
            $row | Add-Member NoteProperty ServerTcpNoDelay 1
            $row | Add-Member NoteProperty ClientTcpNoDelay 1
            $rows.Add($row)
            $rows | Export-Csv -LiteralPath (Join-Path $resultRoot 'comparison-raw.csv') -NoTypeInformation -Encoding UTF8
        }
    }
}

function Get-MedianValue {
    param($Values)
    $sorted = @($Values | ForEach-Object {
        [double]::Parse($_, [System.Globalization.CultureInfo]::InvariantCulture)
    } | Sort-Object)
    $middle = [int][math]::Floor($sorted.Count / 2)
    if (($sorted.Count % 2) -eq 1) { return $sorted[$middle] }
    return ($sorted[$middle - 1] + $sorted[$middle]) / 2.0
}

$summary = foreach ($group in ($rows | Group-Object Variant, Clients)) {
    $runs = @($group.Group)
    [pscustomobject]@{
        Variant = $runs[0].Variant
        Clients = [int]$runs[0].Clients
        Repetitions = $runs.Count
        ServerTcpNoDelay = 1
        ClientTcpNoDelay = 1
        SamplesPerRun = [int64]$runs[0].Samples
        MedianCommandsPerSecond = Get-MedianValue $runs.CommandsPerSecond
        MedianP50Us = Get-MedianValue $runs.P50Us
        MedianP95Us = Get-MedianValue $runs.P95Us
        MedianP99Us = Get-MedianValue $runs.P99Us
    }
}
$summary | Export-Csv -LiteralPath (Join-Path $resultRoot 'comparison-summary.csv') -NoTypeInformation -Encoding UTF8
$summary | Format-Table -AutoSize
Write-Host "Completed comparison: $resultRoot"
