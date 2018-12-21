/**
 * A few functions useful for dealing with strings.
 */
#ifndef STRINGUTILS_H
#define STRINGUTILS_H

#include <string>
#include <vector>

class StringUtils {
  private:
    StringUtils() {}
    ~StringUtils() {}

  public:
    /**
     * Parses a comma-separated list of objects, inserting each of them into the
     * output vector.
     */
    static int parseCsvList(const std::string &in, std::vector<std::string> &out);
    static int parseCsvList(const std::string &in, std::vector<int> &out, int radix = 10);
};

#endif
