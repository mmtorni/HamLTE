#include <cassert>

static int clamp(int number, int low, int high) {
  assert(low<=high);
  if(number < low) return low;
  if(number > high) return high;
  return number;
}

template <class Container, class Key>
bool
has_key(Container &container, const Key &key) {
  return container.find(key) != container.end();
}

#define min(a, b) (((a)<(b))?(a):(b))
#define max(a, b) (((b)<(a))?(a):(b))

