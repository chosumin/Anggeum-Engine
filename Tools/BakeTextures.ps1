# Bakes raw png/jpg assets into mipmapped, UASTC-encoded KTX2 siblings
# (zstd-supercompressed on disk). UASTC is color-space-agnostic and transcodes
# at load on a worker thread to the desktop block format (BC7) - the bake does
# not need to know which textures are sRGB. Runs post-build against the synced
# output Assets folder, so the source tree stays clean; the loader prefers a
# baked .ktx2 sibling when one exists.
param(
    [Parameter(Mandatory)][string]$AssetsDir,
    [Parameter(Mandatory)][string]$Toktx
)

$sources = Get-ChildItem $AssetsDir -Recurse -Include *.png,*.jpg |
    Where-Object { $_.Directory.Name -ne 'screenshot' }

$converted = 0
foreach ($src in $sources) {
    $out = [IO.Path]::ChangeExtension($src.FullName, '.ktx2')
    if ((Test-Path $out) -and (Get-Item $out).LastWriteTime -ge $src.LastWriteTime) {
        continue
    }

    & $Toktx --t2 --genmipmap --assign_oetf linear --assign_primaries none `
        --target_type RGBA --encode uastc --uastc_quality 1 --zcmp 3 `
        $out $src.FullName
    if ($LASTEXITCODE -ne 0) {
        Write-Error "BakeTextures: toktx failed on $($src.FullName)"
        exit 1
    }
    $converted++
}

Write-Host "BakeTextures: $converted baked, $($sources.Count) up to date total"
