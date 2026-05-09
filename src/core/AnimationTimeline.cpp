#include "AnimationTimeline.hpp"
#include "../ColorPalettes.hpp"    // pelpaint::Pixel (layout-compatible with PixelRGBA8)

#include <algorithm>
#include <stdexcept>

namespace pelpaint::core {

AnimationTimeline::AnimationTimeline(std::uint32_t width, std::uint32_t height)
    : width_(width), height_(height)
{
    AddFrame(); // always start with at least one blank frame
}

// ---- Frame management --------------------------------------------------

int AnimationTimeline::AddFrame()
{
    AnimationFrame f;
    f.surface = ImageSurface(width_, height_);
    f.label   = "Frame " + std::to_string(static_cast<int>(frames_.size()) + 1);
    frames_.push_back(std::move(f));
    return static_cast<int>(frames_.size()) - 1;
}

int AnimationTimeline::DuplicateFrame(int index)
{
    if (index < 0 || index >= static_cast<int>(frames_.size()))
        throw std::out_of_range("DuplicateFrame: index out of range");

    AnimationFrame copy;
    const AnimationFrame& src = frames_[static_cast<std::size_t>(index)];

    copy.surface = src.surface;  // Deep copy of all tiles and dirty flags.
    copy.delay = src.delay;
    copy.label = src.label + " (copy)";

    const int insertPos = index + 1;
    frames_.insert(frames_.begin() + insertPos, std::move(copy));

    if (currentFrame_ >= insertPos)
        ++currentFrame_;

    return insertPos;
}

void AnimationTimeline::InsertFrame(int before)
{
    before = std::max(0, std::min(before, static_cast<int>(frames_.size())));

    AnimationFrame f;
    f.surface = ImageSurface(width_, height_);
    f.label   = "Frame";
    frames_.insert(frames_.begin() + before, std::move(f));

    if (currentFrame_ >= before)
        ++currentFrame_;
}

void AnimationTimeline::RemoveFrame(int index)
{
    if (static_cast<int>(frames_.size()) <= 1) return;
    if (index < 0 || index >= static_cast<int>(frames_.size())) return;

    frames_.erase(frames_.begin() + index);

    if (currentFrame_ >= static_cast<int>(frames_.size()))
        currentFrame_ = static_cast<int>(frames_.size()) - 1;

    elapsed_ = 0.f;
}

void AnimationTimeline::MoveFrame(int from, int to)
{
    const int n = static_cast<int>(frames_.size());
    if (from < 0 || from >= n || to < 0 || to >= n || from == to) return;

    AnimationFrame temp = std::move(frames_[static_cast<std::size_t>(from)]);
    frames_.erase(frames_.begin() + from);

    // After erasing `from`, indices >= from shift down by 1.
    const int insertAt = (to > from) ? to - 1 : to;
    frames_.insert(frames_.begin() + insertAt, std::move(temp));

    if (currentFrame_ == from) {
        currentFrame_ = insertAt;
    } else if (from < to) {
        if (currentFrame_ > from && currentFrame_ <= insertAt) --currentFrame_;
    } else {
        if (currentFrame_ >= insertAt && currentFrame_ < from) ++currentFrame_;
    }
}

int AnimationTimeline::FrameCount() const noexcept
{
    return static_cast<int>(frames_.size());
}

AnimationFrame& AnimationTimeline::Frame(int i)
{
    return frames_.at(static_cast<std::size_t>(i));
}

const AnimationFrame& AnimationTimeline::Frame(int i) const
{
    return frames_.at(static_cast<std::size_t>(i));
}

// ---- Playback ----------------------------------------------------------

void AnimationTimeline::Play()
{
    if (state_ == PlaybackState::Stopped) {
        currentFrame_ = 0;
        elapsed_      = 0.f;
    }
    state_ = PlaybackState::Playing;
}

void AnimationTimeline::Pause()
{
    if (state_ == PlaybackState::Playing)
        state_ = PlaybackState::Paused;
}

void AnimationTimeline::Stop()
{
    state_        = PlaybackState::Stopped;
    currentFrame_ = 0;
    elapsed_      = 0.f;
}

void AnimationTimeline::Update(float dt)
{
    if (state_ != PlaybackState::Playing) return;
    if (frames_.empty()) return;

    elapsed_ += dt;
    float frameDur = FrameDelay(currentFrame_);

    while (elapsed_ >= frameDur) {
        elapsed_ -= frameDur;
        const int next = currentFrame_ + 1;

        if (next >= static_cast<int>(frames_.size())) {
            if (looping_) {
                currentFrame_ = 0;
            } else {
                currentFrame_ = static_cast<int>(frames_.size()) - 1;
                state_        = PlaybackState::Stopped;
                elapsed_      = 0.f;
                return;
            }
        } else {
            currentFrame_ = next;
        }

        frameDur = FrameDelay(currentFrame_);
    }
}

void AnimationTimeline::SetCurrentFrame(int i) noexcept
{
    const int n = static_cast<int>(frames_.size());
    if (n == 0) return;
    currentFrame_ = std::max(0, std::min(i, n - 1));
    elapsed_      = 0.f;
}

// ---- Settings ----------------------------------------------------------

void AnimationTimeline::SetFPS(float fps) noexcept
{
    fps_ = (fps > 0.f) ? fps : 1.f;
}

float AnimationTimeline::TotalDuration() const
{
    float total = 0.f;
    for (int i = 0; i < static_cast<int>(frames_.size()); ++i)
        total += FrameDelay(i);
    return total;
}

// ---- Helpers -----------------------------------------------------------

float AnimationTimeline::FrameDelay(int i) const noexcept
{
    if (i < 0 || i >= static_cast<int>(frames_.size()))
        return 1.f / fps_;
    const float d = frames_[static_cast<std::size_t>(i)].delay;
    return (d > 0.f) ? d : (1.f / fps_);
}

// ---- Resize ------------------------------------------------------------

void AnimationTimeline::Resize(std::uint32_t newW, std::uint32_t newH)
{
    width_  = newW;
    height_ = newH;
    for (auto& f : frames_)
        f.surface.Resize(newW, newH);
}

// ---- Operator pipeline integration -----------------------------------

int AnimationTimeline::DeriveFrame(int                     sourceIndex,
                                    operators::FramePipeline pipeline,
                                    operators::DrawMode      mode)
{
    if (sourceIndex < 0 || sourceIndex >= static_cast<int>(frames_.size()))
        throw std::out_of_range("DeriveFrame: sourceIndex out of range");

    AnimationFrame f;
    f.surface       = ImageSurface(width_, height_);
    f.label         = "Frame " + std::to_string(static_cast<int>(frames_.size()) + 1)
                      + " (derived)";
    f.sourceFrame   = sourceIndex;
    f.pipeline      = std::move(pipeline);
    f.mode          = mode;
    f.pipelineDirty = true;

    frames_.push_back(std::move(f));
    return static_cast<int>(frames_.size()) - 1;
}

std::expected<void, pelpaint::Error>
AnimationTimeline::BakeFrame(int frameIndex)
{
    if (frameIndex < 0 || frameIndex >= static_cast<int>(frames_.size()))
        return std::unexpected(pelpaint::Error{
            pelpaint::ErrorCode::OutOfBounds, "BakeFrame: index out of range"});

    AnimationFrame& f = frames_[static_cast<std::size_t>(frameIndex)];

    // Nothing to bake for direct frames.
    if (f.sourceFrame < 0 || f.pipeline.Empty()) {
        f.pipelineDirty = false;
        return {};
    }
    if (f.sourceFrame >= static_cast<int>(frames_.size()))
        return std::unexpected(pelpaint::Error{
            pelpaint::ErrorCode::OutOfBounds, "BakeFrame: sourceFrame out of range"});

    const AnimationFrame& src = frames_[static_cast<std::size_t>(f.sourceFrame)];

    // Flatten source surface into a contiguous RGBA8 buffer.
    const ImageView srcView = src.surface.Flatten();

    // Reinterpret PixelRGBA8* as pelpaint::Pixel* — binary layout-compatible.
    const auto*    pixBase  = reinterpret_cast<const pelpaint::Pixel*>(srcView.data);
    const std::size_t nPx   = static_cast<std::size_t>(width_) * height_;
    const std::span<const pelpaint::Pixel> srcSpan{pixBase, nPx};

    // Run the pipeline.
    auto result = f.pipeline.Apply(srcSpan,
                                   static_cast<int>(width_),
                                   static_cast<int>(height_),
                                   f.mode);
    if (!result)
        return std::unexpected(result.error());

    const std::vector<pelpaint::Pixel>& outPx = *result;

    // Write output back into the frame's tiled surface via the canonical
    // WriteFlat path (row-wise copy — SIMD-friendly, no manual tile indexing).
    f.surface.WriteFlat(std::span<const PixelRGBA8>{
        reinterpret_cast<const PixelRGBA8*>(outPx.data()), outPx.size()});

    f.pipelineDirty = false;
    return {};
}

void AnimationTimeline::BakeAllDirty()
{
    for (int i = 0, n = static_cast<int>(frames_.size()); i < n; ++i) {
        if (frames_[static_cast<std::size_t>(i)].pipelineDirty)
            BakeFrame(i)
                .or_else([](const pelpaint::Error&)
                         -> std::expected<void, pelpaint::Error> { return {}; });
    }
}

void AnimationTimeline::MarkDependentsDirty(int sourceIndex) noexcept
{
    for (auto& f : frames_)
        if (f.sourceFrame == sourceIndex)
            f.pipelineDirty = true;
}

} // namespace pelpaint::core
