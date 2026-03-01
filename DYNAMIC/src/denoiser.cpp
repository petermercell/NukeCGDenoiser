// Copyright (c) 2021-2024 Mateusz Wojt
// Modified for dynamic OIDN 2.4 with GPU support

#include "denoiser.h"
#include <OpenImageDenoise/oidn.hpp>
#include <iostream>
#include <iomanip>
#include <cstring>

// Map dropdown index to OIDN device type
static oidn::DeviceType getOidnDeviceType(int idx)
{
    switch (idx) {
    case 0:  return oidn::DeviceType::Default;  // Best available
    case 1:  return oidn::DeviceType::CPU;
    case 2:  return oidn::DeviceType::SYCL;
    case 3:  return oidn::DeviceType::CUDA;
    case 4:  return oidn::DeviceType::HIP;
    default: return oidn::DeviceType::Default;
    }
}

// Map quality dropdown index to OIDN quality enum
static oidn::Quality getOidnQuality(int idx)
{
    switch (idx) {
    case 0:  return oidn::Quality::Default;
    case 1:  return oidn::Quality::Balanced;
    case 2:  return oidn::Quality::High;
    default: return oidn::Quality::Default;
    }
}

DenoiserIop::DenoiserIop(Node *node) : PlanarIop(node)
{
    m_deviceTypeIdx = 0;     // Default (best available - will pick GPU if present)
    m_qualityIdx = 2;        // High quality by default
    m_bHDR = true;
    m_bCleanAux = false;     // Assume noisy auxiliary by default
    m_numThreads = 0;        // Auto-detect
    m_maxMem = 0.f;          // No limit

    m_width = 0;
    m_height = 0;

    m_lastDeviceTypeIdx = -1; // Force initial device creation

    m_defaultChannels = Mask_RGB;
    m_defaultNumberOfChannels = m_defaultChannels.size();

    setupDevice();
}

void DenoiserIop::setupDevice()
{
    try
    {
        // Release previous device if switching
        releaseDevice();

        oidn::DeviceType devType = getOidnDeviceType(m_deviceTypeIdx);

        // Check if the requested device type is physically available
        int numDevices = oidnGetNumPhysicalDevices();
        std::cout << "[Denoiser] Number of OIDN physical devices: " << numDevices << std::endl;

        for (int i = 0; i < numDevices; i++) {
            oidn::DeviceType type = static_cast<oidn::DeviceType>(
                oidnGetPhysicalDeviceInt(i, "type"));
            const char* name = oidnGetPhysicalDeviceString(i, "name");

            std::string typeName;
            switch (type) {
            case oidn::DeviceType::CPU:  typeName = "CPU"; break;
            case oidn::DeviceType::SYCL: typeName = "SYCL"; break;
            case oidn::DeviceType::CUDA: typeName = "CUDA"; break;
            case oidn::DeviceType::HIP:  typeName = "HIP"; break;
            default: typeName = "Unknown"; break;
            }
            std::cout << "[Denoiser]   Device " << i << ": " << name
                      << " (type: " << typeName << ")" << std::endl;
        }

        // Create device
        m_device = oidn::newDevice(devType);

        const char *errorMessage;
        if (m_device.getError(errorMessage) != oidn::Error::None)
            throw std::runtime_error(errorMessage);

        // CPU-specific settings
        if (devType == oidn::DeviceType::CPU || devType == oidn::DeviceType::Default) {
            m_device.set("numThreads", m_numThreads);
        }

        m_device.commit();

        // Report which device was actually selected
        const char* deviceName = "Unknown";
        // After commit, check actual device type
        std::cout << "[Denoiser] Device created successfully" << std::endl;

        // Create the RT filter
        m_filter = m_device.newFilter("RT");

        m_lastDeviceTypeIdx = m_deviceTypeIdx;
    }
    catch (const std::exception &e)
    {
        std::string message = e.what();
        std::cerr << "[Denoiser] Device setup error: " << message << std::endl;
        error("[OIDN]: %s", message.c_str());
    }
}

void DenoiserIop::releaseDevice()
{
    // Release in reverse order
    m_filter.release();
    m_outputBuffer.release();
    m_normalBuffer.release();
    m_albedoBuffer.release();
    m_colorBuffer.release();
    m_device.release();
}

void DenoiserIop::setupFilter()
{
    try
    {
        // Recreate filter if device changed
        if (m_lastDeviceTypeIdx != m_deviceTypeIdx) {
            setupDevice();
        }

        // If filter was released during device recreation, create a new one
        if (!m_filter) {
            m_filter = m_device.newFilter("RT");
        }

        // Set images
        m_filter.setImage("color", m_colorBuffer, oidn::Format::Float3, m_width, m_height);
        m_filter.setImage("output", m_outputBuffer, oidn::Format::Float3, m_width, m_height);
        m_filter.setImage("albedo", m_albedoBuffer, oidn::Format::Float3, m_width, m_height);
        m_filter.setImage("normal", m_normalBuffer, oidn::Format::Float3, m_width, m_height);

        // Set filter parameters
        m_filter.set("hdr", m_bHDR);
        m_filter.set("cleanAux", m_bCleanAux);
        m_filter.set("quality", getOidnQuality(m_qualityIdx));

        if (m_maxMem > 0.f) {
            m_filter.set("maxMemoryMB", static_cast<int>(m_maxMem));
        }

        m_filter.commit();
    }
    catch (const std::exception &e)
    {
        std::string message = e.what();
        error("[OIDN]: %s", message.c_str());
    }
}

