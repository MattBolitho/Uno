#include <stdexcept>
#include "FilterFactory.hpp"
#include "NonmonotoneFilter.hpp"

// FilterFactory class
std::unique_ptr<Filter> FilterFactory::create(const Options& options) {
   const std::string& filter_type = options.get_string("filter_type");
   if (filter_type == "standard") {
      return std::make_unique<Filter>(options);
   }
   else if (filter_type == "nonmonotone") {
      return std::make_unique<NonmonotoneFilter>(options);
   }
   else {
      throw std::invalid_argument("Filter type " + filter_type + " does not exist");
   }
}