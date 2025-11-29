Write-Host "Configuring ARENA for Profiling (RelWithDebInfo)..." -ForegroundColor Cyan
cmake -B build_profile -S . -DCMAKE_BUILD_TYPE=RelWithDebInfo

Write-Host "`nBuilding project..." -ForegroundColor Cyan
cmake --build build_profile --config RelWithDebInfo

Write-Host "`nBuild complete." -ForegroundColor Green
Write-Host "`n=== PROFILING INSTRUCTIONS ===" -ForegroundColor Yellow
Write-Host "1. To profile with Visual Studio:"
Write-Host "   Open build_profile\arena.sln in Visual Studio."
Write-Host "   Go to Debug -> Performance Profiler (Alt+F2)."
Write-Host "   Select 'CPU Usage' and click Start."
Write-Host "   Analyze the Flame Graph and Hot Path."
Write-Host ""
Write-Host "2. To profile with Intel VTune Profiler (if installed):"
Write-Host "   Run: vtune -collect hotspots -result-dir vtune_results -- .\build_profile\RelWithDebInfo\arena_core.exe"
Write-Host "   Open the results in the VTune GUI to analyze CPU cache misses and lock-free structure bottlenecks."
Write-Host ""
Write-Host "Target executable: .\build_profile\RelWithDebInfo\arena_core.exe"
