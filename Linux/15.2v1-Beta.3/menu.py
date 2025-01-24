import os
import nuke

# Get the plugin path (directory where this script is located)
denoise_plugin_path = os.path.dirname(os.path.realpath(__file__))

# Add the 'icons' directory to the Nuke plugin path
nuke.pluginAddPath(os.path.join(denoise_plugin_path, 'icons'))

# Add 'DENOISE' menu in Nuke with the associated icon
nuke.menu('Nodes').addMenu('DENOISE', icon='denoise.png')

# Set the shared library extension based on the OS
lib_extension = '.so'
if os.name == 'nt':  # For Windows systems, use .dll
    lib_extension = '.dll'
    # Windows doesn't have RPATH, so we need to add the plugin lib folder to PATH
    os.environ['PATH'] += os.pathsep + os.path.join(denoise_plugin_path, 'lib')

# Check if the Denoiser shared library exists
denoiser_lib_path = os.path.join(denoise_plugin_path, 'Denoiser' + lib_extension)
if os.path.isfile(denoiser_lib_path):
    # Add the 'Denoiser' command to the 'DENOISE' menu
    nuke.menu('Nodes').addCommand('DENOISE/Denoiser', lambda: nuke.createNode('Denoiser'), icon='denoise.png')
    
    # Load the Denoiser library into Nuke
    try:
        nuke.load('Denoiser' + lib_extension)
    except Exception as e:
        nuke.message("Failed to load the Denoiser plugin: " + str(e))
else:
    # Provide feedback if the library is not found
    nuke.message("Denoiser plugin not found: " + denoiser_lib_path)

