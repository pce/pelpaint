#pragma once

#include <vector>
#include <string>
#include <string_view>
#include <cstddef>

namespace pelpaint {

// ---------------------------------------------------------------------------
// UndoHistory<T>
//
// Generic undo / redo stack. T must be copyable (snapshots are full copies).
// No knowledge of pixels, layers, or ImGui.
//
// Invariant: undoStack_ always has at least one entry (the initial state)
//            after the first Push(). CanUndo() returns true only when there
//            are 2+ entries (something to roll back to).
// ---------------------------------------------------------------------------

template<typename T>
class UndoHistory {
public:
    explicit UndoHistory(std::size_t maxSteps = 50) noexcept
        : maxSteps_(maxSteps) {}

    // ---- Queries --------------------------------------------------------

    [[nodiscard]] bool        CanUndo()    const noexcept { return undoStack_.size() > 1; }
    [[nodiscard]] bool        CanRedo()    const noexcept { return !redoStack_.empty(); }
    [[nodiscard]] std::size_t UndoCount()  const noexcept { return undoStack_.size(); }
    [[nodiscard]] std::size_t RedoCount()  const noexcept { return redoStack_.empty() ? 0 : redoStack_.size(); }

    // ---- Mutations -------------------------------------------------------

    // Push a new snapshot. Clears the redo stack.
    // Takes snapshot by value — callers should std::move when possible.
    void Push(T snapshot, std::string_view description = "") {
        redoStack_.clear();
        if (undoStack_.size() >= maxSteps_) {
            undoStack_.erase(undoStack_.begin());
        }
        undoStack_.push_back({ std::move(snapshot), std::string(description) });
    }

    // Undo: moves current state to redo stack, returns pointer to the
    // restored state. Returns nullptr if nothing to undo.
    [[nodiscard]] const T* Undo() {
        if (!CanUndo()) return nullptr;
        redoStack_.push_back(std::move(undoStack_.back()));
        undoStack_.pop_back();
        return &undoStack_.back().snapshot;
    }

    // Redo: moves next state from redo stack back onto undo stack.
    // Returns pointer to the restored state. Returns nullptr if nothing to redo.
    [[nodiscard]] const T* Redo() {
        if (!CanRedo()) return nullptr;
        undoStack_.push_back(std::move(redoStack_.back()));
        redoStack_.pop_back();
        return &undoStack_.back().snapshot;
    }

    // Peek at the current (top) state without modifying stacks.
    // Returns nullptr if the stack is empty.
    [[nodiscard]] const T* Current() const noexcept {
        if (undoStack_.empty()) return nullptr;
        return &undoStack_.back().snapshot;
    }

    [[nodiscard]] std::string_view CurrentDescription() const noexcept {
        if (undoStack_.empty()) return {};
        return undoStack_.back().description;
    }

    void Clear() {
        undoStack_.clear();
        redoStack_.clear();
    }

private:
    struct Entry {
        T           snapshot;
        std::string description;
    };

    std::vector<Entry> undoStack_;
    std::vector<Entry> redoStack_;
    std::size_t        maxSteps_;
};

} // namespace pelpaint
