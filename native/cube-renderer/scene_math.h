#pragma once
#include "mirror_session.h"
#include <array>
#include <cmath>

namespace framebridge::render {
using SceneState = native_mirror::SceneState;
using Matrix = std::array<double, 16>;
struct Matrices { Matrix model{}, view{}, projection{}, mvp{}; std::array<std::array<double,3>,8> corners{}; };
inline Matrix Multiply(const Matrix& a, const Matrix& b) {
  Matrix out{};
  for (int c=0;c<4;++c) for(int r=0;r<4;++r) for(int k=0;k<4;++k)
    out[c*4+r] += a[k*4+r]*b[c*4+k];
  return out;
}
inline Matrices SceneMatrices(const SceneState& s) {
  Matrices m;
  const double a=std::cos(double(s.rotationX)), b=std::sin(double(s.rotationX)), c=std::cos(double(s.rotationY)), d=std::sin(double(s.rotationY));
  // Three Euler XYZ: Rx * Ry * Rz, z=0, column-major, column vectors.
  m.model={c,b*d,-a*d,0, 0,a,b,0, d,-b*c,a*c,0, 0,0,0,1};
  m.view={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,-s.cameraZ,1};
  const double f=1.0/std::tan(3.14159265358979323846/6.0), n=.1, z=100;
  m.projection={f/(double(s.width)/s.height),0,0,0, 0,f,0,0, 0,0,z/(n-z),-1, 0,0,z*n/(n-z),0};
  m.mvp=Multiply(m.projection,Multiply(m.view,m.model));
  for(int i=0;i<8;++i) {
    const std::array<double,4> p={(i&1)?.5:-.5,(i&2)?.5:-.5,(i&4)?.5:-.5,1};
    std::array<double,4> clip{};
    for(int r=0;r<4;++r) for(int k=0;k<4;++k) clip[r]+=m.mvp[k*4+r]*p[k];
    for(int r=0;r<3;++r) m.corners[i][r]=clip[r]/clip[3];
  }
  return m;
}
}
