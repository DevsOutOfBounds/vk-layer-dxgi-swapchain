import json
import argparse
import os
from common import CONFIG
from common import FULL_VERSION_STR
from common import IMPL_VERSION_INT
from common import SPEC_VERSION_INT

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
            ]
        }
    }
    fullpath = os.path.abspath(output_json)
    with open(fullpath, "w") as f:
        json.dump(manifest, f, indent=4)
    print(f"[Generator] Wrote {fullpath}")


if __name__ == "__main__":
    args = get_args()
    generate_json(args.output_json, args.library_name)