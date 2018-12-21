#include "StringUtils.h"

#include <string>
#include <vector>
#include <sstream>

#include <stdexcept>

#include <glog/logging.h>

/**
 * Parses a comma-separated list of objects, inserting each of them into the
 * output vector.
 */
int StringUtils::parseCsvList(const std::string &in, std::vector<std::string> &out) {
	int numItems = 0;

	// create a stream of input data
	std::stringstream ss(in);

	// iterate while there is more data in the stream
	while(ss.good()) {
		// read a line separated by a comma
		std::string substr;
		getline(ss, substr, ',');

		// then push it on the output vector
		out.push_back(substr);
		numItems++;
	}

	return numItems;
}

int StringUtils::parseCsvList(const std::string &in, std::vector<int> &out, int radix) {
  std::vector<std::string> results;
  int numStringItems = 0, numItems = 0;

  // parse the list to strings
  numStringItems = StringUtils::parseCsvList(in, results);

  // convert each to an integer
  for(auto it = results.begin(); it != results.end(); it++) {
    try {
      // convert to int and add
      out.push_back(std::stoi(*it, nullptr, radix));

      // success, increment counter
      numItems++;
    } catch(std::exception e) {
      LOG(WARNING) << "Couldn't parse string '" << *it << "' to int: " << e.what();
    }
  }

  // return number of items
  return numItems;
}
