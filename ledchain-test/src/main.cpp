/**
 * Main entrypoint for Lichtenstein client
 */
#include "status/StatusHandler.h"
#include "net/ProtocolHandler.h"

#include <glog/logging.h>
#include <cxxopts.hpp>
#include "INIReader.h"

#include <iostream>
#include <atomic>
#include <bitset>

#include <cstdint>
#include <signal.h>

using namespace std;

// software version
uint32_t kLichtensteinSWVersion = 0x00001000;

// various client components
ProtocolHandler *proto = nullptr;

// when set to false, the client terminates
atomic_bool keepRunning;

// parsing of the config file
INIReader *configReader = nullptr;
void parseConfigFile(string path);

/**
 * Signal handler. This handler is invoked for the following signals to enable
 * us to do a clean shut-down:
 *
 * - SIGINT
 */
void signalHandler(int sig) {
	LOG(WARNING) << "Caught signal " << sig << "; shutting down!";
	keepRunning = false;
}

/**
 * Main function
 */
int main(int argc, const char *argv[]) {
	// set up logging
	FLAGS_logtostderr = 1;
	FLAGS_colorlogtostderr = 1;

	google::InitGoogleLogging(argv[0]);
	google::InstallFailureSignalHandler();

	LOG(INFO) << "lichtenstein client " << GIT_HASH << "/" << GIT_BRANCH
			  << " compiled on " << COMPILE_TIME;

	// parse command-line options
	cxxopts::Options options("lichtenstein_client", "Lichtenstein Client");

	options.add_options()
		("c,config", "Config file", cxxopts::value<std::string>()->default_value("lichtenstein.conf"))
	;

	auto cmdlineOptions = options.parse(argc, argv);

	// first, parse the config file
	parseConfigFile(cmdlineOptions["config"].as<std::string>());

	// set thread name
	#ifdef __APPLE__
		pthread_setname_np("Main Thread");
	#else
		pthread_setname_np(pthread_self(), "Main Thread");
	#endif

	// set up a signal handler for termination so we can close down cleanly
	keepRunning = true;

	struct sigaction sigIntHandler;

	sigIntHandler.sa_handler = signalHandler;
	sigemptyset(&sigIntHandler.sa_mask);
	sigIntHandler.sa_flags = 0;

	sigaction(SIGINT, &sigIntHandler, nullptr);

	// set up the status handler
	StatusHandler::initSingleton(configReader);

	// set up the various components
	proto = new ProtocolHandler(configReader);


	// wait for a signal
	while(keepRunning) {
		pause();
	}

	// before anything, stop accepting protocol requests
	proto->stop();

	// tear down
	delete proto;

	// clean up status handler
	StatusHandler::deallocSingleton();
}

/**
 * Opens the config file for reading and parses it.
 */
void parseConfigFile(string path) {
	int err;

	LOG(INFO) << "Reading configuration from " << path;

	// attempt to open the config file
	configReader = new INIReader(path);

	err = configReader->ParseError();

	if(err == -1) {
		LOG(FATAL) << "Couldn't open config file at " << path;
	} else if(err > 0) {
		LOG(FATAL) << "Parse error on line " << err << " of config file " << path;
	}

	// set up the logging parameters
	int verbosity = configReader->GetInteger("logging", "verbosity", 0);

	if(verbosity < 0) {
		FLAGS_v = abs(verbosity);
		FLAGS_minloglevel = 0;

		LOG(INFO) << "Enabled verbose logging up to level " << abs(verbosity);
	} else {
		// disable verbose logging
		FLAGS_v = 0;

		// ALWAYS log FATAL errors
		FLAGS_minloglevel = min(verbosity, 2);
	}

	FLAGS_logtostderr = configReader->GetBoolean("logging", "stderr", true);
}