void DenoiserIop::executeFilter()
{
    try
    {
        m_filter.execute();

        // Check for errors after execution
        const char *errorMessage;
        if (m_device.getError(errorMessage) != oidn::Error::None) {
            error("[OIDN execution error]: %s", errorMessage);
        }
    }
    catch (const std::exception &e)
    {
        std::string message = e.what();
        error("[OIDN]: %s", message.c_str());
    }
}

void DenoiserIop::knobs(Knob_Callback f)
{
    Divider(f, "Device");
    Enumeration_knob(f, &m_deviceTypeIdx, deviceTypeNames, "device_type", "Device");
    Tooltip(f, "Select the compute device.\n"
               "Default: Automatically picks the best available (GPU preferred).\n"
               "CUDA: NVIDIA GPUs\n"
               "SYCL: Intel GPUs (Arc, Data Center)\n"
               "HIP: AMD GPUs\n"
               "CPU: Fallback, always available.");
    SetFlags(f, Knob::STARTLINE);

    Divider(f, "Quality");
    Enumeration_knob(f, &m_qualityIdx, qualityNames, "quality", "Quality");
    Tooltip(f, "Denoising quality.\n"
               "High: Best quality, slower.\n"
               "Balanced: Good quality, faster.\n"
               "Default: Let OIDN decide.");

    Bool_knob(f, &m_bHDR, "hdr", "HDR");
    Tooltip(f, "Enable for HDR (high dynamic range) images.\n"
               "Works fine for LDR too, so leave enabled unless you see artifacts.");
    SetFlags(f, Knob::STARTLINE);

    Bool_knob(f, &m_bCleanAux, "clean_aux", "Clean auxiliary");
    Tooltip(f, "Enable if your albedo and normal passes are noise-free\n"
               "(e.g., from a path tracer's separate AOV output).\n"
               "This can improve denoising quality.");

    Divider(f, "Advanced");
    Int_knob(f, &m_numThreads, "num_threads", "CPU Threads");
    Tooltip(f, "Number of CPU threads (0 = auto-detect).\nOnly applies when using CPU device.");
    SetFlags(f, Knob::STARTLINE);

    Float_knob(f, &m_maxMem, "max_memory", "Max Memory (MB)");
    Tooltip(f, "Maximum memory usage in MB (0 = no limit).\n"
               "Useful for limiting GPU memory consumption on large images.");
    SetRange(f, 0, 16384);
}

int DenoiserIop::knob_changed(Knob* k)
{
    if (k->is("device_type")) {
        // Device type changed - will recreate on next render
        return 1;
    }
    return PlanarIop::knob_changed(k);
}

const char *DenoiserIop::input_label(int n, char *) const
{
    switch (n) {
    case 0:  return "beauty";
    case 1:  return "albedo";
    case 2:  return "normal";
    default: return 0;
    }
}

void DenoiserIop::_validate(bool for_real)
{
    copy_info();

    for (int i = 0; i < getInputs().size(); i++) {
        if (input(i)) {
            info_.merge(input(i)->info());
        }
    }

    info_.full_size_format(input0().full_size_format());
    info_.format(input0().format());
    info_.channels(Mask_RGBA);
}

void DenoiserIop::getRequests(const Box & box, const ChannelSet & channels, int count, RequestOutput & reqData) const
{
    ChannelSet rgbChannels = Mask_RGB;

    for (int i = 0, endI = getInputs().size(); i < endI; i++) {
        if (input(i)) {
            const ChannelSet availableChannels = input(i)->info().channels();
            ChannelSet requestChannels;

            foreach(z, rgbChannels) {
                if (availableChannels.contains(z)) {
                    requestChannels += z;
                }
            }

            if (!requestChannels.empty()) {
                input(i)->request(requestChannels, count);
            }
        }
    }
}

