# Bootstrap uv without requiring Python or changing the user's PATH.
$ErrorActionPreference = 'Stop'
$pythonArguments = @($args)
$projectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$env:UV_CACHE_DIR = Join-Path $projectRoot 'utils/.pack/uv-cache'
$env:UV_PYTHON_INSTALL_DIR = Join-Path $projectRoot '.python'
$env:UV_PYTHON_PREFERENCE = 'only-managed'
$env:UV_PROJECT_ENVIRONMENT = Join-Path $projectRoot '.venv'

$uvCommand = Get-Command uv -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
if ($uvCommand) {
    $uvPath = $uvCommand.Source
} else {
    $uvDirectory = Join-Path $projectRoot 'utils/.pack/uv-tools'
    $uvPath = Join-Path $uvDirectory 'uv.exe'
    if (-not (Test-Path -LiteralPath $uvPath -PathType Leaf)) {
        [System.IO.Directory]::CreateDirectory($uvDirectory) | Out-Null
        $installer = Join-Path $uvDirectory 'install.ps1'
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        Invoke-WebRequest -UseBasicParsing -Uri 'https://astral.sh/uv/install.ps1' -OutFile $installer
        $env:UV_INSTALL_DIR = $uvDirectory
        $env:UV_NO_MODIFY_PATH = '1'
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $installer
        if ($LASTEXITCODE -ne 0) {
            exit $LASTEXITCODE
        }
    }
}

Remove-Item Env:VIRTUAL_ENV, Env:PYTHONHOME, Env:PYTHONPATH -ErrorAction SilentlyContinue
$env:PYTHONNOUSERSITE = '1'
$env:IKIGAI_PROJECT_PYTHON = '1'
$uvArguments = @('run', '--locked')
if ($pythonArguments.Count -gt 0 -and $pythonArguments[0] -eq '--tests') {
    $uvArguments += @('--group', 'tests', '--extra', 'content')
    $pythonArguments = @($pythonArguments | Select-Object -Skip 1)
} elseif ($pythonArguments.Count -gt 0 -and $pythonArguments[0] -eq '--content') {
    $uvArguments += @('--extra', 'content')
    $pythonArguments = @($pythonArguments | Select-Object -Skip 1)
}
if ($pythonArguments.Count -eq 0) {
    $pythonArguments = @('run_vs.py', '--help')
}

Push-Location -LiteralPath $projectRoot
try {
    & $uvPath @uvArguments python @pythonArguments
    $exitCode = $LASTEXITCODE
} finally {
    Pop-Location
}
exit $exitCode
