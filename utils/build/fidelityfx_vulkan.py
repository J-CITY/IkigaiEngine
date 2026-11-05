"""Build a patched Vulkan FSR runtime from the pinned, unmodified submodule.

The generated source/build live under utils/.pack. Fixes luma image format,
storage-buffer descriptor pool capacity and swapchain semaphore/UI lifetime.
"""

# Support direct CLI invocation from any working directory.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import zipfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
PIN='c6efa6bf7f2027b3ec94f28578bb5965eabb9e55'
BASE=ROOT/'utils/.pack/fidelityfx-vulkan-patched'
PATCHES=['luma-history-rgba16f','descriptor-pool-storage-buffer','present-semaphore-per-image','swapchain-resource-cleanup']
PATCH_REVISION=3
PRESENT_SIGNAL='                                toSignal.add(presenter->frameRenderedSemaphores[realSwapchainImageIndex]);'


def runtime_path():
    return BASE/'source/ffx-api/bin/amd_fidelityfx_vk.dll'


def runtime_ready():
    try:
        manifest=json.loads((BASE/'build-manifest.json').read_text(encoding='utf-8'))
        if (manifest.get('source')!=PIN or manifest.get('patches')!=PATCHES
                or manifest.get('patch_revision')!=PATCH_REVISION):return False
        dll=runtime_path()
        with dll.open('rb') as stream:
            if stream.read(2)!=b'MZ':return False
        expected=manifest.get('runtime_sha256')
        return not expected or hashlib.sha256(dll.read_bytes()).hexdigest()==expected
    except (OSError,ValueError,TypeError,AttributeError):return False


def vulkan_build_environment():
    env=dict(os.environ)
    if not env.get('VULKAN_SDK'):
        compiler=shutil.which('glslangValidator')
        candidates=[Path(compiler).resolve().parent.parent] if compiler else []
        candidates.extend(sorted(Path('C:/VulkanSDK').glob('*'),
                                 key=lambda p:tuple(int(x) if x.isdigit() else 0 for x in p.name.split('.')),
                                 reverse=True))
        for candidate in candidates:
            if (candidate/'Include/vulkan/vulkan.h').is_file() and (candidate/'Bin/glslangValidator.exe').is_file():
                env['VULKAN_SDK']=str(candidate)
                break
    if not env.get('VULKAN_SDK') or not (Path(env['VULKAN_SDK'])/'Bin/glslangValidator.exe').is_file():
        raise RuntimeError('Install the Windows Vulkan SDK or set VULKAN_SDK before building FSR Vulkan')
    env['PATH']=str(Path(env['VULKAN_SDK'])/'Bin')+os.pathsep+env.get('PATH','')
    return env

def run(*args,**kwargs):
    print(' '.join(map(str,args)),flush=True)
    return subprocess.run(list(map(str,args)),check=True,**kwargs)

def replace(path,before,after):
    text=path.read_text(encoding='utf-8-sig')
    # Some patches contain the original text; match the completed patch first.
    if text.count(after)==1 and text.count(before)==int(before in after):return
    if text.count(before)!=1:raise RuntimeError(f'Patch does not match pinned source: {path}')
    path.write_text(text.replace(before,after),encoding='utf-8')


def repair_reused_present_patch(path):
    text=path.read_text(encoding='utf-8-sig')
    repaired=text
    duplicate=PRESENT_SIGNAL+'\n'+PRESENT_SIGNAL
    while duplicate in repaired:
        repaired=repaired.replace(duplicate,PRESENT_SIGNAL)
    if repaired.count(PRESENT_SIGNAL)>1:raise RuntimeError(f'Ambiguous present semaphore patch: {path}')
    if repaired!=text:path.write_text(repaired,encoding='utf-8')


