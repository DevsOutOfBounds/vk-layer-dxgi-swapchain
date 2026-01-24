import os

CONFIG = {
    "LAYER_NAME": "VK_LAYER_DOOB_dxgi_swapchain",
    "VERSION_MAJOR": 1,
    "VERSION_MINOR": 4,
    "VERSION_PATCH": 340,
    "DESCRIPTION": "Replaces the Vulkan swapchain with the DXGI swapchain - https://www.devsoutofbounds.com/"
}

FULL_VERSION_STR = f"{CONFIG['VERSION_MAJOR']}.{CONFIG['VERSION_MINOR']}.{CONFIG['VERSION_PATCH']}"
IMPL_VERSION_INT = 1
SPEC_VERSION_INT = 1

def write_if_changed(filename, new_content):
    if os.path.exists(filename):
        with open(filename, "r") as f:
            existing_content = f.read()
        
        if existing_content == new_content:
            print(f"Skipping {filename} (Up to date)")
            return 

    with open(filename, "w") as f:
        f.write(new_content)
        print(f"[Generator] Wrote {filename}")
