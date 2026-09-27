$url = "https://github.com/EsotericSoftware/spine-runtimes/archive/refs/heads/4.2.zip"
$zipPath = "spine.zip"
Write-Host "Downloading spine-runtimes 4.2..."
Invoke-WebRequest -Uri $url -OutFile $zipPath
Write-Host "Extracting..."
Expand-Archive -Path $zipPath -DestinationPath "spine_tmp" -Force
Write-Host "Updating spine files..."
if (Test-Path "spine") {
    Remove-Item -Recurse -Force "spine"
}
New-Item -ItemType Directory -Force -Path "spine" | Out-Null
Copy-Item -Path "spine_tmp\spine-runtimes-4.2\spine-cpp\spine-cpp\src\spine\*" -Destination "spine" -Force
Copy-Item -Path "spine_tmp\spine-runtimes-4.2\spine-cpp\spine-cpp\include\spine\*" -Destination "spine" -Force
Write-Host "Cleaning up..."
Remove-Item -Recurse -Force "spine_tmp"
Remove-Item -Force $zipPath
Write-Host "Done!"
