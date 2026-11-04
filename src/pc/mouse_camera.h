// Mouse camera (PC Options > Controls > Mouse Moves Camera). See mouse_camera.cpp.

#pragma once

// Switches the mouse between turning the camera directly (while the player can turn it) and
// pushing the right stick. Cheap, so it is called every frame.
void UpdateMouseCamera();

// The on-screen controller's camera (Android): whether a drag turns the camera directly now (the
// player's camera, as for the mouse; otherwise the drag pushes the right stick), and the drag's
// motion in screen heights (right and down positive), taken by the next logic step. Any thread.
bool TouchLookActive();
void AddTouchLook(float dx, float dy);
