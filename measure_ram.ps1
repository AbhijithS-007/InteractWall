$p = Get-Process web_wallpaper -ErrorAction SilentlyContinue
if (-not $p) {
    Write-Output "web_wallpaper is not running."
    exit
}

$parentPid = $p.Id

# Find all msedgewebview2.exe processes
$wv2 = Get-WmiObject Win32_Process -Filter "Name='msedgewebview2.exe'"

$totalWorkingSet = $p.WorkingSet
$count = 0

foreach ($w in $wv2) {
    # Check if this WebView2 process is in the process tree of web_wallpaper
    $currentPid = $w.ProcessId
    $parent = $w.ParentProcessId
    $isChild = $false
    
    # Simple check: is its immediate parent web_wallpaper, or is its parent another msedgewebview2 whose parent is web_wallpaper?
    if ($parent -eq $parentPid) {
        $isChild = $true
    } else {
        # Check if parent is also a msedgewebview2 which is a child
        $parentProc = Get-WmiObject Win32_Process -Filter "ProcessId=$parent"
        if ($parentProc -and $parentProc.Name -eq 'msedgewebview2.exe') {
            if ($parentProc.ParentProcessId -eq $parentPid) {
                $isChild = $true
            }
        }
    }
    
    if ($isChild) {
        # Also need to check further descendants, but usually 2 levels is enough
        # Actually WebView2 creates a browser process (child of app), and then renderer/GPU processes (children of browser process)
        $totalWorkingSet += [uint64]$w.WorkingSetSize
        $count++
    }
}

Write-Output "web_wallpaper.exe RAM: $([math]::Round($p.WorkingSet / 1MB, 2)) MB"
Write-Output "WebView2 Processes ($count): $([math]::Round(($totalWorkingSet - $p.WorkingSet) / 1MB, 2)) MB"
Write-Output "Total RAM (Idle): $([math]::Round($totalWorkingSet / 1MB, 2)) MB"
