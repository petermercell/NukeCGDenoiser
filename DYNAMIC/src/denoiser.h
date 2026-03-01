// Copyright (c) 2021-2024 Mateusz Wojt
// Modified for dynamic OIDN 2.4 with GPU support

#pragma once

#include <DDImage/PlanarIop.h>
#include <DDImage/Interest.h>
#include <DDImage/Row.h>
#include <DDImage/Knobs.h>
#include <DDImage/Knob.h>
#include <DDImage/DDMath.h>

#include <OpenImageDenoise/oidn.hpp>

static const char *const HELP = "CG render denoiser based on Intel's Open Image Denoise 2.4 library.\n\n"
                                "Supports GPU acceleration (CUDA, SYCL, HIP) and CPU.\n\n"
                                "Connect your beauty, albedo, and normal inputs.\n"
                                "Select your preferred device and quality settings.";
static const char *const CLASS = "Denoiser";

using namespace DD::Image;

// Device type names for the knob dropdown
static const char *const deviceTypeNames[] = {
    "Default (best available)",  // OIDN_DEVICE_TYPE_DEFAULT
    "CPU",                       // OIDN_DEVICE_TYPE_CPU
    "SYCL (Intel GPU)",          // OIDN_DEVICE_TYPE_SYCL
    "CUDA (NVIDIA GPU)",         // OIDN_DEVICE_TYPE_CUDA
    "HIP (AMD GPU)",             // OIDN_DEVICE_TYPE_HIP
    nullptr
};

// Quality mode names
static const char *const qualityNames[] = {
    "Default",
    "Balanced (faster)",
    "High (best quality)",
    nullptr
};

class DenoiserIop : public PlanarIop
{
public:
    DenoiserIop(Node *node);

    // Nuke internal methods
    int minimum_inputs() const { return 1; }
    int maximum_inputs() const { return 3; }

    PackedPreference packedPreference() const { return ePackedPreferenceUnpacked; }

    void knobs(Knob_Callback f);
    int knob_changed(Knob* k);

    void _validate(bool);
    void getRequests(const Box& box, const ChannelSet& channels, int count, RequestOutput &reqData) const;
    virtual void renderStripe(ImagePlane& plane);

    bool useStripes() const { return false; }
    bool renderFullPlanes() const { return true; }

    const char *input_label(int n, char *) const;
    static const Iop::Description d;

    const char *Class() const { return d.name; }
    const char *node_help() const { return HELP; }

    // OIDN methods
    void setupDevice();
    void setupFilter();
    void executeFilter();
    void releaseDevice();

private:
    // User-facing settings
    int m_deviceTypeIdx;     // Index into deviceTypeNames
    int m_qualityIdx;        // Index into qualityNames
    bool m_bHDR;
    bool m_bCleanAux;        // Whether auxiliary images (albedo/normal) are noise-free
    int m_numThreads;        // 0 = auto
    float m_maxMem;          // 0 = no limit

    unsigned int m_width, m_height;

    ChannelSet m_defaultChannels;
    int m_defaultNumberOfChannels;

    // OIDN objects
    oidn::DeviceRef m_device;
    oidn::FilterRef m_filter;

    // Buffers
    oidn::BufferRef m_colorBuffer;
    oidn::BufferRef m_albedoBuffer;
    oidn::BufferRef m_normalBuffer;
    oidn::BufferRef m_outputBuffer;

    // Track if device needs recreation
    int m_lastDeviceTypeIdx;
};
