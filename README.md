# NukeCGDenoiser

![Denoiser Node Usage](images/denoiser_node_usage.png)

A Nuke plugin for denoising CG renders using Intel's Open Image Denoise library.

This is a fork of [mateuszwojt/NukeCGDenoiser](https://github.com/mateuszwojt/NukeCGDenoiser) with the following improvements:

- **Portable CPU-only build** – OIDN is statically linked, no external dependencies required
- **Fixed overscan handling** – properly handles images with overscan/data window
- **Updated to OIDN 2.3.3** – latest version of Intel Open Image Denoise

## Supported Platforms

| Platform | Status |
|----------|--------|
| Linux    | ✅ Supported |
| macOS    | ✅ Supported |
| Windows  | ❌ Not available (static linking issues with OIDN 2.3.3) |

## Installation

Simply copy the `Denoiser.so` (Linux) or `Denoiser.dylib` (macOS) to your Nuke plugin path. No additional libraries or dependencies are required – everything is statically linked.

## Usage

1. Create a Denoiser node (`Tab` → type "Denoiser")
2. Connect your inputs:
   - **beauty** – the noisy rendered image (required)
   - **albedo** – albedo/diffuse AOV (optional, improves quality)
   - **normal** – world-space normals AOV (optional, improves quality)

The node automatically denoises your render with optimized settings. HDR images are fully supported.

## Requirements (for building from source)

- CMake 3.13 or later
- Nuke /14.x/15.x/16.x
- [Intel's OpenImageDenoise 2.3.3](https://github.com/RenderKit/oidn) (CPU-only build)


## Limitations

- Only RGB channels are processed; alpha is passed through from the input
- Input AOVs should be at the same resolution for best results

## Credits

- Original plugin by [Mateusz Wojt](https://github.com/mateuszwojt)
- Portable build and fixes by [Peter Mercell](https://github.com/petermercell)
- Denoising powered by [Intel Open Image Denoise](https://www.openimagedenoise.org/)

## License

MIT License – see [LICENSE](LICENSE) for details.
