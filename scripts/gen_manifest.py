import json
import argparse
import os
from common import CONFIG
from common import FULL_VERSION_STR
from common import IMPL_VERSION_INT
from common import SPEC_VERSION_INT
# https://github.com/KhronosGroup/Vulkan-Loader/blob/main/docs/LoaderLayerInterface.md#layer-manifest-file-format

LAYER_SETTINGS = [
    {
        "key": "enable_dxgi_interop",
        "label": "Force Enable DXGI Interop",
        "description": "If enabled, ALL swapchains will be backed by DXGI surfaces.",
        "type": "BOOL",
        "default": False,
        "view": "STANDARD" # Options: STANDARD, ADVANCED, HIDDEN
    },
    {
        "key": "force_dxgi_version",
        "label": "Force DXGI Version",
        "description": "Force a specific DXGI factory version.",
        "type": "ENUM",
        "default": "default",
        "flags": [
            {
                "key": "default",
                "label": "Default",
                "description": "Let the application decide."
            },
            {
                "key": "d3d11",
                "label": "Direct3D 11",
                "description": "Force D3D11 / DXGI 1.2"
            },
            {
                "key": "d3d12",
                "label": "Direct3D 12",
                "description": "Force D3D12 / DXGI 1.4"
            }
        ],
        "view": "STANDARD"
    },
    {
        "key": "log_file_path",
        "label": "Log File Path",
        "description": "Where to save the internal layer logs.",
        "type": "SAVE_FILE", # Creates a file picker in vkconfig
        "default": "./doob_layer_log.txt",
        "view": "ADVANCED"
    }
]

def get_args():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output_json', type=str)
    parser.add_argument('--library_name', type=str)
    args = parser.parse_args()
    return args
def generate_json(output_json, library_name):
    manifest = {
        "file_format_version": "1.1.2",
        "layer": {
            "name": CONFIG['LAYER_NAME'],
            "type": "GLOBAL",
            "library_path": f".\\{library_name}.dll", 
            "api_version": FULL_VERSION_STR,
            "implementation_version": str(IMPL_VERSION_INT),
            "description": CONFIG['DESCRIPTION'],
            "functions": {
                "vkGetInstanceProcAddr": "DOOB_GetInstanceProcAddr",
                "vkGetDeviceProcAddr": "DOOB_GetDeviceProcAddr"
            },
            "device_extensions": [
                {
                    "name": "VK_DOOB_dxgi_swapchain",
                    "spec_version": str(SPEC_VERSION_INT),
                    "entrypoints": [
                        "vkGetDxgiSwapchainHandleDOOB"
                    ]
                }
            ],
            "features": {
                "settings": LAYER_SETTINGS
            }
        }
    }
    fullpath = os.path.abspath(output_json)
    with open(fullpath, "w") as f:
        json.dump(manifest, f, indent=4)
    print(f"[Generator] Wrote {fullpath}")


if __name__ == "__main__":
    args = get_args()
    generate_json(args.output_json, args.library_name)