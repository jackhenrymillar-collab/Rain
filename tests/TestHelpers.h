#pragma once

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace rain::test
{

inline double energy (const float* x, std::size_t n)
{
    double e = 0.0;
    for (std::size_t i = 0; i < n; ++i)
        e += static_cast<double> (x[i]) * x[i];
    return e;
}

inline double correlation (const std::vector<float>& a, const std::vector<float>& b)
{
    double ab = 0.0, aa = 0.0, bb = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        ab += static_cast<double> (a[i]) * b[i];
        aa += static_cast<double> (a[i]) * a[i];
        bb += static_cast<double> (b[i]) * b[i];
    }
    return ab / std::sqrt (aa * bb + 1e-30);
}

inline bool allFinite (const std::vector<float>& x)
{
    return std::all_of (x.begin(), x.end(), [] (float v) { return std::isfinite (v); });
}

} // namespace rain::test
