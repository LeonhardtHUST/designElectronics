// designElectronics — a uniform grid over one view's wires.
//
// Drawing asks "is this wire end touching another wire?" twice per wire. Scanning
// every wire for every end is O(W²) per frame, which starts to show at a few hundred
// wires; the grid answers the same question by looking at a handful of cells.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <unordered_map>
#include <vector>

#include "geom.h"

class WireGrid {
public:
    void clear(float cell) {
        cell_ = std::max(cell, 1.f);
        ends_.clear();
        segs_.clear();
        count_ = 0;
    }

    void addEnd(Vec2 p, int owner) {
        ends_[cellOf(p)].push_back({p, owner});
        count_++;
    }

    // A segment is registered in every cell its bounding box covers, so a point query
    // only has to look at the cells around itself.
    void addSeg(Vec2 a, Vec2 b, int owner) {
        int x0 = cx(std::min(a.x, b.x)), x1 = cx(std::max(a.x, b.x));
        int y0 = cy(std::min(a.y, b.y)), y1 = cy(std::max(a.y, b.y));
        for (int x = x0; x <= x1; x++)
            for (int y = y0; y <= y1; y++) segs_[key(x, y)].push_back({a, b, owner});
        count_++;
    }

    size_t size() const { return count_; }

    // True when an end or a segment of a wire other than `owner` lies within `tol` of `p`.
    bool touches(Vec2 p, int owner, float tol) const {
        if (count_ == 0) return false;
        int cxp = cx(p.x), cyp = cy(p.y);
        int span = (int)std::ceil(tol / cell_) + 1;
        for (int dx = -span; dx <= span; dx++)
            for (int dy = -span; dy <= span; dy++) {
                auto e = ends_.find(key(cxp + dx, cyp + dy));
                if (e != ends_.end())
                    for (auto& it : e->second)
                        if (it.owner != owner && dist(it.p, p) <= tol) return true;
                auto g = segs_.find(key(cxp + dx, cyp + dy));
                if (g != segs_.end())
                    for (auto& it : g->second)
                        if (it.owner != owner && distPointSeg(p, it.a, it.b) <= tol) return true;
            }
        return false;
    }

private:
    struct End {
        Vec2 p;
        int owner = -1;
    };
    struct Seg {
        Vec2 a, b;
        int owner = -1;
    };

    float cell_ = 400.f;
    size_t count_ = 0;
    std::unordered_map<long long, std::vector<End>> ends_;
    std::unordered_map<long long, std::vector<Seg>> segs_;

    int cx(float x) const { return (int)std::floor(x / cell_); }
    int cy(float y) const { return (int)std::floor(y / cell_); }
    long long cellOf(Vec2 p) const { return key(cx(p.x), cy(p.y)); }
    static long long key(int x, int y) { return ((long long)x << 32) ^ (unsigned)y; }
};