void DenoiserIop::renderStripe(ImagePlane &plane)
{
    if (aborted() || cancelled())
        return;

    const Box& renderBox = plane.bounds();

    m_width = renderBox.w();
    m_height = renderBox.h();

    if (m_width <= 0 || m_height <= 0) {
        return;
    }

    // Recreate device if type changed
    if (m_lastDeviceTypeIdx != m_deviceTypeIdx) {
        setupDevice();
    }

    // Make sure device is valid
    if (!m_device) {
        error("[OIDN]: No valid device available");
        return;
    }

    auto bufferSize = m_width * m_height * 3 * sizeof(float);

    // Allocate OIDN buffers (GPU-compatible when using GPU device)
    m_colorBuffer = m_device.newBuffer(bufferSize);
    m_albedoBuffer = m_device.newBuffer(bufferSize);
    m_normalBuffer = m_device.newBuffer(bufferSize);
    m_outputBuffer = m_device.newBuffer(bufferSize);

    // Get writable pointers (for GPU devices, this maps to host-accessible memory)
    float* colorPtr = static_cast<float*>(m_colorBuffer.getData());
    float* albedoPtr = static_cast<float*>(m_albedoBuffer.getData());
    float* normalPtr = static_cast<float*>(m_normalBuffer.getData());
    float* outputPtr = static_cast<float*>(m_outputBuffer.getData());

    if (!colorPtr || !albedoPtr || !normalPtr || !outputPtr) {
        error("Buffer data is nullptr");
        return;
    }

    memset(colorPtr, 0, bufferSize);
    memset(albedoPtr, 0, bufferSize);
    memset(normalPtr, 0, bufferSize);
    memset(outputPtr, 0, bufferSize);

    // Read input data
    for (int inputIdx = 0; inputIdx < node_inputs(); ++inputIdx) {
        if (aborted() || cancelled())
            return;

        Iop* inputIop = dynamic_cast<Iop*>(input(inputIdx));

        if (inputIop == nullptr)
            continue;

        if (!inputIop->tryValidate(true))
            continue;

        ChannelSet rgbChannels = Mask_RGB;
        inputIop->request(renderBox, rgbChannels, 1);

        ImagePlane inputPlane(renderBox, false, rgbChannels, 3);
        inputIop->fetchPlane(inputPlane);

        const size_t chanStride = inputPlane.chanStride();

        float* targetBuffer = nullptr;
        if (inputIdx == 0) targetBuffer = colorPtr;
        else if (inputIdx == 1) targetBuffer = albedoPtr;
        else if (inputIdx == 2) targetBuffer = normalPtr;

        if (!targetBuffer) continue;

        // Convert planar -> interleaved RGB for OIDN
        for (unsigned int y = 0; y < m_height; y++) {
            for (unsigned int x = 0; x < m_width; x++) {
                size_t planeIdx = y * m_width + x;
                size_t bufferIdx = (y * m_width + x) * 3;

                for (int c = 0; c < 3; c++) {
                    const float* chanData = &inputPlane.readable()[chanStride * c];
                    float value = chanData[planeIdx];

                    if (!std::isfinite(value)) {
                        value = 0.0f;
                    }

                    targetBuffer[bufferIdx + c] = value;
                }
            }
        }
    }

    // Setup and execute
    setupFilter();

    if (aborted() || cancelled())
        return;

    executeFilter();

    // Read back output pointer (in case GPU device remapped it)
    outputPtr = static_cast<float*>(m_outputBuffer.getData());

    // Copy denoised output back to plane (interleaved -> planar)
    const size_t chanStride = plane.chanStride();

    for (int c = 0; c < 3; c++) {
        float* outChan = &plane.writable()[chanStride * c];

        for (unsigned int y = 0; y < m_height; y++) {
            for (unsigned int x = 0; x < m_width; x++) {
                size_t planeIdx = y * m_width + x;
                size_t bufferIdx = (y * m_width + x) * 3 + c;
                outChan[planeIdx] = outputPtr[bufferIdx];
            }
        }
    }

    // Alpha pass-through
    if (plane.channels().contains(Chan_Alpha)) {
        float* alphaChan = &plane.writable()[chanStride * 3];

        if (input(0)) {
            Iop* inputIop = dynamic_cast<Iop*>(input(0));
            if (inputIop && inputIop->info().channels().contains(Chan_Alpha)) {
                ChannelSet alphaChannel;
                alphaChannel += Chan_Alpha;
                inputIop->request(renderBox, alphaChannel, 1);

                ImagePlane alphaPlane(renderBox, false, alphaChannel, 1);
                inputIop->fetchPlane(alphaPlane);

                const float* alphaIn = alphaPlane.readable();
                for (unsigned int y = 0; y < m_height; y++) {
                    for (unsigned int x = 0; x < m_width; x++) {
                        size_t idx = y * m_width + x;
                        alphaChan[idx] = alphaIn[idx];
                    }
                }
            } else {
                for (size_t idx = 0; idx < m_width * m_height; idx++)
                    alphaChan[idx] = 1.0f;
            }
        } else {
            for (size_t idx = 0; idx < m_width * m_height; idx++)
                alphaChan[idx] = 1.0f;
        }
    }

    // Release buffers after render to free GPU memory
    m_colorBuffer.release();
    m_albedoBuffer.release();
    m_normalBuffer.release();
    m_outputBuffer.release();
}

static Iop *build(Node *node) { return new DenoiserIop(node); }
const Iop::Description DenoiserIop::d("Denoiser", "Filter/Denoiser", build);
