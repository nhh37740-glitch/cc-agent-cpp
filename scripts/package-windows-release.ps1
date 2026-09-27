param(
    [string]$Version = "0.1.0",
    [string]$Configuration = "Release"
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root "build"
$dist = Join-Path $root "dist"
$stage = Join-Path $build "package-stage"
if (-not (Test-Path $build)) { throw "Build directory not found: $build" }
$exe = Get-ChildItem -Path $build -Filter "edge_agent.exe" -File -Recurse | Select-Object -First 1
if (-not $exe) { throw "edge_agent.exe was not produced by the Release build" }

if (Test-Path $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
New-Item -ItemType Directory -Path $stage | Out-Null
New-Item -ItemType Directory -Path $dist -Force | Out-Null
Copy-Item -LiteralPath $exe.FullName -Destination $stage
Get-ChildItem -Path $exe.DirectoryName -Filter "*.dll" -File | Copy-Item -Destination $stage
Copy-Item -Path (Join-Path $root "skills") -Destination $stage -Recurse
Copy-Item -LiteralPath (Join-Path $root "README.md") -Destination $stage
Copy-Item -LiteralPath (Join-Path $root "docs\MODULES.md") -Destination $stage

$sourceCommit = (& git -C $root rev-parse HEAD 2>$null)
if ($LASTEXITCODE -ne 0) { $sourceCommit = "unversioned" }
$files = Get-ChildItem -Path $stage -File -Recurse | Sort-Object FullName | ForEach-Object {
    [ordered]@{
        path = $_.FullName.Substring($stage.Length + 1).Replace('\', '/')
        bytes = $_.Length
        sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}
$manifest = [ordered]@{
    schemaVersion = 1
    project = "cc-agent-cpp"
    version = $Version
    platform = "windows-x64"
    configuration = $Configuration
    sourceCommit = $sourceCommit.Trim()
    binary = "edge_agent.exe"
    externalModels = @("models/*.gguf", "models/*.mmproj")
    files = @($files)
}
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $stage "manifest.json") -Encoding utf8
$archive = Join-Path $dist "cc-agent-cpp-$Version-windows-x64.zip"
$manifestPath = Join-Path $dist "manifest.json"
Copy-Item -LiteralPath (Join-Path $stage "manifest.json") -Destination $manifestPath -Force
if (Test-Path $archive) { Remove-Item -LiteralPath $archive -Force }
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $archive -CompressionLevel Optimal
$hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash  $(Split-Path -Leaf $archive)" | Set-Content -LiteralPath "$archive.sha256" -Encoding ascii
Write-Host "Packaged $archive"