def patch_swapchain_cleanup(swapchain):
    implementation=swapchain/'FrameInterpolationSwapchainVK.cpp'
    replace(implementation,
            '    // semaphores used for present to wait until rendering is over\n'
            '    for (uint32_t i = 0; i < FFX_FRAME_INTERPOLATION_SWAP_CHAIN_MAX_BUFFER_COUNT; ++i)\n'
            '    {\n'
            '        res = createSemaphoreFormat(presentInfo.frameRenderedSemaphores[i], "AMD FSR FrameRenderedSemaphore %d", i);\n'
            '        EXIT_ON_VKRESULT_NOT_SUCCESS(res);\n'
            '    }',
            '    // Present semaphores are created once the real swapchain image count is known.')
    replace(implementation,
            '    // delete real swapchain\n',
            '    releaseUiBlitGpuResources(nullptr);\n\n    // delete real swapchain\n')
    replace(swapchain/'FrameInterpolationSwapchainVK_UiComposition.h',
            'VkFormat ffxGetVkFormatFromSurfaceFormat(FfxSurfaceFormat fmt);',
            'VkFormat ffxGetVkFormatFromSurfaceFormat(FfxSurfaceFormat fmt);\n'
            'void releaseUiBlitGpuResources(const VkAllocationCallbacks* pAllocator);')
    composition=swapchain/'FrameInterpolationSwapchainVK_UiComposition.cpp'
    replace(composition,
            '    for (uint32_t i = 0; i < _countof(s_uiCompositionDescriptorSets); ++i)\n'
            '    {\n'
            '        vkFreeDescriptorSets(s_uiCompositionDevice, s_uiCompositionDescriptorPool, 1, &s_uiCompositionDescriptorSets[i]);\n'
            '        s_uiCompositionDescriptorSets[i] = VK_NULL_HANDLE;\n'
            '    }',
            '    if (s_uiCompositionDevice == VK_NULL_HANDLE)\n'
            '        return;\n\n'
            '    // Destroying the pool frees all of its descriptor sets.\n'
            '    for (auto& descriptorSet : s_uiCompositionDescriptorSets)\n'
            '        descriptorSet = VK_NULL_HANDLE;')
    replace(composition,
            '    for (uint32_t i = 0; i < _countof(s_uiCompositionImageViews); ++i)\n'
            '    {\n'
            '        vkDestroyImageView(s_uiCompositionDevice, s_uiCompositionImageViews[i], pAllocator);\n'
            '        s_uiCompositionImageViews[i] = VK_NULL_HANDLE;\n'
            '    }\n'
            '    for (uint32_t i = 0; i < _countof(s_uiCompositionFramebuffers); ++i)\n'
            '    {\n'
            '        vkDestroyFramebuffer(s_uiCompositionDevice, s_uiCompositionFramebuffers[i], pAllocator);\n'
            '        s_uiCompositionFramebuffers[i] = VK_NULL_HANDLE;\n'
            '    }',
            '    for (auto& framebuffer : s_uiCompositionFramebuffers)\n'
            '    {\n'
            '        vkDestroyFramebuffer(s_uiCompositionDevice, framebuffer, pAllocator);\n'
            '        framebuffer = VK_NULL_HANDLE;\n'
            '    }\n'
            '    for (auto& imageView : s_uiCompositionImageViews)\n'
            '    {\n'
            '        vkDestroyImageView(s_uiCompositionDevice, imageView, pAllocator);\n'
            '        imageView = VK_NULL_HANDLE;\n'
            '    }\n'
            '    s_uiCompositionDevice = VK_NULL_HANDLE;')
    replace(composition,
            '        if (s_uiCompositionDevice != VK_NULL_HANDLE)\n'
            '        {\n'
            '            // new device\n'
            '            releaseUiBlitGpuResources(pAllocator);\n'
            '        }\n'
            '        else\n'
            '        {\n'
            '            s_uiCompositionDevice = device;\n'
            '        }',
            '        releaseUiBlitGpuResources(pAllocator);\n'
            '        s_uiCompositionDevice = device;')


