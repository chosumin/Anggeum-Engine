# Bakes raw png/jpg assets into mipmapped, UASTC-encoded KTX2 siblings
# (zstd-supercompressed on disk). UASTC transcodes at load on a worker thread
# to the desktop block format (BC7). Runs post-build against the synced output
# Assets folder, so the source tree stays clean; the loader prefers a baked
# .ktx2 sibling when one exists.
#
# Color management: glTF says baseColor and emissive are sRGB, everything else
# is linear data. Those two get the sRGB transfer function, so toktx filters
# their mips in linear light and the loader's transcode lands on BC7_SRGB.
param(
    [Parameter(Mandatory)][string]$AssetsDir,
    [Parameter(Mandatory)][string]$Toktx
)

$srgbImages = @{}
foreach ($gltf in Get-ChildItem $AssetsDir -Recurse -Include *.gltf) {
    $doc = Get-Content $gltf.FullName -Raw | ConvertFrom-Json
    if (-not $doc.materials -or -not $doc.textures -or -not $doc.images) { continue }

    $textureIndices = @()
    foreach ($material in $doc.materials) {
        if ($material.pbrMetallicRoughness -and $material.pbrMetallicRoughness.baseColorTexture) {
            $textureIndices += $material.pbrMetallicRoughness.baseColorTexture.index
        }
        if ($material.emissiveTexture) {
            $textureIndices += $material.emissiveTexture.index
        }
    }

    foreach ($index in $textureIndices) {
        $uri = $doc.images[$doc.textures[$index].source].uri
        if (-not $uri -or $uri.StartsWith('data:')) { continue }

        $path = Join-Path $gltf.DirectoryName ([Uri]::UnescapeDataString($uri))
        $srgbImages[[IO.Path]::GetFullPath($path)] = $true
    }
}

$sources = Get-ChildItem $AssetsDir -Recurse -Include *.png,*.jpg |
    Where-Object { $_.Directory.Name -ne 'screenshot' }

# A changed bake recipe (this script) re-bakes everything.
$recipeTime = (Get-Item $PSCommandPath).LastWriteTime

$converted = 0
foreach ($src in $sources) {
    $out = [IO.Path]::ChangeExtension($src.FullName, '.ktx2')
    if (Test-Path $out) {
        $outTime = (Get-Item $out).LastWriteTime
        if ($outTime -ge $src.LastWriteTime -and $outTime -ge $recipeTime) {
            continue
        }
    }

    $oetf = if ($srgbImages.ContainsKey($src.FullName)) { 'srgb' } else { 'linear' }

    & $Toktx --t2 --genmipmap --assign_oetf $oetf --assign_primaries none `
        --target_type RGBA --encode uastc --uastc_quality 1 --zcmp 3 `
        $out $src.FullName
    if ($LASTEXITCODE -ne 0) {
        Write-Error "BakeTextures: toktx failed on $($src.FullName)"
        exit 1
    }
    $converted++
}

Write-Host "BakeTextures: $converted baked ($($srgbImages.Count) sRGB sources), $($sources.Count) total"
