// Copyright (c) 2021-2024 Mateusz Wojt

#include "denoiser.h"
#include <OpenImageDenoise/oidn.hpp>
#include <iostream>
#include <iomanip>
#include <cstring>  // for memset

DenoiserIop::DenoiserIop(Node *node) : PlanarIop(node)
{
	// Initialize with optimized default settings
	m_bHDR = true;           // HDR enabled by default (works for LDR too)
	m_bAffinity = true;      // Enable thread affinity for best performance
	m_numRuns = 1;           // One pass is usually enough
	m_deviceType = 1;        // CPU (since we built static CPU-only)
	m_numThreads = 0;        // Auto-detect optimal thread count
	m_maxMem = 0.f;          // No memory limit

	m_device = nullptr;
	m_filter = nullptr;

	m_colorBuffer = nullptr;
	m_albedoBuffer = nullptr;
	m_normalBuffer = nullptr;
	m_outputBuffer = nullptr;

	m_defaultChannels = Mask_RGB;
	m_defaultNumberOfChannels = m_defaultChannels.size();

	setupDevice();
};

void DenoiserIop::setupDevice()
{
	try
    {
		// Create CPU device (auto would also work but we know it's CPU-only)
		m_device = oidnNewDevice(OIDN_DEVICE_TYPE_CPU);
		
		const char *errorMessage;
		if (m_device.getError(errorMessage) != oidn::Error::None)
			throw std::runtime_error(errorMessage);

        // Set device parameters for optimal performance
		m_device.set("numThreads", m_numThreads);  // 0 = auto-detect
		m_device.set("setAffinity", m_bAffinity);  // Enable for best performance

		// Commit changes to the device
		m_device.commit();

		// Create the RT (ray tracing) filter - expensive operation done once
		m_filter = m_device.newFilter("RT");
    }
    catch (const std::exception &e)
    {
        std::string message = e.what();
		error("[OIDN]: %s", message.c_str());
    }
}

void DenoiserIop::setupFilter()
{
	try
	{
		// Set the images
		m_filter.setImage("color", m_colorBuffer, oidn::Format::Float3, m_width, m_height);
		m_filter.setImage("output", m_outputBuffer, oidn::Format::Float3, m_width, m_height);
		m_filter.setImage("albedo", m_albedoBuffer, oidn::Format::Float3, m_width, m_height);
		m_filter.setImage("normal", m_normalBuffer, oidn::Format::Float3, m_width, m_height);

		// Set filter parameters
		m_filter.set("hdr", m_bHDR);
		
		// Only set maxMemoryMB if not 0 (0 = no limit)
		if (m_maxMem > 0.f) {
			m_filter.set("maxMemoryMB", static_cast<int>(m_maxMem));
		}

		// Commit changes to the filter
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
		// Execute denoising
		m_filter.execute();
	}
	catch (const std::exception &e)
	{
		std::string message = e.what();
		error("[OIDN]: %s", message.c_str());
	}
}

void DenoiserIop::knobs(Knob_Callback f)
{
	// Empty - no UI controls, just plug and play!
	// All settings are optimized defaults in the background
}

const char *DenoiserIop::input_label(int n, char *) const
{
	switch (n) {
	case 0:
		return "beauty";
	case 1:
		return "albedo";
	case 2:
		return "normal";
	default:
		return 0;
	}
}

void DenoiserIop::_validate(bool for_real)
{
	copy_info();
	
	// Properly merge all input bounding boxes
	for (int i = 0; i < getInputs().size(); i++) {
		if (input(i)) {
			info_.merge(input(i)->info());
		}
	}
	
	// Set output format to match the first input
	// input0() returns Iop& (reference), not Iop* (pointer)
	info_.full_size_format(input0().full_size_format());
	info_.format(input0().format());
	
	// CRITICAL: Only output RGBA channels, nothing else
	// This prevents issues when inputs have extra channels (indirect, normals, etc.)
	info_.channels(Mask_RGBA);
}

