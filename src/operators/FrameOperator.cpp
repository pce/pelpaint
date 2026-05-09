#include "FrameOperator.hpp"

namespace pelpaint::operators {

FramePipeline& FramePipeline::operator|(OpFn op) {
    ops_.push_back(std::move(op));
    return *this;
}

OpResult FramePipeline::Apply(
    std::span<const Pixel> src,
    int w, int h,
    DrawMode mode) const
{
    // Fast path: empty pipeline — caller gets an owned copy of src.
    // (BakeFrame exits early for empty pipelines and never reaches here.)
    if (ops_.empty())
        return std::vector<Pixel>(src.begin(), src.end());

    OpCtx ctx{w, h, mode};

    // Feed `src` directly into the first operator — avoids a full W×H
    // seed-copy that would otherwise be needed to bootstrap `buf`.
    auto first = ops_[0](src, ctx);
    if (!first) return first;   // short-circuit on error

    std::vector<Pixel> buf = std::move(*first);

    for (std::size_t i = 1; i < ops_.size(); ++i) {
        auto res = ops_[i](std::span<const Pixel>{buf}, ctx);
        if (!res) return res;   // short-circuit on error
        buf = std::move(*res);
    }
    return buf;
}

} // namespace pelpaint::operators
