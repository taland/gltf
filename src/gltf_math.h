// Minimal educational glTF 2.0 loader (C11).
//
// Internal math utilities.
// This header declares a minimal set of linear algebra helpers used internally
// by the glTF loader (scene graph evaluation, world-space transforms, AABBs).
//
// Scope:
//   - provides only what is required by the loader
//   - not intended to be a general-purpose math library
//
// Matrix conventions:
//   - mat4 is represented as float[16]
//   - column-major layout: m[col * 4 + row]
//   - compatible with OpenGL / glTF conventions
//
// Operations:
//   - mat4_identity(m): sets m to the identity matrix
//   - mat4_mul(a, b, out): computes matrix product out = a * b
//
// Design goals:
//   - small and dependency-free
//   - easy to read and reason about (educational)
//   - deterministic behavior, no hidden allocations
//
// Usage notes:
//   - all functions write results into caller-provided buffers
//   - input and output matrices must not alias unless explicitly documented
//
// Note:
//   - This is an internal header and is not part of the public API.
//   - Library users should not include this file directly.


#include <string.h>


static inline void mat4_identity(float m[16]) {
    memset(m, 0, 16 * sizeof(float));
    m[0] = 1.f;
    m[5] = 1.f;
    m[10] = 1.f;
    m[15] = 1.f;
}

// mat4 from quaternion (x,y,z,w), column-major
static inline void mat4_from_quat(float out[16], const float q[4]) {
  const float x = q[0], y = q[1], z = q[2], w = q[3];

  const float xx = x * x, yy = y * y, zz = z * z;
  const float xy = x * y, xz = x * z, yz = y * z;
  const float wx = w * x, wy = w * y, wz = w * z;

  // 3x3 rotation into top-left of 4x4
  // column-major: out[col*4 + row]
  out[0]  = 1.f - 2.f * (yy + zz);
  out[1]  = 2.f * (xy + wz);
  out[2]  = 2.f * (xz - wy);
  out[3]  = 0.f;

  out[4]  = 2.f * (xy - wz);
  out[5]  = 1.f - 2.f * (xx + zz);
  out[6]  = 2.f * (yz + wx);
  out[7]  = 0.f;

  out[8]  = 2.f * (xz + wy);
  out[9]  = 2.f * (yz - wx);
  out[10] = 1.f - 2.f * (xx + yy);
  out[11] = 0.f;

  out[12] = 0.f;
  out[13] = 0.f;
  out[14] = 0.f;
  out[15] = 1.f;
}

static inline void mat4_apply_translation(float m[16], const float t[3]) {
  // For column-major affine matrix, translation is column 3 (indices 12..14)
  m[12] = t[0];
  m[13] = t[1];
  m[14] = t[2];
}

static inline void mat4_apply_scale(float m[16], const float s[3]) {
  // local = R * S means scaling the basis vectors (columns 0..2)
  // Here we build TRS as: start with R, scale its basis, then set translation.
  m[0] *= s[0]; m[1] *= s[0]; m[2]  *= s[0];
  m[4] *= s[1]; m[5] *= s[1]; m[6]  *= s[1];
  m[8] *= s[2]; m[9] *= s[2]; m[10] *= s[2];
}

static inline void mat4_mul(const float a[16], const float b[16], float out[16]) {
  float r[16];
  for (int c = 0; c < 4; ++c) {
    for (int row = 0; row < 4; ++row) {
      r[c*4 + row] =
        a[0*4 + row] * b[c*4 + 0] +
        a[1*4 + row] * b[c*4 + 1] +
        a[2*4 + row] * b[c*4 + 2] +
        a[3*4 + row] * b[c*4 + 3];
    }
  }
  memcpy(out, r, sizeof(r));
}
