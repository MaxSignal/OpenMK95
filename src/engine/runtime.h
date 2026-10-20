// Program structure, main loop, input, waits (01-runtime.md) and the message
// functions of 06-windows-text.md §9-§11.
#pragma once
#include "core/common.h"

namespace rt {

// message-processing pass (01 §3.2)
void processMessages();
// Wait(n) in tenths (01 §5.1)
void wait(int n);
// Movie form of Wait(0): returns when the movie window was closed.
void waitMovie(bool (*playing)(), void (*requestClose)());
void flushKeysNoWait();
void flushKeysWait();

// 06 §9: Message(text, factor, position) ; factorX10 avoids floating point issues:
// factor is given as one of 1 (0.1), 2 (0.2), 5, 7, 10, 20 tenths.
void message(const sjis& text, int factorTenths, bool bottom = false);
int messageWaitTenths(int factorTenths);
// pop-up notice (06 §11)
void popup(int ownerSlot, const sjis& text);
// Version information box
void versionBox();

bool partyDown();
void startNewGameFlow();
void mainLoop();
void runTitle();
void screenModeToggle();
// 03 §7.2 + 04 §11.2
void showMap();
// Called by the window-close request
void askQuit();

int messageSpeed();

}  // namespace rt
