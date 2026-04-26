#include "AnimationTimeline.hpp"

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

    copy.surface = ImageSurface(width_, height_);
    for (std::uint32_t ty = 0; ty < src.surface.TilesY(); ++ty) {
        for (std::uint32_t tx = 0; tx < src.surface.TilesX(); ++tx) {
            auto srcSpan = src.surface.TilePixels(tx, ty);
            if (srcSpan.empty()) continue;
            auto dstSpan = copy.surface.TilePixelsMutable(tx, ty);
            std::copy(srcSpan.begin(), srcSpan.end(), dstSpan.begin());
        }
    }
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

} // namespace pelpaint::core
