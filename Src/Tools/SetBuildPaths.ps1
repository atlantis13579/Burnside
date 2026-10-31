$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$build = Join-Path $repository 'Build'
# CMake's VS generator otherwise hardcodes object directories under the project directory.
Get-ChildItem -LiteralPath $build -Filter '*.vcxproj' | ForEach-Object {
    $file = $_.FullName
    $xml = [IO.File]::ReadAllText($file)
    $intermediate = '..\Intermediate\$(Configuration)\' + $_.BaseName + '\'
    $xml = [regex]::Replace($xml, '(<IntDir\b[^>]*>)[^<]*(</IntDir>)', {
        param($match)
        $match.Groups[1].Value + $intermediate + $match.Groups[2].Value
    })
    [IO.File]::WriteAllText($file, $xml, (New-Object Text.UTF8Encoding $false))
}
