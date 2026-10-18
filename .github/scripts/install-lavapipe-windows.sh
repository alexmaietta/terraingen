#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
set -euo pipefail

# 26.2.4 or later: earlier releases free llvmpipe's memory on Windows
# with free() where _aligned_malloc() allocated it, which corrupts the
# heap at every vkFreeMemory.
mesa_version=26.2.4
mesa_sha256=351fc8c8b695878ffb3eaa044b3ead08672a48b1a045e3c3e3975811df0f6695
loader_version=1.4.363.0
loader_sha256=a25a927aa8b9f0371048f1861cf88ac3b9bc9b1fb332c42d897c8ab32695769a

vulkan="$RUNNER_TEMP/vulkan"
mkdir -p "$vulkan"
cd "$vulkan"
curl -fsSL -o mesa.7z "https://github.com/pal1000/mesa-dist-win/releases/download/$mesa_version/mesa3d-$mesa_version-release-msvc.7z"
curl -fsSL -o loader.zip "https://sdk.lunarg.com/sdk/download/$loader_version/windows/VulkanRT-X64-$loader_version-Components.zip"
echo "$mesa_sha256  mesa.7z" | sha256sum -c -
echo "$loader_sha256  loader.zip" | sha256sum -c -
7z x -y -omesa mesa.7z > /dev/null
7z x -y -oloader loader.zip > /dev/null
cp loader/*/x64/vulkan-1.dll mesa/x64/
cygpath -w "$vulkan/mesa/x64" >> "$GITHUB_PATH"
reg add 'HKLM\SOFTWARE\Khronos\Vulkan\Drivers' //f //t REG_DWORD //d 0 \
    //v "$(cygpath -w "$vulkan/mesa/x64/lvp_icd.x86_64.json")"
