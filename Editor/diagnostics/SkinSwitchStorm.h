#pragma once

class MainWindow;

namespace SkinSwitchStorm
{
// Runs the hidden field diagnostic against the real window and exits with a
// non-zero status if toolbar health regresses.
void run(MainWindow& window);
}
