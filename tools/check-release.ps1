# Checks a GScreen package before release.
#   -Versions   : CMake and csproj versions match (and match the git tag, if given)
#   -Signatures : every .exe/.dll in the package has a valid Authenticode signature
param(
    [string]$Package = "$PSScriptRoot\..\out\GScreen",
    [string]$Tag = "",
    [switch]$Versions,
    [switch]$Signatures
)
$ErrorActionPreference = 'Stop'
$root = Resolve-Path "$PSScriptRoot\.."

if ($Versions) {
    $cmake = Get-Content "$root\CMakeLists.txt" -Raw
    if ($cmake -notmatch 'project\(GScreen[^)]*VERSION\s+(\d+\.\d+\.\d+)\)') { throw 'No VERSION in CMakeLists.txt' }
    $cmakeVer = $Matches[1]
    [xml]$proj = Get-Content "$root\ui\GScreen.App\GScreen.App.csproj"
    $csVer = ($proj.Project.PropertyGroup | ForEach-Object { $_.Version } | Where-Object { $_ }) | Select-Object -First 1
    if ($cmakeVer -ne $csVer) { throw "Version mismatch: CMake $cmakeVer, csproj $csVer" }
    if ($Tag -and $Tag -ne "v$cmakeVer") { throw "Tag $Tag does not match version $cmakeVer" }
    Write-Output "version $cmakeVer"
    if ($env:GITHUB_OUTPUT) { "version=$cmakeVer" | Out-File -Append -Encoding utf8 $env:GITHUB_OUTPUT }
}

if ($Signatures) {
    $files = Get-ChildItem $Package -Recurse -Include *.exe, *.dll
    $bad = $files | Where-Object { (Get-AuthenticodeSignature $_.FullName).Status -ne 'Valid' }
    if ($bad) {
        $bad | ForEach-Object { Write-Output "UNSIGNED: $($_.Name)" }
        throw "$($bad.Count) of $($files.Count) binaries are not signed"
    }
    Write-Output "all $($files.Count) binaries signed"
}
