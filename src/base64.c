#include <stdint.h>
#include <stddef.h>

#define B64_INVALID 0u
#define B64_PAD     0xFEu
#define B64_SKIP    0xFFu

static const uint8_t k_b64_lut[256] = {
  ['A']=1,  ['B']=2,  ['C']=3,  ['D']=4,  ['E']=5,  ['F']=6,  ['G']=7,
  ['H']=8,  ['I']=9,  ['J']=10, ['K']=11, ['L']=12, ['M']=13, ['N']=14,
  ['O']=15, ['P']=16, ['Q']=17, ['R']=18, ['S']=19, ['T']=20, ['U']=21,
  ['V']=22, ['W']=23, ['X']=24, ['Y']=25, ['Z']=26,

  ['a']=27, ['b']=28, ['c']=29, ['d']=30, ['e']=31, ['f']=32, ['g']=33,
  ['h']=34, ['i']=35, ['j']=36, ['k']=37, ['l']=38, ['m']=39, ['n']=40,
  ['o']=41, ['p']=42, ['q']=43, ['r']=44, ['s']=45, ['t']=46, ['u']=47,
  ['v']=48, ['w']=49, ['x']=50, ['y']=51, ['z']=52,

  ['0']=53, ['1']=54, ['2']=55, ['3']=56, ['4']=57, ['5']=58, ['6']=59,
  ['7']=60, ['8']=61, ['9']=62,

  ['+']=63,
  ['/']=64,

  ['=']  = B64_PAD,
  [' ']  = B64_SKIP,
  ['\t'] = B64_SKIP,
  ['\n'] = B64_SKIP,
  ['\r'] = B64_SKIP,
  ['\f'] = B64_SKIP,
  ['\v'] = B64_SKIP,
};

size_t gltf_base64_max_decoded_size(size_t in_len) {
  // Upper bound: every 4 chars -> up to 3 bytes. Add 3 for rounding.
  // Conservative; may over-allocate if whitespace is present.
  if (in_len > (SIZE_MAX / 3u) * 4u) return SIZE_MAX;
  return (in_len / 4u) * 3u + 3u;
}

int gltf_base64_decode(const char* in, size_t in_len,
                       uint8_t* out, size_t out_cap,
                       size_t* out_len) {
  if (!out_len) return 0;
  *out_len = 0;
  if (!in) return 0;
  if (out_cap > 0 && !out) return 0;

  size_t o = 0;
  uint8_t quad[4];
  int qn = 0;
  int finished = 0;

  for (size_t i = 0; i < in_len; i++) {
    const uint8_t c = (uint8_t)(unsigned char)in[i];
    const uint8_t t = k_b64_lut[c];

    if (t == B64_SKIP) continue;

    if (finished) return 0; // only whitespace allowed after padding
    if (t == B64_INVALID) return 0;

    quad[qn++] = t;
    if (qn < 4) continue;

    const uint8_t a = quad[0], b = quad[1], c2 = quad[2], d = quad[3];

    if (a == B64_PAD || b == B64_PAD) return 0; // padding only allowed at 2/3

    const uint32_t va = (uint32_t)(a - 1u);
    const uint32_t vb = (uint32_t)(b - 1u);

    if (c2 == B64_PAD) {
      if (d != B64_PAD) return 0; // "xx=="
      if (o + 1u > out_cap) return 0;
      out[o++] = (uint8_t)((va << 2u) | (vb >> 4u));
      finished = 1;
    } else if (d == B64_PAD) {
      const uint32_t vc = (uint32_t)(c2 - 1u);
      if (o + 2u > out_cap) return 0;
      out[o++] = (uint8_t)((va << 2u) | (vb >> 4u));
      out[o++] = (uint8_t)(((vb & 0xFu) << 4u) | (vc >> 2u));
      finished = 1;
    } else {
      const uint32_t vc = (uint32_t)(c2 - 1u);
      const uint32_t vd = (uint32_t)(d - 1u);
      if (o + 3u > out_cap) return 0;
      out[o++] = (uint8_t)((va << 2u) | (vb >> 4u));
      out[o++] = (uint8_t)(((vb & 0xFu) << 4u) | (vc >> 2u));
      out[o++] = (uint8_t)(((vc & 0x3u) << 6u) | vd);
    }

    qn = 0;
  }

  if (qn != 0) return 0; // incomplete trailing group

  *out_len = o;
  return 1;
}