void DenoiserIop::getRequests(const Box & box, const ChannelSet & channels, int count, RequestOutput & reqData) const
{
	// Only request RGB channels from inputs, ignore any extra channels
	ChannelSet rgbChannels = Mask_RGB;
	
	for (int i = 0, endI = getInputs().size(); i < endI; i++) {
		if (input(i)) {
			// Only request RGB channels that exist in the input
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

	// Get the actual render bounds from the plane
	const Box& renderBox = plane.bounds();
	
	m_width = renderBox.w();
	m_height = renderBox.h();
	
	// Early exit if nothing to render
	if (m_width <= 0 || m_height <= 0) {
		return;
	}
	
	// OIDN processes RGB only (3 channels)
	auto bufferSize = m_width * m_height * 3 * sizeof(float);

	// Allocate OIDN buffers (RGB only)
	m_colorBuffer = m_device.newBuffer(bufferSize);
	m_albedoBuffer = m_device.newBuffer(bufferSize);
	m_normalBuffer = m_device.newBuffer(bufferSize);
	m_outputBuffer = m_device.newBuffer(bufferSize);
	
	// Acquire pointers to the float data of each buffer
	float* colorPtr = static_cast<float*>(m_colorBuffer.getData());
	float* albedoPtr = static_cast<float*>(m_albedoBuffer.getData());
	float* normalPtr = static_cast<float*>(m_normalBuffer.getData());
	float* outputPtr = static_cast<float*>(m_outputBuffer.getData());

	if (!colorPtr || !albedoPtr || !normalPtr || !outputPtr) {
		error("Buffer data is nullptr");
		return;
	}

	// Initialize buffers to zero (important for missing inputs)
	memset(colorPtr, 0, bufferSize);
	memset(albedoPtr, 0, bufferSize);
	memset(normalPtr, 0, bufferSize);
	memset(outputPtr, 0, bufferSize);

	// Read input data from each connected input (RGB only)
	for (int inputIdx = 0; inputIdx < node_inputs(); ++inputIdx) {
		if (aborted() || cancelled())
			return;

		Iop* inputIop = dynamic_cast<Iop*>(input(inputIdx));
		
		if (inputIop == nullptr) {
			continue;
		}

		if (!inputIop->tryValidate(true)) {
			continue;
		}

		// Request only RGB channels for denoising
		ChannelSet rgbChannels = Mask_RGB;
		inputIop->request(renderBox, rgbChannels, 1);

		// Fetch plane with RGB channels
		ImagePlane inputPlane(renderBox, false, rgbChannels, 3);
		inputIop->fetchPlane(inputPlane);

		// Get channel stride for accessing individual channels
		const size_t chanStride = inputPlane.chanStride();
		
		// Select target buffer based on input index
		float* targetBuffer = nullptr;
		if (inputIdx == 0) targetBuffer = colorPtr;
		else if (inputIdx == 1) targetBuffer = albedoPtr;
		else if (inputIdx == 2) targetBuffer = normalPtr;
		
		if (!targetBuffer) continue;
		
		// Copy data from ImagePlane to OIDN buffer (interleaved RGB format)
		// ImagePlane stores channels separately (planar)
		// OIDN needs interleaved: RGBRGBRGB...
		for (unsigned int y = 0; y < m_height; y++) {
			for (unsigned int x = 0; x < m_width; x++) {
				size_t planeIdx = y * m_width + x;
				size_t bufferIdx = (y * m_width + x) * 3;  // RGB = 3 channels
				
				// Copy RGB channels
				for (int c = 0; c < 3; c++) {
					// Access the channel data: readable() + chanStride * channel
					const float* chanData = &inputPlane.readable()[chanStride * c];
					float value = chanData[planeIdx];
					
					// Check for invalid values
					if (!std::isfinite(value)) {
						value = 0.0f;
					}
					
					targetBuffer[bufferIdx + c] = value;
				}
			}
		}
	}

	// Setup and execute the OIDN filter
	setupFilter();
	
	if (aborted() || cancelled())
		return;
	
	executeFilter();

	// Copy denoised RGB output back to the image plane
	// Convert from OIDN's interleaved RGB format back to Nuke's planar format
	const size_t chanStride = plane.chanStride();
	
	// Write RGB channels
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
	
	// Handle Alpha channel - pass through from input or set to 1.0
	if (plane.channels().contains(Chan_Alpha)) {
		float* alphaChan = &plane.writable()[chanStride * 3];  // Alpha is 4th channel
		
		// Try to get alpha from input
		if (input(0)) {
			Iop* inputIop = dynamic_cast<Iop*>(input(0));
			if (inputIop && inputIop->info().channels().contains(Chan_Alpha)) {
				// Pass through alpha from input
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
				// No alpha in input, set to 1.0 (opaque)
				for (unsigned int y = 0; y < m_height; y++) {
					for (unsigned int x = 0; x < m_width; x++) {
						size_t idx = y * m_width + x;
						alphaChan[idx] = 1.0f;
					}
				}
			}
		} else {
			// No input, set alpha to 1.0
			for (unsigned int y = 0; y < m_height; y++) {
				for (unsigned int x = 0; x < m_width; x++) {
					size_t idx = y * m_width + x;
					alphaChan[idx] = 1.0f;
				}
			}
		}
	}
}

static Iop *build(Node *node) { return new DenoiserIop(node); }
const Iop::Description DenoiserIop::d("Denoiser", "Filter/Denoiser", build);
