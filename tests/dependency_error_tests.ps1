# R-028 acceptance: dependency-missing scenarios must fail at configure stage.
# Usage: powershell -File dependency_error_tests.ps1 -Cmake <cmake.exe> -Source <repo root> -WorkRoot <temp dir>
# Five scenarios: missing include / missing lib / missing DLL / git unavailable / wrong commit.
param(
    [Parameter(Mandatory=$true)][string]$Cmake,
    [Parameter(Mandatory=$true)][string]$Source,
    [Parameter(Mandatory=$true)][string]$WorkRoot
)

$ErrorActionPreference = 'Continue'
$script:failures = 0

# Use a unique child directory so stale .git data cannot affect a run.
if (-not $WorkRoot) { $WorkRoot = "$env:TEMP\edge_dep_test" }
$WorkRoot = Join-Path $WorkRoot ([System.Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $WorkRoot | Out-Null
Write-Output "[WorkRoot] $WorkRoot"

function New-FakeTree {
    param([string]$Dir, [bool]$Include, [bool]$Lib, [bool]$Dll)
    New-Item -ItemType Directory -Force -Path "$Dir\third_party\ffmpeg\include\libavformat" | Out-Null
    New-Item -ItemType Directory -Force -Path "$Dir\third_party\ffmpeg\lib" | Out-Null
    New-Item -ItemType Directory -Force -Path "$Dir\third_party\ffmpeg\bin" | Out-Null
    if ($Include) { Copy-Item "$Source\third_party\ffmpeg\include\libavformat\avformat.h" "$Dir\third_party\ffmpeg\include\libavformat\" }
    if ($Lib) {
        foreach ($l in @('avcodec.lib','avformat.lib','swscale.lib','avutil.lib')) {
            Copy-Item "$Source\third_party\ffmpeg\lib\$l" "$Dir\third_party\ffmpeg\lib\"
        }
    }
    if ($Dll) {
        foreach ($d in @('avcodec-63.dll','avformat-63.dll','avutil-61.dll','swscale-10.dll','swresample-7.dll')) {
            Copy-Item "$Source\third_party\ffmpeg\bin\$d" "$Dir\third_party\ffmpeg\bin\"
        }
    }
}

function Test-Configure-Fails {
    param([string]$Name, [scriptblock]$Setup, [string[]]$ExpectKeywords)
    $dir = Join-Path $WorkRoot ("dep-" + $Name)
    if (Test-Path $dir) { Remove-Item -Recurse -Force $dir }
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    & $Setup $dir

    # Native stderr must not throw under redirection (PS 5.1 quirk)
    $ErrorActionPreference = 'Continue'
    $out = & $Cmake -S $dir -B (Join-Path $dir "build") -G "Visual Studio 17 2022" -A x64 2>&1
    $text = ($out | Out-String)
    $failed = ($LASTEXITCODE -ne 0)
    Write-Output ("[{0}] configure exit={1}" -f $Name, $LASTEXITCODE)

    if (-not $failed) {
        Write-Output "[$Name] FAIL: configure should have failed but succeeded"
        $script:failures++
        return
    }
    foreach ($kw in $ExpectKeywords) {
        if ($text -notmatch [regex]::Escape($kw)) {
            Write-Output "[$Name] FAIL: error output missing keyword '$kw'"
            $script:failures++
            return
        }
    }
    # No silent fallback allowed: our diagnostics must contain recovery steps
    # Stable supplier URL / checkout text is an auditable recovery hint.
    if ($text -notmatch 'gyan\.dev|checkout') {
        Write-Output "[$Name] FAIL: missing recovery hint in diagnostics"
        $script:failures++
        return
    }
    Write-Output "[$Name] PASS"
}

# ---- scenario 1: missing include ----
Test-Configure-Fails -Name 'missing-include' -ExpectKeywords @('include') -Setup {
    param($d)
    New-FakeTree -Dir $d -Include:$false -Lib:$true -Dll:$true
    Copy-Item "$Source\CMakeLists.txt" $d
}

# ---- scenario 2: missing lib ----
Test-Configure-Fails -Name 'missing-lib' -ExpectKeywords @('avcodec') -Setup {
    param($d)
    New-FakeTree -Dir $d -Include:$true -Lib:$false -Dll:$true
    Copy-Item "$Source\CMakeLists.txt" $d
}

# ---- scenario 3: missing DLL ----
Test-Configure-Fails -Name 'missing-dll' -ExpectKeywords @('avcodec-63.dll') -Setup {
    param($d)
    New-FakeTree -Dir $d -Include:$true -Lib:$true -Dll:$false
    Copy-Item "$Source\CMakeLists.txt" $d
}

# ---- scenario 4: git unavailable (submodule without .git) ----
Test-Configure-Fails -Name 'git-unavailable' -ExpectKeywords @('c1d0e7a004015f23bc0233470b747b596f29b264') -Setup {
    param($d)
    New-FakeTree -Dir $d -Include:$true -Lib:$true -Dll:$true
    Copy-Item "$Source\CMakeLists.txt" $d
    New-Item -ItemType Directory -Force -Path "$d\third_party\llama.cpp\tools\mtmd" | Out-Null
    Set-Content "$d\third_party\llama.cpp\CMakeLists.txt" "# stub"
    Set-Content "$d\third_party\llama.cpp\tools\mtmd\mtmd.h" "// stub"
    # no .git created -> rev-parse must fail
}

# ---- scenario 5: wrong commit ----
Test-Configure-Fails -Name 'wrong-commit' -ExpectKeywords @('c1d0e7a004015f23bc0233470b747b596f29b264') -Setup {
    param($d)
    New-FakeTree -Dir $d -Include:$true -Lib:$true -Dll:$true
    Copy-Item "$Source\CMakeLists.txt" $d
    New-Item -ItemType Directory -Force -Path "$d\third_party\llama.cpp\tools\mtmd" | Out-Null
    Set-Content "$d\third_party\llama.cpp\CMakeLists.txt" "# stub"
    Set-Content "$d\third_party\llama.cpp\tools\mtmd\mtmd.h" "// stub"
    git -C "$d\third_party\llama.cpp" init | Out-Null
    git -C "$d\third_party\llama.cpp" -c user.name=t -c user.email=t@t commit --allow-empty -m init | Out-Null
}

if ($script:failures -gt 0) {
    Write-Output "[FAIL] dependency_error_tests: $($script:failures) scenario(s) failed"
    exit 1
}
Write-Output "[PASS] dependency_error_tests"
exit 0
