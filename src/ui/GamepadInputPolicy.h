#pragma once

#include <cstdint>

namespace hyperbrowse::ui
{
    enum class GamepadContext
    {
        Browser,
        Viewer,
    };

    enum GamepadButton : std::uint16_t
    {
        GamepadDpadUp = 0x0001,
        GamepadDpadDown = 0x0002,
        GamepadDpadLeft = 0x0004,
        GamepadDpadRight = 0x0008,
        GamepadLeftShoulder = 0x0100,
        GamepadRightShoulder = 0x0200,
        GamepadA = 0x1000,
        GamepadX = 0x4000,
        GamepadB = 0x2000,
        GamepadY = 0x8000,
    };

    struct GamepadState
    {
        std::uint16_t buttons{};
        float leftStickX{};
        float leftStickY{};
        float rightStickX{};
        float rightStickY{};
        float leftTrigger{};
        float rightTrigger{};
    };

    struct GamepadActions
    {
        int navigationKey{};
        bool open{};
        bool toggleViewerZoom{};
        bool toggleViewerOverlays{};
        bool escape{};
        bool recycle{};
        float zoomRate{};
        float panX{};
        float panY{};
        float panSpeedMultiplier{1.0F};
    };

    class GamepadInputPolicy
    {
    public:
        GamepadActions Update(const GamepadState& state,
                              GamepadContext context,
                              bool enabled,
                              float elapsedSeconds) noexcept;

    private:
        static int BrowserDirection(const GamepadState& state) noexcept;
        static int ViewerDirection(const GamepadState& state) noexcept;
        static void NormalizeStick(float& x, float& y, float deadZone) noexcept;

        std::uint16_t previousButtons_{};
        int repeatingDirection_{};
        float repeatElapsedSeconds_{};
        float panHeldSeconds_{};
        float filteredPanX_{};
        float filteredPanY_{};
    };
}
