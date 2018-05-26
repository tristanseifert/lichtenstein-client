#include "MAX10OutputPlugin.h"

#include <glog/logging.h>
#include <INIReader.h>

#include <OutputFrame.h>

// size of each "block" in the framebuffer allocation map
static const size_t kFBMapBlockSize = 16;



/**
 * Invokes the constructor for the plugin and returns it.
 */
OutputPlugin *MAX10OutputPlugin::create(PluginHandler *handler, void *romData, size_t length) {
	return new MAX10OutputPlugin(handler, romData, length);
}

/**
 * Initializes the output plugin, allocating memory and setting up the hardware.
 */
MAX10OutputPlugin::MAX10OutputPlugin(PluginHandler *_handler, void *romData, size_t length) : handler(_handler), OutputPlugin(romData, length) {
	// get SPI settings
	this->configureSPI();

	// allocate framebuffer
	this->allocateFramebuffer();

	// reset the output chip
	this->reset();
}

/**
 * Configures the SPI bus.
 */
void MAX10OutputPlugin::configureSPI(void) {
	INIReader *config = this->handler->getConfig();

	// TODO: read from EEPROM data
	this->spiBaud = config->GetInteger("output_max10", "baud", 2500000);

	this->spiCsLine = config->GetInteger("output_max10", "cs", -1);
	CHECK(this->spiCsLine >= 0) << "Invalid chip select line " << this->spiCsLine;
}

/**
 * Allocates a shadow copy of the framebuffer on the board: received frames are
 * "fit" into this buffer and will later be transferred to the chip.
 */
void MAX10OutputPlugin::allocateFramebuffer(void) {
	INIReader *config = this->handler->getConfig();

	// TODO: read from EEPROM data
	this->framebufferLen = config->GetInteger("output_max10", "fbsize", 131072);

	if((this->framebufferLen % kFBMapBlockSize) != 0) {
		LOG(FATAL) << "Framebuffer length must be a multiple of "
			<< kFBMapBlockSize << "; config specified " << this->framebufferLen;
	}

	this->framebuffer = static_cast<uint8_t *>(malloc(this->framebufferLen));
	CHECK(this->framebuffer != nullptr) << "Couldn't allocate framebuffer!";

	// allocate the framebuffer use
	size_t blocks = this->framebufferLen / kFBMapBlockSize;

	this->fbUsedMap.clear();
	this->fbUsedMap.reserve(blocks);

	for(size_t i = 0; i < blocks; i++) {
		this->fbUsedMap.push_back(false);
	}
}



/**
 * Resets the hardware and de-allocates memory.
 */
MAX10OutputPlugin::~MAX10OutputPlugin() {
	// reset peripheral
	this->reset();

	// de-allocate framebuffer
	if(this->framebuffer) {
		free(this->framebuffer);
	}
}



const std::string MAX10OutputPlugin::name(void) {
	return "MAX10 FPGA Output Board";
}
const unsigned int MAX10OutputPlugin::maxChannels(void) {
	return 16;
}

int MAX10OutputPlugin::setEnabledChannels(unsigned int channels) {
	return -1;
}



/**
 * Resets the output chip.
 */
void MAX10OutputPlugin::reset(void) {
	// TODO: implement
}



/**
 * Queues a frame for output: this could convert the frame to the output
 * representation, for example.
 */
int MAX10OutputPlugin::queueFrame(OutputFrame *frame) {
	// TODO: implement
	return -1;
}

/**
 * Outputs all channels whose bits are set. This uses absolute channel
 * numbering.
 */
int MAX10OutputPlugin::outputChannels(std::bitset<32> &channels) {
	// TODO: implement
	return -1;
}
