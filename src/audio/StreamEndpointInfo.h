#include <string>

#include "../masaj/JsonNode.h"

struct StreamEndpointInfo {
	std::string deviceId;
	int sampleRate;
	int channelOffset;
	int numChannels;

	StreamEndpointInfo(const AudioIOManager::DeviceState & device, int sampleRate)
		: deviceId(device.id), channelOffset(0)
		, numChannels(device.numChannels())
		, sampleRate(sampleRate)
	{ }

	StreamEndpointInfo(const JsonNode &data)
		: deviceId(data["deviceId"].str)
		, sampleRate((int)data["sampleRate"].asNum())
		, channelOffset((uint8_t)data["channelOffset"].asNum())
		, numChannels((uint8_t)data["numChannels"].asNum())
	{
		if (deviceId.empty())
			throw std::runtime_error("can't construct StreamEndpointInfo with empty device id");
	}

	const AudioIOManager::DeviceState &validOrThrow(const AudioIOManager &mgr) {
		auto &dev = mgr.getDevice(deviceId);
		if (!dev.exists())
			throw std::runtime_error("Device " + deviceId + " not found!");

		if (numChannels == 0)
			numChannels = dev.numChannels();

		if (dev.numChannels() < (channelOffset + numChannels)) {
			throw std::runtime_error("Invalid enpoint channel count!");
		}

		auto srs = dev.getSampleRateCurrentlyAvailable();
		if (sampleRate == 0) {
			sampleRate = srs[0];
		}
		else {
			if (std::find(srs.begin(), srs.end(), sampleRate) == srs.end())
				throw std::runtime_error("Device " + deviceId + " does not work at " + std::to_string(sampleRate) + " sampling rate!");
		}
		return dev;
	}
};