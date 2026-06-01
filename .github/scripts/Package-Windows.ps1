[CmdletBinding()]
param(
    [ValidateSet('x64')]
    [string] $Target = 'x64',
    [ValidateSet('Debug', 'RelWithDebInfo', 'Release', 'MinSizeRel')]
    [string] $Configuration = 'RelWithDebInfo'
)

$ErrorActionPreference = 'Stop'

if ( $DebugPreference -eq 'Continue' ) {
    $VerbosePreference = 'Continue'
    $InformationPreference = 'Continue'
}

if ( $env:CI -eq $null ) {
    throw "Package-Windows.ps1 requires CI environment"
}

if ( ! ( [System.Environment]::Is64BitOperatingSystem ) ) {
    throw "Packaging script requires a 64-bit system to build and run."
}

if ( $PSVersionTable.PSVersion -lt '7.2.0' ) {
    Write-Warning 'The packaging script requires PowerShell Core 7. Install or upgrade your PowerShell version: https://aka.ms/pscore6'
    exit 2
}

function Package {
    trap {
        Write-Error $_
        exit 2
    }

    $ScriptHome = $PSScriptRoot
    $ProjectRoot = Resolve-Path -Path "$PSScriptRoot/../.."
    $BuildSpecFile = "${ProjectRoot}/buildspec.json"

    $UtilityFunctions = Get-ChildItem -Path $PSScriptRoot/utils.pwsh/*.ps1 -Recurse

    foreach( $Utility in $UtilityFunctions ) {
        Write-Debug "Loading $($Utility.FullName)"
        . $Utility.FullName
    }

    $BuildSpec = Get-Content -Path ${BuildSpecFile} -Raw | ConvertFrom-Json
    $ProductName = $BuildSpec.name
    $ProductVersion = $BuildSpec.version

    $OutputName = "${ProductName}-${ProductVersion}-windows-${Target}"

    $RemoveArgs = @{
        ErrorAction = 'SilentlyContinue'
        Path = @(
            "${ProjectRoot}/release/${ProductName}-*-windows-*.zip"
        )
    }

    Remove-Item @RemoveArgs

    Log-Group "Archiving ${ProductName}..."
    $CompressArgs = @{
        Path = (Get-ChildItem -Path "${ProjectRoot}/release/${Configuration}" -Exclude "${OutputName}*.*")
        CompressionLevel = 'Optimal'
        DestinationPath = "${ProjectRoot}/release/${OutputName}.zip"
        Verbose = ($Env:CI -ne $null)
    }
    Compress-Archive -Force @CompressArgs
    Log-Group

    Log-Group "Building Windows installer for ${ProductName}..."

    $IsccPath = (Get-Command 'iscc' -ErrorAction SilentlyContinue).Source
    if ( -not $IsccPath ) {
        foreach ( $Candidate in @(
            "${Env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
            "${Env:ProgramFiles}\Inno Setup 6\ISCC.exe"
        ) ) {
            if ( Test-Path $Candidate ) { $IsccPath = $Candidate; break }
        }
    }

    if ( $IsccPath ) {
        $IssFile = "${ProjectRoot}/build-aux/installer-Windows.iss"
        $SourceDir = "${ProjectRoot}/release/${Configuration}/${ProductName}"

        Invoke-External $IsccPath `
            "/DMyAppName=${ProductName}" `
            "/DMyAppVersion=${ProductVersion}" `
            "/DSourceDir=${SourceDir}" `
            "/DOutputDir=${ProjectRoot}/release" `
            $IssFile
    } else {
        Write-Warning 'Inno Setup (ISCC.exe) not found; skipping installer build.'
    }

    Log-Group
}

Package
