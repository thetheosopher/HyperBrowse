#include "ui/GamepadInputPolicy.h"

#include <algorithm>
#include <cmath>

#include <windows.h>

namespace hyperbrowse::ui
{
    namespace
    {
        constexpr float kNavigationStickDeadZone = 0.45F;
        constexpr float kPanStickDeadZone = 0.22F;
        constexpr float kTriggerDeadZone = 0.05F;
        constexpr float kInitialRepeatDelaySeconds = 0.36F;
        constexpr float kRepeatIntervalSeconds = 0.12F;
        constexpr float kPanAccelerationSeconds = 1.5F;
        constexpr float kMaximumPanSpeedMultiplier = 4.0F;
        constexpr float kPanSmoothingTimeSeconds = 0.045F;

        int DirectionFromStick(float x, float y) noexcept
        {
            if (std::abs(x) < kNavigationStickDeadZone
                && std::abs(y) < kNavigationStickDeadZone)
            {
                return 0;
            }

            if (std::abs(x) > std::abs(y) * 1.1F)
            {
                return x < 0.0F ? VK_LEFT : VK_RIGHT;
            }
            return y > 0.0F ? VK_UP : VK_DOWN;
        }
    }

    int GamepadInputPolicy::BrowserDirection(const GamepadState& state) noexcept
    {
        const bool up = (state.buttons & GamepadDpadUp) != 0;
        const bool down = (state.buttons & GamepadDpadDown) != 0;
        const bool left = (state.buttons & GamepadDpadLeft) != 0;
        const bool right = (state.buttons & GamepadDpadRight) != 0;
        if (up != down)
        {
            return up ? VK_UP : VK_DOWN;
        }
        if (left != right)
        {
            return left ? VK_LEFT : VK_RIGHT;
        }
        if (up || down || left || right)
        {
            return 0;
        }
        return DirectionFromStick(state.leftStickX, state.leftStickY);
    }

    int GamepadInputPolicy::ViewerDirection(const GamepadState& state) noexcept
    {
        const bool previous = (state.buttons & GamepadLeftShoulder) != 0;
        const bool next = (state.buttons & GamepadRightShoulder) != 0;
        if (previous != next)
        {
            return previous ? VK_PRIOR : VK_NEXT;
        }

        const int direction = BrowserDirection(state);
        switch (direction)
        {
        case VK_LEFT:
        case VK_UP:
            return VK_PRIOR;
        case VK_RIGHT:
        case VK_DOWN:
            return VK_NEXT;
        default:
            return 0;
        }
    }

    void GamepadInputPolicy::NormalizeStick(float& x, float& y, float deadZone) noexcept
    {
        x = std::clamp(x, -1.0F, 1.0F);
        y = std::clamp(y, -1.0F, 1.0F);
        const float magnitude = std::sqrt((x * x) + (y * y));
        if (magnitude <= deadZone)
        {
            x = 0.0F;
            y = 0.0F;
            return;
        }

        const float normalizedMagnitude = std::min(1.0F, (magnitude - deadZone) / (1.0F - deadZone));
        const float scale = normalizedMagnitude / magnitude;
        x *= scale;
        y *= scale;
    }

    GamepadActions GamepadInputPolicy::Update(const GamepadState& state,
                                               GamepadContext context,
                                               bool enabled,
                                               float elapsedSeconds) noexcept
    {
        const std::uint16_t pressed = static_cast<std::uint16_t>(state.buttons & ~previousButtons_);
        previousButtons_ = state.buttons;

        GamepadActions actions;
        if (!enabled)
        {
            repeatingDirection_ = 0;
            repeatElapsedSeconds_ = 0.0F;
            panHeldSeconds_ = 0.0F;
            filteredPanX_ = 0.0F;
            filteredPanY_ = 0.0F;
            return actions;
        }

        actions.open = (pressed & GamepadA) != 0;
        actions.recycle = (pressed & GamepadX) != 0;
        actions.escape = (pressed & GamepadB) != 0;
        if (context == GamepadContext::Viewer)
        {
            actions.toggleViewerZoom = actions.open;
            actions.toggleViewerOverlays = (pressed & GamepadY) != 0;
        }

        const int direction = context == GamepadContext::Browser
            ? BrowserDirection(state)
            : ViewerDirection(state);
        const float elapsed = std::clamp(elapsedSeconds, 0.0F, 0.1F);
        if (direction != repeatingDirection_)
        {
            repeatingDirection_ = direction;
            repeatElapsedSeconds_ = 0.0F;
            if (direction != 0)
            {
                actions.navigationKey = direction;
            }
        }
        else if (direction != 0)
        {
            repeatElapsedSeconds_ += elapsed;
            if (repeatElapsedSeconds_ >= kInitialRepeatDelaySeconds)
            {
                actions.navigationKey = direction;
                repeatElapsedSeconds_ = kInitialRepeatDelaySeconds
                    + std::fmod(repeatElapsedSeconds_ - kInitialRepeatDelaySeconds, kRepeatIntervalSeconds);
            }
        }

        if (context == GamepadContext::Viewer)
        {
            float rightTrigger = std::clamp(state.rightTrigger, 0.0F, 1.0F);
            float leftTrigger = std::clamp(state.leftTrigger, 0.0F, 1.0F);
            if (rightTrigger < kTriggerDeadZone)
            {
                rightTrigger = 0.0F;
            }
            if (leftTrigger < kTriggerDeadZone)
            {
                leftTrigger = 0.0F;
            }
            actions.zoomRate = rightTrigger - leftTrigger;
            actions.panX = -state.rightStickX;
            actions.panY = state.rightStickY;
            NormalizeStick(actions.panX, actions.panY, kPanStickDeadZone);
            const float targetPanX = actions.panX;
            const float targetPanY = actions.panY;
            if (actions.panX == 0.0F && actions.panY == 0.0F)
            {
                panHeldSeconds_ = 0.0F;
            }
            else
            {
                panHeldSeconds_ = std::min(kPanAccelerationSeconds, panHeldSeconds_ + elapsed);
                actions.panSpeedMultiplier = std::min(
                    kMaximumPanSpeedMultiplier,
                    1.0F + ((kMaximumPanSpeedMultiplier - 1.0F)
                            * panHeldSeconds_ / kPanAccelerationSeconds));
            }

            const float smoothingAlpha = 1.0F - std::exp(-elapsed / kPanSmoothingTimeSeconds);
            filteredPanX_ += (targetPanX - filteredPanX_) * smoothingAlpha;
            filteredPanY_ += (targetPanY - filteredPanY_) * smoothingAlpha;
            if (targetPanX == 0.0F && std::abs(filteredPanX_) < 0.01F)
            {
                filteredPanX_ = 0.0F;
            }
            if (targetPanY == 0.0F && std::abs(filteredPanY_) < 0.01F)
            {
                filteredPanY_ = 0.0F;
            }
            actions.panX = filteredPanX_;
            actions.panY = filteredPanY_;
        }
        else
        {
            panHeldSeconds_ = 0.0F;
            filteredPanX_ = 0.0F;
            filteredPanY_ = 0.0F;
        }
        return actions;
    }
}
