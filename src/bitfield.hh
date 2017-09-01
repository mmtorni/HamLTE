#include <type_traits>
#include <functional>
#include <cstdint>
#include <cassert>

// A fixed precision unsigned integer field. operator+ is concatenation
template <unsigned Width, typename = typename std::enable_if<Width<=8*sizeof(unsigned)>::type>
struct f {
  static constexpr unsigned width = Width;
  f():value(0) { }
  f(unsigned i):value(i) { }
  f(int i):value(i) { }
  unsigned value;
  template <unsigned Width2>
  auto operator+(f<Width2> rhs) { return f<Width+Width2>((value<<Width2) | rhs.value); }
};

struct bits {
  uint8_t *data;
  unsigned read_offset;
  unsigned write_offset;
  bits(uint8_t *backing_store) : data(backing_store), read_offset(0), write_offset(0) {}
  template <unsigned Width> bits &operator+=(f<Width> value) { push_bits(Width, value.value); return *this; }
  void push_bit(bool bit) {
    data[write_offset/8] |= (!!bit)<<(7-write_offset%8);
    ++write_offset;
  }
  void push_bits(unsigned n_bits, unsigned value) { while(n_bits) { push_bit((value >> --n_bits)&1); } }
  template <unsigned WidthInBits> void push_bits(unsigned value) { push_bits(WidthInBits, value); }
  void push(unsigned n_bits, unsigned value) { while(n_bits) { push_bit((value >> --n_bits)&1); } }
  template <unsigned WidthInBits> void push(unsigned value) { push_bits(WidthInBits, value); }
  bool read_bit() { unsigned i = read_offset++; return (data[i/8] >> (7-i%8))&1; }
  f<1> read_bits() { return read_bit(); }
  template <unsigned WidthInBits>
  f<WidthInBits> read_bits() { return read_bit() + read_bits<WidthInBits-1>(); }

  unsigned operator/(unsigned n_bits) {
    unsigned value = 0;
    while (n_bits--) { value = (value<<1) | read_bit(); }
    return value;
  }
};


void bits_add_bit(struct bits &self, bool bit) {
  self.data[self.write_offset/8] |= (!!bit)<<((8-self.write_offset%8)-1);
  ++self.write_offset;
}
void bits_add_int(struct bits &self, unsigned n_bits, int value) {
  assert(value >= 0 && value <= (1<<n_bits));
  unsigned i = (unsigned)value;
  while(n_bits) {
    bits_add_bit(self, (i >> --n_bits)&1);
  }
}

void bits_pad_to_octet(struct bits &self) {
  bits_add_int(self, (8 - self.write_offset % 8) % 8, 0);
}

/******
 ** Modular arithmetic sequence number for window size calculations
 **/
template <unsigned WidthInBits>
struct sequence_number {
  static constexpr unsigned width = WidthInBits;
  unsigned value; // This needs to be unsigned to avoid C/C++ undefined behaviour
  sequence_number() : value(0) { }
  sequence_number(unsigned i) : value(i%(1u<<WidthInBits)) { }
  sequence_number operator+(int i) const { return value + (unsigned)i; }
  sequence_number &operator+=(int i) { return *this = (*this + i); }
  sequence_number operator++(int) { unsigned oldvalue = value; *this += 1; return oldvalue; }
  sequence_number &operator++() { *this += 1; return *this; }
  sequence_number &operator--() { *this += -1; return *this; }
  bool operator<(sequence_number<WidthInBits> rhs) const { return rhs - *this > 0; }
  bool operator>(sequence_number<WidthInBits> rhs) const { return rhs < *this; }
  bool operator<=(sequence_number<WidthInBits> rhs) const { return !(*this > rhs); }
  bool operator>=(sequence_number<WidthInBits> rhs) const { return !(*this < rhs); }
  bool operator!=(sequence_number<WidthInBits> rhs) const { return value != rhs.value; }
  bool operator==(sequence_number<WidthInBits> rhs) const { return value == rhs.value; }
  int operator-(sequence_number<WidthInBits> rhs) const { return difference_to_int(value - rhs.value); }
protected:
  int difference_to_int(unsigned result) const {
      unsigned sign_mask = ~0u ^ ((1u<<WidthInBits)-1u);
      if (result & (1u<<(WidthInBits-1)))
	result |= sign_mask;
      else
	result &= ~sign_mask;
      return (int)result;
  }
};

template <unsigned WidthInBits>
auto
operator +=(bits &lhs, sequence_number<WidthInBits> rhs) { return lhs += f<WidthInBits>(rhs.value); }

namespace std {
  template <unsigned WidthInBits>
  struct hash<sequence_number<WidthInBits> >
  {
    size_t operator()(sequence_number<WidthInBits> sn) const { return hash<unsigned>()(sn.value); }
  };
}

static unsigned bits_to_bytes(int bits) { return (bits + 7) / 8; }

template <class Container, typename T>
static auto
sum(const Container&c, T init) {
  return boost::accumulate(c, init);
}

