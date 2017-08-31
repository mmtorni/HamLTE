#include <cassert>

static int clamp(int number, int low, int high) {
  assert(low<=high);
  if(number < low) return low;
  if(number > high) return high;
  return number;
}

template <class Container, typename Key>
bool
has_key(Container &container, Key &&key) {
  return container.find(std::forward<Key>(key)) != container.end();
}

using std::min;
using std::max;