def build_runtime(generator='Visual Studio 17 2022',parallel=3,reuse_source=False):
    sdk=ROOT/'3rd/FidelityFX-Vulkan'
    head=subprocess.check_output(['git','-C',str(sdk),'rev-parse','HEAD'],text=True).strip()
    if head!=PIN:raise RuntimeError('SDK pin differs; review compatibility patches before rebuilding')
    BASE.mkdir(parents=True,exist_ok=True)
    archive=BASE/'source.zip';source=BASE/'source'
    if reuse_source:
        if json.loads((BASE/'build-manifest.json').read_text())['source']!=PIN:raise RuntimeError('Generated source pin differs')
    else:
        run('git','-C',sdk,'archive','--format=zip',f'--output={archive}',PIN,'sdk','ffx-api','LICENSE.txt')
        with zipfile.ZipFile(archive) as content:
            for item in content.infolist():
                target=(source/item.filename).resolve()
                if source.resolve() not in target.parents:raise RuntimeError('Unsafe archive path')
            content.extractall(source)
    replace(source/'sdk/include/FidelityFX/gpu/fsr3upscaler/ffx_fsr3upscaler_callbacks_glsl.h',
            'binding = FSR3UPSCALER_BIND_UAV_LUMA_HISTORY, rgba8)',
            'binding = FSR3UPSCALER_BIND_UAV_LUMA_HISTORY, rgba16f)')
    replace(source/'sdk/src/backends/vk/ffx_vk.cpp',
            'descriptorPoolCreateInfo.poolSizeCount = 5;',
            'descriptorPoolCreateInfo.poolSizeCount = sizeof(poolSizes) / sizeof(poolSizes[0]);')
    swapchain=source/'sdk/src/backends/vk/FrameInterpolationSwapchain/FrameInterpolationSwapchainVK.cpp'
    repair_reused_present_patch(swapchain)
    replace(swapchain,'                            toSignal.add(presenter->frameRenderedSemaphores[0]);  // not a timeline semaphore',
            '                            // Signal the semaphore of the acquired WSI image below.')
    replace(swapchain,'                                toWait.add(acquireSemaphore);',
            PRESENT_SIGNAL+'\n                                toWait.add(acquireSemaphore);')
    replace(swapchain,'presentToSwapChain(presenter, realSwapchainImageIndex);',
            'presentToSwapChain(presenter, realSwapchainImageIndex, realSwapchainImageIndex);')
    patch_swapchain_cleanup(swapchain.parent)
    env=vulkan_build_environment()
    run('cmake','-S',source/'ffx-api','-B',BASE/'build','-G',generator,'-A','x64','-DFFX_API_BACKEND=VK_X64',env=env)
    run('cmake','--build',BASE/'build','--config','Release','--target','amd_fidelityfx_vk','--parallel',parallel,env=env)
    dll=runtime_path()
    if not dll.exists():raise RuntimeError('Vulkan runtime was not produced')
    (BASE/'build-manifest.json').write_text(json.dumps({'source':PIN,'patches':PATCHES,'runtime':str(dll),
                                                    'patch_revision':PATCH_REVISION,
                                                    'runtime_sha256':hashlib.sha256(dll.read_bytes()).hexdigest()},indent=2),encoding='utf-8')
    print(f'Patched Vulkan runtime: {dll}',flush=True)
    return dll


def ensure_runtime(generator='Visual Studio 17 2022'):
    sdk=ROOT/'3rd/FidelityFX-Vulkan'
    head=subprocess.check_output(['git','-C',str(sdk),'rev-parse','HEAD'],text=True).strip()
    if head!=PIN:raise RuntimeError('SDK pin differs; review compatibility patches before rebuilding')
    if runtime_ready():
        print(f'Patched FSR Vulkan runtime is ready: {runtime_path()}',flush=True)
        return runtime_path()
    try:
        manifest=json.loads((BASE/'build-manifest.json').read_text(encoding='utf-8'))
        reuse=manifest.get('source')==PIN and (BASE/'source/ffx-api/CMakeLists.txt').is_file()
    except (OSError,ValueError,TypeError,AttributeError):reuse=False
    cache=BASE/'build/CMakeCache.txt'
    if cache.is_file():
        for line in cache.read_text(encoding='utf-8').splitlines():
            if line.startswith('CMAKE_GENERATOR:INTERNAL='):
                generator=line.split('=',1)[1]
                break
    return build_runtime(generator=generator,reuse_source=reuse)


def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--generator',default='Visual Studio 17 2022')
    parser.add_argument('--parallel',type=int,default=3)
    parser.add_argument('--reuse-source',action='store_true',help='Reuse the generated SDK source with the same verified pin')
    args=parser.parse_args(argv)
    build_runtime(args.generator,args.parallel,args.reuse_source)

if __name__=='__main__':main()
